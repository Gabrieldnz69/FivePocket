package dev.recompbootstrap.smoke;

import java.util.Arrays;

/**
 * Update 1 touch optimizations (owner 2026-11-08: "touch control optimizations can begin whenever"): how old each touch MOVE is when
 * QolTouch publishes the pad state from it (the event's own time to the publish, uptime ms). Collected over one gesture and reported
 * once when it ends (QolTouch logs "TOUCH_LAT moves=N p50= p95= max= unbuffered=0|1"). Android holds touch moves back until the window's
 * next frame (batching); with unbuffered dispatch each move is delivered as it arrives. This line is what the device check compares.
 * Pure Java (host-tested: TouchLatencyTest).
 */
final class TouchLatency {
    private final int[] ages = new int[512];
    private int n, over;   // the ages kept; moves past the buffer are counted, not kept

    void add(long ageMs) {
        if (n < ages.length) ages[n++] = (int) Math.max(0, Math.min(ageMs, 10_000));
        else ++over;
    }

    /** "moves=N p50=A p95=B max=C" (ms, nearest rank) over the moves added since the last call, or null with none; then empty. */
    String take() {
        if (n == 0) { over = 0; return null; }
        final int[] a = Arrays.copyOf(ages, n);
        Arrays.sort(a);
        final String s = "moves=" + (n + over) + " p50=" + a[rank(n, 50)] + " p95=" + a[rank(n, 95)] + " max=" + a[n - 1];
        n = 0; over = 0;
        return s;
    }

    /** The nearest-rank index of percentile p in n sorted values. */
    static int rank(int n, int p) {
        return Math.max(0, Math.min(n - 1, (int) Math.ceil(p / 100.0 * n) - 1));
    }
}
