#include "recorder.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libavutil/display.h>

#include "util/log.h"
#include "util/str.h"

/** Downcast packet sinks to recorder */
#define DOWNCAST_VIDEO(SINK) \
    container_of(SINK, struct sc_recorder, video_packet_sink)
#define DOWNCAST_AUDIO(SINK) \
    container_of(SINK, struct sc_recorder, audio_packet_sink)

static const AVRational SCRCPY_TIME_BASE = {1, 1000000}; // timestamps in us

static const AVOutputFormat *
find_muxer(const char *name) {
    void *opaque = NULL;
    const AVOutputFormat *oformat = NULL;
    do {
        oformat = av_muxer_iterate(&opaque);
        // until null or containing the requested name
    } while (oformat && !sc_str_list_contains(oformat->name, ',', name));
    return oformat;
}

static AVPacket *
sc_recorder_packet_ref(const AVPacket *packet) {
    AVPacket *p = av_packet_alloc();
    if (!p) {
        LOG_OOM();
        return NULL;
    }

    if (av_packet_ref(p, packet)) {
        LOG_OOM();
        av_packet_free(&p);
        return NULL;
    }

    return p;
}

static void
sc_recorder_queue_clear(struct sc_recorder_queue *queue) {
    while (!sc_vecdeque_is_empty(queue)) {
        AVPacket *p = sc_vecdeque_pop(queue);
        av_packet_free(&p);
    }
}

static const char *
sc_recorder_get_format_name(enum sc_record_format format) {
    switch (format) {
        case SC_RECORD_FORMAT_MP4:
        case SC_RECORD_FORMAT_M4A:
        case SC_RECORD_FORMAT_AAC:
            return "mp4";
        case SC_RECORD_FORMAT_MKV:
        case SC_RECORD_FORMAT_MKA:
            return "matroska";
        case SC_RECORD_FORMAT_OPUS:
            return "opus";
        case SC_RECORD_FORMAT_FLAC:
            return "flac";
        case SC_RECORD_FORMAT_WAV:
            return "wav";
        default:
            return NULL;
    }
}

static bool
sc_recorder_set_extradata(AVStream *ostream, const AVPacket *packet) {
    uint8_t *extradata = av_malloc(packet->size * sizeof(uint8_t));
    if (!extradata) {
        LOG_OOM();
        return false;
    }

    // copy the first packet to the extra data
    memcpy(extradata, packet->data, packet->size);

    ostream->codecpar->extradata = extradata;
    ostream->codecpar->extradata_size = packet->size;
    return true;
}

static inline void
sc_recorder_rescale_packet(AVStream *stream, AVPacket *packet) {
    av_packet_rescale_ts(packet, SCRCPY_TIME_BASE, stream->time_base);
}

static bool
sc_recorder_write_stream(struct sc_recorder *recorder,
                         struct sc_recorder_stream *st, AVPacket *packet) {
    AVStream *stream = recorder->ctx->streams[st->index];
    sc_recorder_rescale_packet(stream, packet);
    if (st->last_pts != AV_NOPTS_VALUE && packet->pts <= st->last_pts) {
        LOGD("Fixing PTS non monotonically increasing in stream %d "
             "(%" PRIi64 " >= %" PRIi64 ")",
             st->index, st->last_pts, packet->pts);
        packet->pts = ++st->last_pts;
        packet->dts = packet->pts;
    } else {
        st->last_pts = packet->pts;
    }
    return av_interleaved_write_frame(recorder->ctx, packet) >= 0;
}

static inline bool
sc_recorder_write_video(struct sc_recorder *recorder, AVPacket *packet) {
    return sc_recorder_write_stream(recorder, &recorder->video_stream, packet);
}

static inline bool
sc_recorder_write_audio(struct sc_recorder *recorder, AVPacket *packet) {
    return sc_recorder_write_stream(recorder, &recorder->audio_stream, packet);
}

