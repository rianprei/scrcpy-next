#ifndef SC_LATENCY_PROBE_H
#define SC_LATENCY_PROBE_H

#include "common.h"

#include <stdbool.h>
#include <stdint.h>
#include <libavutil/frame.h>

#include "controller.h"
#include "latency_probe_calc.h"
#include "trait/frame_sink.h"
#include "util/thread.h"
#include "util/tick.h"

/**
 * Round-trip latency probe (--latency-probe).
 *
 * Every second, a touch is injected at a fixed position of the device screen
 * (with "pointer location" enabled, so that the device draws a crosshair:
 * "show touches" ignores injected events), and the delay until the luma of a
 * small box around this position changes in a decoded frame is measured. It covers the control channel, the capture, the
 * encoding, the transport and the decoding (not the rendering on the
 * computer).
 *
 * The measured box must not change by itself (use a static screen area, where
 * a tap does nothing, e.g. an empty area of the home screen).
 */

#define SC_LATENCY_PROBE_BOX_RADIUS 4
#define SC_LATENCY_PROBE_BOX_LEN \
    ((2 * SC_LATENCY_PROBE_BOX_RADIUS + 1) * (2 * SC_LATENCY_PROBE_BOX_RADIUS + 1))
#define SC_LATENCY_PROBE_MAX_SAMPLES 4096

struct sc_latency_probe {
    struct sc_frame_sink frame_sink; // receives the decoded video frames

    struct sc_controller *controller;
    float x; // normalized probe position
    float y;

    sc_thread thread;
    sc_mutex mutex;
    sc_cond cond;
    bool stopped;

    bool waiting; // a touch is down, waiting for the spot
    sc_tick down_time;
    sc_tick deadline; // next probe (if !waiting) or timeout (if waiting)
    uint16_t touch_width; // frame size used for the injected touch
    uint16_t touch_height;
    AVFrame *last_frame; // latest frame received while not waiting
    AVFrame *sw_frame; // to download hardware frames
    uint8_t baseline[SC_LATENCY_PROBE_BOX_LEN];

    sc_tick samples[SC_LATENCY_PROBE_MAX_SAMPLES];
    unsigned count;
    unsigned lost;
    bool warned; // a frame could not be sampled (logged once)
};

bool
sc_latency_probe_init(struct sc_latency_probe *probe,
                      struct sc_controller *controller, float x, float y);

bool
sc_latency_probe_start(struct sc_latency_probe *probe);

void
sc_latency_probe_stop(struct sc_latency_probe *probe);

void
sc_latency_probe_join(struct sc_latency_probe *probe);

// Print the summary
void
sc_latency_probe_destroy(struct sc_latency_probe *probe);

#endif
