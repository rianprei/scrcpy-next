#ifndef SC_LATENCY_STATS_H
#define SC_LATENCY_STATS_H

#include "common.h"

#include "util/tick.h"

/**
 * Per-stage latency instrumentation.
 *
 * The three stages are measured on the host monotonic clock (sc_tick_now()):
 *
 *   - reception: the video packet has been fully read from the socket;
 *   - decoding: the decoder thread produced the corresponding frame;
 *   - application: the frame has been applied to the texture, before the
 *     render (or discarded without being applied).
 *
 * The reported "recv->apply" delay is the time between the reception of a
 * packet and the application (or the discard) of the frame it produced. It
 * does not include the capture, the encoding and the scanout on the device.
 *
 * The instrumentation is disabled by default. When enabled, it never
 * allocates in the hot path: the frame records are stored in a fixed
 * circular buffer, and the reporting window uses fixed-size arrays.
 */

bool
sc_latency_stats_init(void);

void
sc_latency_stats_destroy(void);

bool
sc_latency_stats_start(void);

void
sc_latency_stats_stop(void);

void
sc_latency_stats_interrupt(void);

void
sc_latency_stats_join(void);

bool
sc_latency_stats_enabled(void);

/**
 * Record the reception of a video packet.
 *
 * Returns the identifier to attach to the packet (0 if the instrumentation is
 * disabled). The identifier is carried over to the decoded frame.
 */
uint64_t
sc_latency_stats_recv(sc_tick now);

/**
 * Record the decoding of the frame with the given identifier.
 *
 * "start" is the instant the decoder started to work on the packet, "end"
 * the instant the frame was produced.
 */
void
sc_latency_stats_decode(uint64_t id, sc_tick start, sc_tick end);

/**
 * Record that a frame entered the rendering pipeline.
 */
void
sc_latency_stats_received(uint64_t id);

/**
 * Record the presentation (or the discard) of the frame with the given
 * identifier.
 */
void
sc_latency_stats_present(uint64_t id, sc_tick now, bool dropped);

#endif