static bool
sc_recorder_open_output_file(struct sc_recorder *recorder) {
    const char *format_name = sc_recorder_get_format_name(recorder->format);
    assert(format_name);
    // With --record-segment, the "segment" muxer opens the files itself and
    // writes each one with the muxer of the record format
    const AVOutputFormat *format =
        find_muxer(recorder->segment_seconds ? "segment" : format_name);
    if (!format) {
        LOGE("Could not find muxer");
        return false;
    }

    recorder->ctx = avformat_alloc_context();
    if (!recorder->ctx) {
        LOG_OOM();
        return false;
    }

    if (recorder->segment_seconds) {
        recorder->ctx->oformat = format;
        recorder->ctx->url = av_strdup(recorder->filename);
        if (!recorder->ctx->url) {
            avformat_free_context(recorder->ctx);
            LOG_OOM();
            return false;
        }
        av_dict_set(&recorder->ctx->metadata, "comment",
                    "Recorded by scrcpy " SCRCPY_VERSION, 0);
        LOGI("Recording started to %s files of %" PRIu32 " s: %s",
             format_name, recorder->segment_seconds, recorder->filename);
        return true;
    }

    char *file_url = sc_str_concat("file:", recorder->filename);
    if (!file_url) {
        avformat_free_context(recorder->ctx);
        return false;
    }

    int ret = avio_open(&recorder->ctx->pb, file_url, AVIO_FLAG_WRITE);
    free(file_url);
    if (ret < 0) {
        LOGE("Failed to open output file: %s", recorder->filename);
        avformat_free_context(recorder->ctx);
        return false;
    }

    recorder->ctx->oformat = format;

    av_dict_set(&recorder->ctx->metadata, "comment",
                "Recorded by scrcpy " SCRCPY_VERSION, 0);

    LOGI("Recording started to %s file: %s", format_name, recorder->filename);
    return true;
}

