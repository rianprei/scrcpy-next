#ifndef SC_LATENCY_PROBE_CALC_H
#define SC_LATENCY_PROBE_CALC_H

#include "common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util/tick.h"

// Pure helpers of --latency-probe (see latency_probe.h)

// Mean absolute difference (in luma levels) above which a probe box changed
#define SC_LATENCY_PROBE_CHANGE_THRESHOLD 10

/**
 * Return true if the luma box differs from the baseline (mean absolute
 * difference above SC_LATENCY_PROBE_CHANGE_THRESHOLD).
 */
bool
sc_latency_probe_box_changed(const uint8_t *box, const uint8_t *baseline,
                             size_t len);

/**
 * Sort the samples in place and return the value at the given percentile
 * (nearest rank, p in [0; 100]). The count must not be 0.
 */
sc_tick
sc_latency_probe_percentile(sc_tick *samples, size_t count, unsigned p);

#endif
