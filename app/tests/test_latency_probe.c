#include "common.h"

#include <assert.h>
#include <string.h>

#include "latency_probe_calc.h"

static void test_box_changed(void) {
    uint8_t base[16];
    uint8_t box[16];
    memset(base, 100, sizeof(base));
    memcpy(box, base, sizeof(box));
    assert(!sc_latency_probe_box_changed(box, base, sizeof(box)));

    // noise below the threshold
    for (size_t i = 0; i < sizeof(box); ++i) {
        box[i] = i % 2 ? 108 : 92;
    }
    assert(!sc_latency_probe_box_changed(box, base, sizeof(box)));

    // a touch spot over half of the box
    memcpy(box, base, sizeof(box));
    memset(box, 160, 8);
    assert(sc_latency_probe_box_changed(box, base, sizeof(box)));

    // darker also counts
    memset(box, 40, sizeof(box));
    assert(sc_latency_probe_box_changed(box, base, sizeof(box)));
}

static void test_percentile(void) {
    sc_tick s[] = {50, 10, 40, 20, 30};
    assert(sc_latency_probe_percentile(s, 5, 50) == 30);
    assert(sc_latency_probe_percentile(s, 5, 0) == 10);
    assert(sc_latency_probe_percentile(s, 5, 100) == 50);
    assert(sc_latency_probe_percentile(s, 5, 95) == 50);
    assert(sc_latency_probe_percentile(s, 5, 20) == 10);

    sc_tick one[] = {7};
    assert(sc_latency_probe_percentile(one, 1, 95) == 7);
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_box_changed();
    test_percentile();
    return 0;
}