static void
sc_recorder_close_output_file(struct sc_recorder *recorder) {
    if (!(recorder->ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_close(recorder->ctx->pb);
    }
    avformat_free_context(recorder->ctx);
}

static inline bool
sc_recorder_must_wait_for_config_packets(struct sc_recorder *recorder) {
    if (recorder->video && recorder->video_expects_config_packet
            && sc_vecdeque_is_empty(&recorder->video_queue)) {
        // The video queue is empty
        return true;
    }

    if (recorder->audio && recorder->audio_expects_config_packet
            && sc_vecdeque_is_empty(&recorder->audio_queue)) {
        // The audio queue is empty (when audio is enabled)
        return true;
    }

    // No queue is empty
    return false;
}

static bool
sc_recorder_process_header(struct sc_recorder *recorder) {
    sc_mutex_lock(&recorder->mutex);

    while (!recorder->stopped &&
              ((recorder->video && !recorder->video_init)
            || (recorder->audio && !recorder->audio_init)
            || sc_recorder_must_wait_for_config_packets(recorder))) {
        sc_cond_wait(&recorder->cond, &recorder->mutex);
    }

    if (recorder->stopped && recorder->video
            && sc_vecdeque_is_empty(&recorder->video_queue)) {
        // If the recorder is stopped, don't process anything if there are not
        // at least video packets
        sc_mutex_unlock(&recorder->mutex);
        LOGW("Recording stopped before headers were processed");
        return false;
    }

    AVPacket *video_pkt = NULL;
    if (recorder->video_expects_config_packet &&
            !sc_vecdeque_is_empty(&recorder->video_queue)) {
        assert(recorder->video);
        video_pkt = sc_vecdeque_pop(&recorder->video_queue);
    }

    AVPacket *audio_pkt = NULL;
    if (recorder->audio_expects_config_packet &&
            !sc_vecdeque_is_empty(&recorder->audio_queue)) {
        assert(recorder->audio);
        audio_pkt = sc_vecdeque_pop(&recorder->audio_queue);
    }

    sc_mutex_unlock(&recorder->mutex);

    int ret = false;

    if (video_pkt) {
        if (video_pkt->pts != AV_NOPTS_VALUE) {
            LOGE("The first video packet is not a config packet");
            goto end;
        }

        assert(recorder->video_stream.index >= 0);
        AVStream *video_stream =
            recorder->ctx->streams[recorder->video_stream.index];
        bool ok = sc_recorder_set_extradata(video_stream, video_pkt);
        if (!ok) {
            goto end;
        }
    }

    if (audio_pkt) {
        if (audio_pkt->pts != AV_NOPTS_VALUE) {
            LOGE("The first audio packet is not a config packet");
            goto end;
        }

        assert(recorder->audio_stream.index >= 0);
        AVStream *audio_stream =
            recorder->ctx->streams[recorder->audio_stream.index];
        bool ok = sc_recorder_set_extradata(audio_stream, audio_pkt);
        if (!ok) {
            goto end;
        }
    }

    AVDictionary *muxer_options = NULL;
    if (recorder->segment_seconds) {
        char seconds[16];
        snprintf(seconds, sizeof(seconds), "%" PRIu32,
                 recorder->segment_seconds);
        av_dict_set(&muxer_options, "segment_time", seconds, 0);
        av_dict_set(&muxer_options, "segment_format",
                    sc_recorder_get_format_name(recorder->format), 0);
        // Every file starts at timestamp 0
        av_dict_set(&muxer_options, "reset_timestamps", "1", 0);
    }

    bool ok = avformat_write_header(recorder->ctx, &muxer_options) >= 0;
    av_dict_free(&muxer_options);
    if (!ok) {
        LOGE("Failed to write header to %s", recorder->filename);
        goto end;
    }

    ret = true;

end:
    if (video_pkt) {
        av_packet_free(&video_pkt);
    }
    if (audio_pkt) {
        av_packet_free(&audio_pkt);
    }

    return ret;
}

static bool
sc_recorder_write_pts(struct sc_recorder *recorder, int64_t pts_us) {
    if (!recorder->pts_file) {
        return true;
    }

    int r = fprintf(recorder->pts_file, "%" PRIu64 ",%" PRId64 "\n",
                    recorder->pts_frame_index++, pts_us);
    if (r < 0) {
        LOGE("Could not write record PTS file: %s", recorder->pts_filename);
        return false;
    }

    return true;
}

static bool
sc_recorder_process_packets(struct sc_recorder *recorder) {
    int64_t pts_origin = AV_NOPTS_VALUE;
    // --record-segment: pts (relative to pts_origin) of the next cut. Like the
    // segment muxer, count from the first video packet.
    int64_t segment_next_pts = AV_NOPTS_VALUE;
    int64_t segment_us = (int64_t) recorder->segment_seconds * 1000000;
    bool key_frame_requested = false;

    bool header_written = sc_recorder_process_header(recorder);
    if (!header_written) {
        return false;
    }

    AVPacket *video_pkt = NULL;
    AVPacket *audio_pkt = NULL;

    // We can write a video packet only once we received the next one so that
    // we can set its duration (next_pts - current_pts)
    AVPacket *video_pkt_previous = NULL;

    bool error = false;

    for (;;) {
        sc_mutex_lock(&recorder->mutex);

        while (!recorder->stopped) {
            if (recorder->video && !video_pkt &&
                    !sc_vecdeque_is_empty(&recorder->video_queue)) {
                // A new packet may be assigned to video_pkt and be processed
                break;
            }
            if (recorder->audio && !audio_pkt
                    && !sc_vecdeque_is_empty(&recorder->audio_queue)) {
                // A new packet may be assigned to audio_pkt and be processed
                break;
            }
            sc_cond_wait(&recorder->cond, &recorder->mutex);
        }

        // If stopped is set, continue to process the remaining events (to
        // finish the recording) before actually stopping.

        // If there is no video, then the video_queue will remain empty forever
        // and video_pkt will always be NULL.
        assert(recorder->video || (!video_pkt
                && sc_vecdeque_is_empty(&recorder->video_queue)));

        // If there is no audio, then the audio_queue will remain empty forever
        // and audio_pkt will always be NULL.
        assert(recorder->audio || (!audio_pkt
                && sc_vecdeque_is_empty(&recorder->audio_queue)));

        if (!video_pkt && !sc_vecdeque_is_empty(&recorder->video_queue)) {
            video_pkt = sc_vecdeque_pop(&recorder->video_queue);
        }

        if (!audio_pkt && !sc_vecdeque_is_empty(&recorder->audio_queue)) {
            audio_pkt = sc_vecdeque_pop(&recorder->audio_queue);
        }

        if (recorder->stopped && !video_pkt && !audio_pkt) {
            assert(sc_vecdeque_is_empty(&recorder->video_queue));
            assert(sc_vecdeque_is_empty(&recorder->audio_queue));
            sc_mutex_unlock(&recorder->mutex);
            break;
        }

        assert(video_pkt || audio_pkt); // at least one

        sc_mutex_unlock(&recorder->mutex);

        // Ignore further config packets (e.g. on device orientation
        // change). The next non-config packet will have the config packet
        // data prepended.
        if (video_pkt && video_pkt->pts == AV_NOPTS_VALUE) {
            av_packet_free(&video_pkt);
            video_pkt = NULL;
        }

        if (audio_pkt && audio_pkt->pts == AV_NOPTS_VALUE) {
            av_packet_free(&audio_pkt);
            audio_pkt = NULL;
        }

        if (pts_origin == AV_NOPTS_VALUE) {
            if (!recorder->audio && video_pkt) {
                pts_origin = video_pkt->pts;
            } else if (!recorder->video && audio_pkt) {
                pts_origin = audio_pkt->pts;
            } else if (recorder->video && recorder->audio && video_pkt
                    && audio_pkt) {
                pts_origin = MIN(video_pkt->pts, audio_pkt->pts);
            } else if (recorder->stopped) {
                if (video_pkt) {
                    // The recorder is stopped without audio, record the video
                    // packets
                    pts_origin = video_pkt->pts;
                } else {
                    // Fail if there is no video
                    error = true;
                    goto end;
                }
            } else {
                // We need both video and audio packets to initialize pts_origin
                continue;
            }
        }

        assert(pts_origin != AV_NOPTS_VALUE);

        if (video_pkt) {
            video_pkt->pts -= pts_origin;
            video_pkt->dts = video_pkt->pts;

            if (segment_us && segment_next_pts == AV_NOPTS_VALUE) {
                segment_next_pts = video_pkt->pts + segment_us;
            } else if (segment_us && video_pkt->pts >= segment_next_pts) {
                if (video_pkt->flags & AV_PKT_FLAG_KEY) {
                    // The segment muxer cuts on this key frame
                    segment_next_pts += segment_us;
                    key_frame_requested = false;
                } else if (!key_frame_requested
                        && recorder->cbs->on_key_frame_wanted) {
                    recorder->cbs->on_key_frame_wanted(recorder,
                                                      recorder->cbs_userdata);
                    key_frame_requested = true;
                }
            }

            if (video_pkt_previous) {
                // we now know the duration of the previous packet
                video_pkt_previous->duration = video_pkt->pts
                                             - video_pkt_previous->pts;

                int64_t raw_pts = video_pkt_previous->pts + pts_origin;
                bool ok = sc_recorder_write_video(recorder, video_pkt_previous);
                av_packet_free(&video_pkt_previous);
                if (!ok) {
                    LOGE("Could not record video packet");
                    error = true;
                    goto end;
                }
                if (!sc_recorder_write_pts(recorder, raw_pts)) {
                    error = true;
                    goto end;
                }
            }

            video_pkt_previous = video_pkt;
            video_pkt = NULL;
        }

        if (audio_pkt) {
            audio_pkt->pts -= pts_origin;
            audio_pkt->dts = audio_pkt->pts;

            bool ok = sc_recorder_write_audio(recorder, audio_pkt);
            if (!ok) {
                LOGE("Could not record audio packet");
                error = true;
                goto end;
            }

            av_packet_free(&audio_pkt);
            audio_pkt = NULL;
        }
    }

    // Write the last video packet
    AVPacket *last = video_pkt_previous;
    video_pkt_previous = NULL;
    if (last) {
        // assign an arbitrary duration to the last packet
        last->duration = 100000;
        int64_t raw_pts = last->pts + pts_origin;
        bool ok = sc_recorder_write_video(recorder, last);
        if (!ok) {
            // failing to write the last frame is not very serious, no
            // future frame may depend on it, so the resulting file
            // will still be valid
            LOGW("Could not record last packet");
        } else if (!sc_recorder_write_pts(recorder, raw_pts)) {
            error = true;
        }
        av_packet_free(&last);
    }

    int ret = av_write_trailer(recorder->ctx);
    if (ret < 0) {
        LOGE("Failed to write trailer to %s", recorder->filename);
        error = true;
    }

end:
    if (video_pkt) {
        av_packet_free(&video_pkt);
    }
    if (audio_pkt) {
        av_packet_free(&audio_pkt);
    }
    if (video_pkt_previous) {
        av_packet_free(&video_pkt_previous);
    }

    return !error;
}

static bool
sc_recorder_record(struct sc_recorder *recorder) {
    bool ok = sc_recorder_open_output_file(recorder);
    if (!ok) {
        return false;
    }

    ok = sc_recorder_process_packets(recorder);
    sc_recorder_close_output_file(recorder);
    return ok;
}

static int
run_recorder(void *data) {
    struct sc_recorder *recorder = data;

    // Recording is a background task
    bool ok = sc_thread_set_priority(SC_THREAD_PRIORITY_LOW);
    (void) ok; // We don't care if it worked

    bool success = sc_recorder_record(recorder);

    if (recorder->pts_file) {
        bool pts_success = fflush(recorder->pts_file) == 0;
        pts_success = fclose(recorder->pts_file) == 0 && pts_success;
        recorder->pts_file = NULL;
        if (!pts_success) {
            LOGE("Could not finalize record PTS file: %s",
                 recorder->pts_filename);
            success = false;
        }
    }

    sc_mutex_lock(&recorder->mutex);
    // Prevent the producer from pushing any new packet
    recorder->stopped = true;
    // Discard pending packets
    sc_recorder_queue_clear(&recorder->video_queue);
    sc_recorder_queue_clear(&recorder->audio_queue);
    sc_mutex_unlock(&recorder->mutex);

    if (success) {
        const char *format_name = sc_recorder_get_format_name(recorder->format);
        LOGI("Recording complete to %s file: %s", format_name,
                                                  recorder->filename);
    } else {
        LOGE("Recording failed to %s", recorder->filename);
    }

    LOGD("Recorder thread ended");

    recorder->cbs->on_ended(recorder, success, recorder->cbs_userdata);

    return 0;
}

static bool
sc_recorder_set_orientation(AVStream *stream, enum sc_orientation orientation) {
    assert(!sc_orientation_is_mirror(orientation));

    uint8_t *raw_data;
#ifdef SCRCPY_LAVC_HAS_CODECPAR_CODEC_SIDEDATA
    AVPacketSideData *sd =
        av_packet_side_data_new(&stream->codecpar->coded_side_data,
                                &stream->codecpar->nb_coded_side_data,
                                AV_PKT_DATA_DISPLAYMATRIX,
                                sizeof(int32_t) * 9, 0);
    if (!sd) {
        LOG_OOM();
        return false;
    }

    raw_data = sd->data;
#else
    raw_data = av_stream_new_side_data(stream, AV_PKT_DATA_DISPLAYMATRIX,
                                      sizeof(int32_t) * 9);
    if (!raw_data) {
        LOG_OOM();
        return false;
    }
#endif

    int32_t *matrix = (int32_t *) raw_data;

    unsigned rotation = orientation;
    unsigned angle = rotation * 90;

    av_display_rotation_set(matrix, angle);

    return true;
}

static bool
sc_recorder_video_packet_sink_open(struct sc_packet_sink *sink,
                                   const AVCodecParameters *params,
                                   const struct sc_stream_session *session) {
    (void) session;

    struct sc_recorder *recorder = DOWNCAST_VIDEO(sink);
    // only written from this thread, no need to lock
    assert(!recorder->video_init);

    sc_mutex_lock(&recorder->mutex);
    if (recorder->stopped) {
        sc_mutex_unlock(&recorder->mutex);
        return false;
    }

    AVStream *stream = avformat_new_stream(recorder->ctx, NULL);
    if (!stream) {
        sc_mutex_unlock(&recorder->mutex);
        return false;
    }

    int r = avcodec_parameters_copy(stream->codecpar, params);
    if (r < 0) {
        sc_mutex_unlock(&recorder->mutex);
        return false;
    }

    recorder->video_stream.index = stream->index;

    if (recorder->orientation != SC_ORIENTATION_0) {
        if (!sc_recorder_set_orientation(stream, recorder->orientation)) {
            sc_mutex_unlock(&recorder->mutex);
            return false;
        }

        LOGI("Record orientation set to %s",
             sc_orientation_get_name(recorder->orientation));
    }

    enum AVCodecID codec_id = params->codec_id;

    // A config packet is provided for all supported formats except VPx
    recorder->video_expects_config_packet = codec_id != AV_CODEC_ID_VP8
                                         && codec_id != AV_CODEC_ID_VP9;

    recorder->video_init = true;
    sc_cond_signal(&recorder->cond);
    sc_mutex_unlock(&recorder->mutex);

    return true;
}

static void
sc_recorder_video_packet_sink_close(struct sc_packet_sink *sink) {
    struct sc_recorder *recorder = DOWNCAST_VIDEO(sink);
    // only written from this thread, no need to lock
    assert(recorder->video_init);

    sc_mutex_lock(&recorder->mutex);
    // EOS also stops the recorder
    recorder->stopped = true;
    sc_cond_signal(&recorder->cond);
    sc_mutex_unlock(&recorder->mutex);
}

static enum sc_sink_result
sc_recorder_video_packet_sink_push(struct sc_packet_sink *sink,
                                   const AVPacket *packet) {
    struct sc_recorder *recorder = DOWNCAST_VIDEO(sink);
    // only written from this thread, no need to lock
    assert(recorder->video_init);

    sc_mutex_lock(&recorder->mutex);

    if (recorder->stopped) {
        // reject any new packet
        sc_mutex_unlock(&recorder->mutex);
        return SC_SINK_STOPPED;
    }

    AVPacket *rec = sc_recorder_packet_ref(packet);
    if (!rec) {
        sc_mutex_unlock(&recorder->mutex);
        return SC_SINK_KO;
    }

    rec->stream_index = recorder->video_stream.index;

    bool ok = sc_vecdeque_push(&recorder->video_queue, rec);
    if (!ok) {
        LOG_OOM();
        av_packet_free(&rec);
        sc_mutex_unlock(&recorder->mutex);
        av_packet_free(&rec);
        return SC_SINK_KO;
    }

    sc_cond_signal(&recorder->cond);

    sc_mutex_unlock(&recorder->mutex);
    return SC_SINK_OK;
}

static bool
sc_recorder_audio_packet_sink_open(struct sc_packet_sink *sink,
                                   const AVCodecParameters *params,
                                   const struct sc_stream_session *session) {
    (void) session;

    struct sc_recorder *recorder = DOWNCAST_AUDIO(sink);
    assert(recorder->audio);
    // only written from this thread, no need to lock
    assert(!recorder->audio_init);

    sc_mutex_lock(&recorder->mutex);

    AVStream *stream = avformat_new_stream(recorder->ctx, NULL);
    if (!stream) {
        sc_mutex_unlock(&recorder->mutex);
        return false;
    }

    int r = avcodec_parameters_copy(stream->codecpar, params);
    if (r < 0) {
        sc_mutex_unlock(&recorder->mutex);
        return false;
    }

    recorder->audio_stream.index = stream->index;

    // A config packet is provided for all supported formats except raw audio
    recorder->audio_expects_config_packet =
        params->codec_id != AV_CODEC_ID_PCM_S16LE;

    recorder->audio_init = true;
    sc_cond_signal(&recorder->cond);
    sc_mutex_unlock(&recorder->mutex);

    return true;
}

static void
sc_recorder_audio_packet_sink_close(struct sc_packet_sink *sink) {
    struct sc_recorder *recorder = DOWNCAST_AUDIO(sink);
    assert(recorder->audio);
    // only written from this thread, no need to lock
    assert(recorder->audio_init);

    sc_mutex_lock(&recorder->mutex);
    // EOS also stops the recorder
    recorder->stopped = true;
    sc_cond_signal(&recorder->cond);
    sc_mutex_unlock(&recorder->mutex);
}

static enum sc_sink_result
sc_recorder_audio_packet_sink_push(struct sc_packet_sink *sink,
                                   const AVPacket *packet) {
    struct sc_recorder *recorder = DOWNCAST_AUDIO(sink);
    assert(recorder->audio);
    // only written from this thread, no need to lock
    assert(recorder->audio_init);

    sc_mutex_lock(&recorder->mutex);

    if (recorder->stopped) {
        // reject any new packet
        sc_mutex_unlock(&recorder->mutex);
        return SC_SINK_STOPPED;
    }

    AVPacket *rec = sc_recorder_packet_ref(packet);
    if (!rec) {
        sc_mutex_unlock(&recorder->mutex);
        return SC_SINK_KO;
    }

    rec->stream_index = recorder->audio_stream.index;

    bool ok = sc_vecdeque_push(&recorder->audio_queue, rec);
    if (!ok) {
        LOG_OOM();
        sc_mutex_unlock(&recorder->mutex);
        av_packet_free(&rec);
        return SC_SINK_KO;
    }

    sc_cond_signal(&recorder->cond);

    sc_mutex_unlock(&recorder->mutex);
    return SC_SINK_OK;
}

static void
sc_recorder_audio_packet_sink_disable(struct sc_packet_sink *sink) {
    struct sc_recorder *recorder = DOWNCAST_AUDIO(sink);
    assert(recorder->audio);
    // only written from this thread, no need to lock
    assert(!recorder->audio_init);

    LOGW("Audio stream recording disabled");

    sc_mutex_lock(&recorder->mutex);
    recorder->audio = false;
    recorder->audio_init = true;
    sc_cond_signal(&recorder->cond);
    sc_mutex_unlock(&recorder->mutex);
}

static void
sc_recorder_stream_init(struct sc_recorder_stream *stream) {
    stream->index = -1;
    stream->last_pts = AV_NOPTS_VALUE;
}

bool
sc_recorder_init(struct sc_recorder *recorder, const char *filename,
                 const char *pts_filename, uint32_t segment_seconds,
                 enum sc_record_format format, bool video, bool audio,
                 enum sc_orientation orientation,
                 const struct sc_recorder_callbacks *cbs, void *cbs_userdata) {
    assert(!sc_orientation_is_mirror(orientation));

    recorder->filename = strdup(filename);
    if (!recorder->filename) {
        LOG_OOM();
        return false;
    }

    recorder->segment_seconds = segment_seconds;
    recorder->pts_filename = pts_filename ? strdup(pts_filename) : NULL;
    if (pts_filename && !recorder->pts_filename) {
        LOG_OOM();
        free(recorder->filename);
        return false;
    }
    recorder->pts_file = NULL;
    recorder->pts_frame_index = 0;

    bool ok = sc_mutex_init(&recorder->mutex);
    if (!ok) {
        goto error_free_filename;
    }

    ok = sc_cond_init(&recorder->cond);
    if (!ok) {
        goto error_mutex_destroy;
    }

    assert(video || audio);
    recorder->video = video;
    recorder->audio = audio;

    recorder->orientation = orientation;

    sc_vecdeque_init(&recorder->video_queue);
    sc_vecdeque_init(&recorder->audio_queue);
    recorder->stopped = false;

    recorder->video_init = false;
    recorder->audio_init = false;

    recorder->video_expects_config_packet = false;
    recorder->audio_expects_config_packet = false;

    sc_recorder_stream_init(&recorder->video_stream);
    sc_recorder_stream_init(&recorder->audio_stream);

    recorder->format = format;

    if (recorder->pts_filename) {
        recorder->pts_file = fopen(recorder->pts_filename, "w");
        if (!recorder->pts_file) {
            LOGE("Could not open record PTS file: %s", recorder->pts_filename);
            goto error_cond_destroy;
        }
        if (fprintf(recorder->pts_file, "frame,pts_us\n") < 0) {
            LOGE("Could not write record PTS header: %s",
                 recorder->pts_filename);
            fclose(recorder->pts_file);
            recorder->pts_file = NULL;
            goto error_cond_destroy;
        }
    }

    assert(cbs && cbs->on_ended);
    recorder->cbs = cbs;
    recorder->cbs_userdata = cbs_userdata;

    if (video) {
        static const struct sc_packet_sink_ops video_ops = {
            .open = sc_recorder_video_packet_sink_open,
            .close = sc_recorder_video_packet_sink_close,
            .push = sc_recorder_video_packet_sink_push,
        };

        recorder->video_packet_sink.ops = &video_ops;
    }

    if (audio) {
        static const struct sc_packet_sink_ops audio_ops = {
            .open = sc_recorder_audio_packet_sink_open,
            .close = sc_recorder_audio_packet_sink_close,
            .push = sc_recorder_audio_packet_sink_push,
            .disable = sc_recorder_audio_packet_sink_disable,
        };

        recorder->audio_packet_sink.ops = &audio_ops;
    }

    return true;

error_cond_destroy:
    sc_cond_destroy(&recorder->cond);
error_mutex_destroy:
    sc_mutex_destroy(&recorder->mutex);
error_free_filename:
    free(recorder->pts_filename);
    free(recorder->filename);

    return false;
}

bool
sc_recorder_start(struct sc_recorder *recorder) {
    bool ok = sc_thread_create(&recorder->thread, run_recorder,
                               "scrcpy-recorder", recorder);
    if (!ok) {
        LOGE("Could not start recorder thread");
        return false;
    }

    return true;
}

void
sc_recorder_stop(struct sc_recorder *recorder) {
    sc_mutex_lock(&recorder->mutex);
    recorder->stopped = true;
    sc_cond_signal(&recorder->cond);
    sc_mutex_unlock(&recorder->mutex);
}

void
sc_recorder_join(struct sc_recorder *recorder) {
    sc_thread_join(&recorder->thread, NULL);
}

void
sc_recorder_destroy(struct sc_recorder *recorder) {
    sc_vecdeque_destroy(&recorder->video_queue);
    sc_vecdeque_destroy(&recorder->audio_queue);
    if (recorder->pts_file) {
        fclose(recorder->pts_file);
    }
    sc_cond_destroy(&recorder->cond);
    sc_mutex_destroy(&recorder->mutex);
    free(recorder->pts_filename);
    free(recorder->filename);
}
