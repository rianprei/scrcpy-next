#include "latency_stats.h"

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "util/log.h"
#include "util/thread.h"

// Number of frame records kept in the fixed circular buffer (power of 2)
#define SC_LATENCY_SLOTS 1024

// Maximum number of samples aggregated in a reporting window
#define SC_LATENCY_WINDOW_SAMPLES 1024

// Reporting interval
#define SC_LATENCY_INTERVAL SC_TICK_FROM_SEC(1)

struct sc_latency_sample {
    sc_tick t_recv;
    sc_tick t_decode;
    sc_tick t_present;
    bool decoded;
    bool presented;
};

static struct sc_latency_stats {
    bool enabled;
    bool thread_started;
    bool interrupted;

    struct sc_mutex mutex;
    struct sc_cond cond;
    struct sc_thread thread;

    // Fixed circular buffer, indexed by frame identifier
    struct sc_latency_sample samples[SC_LATENCY_SLOTS];
    uint64_t next_id;

    // Aggregation window
    double recv_present[SC_LATENCY_WINDOW_SAMPLES];
    double decode[SC_LATENCY_WINDOW_SAMPLES];
    double jitter[SC_LATENCY_WINDOW_SAMPLES];
    size_t nb_recv_present;
    size_t nb_decode;
    size_t nb_jitter;

    uint32_t nb_received;
    uint32_t nb_dropped;
    uint32_t nb_rendered;

    sc_tick last_recv;
    bool has_last_recv;
} stats;

bool
sc_latency_stats_init(void) {
    assert(!stats.enabled);

    bool ok = sc_mutex_init(&stats.mutex);
    if (!ok) {
        LOG_OOM();
        return false;
    }

    ok = sc_cond_init(&stats.cond);
    if (!ok) {
        LOG_OOM();
        sc_mutex_destroy(&stats.mutex);
        return false;
    }

    memset(&stats.samples, 0, sizeof(stats.samples));
    stats.next_id = 0;
    stats.nb_recv_present = 0;
    stats.nb_decode = 0;
    stats.nb_jitter = 0;
    stats.nb_received = 0;
    stats.nb_dropped = 0;
    stats.nb_rendered = 0;
    stats.last_recv = 0;
    stats.has_last_recv = false;
    stats.thread_started = false;
    stats.interrupted = false;

    stats.enabled = true;

    return true;
}

void
sc_latency_stats_destroy(void) {
    if (!stats.enabled) {
        return;
    }

    stats.enabled = false;

    sc_cond_destroy(&stats.cond);
    sc_mutex_destroy(&stats.mutex);
}

bool
sc_latency_stats_enabled(void) {
    return stats.enabled;
}

static int
compare_double(const void *a, const void *b) {
    double da = *(const double *) a;
    double db = *(const double *) b;
    if (da < db) {
        return -1;
    }
    if (da > db) {
        return 1;
    }
    return 0;
}

// Compute the "p" percentile (in [0, 1]) of the given samples, by linear
// interpolation. The samples are sorted in a local copy, so the input array
// is left untouched.
static double
percentile(const double *values, size_t n, double p) {
    if (!n) {
        return NAN;
    }

    double sorted[SC_LATENCY_WINDOW_SAMPLES];
    assert(n <= SC_LATENCY_WINDOW_SAMPLES);
    memcpy(sorted, values, n * sizeof(double));
    qsort(sorted, n, sizeof(double), compare_double);

    double index = p * (double) (n - 1);
    size_t low = (size_t) index;
    size_t high = low + 1 < n ? low + 1 : low;
    double frac = index - (double) low;

    return sorted[low] * (1.0 - frac) + sorted[high] * frac;
}

// Print the statistics of the current window. The mutex must be locked.
static void
print_window(void) {
    double p50_rp = percentile(stats.recv_present, stats.nb_recv_present, 0.50);
    double p95_rp = percentile(stats.recv_present, stats.nb_recv_present, 0.95);
    double p99_rp = percentile(stats.recv_present, stats.nb_recv_present, 0.99);
    double p50_dc = percentile(stats.decode, stats.nb_decode, 0.50);
    double p95_dc = percentile(stats.decode, stats.nb_decode, 0.95);
    double p99_dc = percentile(stats.decode, stats.nb_decode, 0.99);
    double p50_jt = percentile(stats.jitter, stats.nb_jitter, 0.50);
    double p95_jt = percentile(stats.jitter, stats.nb_jitter, 0.95);

    LOGI("LAT recv->present p50=%.2f p95=%.2f p99=%.2f "
         "decode p50=%.2f p95=%.2f p99=%.2f "
         "jitter p50=%.2f p95=%.2f "
         "fps=%.1f dropped=%" PRIu32 " received=%" PRIu32 " (ms)",
         p50_rp, p95_rp, p99_rp,
         p50_dc, p95_dc, p99_dc,
         p50_jt, p95_jt,
         (double) stats.nb_rendered, stats.nb_dropped, stats.nb_received);

    stats.nb_recv_present = 0;
    stats.nb_decode = 0;
    stats.nb_jitter = 0;
    stats.nb_received = 0;
    stats.nb_dropped = 0;
    stats.nb_rendered = 0;
}

