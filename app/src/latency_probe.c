#include "latency_probe.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>

#include "control_msg.h"
#include "util/log.h"

#define DOWNCAST(SINK) container_of(SINK, struct sc_latency_probe, frame_sink)

#define PROBE_INTERVAL SC_TICK_FROM_SEC(1)
#define PROBE_TIMEOUT SC_TICK_FROM_SEC(2)
#define RETRY_DELAY SC_TICK_FROM_MS(100)

// Read the luma box around the probe position.
// The probe mutex must be held (sw_frame is shared).
static bool
sample_box(struct sc_latency_probe *probe, const AVFrame *frame,
           uint8_t *box) {
    const AVFrame *f = frame;
    if (frame->hw_frames_ctx) {
        av_frame_unref(probe->sw_frame);
        if (av_hwframe_transfer_data(probe->sw_frame, frame, 0) < 0) {
            if (!probe->warned) {
                LOGW("Latency probe: could not download a hardware frame");
                probe->warned = true;
            }
            return false;
        }
        f = probe->sw_frame;
    }

    // Any format whose first component is 8-bit luma in plane 0 (yuv*p, nv12)
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(f->format);
    if (!desc || (desc->flags & AV_PIX_FMT_FLAG_RGB)
            || desc->comp[0].plane != 0 || desc->comp[0].depth != 8
            || desc->comp[0].step != 1) {
        if (!probe->warned) {
            LOGW("Latency probe: unsupported frame format %s",
                 desc ? desc->name : "?");
            probe->warned = true;
        }
        return false;
    }

    int cx = (int) (probe->x * (f->width - 1));
    int cy = (int) (probe->y * (f->height - 1));
    int r = SC_LATENCY_PROBE_BOX_RADIUS;
    size_t i = 0;
    for (int dy = -r; dy <= r; ++dy) {
        int y = CLAMP(cy + dy, 0, f->height - 1);
        for (int dx = -r; dx <= r; ++dx) {
            int x = CLAMP(cx + dx, 0, f->width - 1);
            box[i++] = f->data[0][y * f->linesize[0] + x];
        }
    }
    assert(i == SC_LATENCY_PROBE_BOX_LEN);
    return true;
}

static void
inject_touch(struct sc_latency_probe *probe, bool down) {
    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT;
    msg.inject_touch_event.action = down ? AMOTION_EVENT_ACTION_DOWN
                                         : AMOTION_EVENT_ACTION_UP;
    struct sc_size size = {probe->touch_width, probe->touch_height};
    msg.inject_touch_event.position.screen_size = size;
    msg.inject_touch_event.position.point.x =
        (int32_t) (probe->x * (size.width - 1));
    msg.inject_touch_event.position.point.y =
        (int32_t) (probe->y * (size.height - 1));
    msg.inject_touch_event.pointer_id = SC_POINTER_ID_LATENCY_PROBE;
    msg.inject_touch_event.pressure = down ? 1.0f : 0.0f;
    msg.inject_touch_event.action_button = 0;
    msg.inject_touch_event.buttons = 0;

    if (!sc_controller_push_msg(probe->controller, &msg)) {
        LOGW("Latency probe: could not inject touch");
    }
}

static void
log_summary(struct sc_latency_probe *probe, const char *prefix) {
    if (!probe->count) {
        LOGI("%s: no measurement (%u lost)", prefix, probe->lost);
        return;
    }
    sc_tick sorted[SC_LATENCY_PROBE_MAX_SAMPLES];
    size_t n = probe->count;
    memcpy(sorted, probe->samples, n * sizeof(*sorted));
    LOGI("%s: n=%" SC_PRIsizet " min=%" PRItick " p50=%" PRItick " p95=%" PRItick
         " max=%" PRItick " ms, %u lost", prefix, n,
         SC_TICK_TO_MS(sc_latency_probe_percentile(sorted, n, 0)),
         SC_TICK_TO_MS(sc_latency_probe_percentile(sorted, n, 50)),
         SC_TICK_TO_MS(sc_latency_probe_percentile(sorted, n, 95)),
         SC_TICK_TO_MS(sc_latency_probe_percentile(sorted, n, 100)),
         probe->lost);
}

static int
run_probe(void *data) {
    struct sc_latency_probe *probe = data;

    sc_mutex_lock(&probe->mutex);
    while (!probe->stopped) {
        sc_tick now = sc_tick_now();
        if (now >= probe->deadline) {
            if (probe->waiting) {
                ++probe->lost;
                LOGW("Latency probe: no change within %" PRItick " ms",
                     SC_TICK_TO_MS(PROBE_TIMEOUT));
                inject_touch(probe, false);
                probe->waiting = false;
                probe->deadline = now + PROBE_INTERVAL;
            } else if (probe->last_frame->buf[0]
                    && sample_box(probe, probe->last_frame,
                                  probe->baseline)) {
                // The baseline is the latest frame (the screen may be static)
                probe->touch_width = probe->last_frame->width;
                probe->touch_height = probe->last_frame->height;
                av_frame_unref(probe->last_frame);
                probe->waiting = true;
                probe->down_time = sc_tick_now();
                inject_touch(probe, true);
                probe->deadline = probe->down_time + PROBE_TIMEOUT;
            } else {
                // No frame yet
                probe->deadline = now + RETRY_DELAY;
            }
        }
        sc_cond_timedwait(&probe->cond, &probe->mutex, probe->deadline);
    }
    if (probe->waiting) {
        inject_touch(probe, false);
        probe->waiting = false;
    }
    sc_mutex_unlock(&probe->mutex);

    return 0;
}

