package dev.recompbootstrap.smoke;

import java.io.File;
import java.util.Collections;
import java.util.List;
import java.util.function.LongSupplier;

/**
 * My Music (Update 1 P6): the player's state machine, which decides whether the game keeps its music muted. Pure Java (no android.*):
 * the host test (MyMusicTest.engine) drives it with a fake clock, game, media player and scheduler. MyMusicPlayer is the thin Android
 * adapter (MediaPlayer, AudioManager, a HandlerThread).
 *
 * Threads: every method runs on the player thread (the Scheduler's), except foreground(), release(), info() and trackCount(), which
 * any thread may call. Nothing here waits or allocates per tick (one tick: a few field reads, one JNI load, at most one JNI store).
 *
 * What the game hears (recomp_my_music_v1, my_music.h):
 *  - while not idle, a report every tick (TICK_MS): the library treats a report older than 1 s as idle (a stopped app never keeps the
 *    game's music muted), so the heartbeat must never stop while My Music plays or pauses;
 *  - idle exactly once when My Music stops (Off, no tracks, every track failed); then the tick stops once the fade-out ended;
 *  - "playing" only while the platform player really plays and the rules say play; "paused" at once when the rules pause;
 *  - release(): idle at once from the caller's thread (no tick after it reports again), and idle again as the player thread's
 *    last word, after every queued task was dropped.
 * Audio focus: asked for when the music starts. Loss: pause, and ask again only after Next / Previous / Look For New Music or a row
 * change. Transient loss: pause, then play on regain. Duck: 30 % (MyMusicSettings.gain). Given back when stopped or in the background.
 * Errors: a track that cannot be opened or fails while preparing is skipped for the session (MyMusicQueue.failed); completion plays
 * the next track.
 */
final class MyMusicEngine {
    static final long TICK_MS = 100, FADE_MS = 300, FADE_STEP_MS = 30;
    /** A gap between two heartbeat reports above this is logged, once per new largest gap (E-SR5: the 1 s staleness rule's margin). */
    static final long LATE_GAP_MS = 300;
    /** AudioManager's focus changes (the same values: AUDIOFOCUS_GAIN, _LOSS, _LOSS_TRANSIENT, _LOSS_TRANSIENT_CAN_DUCK). */
    static final int FOCUS_GAIN = 1, FOCUS_LOSS = -1, FOCUS_LOSS_TRANSIENT = -2, FOCUS_DUCK = -3;

    /** The library's two exports through the activity's JNI (-100: not in this library). */
    interface Game { int state(); int report(int state); }

    /** The platform player and audio focus. Its callbacks (prepared, completed, error, focus) come back on the player thread. */
    interface Media {
        /** Resets the player and opens the file, preparing asynchronously: prepared() or error() follows. false: it cannot be opened. */
        boolean open(File file);
        void start();
        void pause();
        boolean playing();
        long position();
        void volume(float gain);
        void reset();
        void release();
        /** AUDIOFOCUS_GAIN for music; true when granted. */
        boolean requestFocus();
        void abandonFocus();
    }

    /** The player thread's queue (Android: a Handler on the HandlerThread). */
    interface Scheduler { void post(Runnable r); void postDelayed(Runnable r, long ms); void cancelAll(); }

    interface Log { void i(String line); }

    private final Game game;
    private final Media media;
    private final Scheduler s;
    private final LongSupplier clock;
    private final Log log;
    private final long seed;

    // ---- player-thread state
    private MyMusicSettings settings = MyMusicSettings.DEFAULTS;
    private List<MyMusicLibrary.Track> tracks = Collections.emptyList();
    private final MyMusicQueue queue = new MyMusicQueue();
    private final MyMusicRules.Debounce debounce = new MyMusicRules.Debounce();
    private int loaded = -1;              // the track index open in the media player (prepared or preparing); -1 none
    private boolean prepared, wantStart;  // wantStart: start as soon as prepared
    private float fade, fadeTarget;
    private boolean fading;
    private boolean focusHeld, focusLost, ducked;
    private int lastReport = -1;
    private long lastReportMs = -1, maxGapMs;
    private MyMusicRules.Why lastWhy;
    private boolean ticking, released, staleSeen;
    private int staleCount;

    // ---- any thread
    private volatile boolean foreground = true, releasing;
    private volatile String info = MyMusicRules.text(MyMusicRules.Why.OFF, "", 0);
    private volatile int trackCount;

    MyMusicEngine(Game game, Media media, Scheduler scheduler, LongSupplier clock, Log log, long seed) {
        this.game = game;
        this.media = media;
        this.s = scheduler;
        this.clock = clock;
        this.log = log;
        this.seed = seed;
    }

    // ------------------------------------------------------------------------------------------------ any thread
    String info() { return info; }
    int trackCount() { return trackCount; }

