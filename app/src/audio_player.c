#include "audio_player.h"

#include <string.h>

#include "util/log.h"
#include "util/memory.h"
#include "SDL3/SDL_hints.h"

/** Downcast frame_sink to sc_audio_player */
#define DOWNCAST(SINK) container_of(SINK, struct sc_audio_player, frame_sink)

#define SC_SDL_SAMPLE_FMT SDL_AUDIO_F32LE

static void SDLCALL
sc_audio_player_stream_callback(void *userdata, SDL_AudioStream *stream,
                                int additional_amount, int total_amount) {
    (void) total_amount;

    struct sc_audio_player *ap = userdata;

    size_t len = additional_amount;
    assert(len % ap->audioreg.sample_size == 0);

    // The requested amount may exceed the internal aout_buffer size.
    // In this (unlikely) case, send the data to the stream in multiple chunks.
    while (len) {
        size_t chunk_size = MIN(ap->aout_buffer_size, len);
        uint32_t out_samples = chunk_size / ap->audioreg.sample_size;
        sc_audio_regulator_pull(&ap->audioreg, ap->aout_buffer,
                                out_samples);

        assert(chunk_size <= len);
        len -= chunk_size;
        bool ok =
            SDL_PutAudioStreamData(stream, ap->aout_buffer, chunk_size);
        if (!ok) {
            LOGW("Audio stream error: %s", SDL_GetError());
            return;
        }
    }
}

static enum sc_sink_result
sc_audio_player_frame_sink_push(struct sc_frame_sink *sink,
                                const AVFrame *frame) {
    struct sc_audio_player *ap = DOWNCAST(sink);

    bool ok = sc_audio_regulator_push(&ap->audioreg, frame);
    return ok ? SC_SINK_OK : SC_SINK_KO;
}

// Return the playback device with this name, or 0 (after listing the names)
static SDL_AudioDeviceID
sc_audio_player_find_device(const char *name) {
    int count;
    SDL_AudioDeviceID *ids = SDL_GetAudioPlaybackDevices(&count);
    if (!ids) {
        LOGE("Could not list audio output devices: %s", SDL_GetError());
        return 0;
    }

    SDL_AudioDeviceID found = 0;
    for (int i = 0; i < count && !found; ++i) {
        const char *device_name = SDL_GetAudioDeviceName(ids[i]);
        if (device_name && !strcmp(device_name, name)) {
            found = ids[i];
        }
    }

    if (!found) {
        LOGE("Audio output device not found: \"%s\". Available devices:", name);
        for (int i = 0; i < count; ++i) {
            const char *device_name = SDL_GetAudioDeviceName(ids[i]);
            LOGE("    \"%s\"", device_name ? device_name : "(unknown)");
        }
    }

    SDL_free(ids);
    return found;
}

static bool
sc_audio_player_frame_sink_open(struct sc_frame_sink *sink,
                                const AVCodecContext *ctx,
                                const struct sc_stream_session *session) {
    (void) session;

    struct sc_audio_player *ap = DOWNCAST(sink);

    assert(ctx->ch_layout.nb_channels > 0 && ctx->ch_layout.nb_channels < 256);
    uint8_t nb_channels = ctx->ch_layout.nb_channels;

    assert(ctx->sample_rate > 0);
    assert(!av_sample_fmt_is_planar(SC_AV_SAMPLE_FMT));
    int out_bytes_per_sample = av_get_bytes_per_sample(SC_AV_SAMPLE_FMT);
    assert(out_bytes_per_sample > 0);

    uint32_t target_buffering_samples =
        ap->target_buffering_delay * ctx->sample_rate / SC_TICK_FREQ;

    size_t sample_size = nb_channels * out_bytes_per_sample;
    bool ok = sc_audio_regulator_init(&ap->audioreg, sample_size, ctx,
                                      target_buffering_samples);
    if (!ok) {
        return false;
    }

    uint64_t aout_samples = ap->output_buffer_duration * ctx->sample_rate
                                                       / SC_TICK_FREQ;
    assert(aout_samples <= 0xFFFF);

    char str[5 + 1]; // max 65535
    int r = snprintf(str, sizeof(str), "%" PRIu16, (uint16_t) aout_samples);
    assert(r >= 0 && (size_t) r < sizeof(str));
    (void) r;
    if (!SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, str)) {
        LOGE("Could not set audio output buffer");
        sc_audio_regulator_destroy(&ap->audioreg);
        return false;
    }

    // Make the buffer at least 1024 samples long (the hint is not always
    // honored)
    uint64_t aout_buffer_samples = MAX(1024, aout_samples);
    ap->aout_buffer_size = aout_buffer_samples * sample_size;
    ap->aout_buffer = sc_allocarray(aout_buffer_samples, sample_size);
    if (!ap->aout_buffer) {
        sc_audio_regulator_destroy(&ap->audioreg);
        return false;
    }

    SDL_AudioSpec spec = {
        .freq = ctx->sample_rate,
        .format = SC_SDL_SAMPLE_FMT,
        .channels = nb_channels,
    };

    SDL_AudioDeviceID devid = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    if (ap->device_name) {
        devid = sc_audio_player_find_device(ap->device_name);
        if (!devid) {
            free(ap->aout_buffer);
            sc_audio_regulator_destroy(&ap->audioreg);
            return false;
        }
    }

    ap->stream = SDL_OpenAudioDeviceStream(devid, &spec,
                                           sc_audio_player_stream_callback, ap);
    if (!ap->stream) {
        LOGE("Could not open audio device: %s", SDL_GetError());
        free(ap->aout_buffer);
        sc_audio_regulator_destroy(&ap->audioreg);
        return false;
    }

    ap->device = SDL_GetAudioStreamDevice(ap->stream);
    assert(ap->device);

    ok = SDL_ResumeAudioDevice(ap->device);
    if (!ok) {
        LOGE("Could not resume audio device: %s", SDL_GetError());
        SDL_DestroyAudioStream(ap->stream);
        free(ap->aout_buffer);
        sc_audio_regulator_destroy(&ap->audioreg);
        return false;
    }

    return true;
}

static void
sc_audio_player_frame_sink_close(struct sc_frame_sink *sink) {
    struct sc_audio_player *ap = DOWNCAST(sink);

    assert(ap->stream);
    assert(ap->device);
    SDL_PauseAudioDevice(ap->device);

    // ap->device is owned by ap->stream
    SDL_DestroyAudioStream(ap->stream);

    sc_audio_regulator_destroy(&ap->audioreg);

    free(ap->aout_buffer);
}

void
sc_audio_player_init(struct sc_audio_player *ap, sc_tick target_buffering,
                     sc_tick output_buffer_duration, const char *device_name) {
    ap->target_buffering_delay = target_buffering;
    ap->output_buffer_duration = output_buffer_duration;
    ap->device_name = device_name;

    static const struct sc_frame_sink_ops ops = {
        .open = sc_audio_player_frame_sink_open,
        .close = sc_audio_player_frame_sink_close,
        .push = sc_audio_player_frame_sink_push,
    };

    ap->frame_sink.ops = &ops;
}