static bool
sc_latency_probe_frame_sink_open(struct sc_frame_sink *sink,
                                 const AVCodecContext *ctx,
                                 const struct sc_stream_session *session) {
    (void) sink;
    (void) ctx;
    (void) session;
    return true;
}

static void
sc_latency_probe_frame_sink_close(struct sc_frame_sink *sink) {
    (void) sink;
}

static enum sc_sink_result
sc_latency_probe_frame_sink_push(struct sc_frame_sink *sink,
                                 const AVFrame *frame) {
    struct sc_latency_probe *probe = DOWNCAST(sink);

    sc_mutex_lock(&probe->mutex);
    if (!probe->waiting) {
        // Keep a reference to the latest frame for the next baseline
        av_frame_unref(probe->last_frame);
        if (av_frame_ref(probe->last_frame, frame) < 0) {
            LOGW("Latency probe: could not reference frame");
        }
    } else if (frame->width == probe->touch_width
            && frame->height == probe->touch_height) {
        uint8_t box[SC_LATENCY_PROBE_BOX_LEN];
        if (sample_box(probe, frame, box)
                && sc_latency_probe_box_changed(box, probe->baseline,
                                                SC_LATENCY_PROBE_BOX_LEN)) {
            sc_tick now = sc_tick_now();
            sc_tick latency = now - probe->down_time;
            if (probe->count < SC_LATENCY_PROBE_MAX_SAMPLES) {
                probe->samples[probe->count++] = latency;
            }
            LOGI("Latency probe: %" PRItick " ms", SC_TICK_TO_MS(latency));
            if (probe->count % 10 == 0) {
                log_summary(probe, "Latency probe");
            }
            inject_touch(probe, false);
            probe->waiting = false;
            // Let the spot fade out before the next baseline
            probe->deadline = now + PROBE_INTERVAL;
            sc_cond_signal(&probe->cond);
        }
    }
    sc_mutex_unlock(&probe->mutex);

    return SC_SINK_OK;
}

bool
sc_latency_probe_init(struct sc_latency_probe *probe,
                      struct sc_controller *controller, float x, float y) {
    assert(x >= 0 && x <= 1 && y >= 0 && y <= 1);

    probe->last_frame = av_frame_alloc();
    probe->sw_frame = av_frame_alloc();
    if (!probe->last_frame || !probe->sw_frame) {
        LOG_OOM();
        av_frame_free(&probe->last_frame);
        av_frame_free(&probe->sw_frame);
        return false;
    }

    if (!sc_mutex_init(&probe->mutex)) {
        goto error_free_frames;
    }
    if (!sc_cond_init(&probe->cond)) {
        sc_mutex_destroy(&probe->mutex);
        goto error_free_frames;
    }

    probe->controller = controller;
    probe->x = x;
    probe->y = y;
    probe->stopped = false;
    probe->waiting = false;
    probe->deadline = 0;
    probe->count = 0;
    probe->lost = 0;
    probe->warned = false;

    static const struct sc_frame_sink_ops ops = {
        .open = sc_latency_probe_frame_sink_open,
        .close = sc_latency_probe_frame_sink_close,
        .push = sc_latency_probe_frame_sink_push,
    };
    probe->frame_sink.ops = &ops;

    return true;

error_free_frames:
    av_frame_free(&probe->last_frame);
    av_frame_free(&probe->sw_frame);
    return false;
}

bool
sc_latency_probe_start(struct sc_latency_probe *probe) {
    probe->deadline = sc_tick_now() + PROBE_INTERVAL;
    bool ok = sc_thread_create(&probe->thread, run_probe, "scrcpy-lprobe",
                               probe);
    if (!ok) {
        LOGE("Could not start latency probe thread");
        return false;
    }
    LOGI("Latency probe: touching (%.2f, %.2f) every second", probe->x,
         probe->y);
    return true;
}

void
sc_latency_probe_stop(struct sc_latency_probe *probe) {
    sc_mutex_lock(&probe->mutex);
    probe->stopped = true;
    sc_cond_signal(&probe->cond);
    sc_mutex_unlock(&probe->mutex);
}

void
sc_latency_probe_join(struct sc_latency_probe *probe) {
    sc_thread_join(&probe->thread, NULL);
}

void
sc_latency_probe_destroy(struct sc_latency_probe *probe) {
    log_summary(probe, "Latency probe summary");
    sc_cond_destroy(&probe->cond);
    sc_mutex_destroy(&probe->mutex);
    av_frame_free(&probe->last_frame);
    av_frame_free(&probe->sw_frame);
}
