package com.genymobile.scrcpy.video;

import com.genymobile.scrcpy.util.Ln;

import java.util.Arrays;
import java.util.Locale;

/**
 * Per-second statistics of the delay between frame capture and encoder output, on the device.
 * <p>
 * With a Surface input, the encoder output PTS is the timestamp of the captured buffer, in the CLOCK_MONOTONIC base used by
 * {@link System#nanoTime()}, so both are directly comparable (same clock, same device). Not allocation-free on purpose: it is
 * only enabled for measurements (--latency-stats).
 */
public final class EncodeLatencyStats {

    private static final long WINDOW_US = 1_000_000;
    private static final int CAPACITY = 1024;

    private final long[] samples = new long[CAPACITY];
    private int count;
    private int dropped; // samples outside of the expected range (repeated frames, clock mismatch)
    private long windowStartUs;

    public void onFrame(long ptsUs) {
        long nowUs = System.nanoTime() / 1000;
        if (windowStartUs == 0) {
            windowStartUs = nowUs;
        }
        long latencyUs = nowUs - ptsUs;
        if (latencyUs < 0 || latencyUs > 10_000_000) {
            ++dropped;
        } else if (count < CAPACITY) {
            samples[count++] = latencyUs;
        }
        if (nowUs - windowStartUs >= WINDOW_US) {
            report();
            count = 0;
            dropped = 0;
            windowStartUs = nowUs;
        }
    }

    private void report() {
        if (count == 0) {
            Ln.i("ENC capture->out n=0 out_of_range=" + dropped);
            return;
        }
        Arrays.sort(samples, 0, count);
        Ln.i(String.format(Locale.ROOT, "ENC capture->out p50=%.2f p95=%.2f p99=%.2f max=%.2f n=%d out_of_range=%d (ms)", percentile(50), percentile(95),
                percentile(99), samples[count - 1] / 1000.0, count, dropped));
    }

    private double percentile(int p) {
        int index = (int) Math.ceil(p / 100.0 * count) - 1;
        return samples[Math.max(index, 0)] / 1000.0;
    }
}
