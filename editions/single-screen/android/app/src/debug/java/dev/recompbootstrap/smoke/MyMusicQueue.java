package dev.recompbootstrap.smoke;

/**
 * My Music (Update 1 P6): the play order over the scanned tracks and the place in it. Pure Java (the host test drives it).
 * Next wraps to the start (a new shuffle each time round when shuffling); Previous restarts the track when more than RESTART_MS of it
 * has played, else goes one back. A track that fails to play is marked bad for this session and skipped; when every track is bad
 * current() is -1.
 */
final class MyMusicQueue {
    static final long RESTART_MS = 3000;
    private int[] order = new int[0];
    private boolean[] bad = new boolean[0];
    private int at;
    private boolean shuffle;
    private long seed;

    /** A new order over n tracks; seed: the shuffle's seed (the player keeps one per session). */
    void reset(int n, boolean shuffle, long seed) {
        this.shuffle = shuffle;
        this.seed = seed;
        order = MyMusicLibrary.order(n, shuffle, seed);
        bad = new boolean[Math.max(0, n)];
        at = 0;
        if (n > 0 && bad[order[0]]) next();
    }

    int size() { return order.length; }

    /**
     * After reset(): the order goes on from this track (the one playing when the folder was read again or the order changed). In order:
     * the place moves to it. Shuffled: it moves to the front of the new order, so every other track still comes once before the wrap.
     */
    void keep(int track) {
        for (int i = 0; i < order.length; ++i) {
            if (order[i] != track) continue;
            if (shuffle) { order[i] = order[0]; order[0] = track; at = 0; } else at = i;
            return;
        }
    }

    /** The track index to play, or -1 when there is none (no tracks, or every one failed). */
    int current() {
        if (order.length == 0 || allBad()) return -1;
        return order[at];
    }

    boolean allBad() {
        if (bad.length == 0) return false;
        for (boolean b : bad) if (!b) return false;
        return true;
    }

    /** The next playable track (wrapping; a new shuffle when the order starts over); -1 when none. */
    int next() {
        if (order.length == 0 || allBad()) return -1;
        for (int step = 0; step < order.length; ++step) {
            if (++at >= order.length) {
                at = 0;
                if (shuffle) order = MyMusicLibrary.order(order.length, true, ++seed);
            }
            if (!bad[order[at]]) return order[at];
        }
        return -1;
    }

    /** Previous: the same track again after RESTART_MS of it, else the playable track before it; -1 when none. */
    int previous(long playedMs) {
        if (order.length == 0 || allBad()) return -1;
        if (playedMs > RESTART_MS && !bad[order[at]]) return order[at];
        for (int step = 0; step < order.length; ++step) {
            if (--at < 0) at = order.length - 1;
            if (!bad[order[at]]) return order[at];
        }
        return -1;
    }

    /** The track failed (MediaPlayer error, unreadable file): never chosen again this session. Returns the next playable one or -1. */
    int failed(int track) {
        if (track >= 0 && track < bad.length) bad[track] = true;
        return next();
    }
}