static int
run_latency_stats(void *data) {
    (void) data;

    sc_mutex_lock(&stats.mutex);

    while (!stats.interrupted) {
        sc_tick deadline = sc_tick_now() + SC_LATENCY_INTERVAL;
        sc_cond_timedwait(&stats.cond, &stats.mutex, deadline);
        if (stats.interrupted) {
            break;
        }
        print_window();
    }

    sc_mutex_unlock(&stats.mutex);

    return 0;
}

bool
sc_latency_stats_start(void) {
    assert(stats.enabled);
    assert(!stats.thread_started);

    bool ok = sc_thread_create(&stats.thread, run_latency_stats,
                               "scrcpy-latency", NULL);
    if (!ok) {
        LOGE("Could not start latency stats thread");
        return false;
    }

    stats.thread_started = true;

    return true;
}

void
sc_latency_stats_stop(void) {
    if (!stats.enabled || !stats.thread_started) {
        return;
    }

    sc_mutex_lock(&stats.mutex);
    stats.interrupted = true;
    sc_cond_signal(&stats.cond);
    sc_mutex_unlock(&stats.mutex);
}

void
sc_latency_stats_interrupt(void) {
    if (!stats.enabled || !stats.thread_started) {
        return;
    }

    sc_mutex_lock(&stats.mutex);
    stats.interrupted = true;
    sc_cond_signal(&stats.cond);
    sc_mutex_unlock(&stats.mutex);
}

void
sc_latency_stats_join(void) {
    if (!stats.enabled || !stats.thread_started) {
        return;
    }

    sc_thread_join(&stats.thread, NULL);
    stats.thread_started = false;
}

uint64_t
sc_latency_stats_recv(sc_tick now) {
    if (!stats.enabled) {
        return 0;
    }

    sc_mutex_lock(&stats.mutex);

    uint64_t id = ++stats.next_id;

    struct sc_latency_sample *sample = &stats.samples[id % SC_LATENCY_SLOTS];
    sample->t_recv = now;
    sample->t_decode = 0;
    sample->t_present = 0;
    sample->decoded = false;
    sample->presented = false;

    if (stats.has_last_recv) {
        sc_tick delta = now - stats.last_recv;
        if (delta >= 0 && stats.nb_jitter < SC_LATENCY_WINDOW_SAMPLES) {
            stats.jitter[stats.nb_jitter++] = (double) SC_TICK_TO_MS(delta);
        }
    }
    stats.last_recv = now;
    stats.has_last_recv = true;

    sc_mutex_unlock(&stats.mutex);

    return id;
}

void
sc_latency_stats_decode(uint64_t id, sc_tick start, sc_tick end) {
    if (!stats.enabled || !id) {
        return;
    }

    sc_mutex_lock(&stats.mutex);

    struct sc_latency_sample *sample = &stats.samples[id % SC_LATENCY_SLOTS];
    sample->t_decode = end;
    sample->decoded = true;

    if (stats.nb_decode < SC_LATENCY_WINDOW_SAMPLES) {
        stats.decode[stats.nb_decode++] = (double) SC_TICK_TO_MS(end - start);
    }

    sc_mutex_unlock(&stats.mutex);
}

void
sc_latency_stats_received(uint64_t id) {
    if (!stats.enabled || !id) {
        return;
    }

    sc_mutex_lock(&stats.mutex);
    stats.nb_received++;
    sc_mutex_unlock(&stats.mutex);
}

void
sc_latency_stats_present(uint64_t id, sc_tick now, bool dropped) {
    if (!stats.enabled || !id) {
        return;
    }

    sc_mutex_lock(&stats.mutex);

    struct sc_latency_sample *sample = &stats.samples[id % SC_LATENCY_SLOTS];
    sample->t_present = now;
    sample->presented = true;

    if (dropped) {
        stats.nb_dropped++;
    } else {
        stats.nb_rendered++;
        if (sample->t_recv) {
            sc_tick delta = now - sample->t_recv;
            if (delta >= 0
                    && stats.nb_recv_present < SC_LATENCY_WINDOW_SAMPLES) {
                stats.recv_present[stats.nb_recv_present++] =
                    (double) SC_TICK_TO_MS(delta);
            }
        }
    }

    sc_mutex_unlock(&stats.mutex);
}