    /** The game window started (true) or stopped (false): in the background the music pauses at once. */
    void foreground(boolean on) {
        foreground = on;
        s.post(() -> { tick(); startTicking(); });
    }

    /**
     * The window is destroyed. Idle to the game at once from the caller's thread; from then on no tick reports again. On the player
     * thread: every queued task is dropped, the player and focus are released, idle is reported again (a tick that was already past its
     * check when releasing was set cannot leave "playing" behind), then after runs (the adapter quits its threads there).
     */
    void release(Runnable after) {
        releasing = true;
        game.report(MyMusicRules.REPORT_IDLE);
        s.post(() -> {
            released = true;
            s.cancelAll();
            media.release();
            abandonFocus();
            game.report(MyMusicRules.REPORT_IDLE);
            if (after != null) after.run();
        });
    }

    // ------------------------------------------------------------------------------------------------ player thread
    /** The player's rows (at start and after every change). A row change asks for audio focus again; a new order keeps the song. */
    void settings(MyMusicSettings next) {
        final boolean orderChanged = next.shuffle != settings.shuffle;
        settings = next;
        log.i("MY_MUSIC settings " + next);
        if (orderChanged) {
            queue.reset(tracks.size(), settings.shuffle, seed);
            if (loaded >= 0) queue.keep(loaded);
        }
        volume();
        focusLost = false;
        startTicking();
    }

    /** Next Track, Previous Track (MyMusicMenu ids). Look For New Music is the adapter's (a scan, then tracks()). Asks for focus again. */
    void action(String id) {
        focusLost = false;
        if (MyMusicMenu.ID_NEXT.equals(id)) load(queue.next(), playing());
        else if (MyMusicMenu.ID_PREVIOUS.equals(id)) load(queue.previous(prepared ? media.position() : 0), playing());
        log.i("MY_MUSIC action " + id + " track=" + loaded);
        startTicking();
    }

    /** A scan's result. The song playing keeps playing when its file is still there; the failed marks start over. */
    void tracks(List<MyMusicLibrary.Track> list) {
        if (released) return;
        final File current = loaded >= 0 && loaded < tracks.size() ? tracks.get(loaded).file : null;
        tracks = list;
        trackCount = list.size();
        queue.reset(list.size(), settings.shuffle, seed);
        int keep = -1;
        if (current != null) for (int i = 0; i < list.size() && keep < 0; ++i) if (list.get(i).file.equals(current)) keep = i;
        if (keep >= 0) {
            queue.keep(keep);
            loaded = keep;
        } else if (loaded >= 0) {
            media.reset();
            prepared = false;
            loaded = -1;
        }
        log.i("MY_MUSIC scan tracks=" + list.size() + (keep >= 0 ? " (the song playing stays)" : ""));
        focusLost = false;
        startTicking();
    }

    // ---- the media player's callbacks
    void prepared() {
        if (released) return;
        prepared = true;
        if (wantStart) startNow();
    }

    void completed() {
        if (released) return;
        load(queue.next(), true);
    }

    void error() {
        if (released) return;
        final int failed = loaded;
        log.i("MY_MUSIC error track=" + failed);
        media.reset();
        prepared = false;
        loaded = -1;
        final int next = queue.failed(failed);
        if (next >= 0) load(next, lastWhy == MyMusicRules.Why.PLAY);
    }

    void focus(int change) {
        switch (change) {
            case FOCUS_GAIN: focusLost = false; ducked = false; volume(); break;
            case FOCUS_LOSS: focusHeld = false; focusLost = true; break;      // (asked again only after an action or a row change)
            case FOCUS_LOSS_TRANSIENT: focusLost = true; break;               // (GAIN follows; held stays true)
            case FOCUS_DUCK: ducked = true; volume(); break;
            default: break;
        }
        log.i("MY_MUSIC focus change=" + change);
        tick();
    }

    // ---- the tick
    private void startTicking() {
        if (released || ticking) return;
        ticking = true;
        s.post(this::tickLoop);
    }

    private void tickLoop() {
        if (released) return;
        tick();
        // Ticks while the game hears playing or paused (the heartbeat the library needs) or a fade-out still plays. Once the single
        // idle report went out (Off, no tracks, every track failed, a library without My Music) nothing changes until settings(),
        // tracks(), action() or foreground() runs, and each of those starts the tick again. (A decision still settling keeps it going.)
        if (lastReport != MyMusicRules.REPORT_IDLE || playing() || debounce.settling()) s.postDelayed(this::tickLoop, TICK_MS);
        else ticking = false;
    }

