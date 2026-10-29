#include "latency_probe_calc.h"

#include <assert.h>
#include <stdlib.h>

bool
sc_latency_probe_box_changed(const uint8_t *box, const uint8_t *baseline,
                             size_t len) {
    assert(len);
    unsigned sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum += abs((int) box[i] - (int) baseline[i]);
    }
    return sum > SC_LATENCY_PROBE_CHANGE_THRESHOLD * len;
}

static int
compare_ticks(const void *a, const void *b) {
    sc_tick ta = *(const sc_tick *) a;
    sc_tick tb = *(const sc_tick *) b;
    return (ta > tb) - (ta < tb);
}

sc_tick
sc_latency_probe_percentile(sc_tick *samples, size_t count, unsigned p) {
    assert(count);
    assert(p <= 100);
    qsort(samples, count, sizeof(*samples), compare_ticks);
    // nearest rank: ceil(p / 100 * count), 1-based
    size_t rank = (p * count + 99) / 100;
    return samples[rank ? rank - 1 : 0];
}