    private void tick() {
        if (released || releasing) return;
        final int word = settings.mode == MyMusicSettings.MODE_OFF ? MyMusicRules.NO_GUEST : game.state();
        noteStale(word);
        final MyMusicRules.Why why = debounce.step(MyMusicRules.decide(settings, word, foreground, tracks.size(), queue.allBad(), focusLost));
        if (why == MyMusicRules.Why.PLAY) play(); else pause(why);
        final int report = MyMusicRules.report(playing() && fadeTarget > 0f, why);
        // the heartbeat: every tick while not idle; idle once
        if (report != MyMusicRules.REPORT_IDLE || lastReport != MyMusicRules.REPORT_IDLE) send(report);
        if (why != lastWhy || report != lastReport) log.i("MY_MUSIC decision " + why + " report=" + report + " word=" + Integer.toHexString(word));
        lastReport = report;
        lastWhy = why;
        info = MyMusicRules.text(why, loaded >= 0 && loaded < tracks.size() ? tracks.get(loaded).title : "", tracks.size());
    }

    private void send(int report) {
        final long now = clock.getAsLong();
        if (lastReportMs >= 0 && lastReport != MyMusicRules.REPORT_IDLE) {
            final long gap = now - lastReportMs;
            if (gap > maxGapMs) {
                maxGapMs = gap;
                if (gap > LATE_GAP_MS) log.i("MY_MUSIC heartbeat gap=" + gap + " ms (largest so far)");
            }
        }
        lastReportMs = now;
        game.report(report);
    }

    /** The state word's stale bit (the library heard no report for over 1 s while not idle): each new stale spell is counted. */
    private void noteStale(int word) {
        if (word < 0) return;
        final boolean stale = (word & MyMusicRules.STALE) != 0;
        if (stale && !staleSeen) log.i("MY_MUSIC stale count=" + ++staleCount);
        staleSeen = stale;
    }

    /** E-SR5: the largest gap between two heartbeat reports so far (ms), and the stale spells the library saw. */
    long maxGapMs() { return maxGapMs; }
    int staleCount() { return staleCount; }

    private void play() {
        if (!requestFocus()) { focusLost = true; return; }
        final int want = queue.current();
        if (want < 0) return;
        if (loaded != want) { load(want, true); return; }
        if (!prepared) { wantStart = true; return; }
        if (!media.playing()) startNow();
        else fadeTo(1f);
    }

    private void startNow() {
        wantStart = false;
        fade = 0f;
        volume();
        media.start();
        fadeTo(1f);
        log.i("MY_MUSIC start " + (loaded >= 0 && loaded < tracks.size() ? tracks.get(loaded).title : "?"));
    }

    private void pause(MyMusicRules.Why why) {
        wantStart = false;
        if (playing()) fadeTo(0f);
        if (why == MyMusicRules.Why.OFF || why == MyMusicRules.Why.BACKGROUND || why == MyMusicRules.Why.NO_TRACKS
                || why == MyMusicRules.Why.ALL_FAILED || why == MyMusicRules.Why.NOT_IN_BUILD) {
            if (!playing()) abandonFocus();
        }
    }

    /** Opens a track (prepared asynchronously); starts it when prepared if start. A track that cannot be opened is skipped (no recursion). */
    private void load(int track, boolean start) {
        for (int tries = 0; tries <= tracks.size() && track >= 0 && track < tracks.size(); ++tries) {
            prepared = false;
            loaded = track;
            wantStart = start;
            if (media.open(tracks.get(track).file)) return;
            log.i("MY_MUSIC cannot open track=" + track);
            loaded = -1;
            final int next = queue.failed(track);
            if (next == track) return;
            track = next;
        }
    }

    private boolean playing() {
        return prepared && media.playing();
    }

    /** Volume ramps in FADE_STEP_MS steps on this thread (posted, never a wait); at 0 the player pauses. */
    private void fadeTo(float target) {
        fadeTarget = target;
        if (!fading) { fading = true; s.post(this::fadeStep); }
    }

    private void fadeStep() {
        if (released || !prepared) { fading = false; return; }
        final float step = (float) FADE_STEP_MS / FADE_MS;
        fade = fadeTarget > fade ? Math.min(fadeTarget, fade + step) : Math.max(fadeTarget, fade - step);
        volume();
        if (fade == fadeTarget) {
            fading = false;
            if (fade == 0f && media.playing()) { media.pause(); log.i("MY_MUSIC paused"); }
            return;
        }
        s.postDelayed(this::fadeStep, FADE_STEP_MS);
    }

    private void volume() {
        media.volume(settings.gain(ducked) * fade);
    }

    private boolean requestFocus() {
        if (focusHeld) return !focusLost;
        focusHeld = media.requestFocus();
        if (focusHeld) focusLost = false;
        log.i("MY_MUSIC focus request granted=" + focusHeld);
        return focusHeld;
    }

    private void abandonFocus() {
        if (!focusHeld) return;
        media.abandonFocus();
        focusHeld = false;
    }
}
