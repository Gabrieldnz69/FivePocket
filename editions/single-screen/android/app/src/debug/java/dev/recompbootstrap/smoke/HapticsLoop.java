package dev.recompbootstrap.smoke;

/**
 * Update 1 haptics (P2, Android): the haptics thread's work. Pure Java (tests/HapticsLoopTest.java drives step() with fakes); Haptics
 * gives it the JNI port, the device's vibrator and the clock, and runs run() on its own thread "recomp-haptics".
 *
 * The thread blocks in Port.waitChange, which is recomp_rumble_wait_v1 (rumble.h): it returns when the game sends a CHANGED motor
 * value, or when the timeout ends. Nothing runs per frame and nothing polls: the timeout is IDLE_MS (it only bounds how late a
 * stop, a resume or a live switch to Light / Full is noticed), or the time to the Full cap when that comes first.
 *
 * Binding (rumble.h bind): the library answers the game's rumble with success only while a consumer is bound. This loop binds once it
 * has seen the library's export (the first waitChange that answers) and the mode is not Off; with Off at the game's start it does not
 * bind, so the game keeps today's honest "not supported" answer. A later live switch to Light or Full binds then. Off after binding
 * keeps the binding (the game's rumble is then a performed no-op by the player's choice; unbinding would bring back the game's
 * per-frame retry). stop() unbinds (rumble.h hands a waiter the motors' stop).
 *
 * Pause (onPause, and the 25 s background grace that follows onStop): the motor is cancelled at once, under the lock that every motor
 * command takes, so a command the thread was computing cannot land after it; while paused no command is sent. resume() asks the
 * thread for one look at the current value (lastSeq LOOK), so a Full rumble that is still on comes back within IDLE_MS (Light does not
 * pulse for a look: it is not an edge the game made).
 */
final class HapticsLoop implements Runnable {
    /** recomp_rumble_bind_v1 / recomp_rumble_wait_v1 through the JNI (Haptics.rumbleBind / rumbleWait; rumble_port.h). */
    interface Port {
        int bind(int on);
        /** >= 0: seq << 32 | packed (unsigned); TIMEOUT, NO_EXPORT (the library has no rumble) or NOT_LOADED (no library yet). */
        long waitChange(int lastSeq, int timeoutMs);
    }
    /** The actuator (Haptics: the device vibrator, or the playing controller's motors). Called with the loop's lock held; must not block. */
    interface Motor {
        void cancel();
        void oneShot(int ms, int amplitude);
        void loop(int amplitude);
    }
    interface Clock { long now(); }

    static final long TIMEOUT = -1, NO_EXPORT = -2, NOT_LOADED = -3;
    /** bind's answer before the library is open or without the export (rumble_port.h NotOpen). */
    static final int BIND_ABSENT = -100;
    /** A lastSeq no seq can equal (rumble.h keeps seq in 0..0x7FFFFFFF): the wait answers at once with the current value. */
    static final int LOOK = 0x80000000;
    static final int IDLE_MS = 1000;

    private final Port port; private final Clock clock;
    private Motor motor; private RumbleMapper mapper; // (lock; retarget() replaces both: upd1-wave3 android-dualsense)
    private final Object lock = new Object();
    private int mode;                    // RumbleMapper.OFF / LIGHT / FULL (lock)
    private boolean paused, stopping, bound, exportSeen, look = true; // (lock)
    private int lastSeq = LOOK;          // (the thread's own)
    private int sent;                    // motor commands sent (lock; log and tests)
    private Throwable failure;           // what ended run() early (lock; logged by Haptics)

    /**
     * The JNI port made safe for both of its callers (the haptics thread, and the main thread through setMode / stop): a LinkageError
     * (UnsatisfiedLinkError if the Java and the loader ever diverge in an integration) reads as "this library has no rumble", so it
     * can never end the game's process. Nothing else is caught here.
     */
    static Port guarded(final Port raw) {
        return new Port() {
            @Override public int bind(int on) {
                try { return raw.bind(on); } catch (LinkageError e) { return BIND_ABSENT; }
            }
            @Override public long waitChange(int lastSeq, int timeoutMs) {
                try { return raw.waitChange(lastSeq, timeoutMs); } catch (LinkageError e) { return NO_EXPORT; }
            }
        };
    }

    HapticsLoop(Port port, Motor motor, Clock clock, boolean amplitudeControl, int mode) {
        this.port = port; this.motor = motor; this.clock = clock; this.mapper = new RumbleMapper(amplitudeControl); this.mode = mode;
    }

    /** The player's choice changed (side menu). */
    void setMode(int next) {
        synchronized (lock) {
            if (next == mode) return;
            mode = next;
            if (mapper.looping()) { motor.cancel(); sent++; }
            mapper.reset();
            if (next != RumbleMapper.OFF) { bindLocked(); look = true; }
        }
    }
    void pause() {
        synchronized (lock) {
            if (paused || stopping) return;  // (after stop() no command at all)
            paused = true; motor.cancel(); sent++; mapper.reset();
        }
    }
    void resume() { synchronized (lock) { if (!paused) return; paused = false; look = true; } }
    /**
     * upd1-wave3 android-dualsense: the rumble moves to another actuator (Haptics: the playing controller's motors, or back to the
     * device's own motor; RumbleTarget). The old one is cancelled under the lock (nothing it was given keeps playing), the new one starts
     * from a clean mapper made for its amplitude control, and the thread looks at the game's current value once (within IDLE_MS, as for
     * a mode switch): a Full rumble that is still on continues on the new actuator; Light gives no pulse for the look. The binding is
     * not touched. Nothing after stop(); the same motor again is nothing.
     */
    void retarget(Motor next, boolean amplitudeControl) {
        synchronized (lock) {
            if (stopping || next == null || next == motor) return;
            motor.cancel(); sent++;
            motor = next; mapper = new RumbleMapper(amplitudeControl);
            look = true;
        }
    }
    /** The game window goes: no command after this one; unbinds (the library answers as before a consumer existed). */
    void stop() {
        synchronized (lock) {
            if (stopping) return;
            stopping = true; motor.cancel(); sent++; mapper.reset();
            if (bound) { port.bind(0); bound = false; }
        }
    }
    int mode() { synchronized (lock) { return mode; } }
    boolean bound() { synchronized (lock) { return bound; } }
    int sent() { synchronized (lock) { return sent; } }
    /** What ended run() early (an Error or exception from the motor or the port), or null. */
    Throwable failure() { synchronized (lock) { return failure; } }

    /**
     * The thread's body. Never throws: on Android an uncaught Throwable on any thread kills the game's process (the player loses what
     * was not saved). A failure is kept for the log, then the loop stops as stop() does (motor cancelled, unbound), as far as it can.
     */
    @Override public void run() {
        try {
            while (step()) { }
        } catch (Throwable t) {
            synchronized (lock) { failure = t; }
            try { stop(); } catch (Throwable ignored) { /* the motor or the port is broken: nothing more to do */ }
        }
    }

    /** One wait and what follows it. false: the loop is over (stopped, or the library has no rumble export). */
    boolean step() {
        final int timeout;
        synchronized (lock) {
            if (stopping) return false;
            if (look) { look = false; lastSeq = LOOK; }
            final long deadline = mapper.deadline();
            final long left = deadline == RumbleMapper.NO_DEADLINE ? IDLE_MS : deadline - clock.now();
            timeout = (int) Math.max(1, Math.min(IDLE_MS, left));
        }
        final boolean looked = lastSeq == LOOK;
        final long r = port.waitChange(lastSeq, timeout);
        synchronized (lock) {
            if (stopping) return false;
            if (r == NO_EXPORT) return false;   // a game build without rumble: the thread ends (the tick still works)
            if (r == NOT_LOADED) return true;   // the library is not open yet
            if (!exportSeen) { exportSeen = true; if (mode != RumbleMapper.OFF) bindLocked(); }
            final long now = clock.now();
            if (r == TIMEOUT) { apply(mapper.tick(now)); return true; }
            if (r < 0) return true;             // (an answer this version does not know)
            lastSeq = (int) (r >>> 32);
            final int packed = (int) r;
            if (paused) return true;            // (resume looks again)
            // A look (start, resume, a switch to Light or Full) is not an edge the game made: Light does not pulse for it.
            if (looked && mode == RumbleMapper.LIGHT) { mapper.prime(packed); return true; }
            apply(mapper.update(packed, mode, now));
            return true;
        }
    }

    private void bindLocked() {
        if (bound || !exportSeen || stopping) return;
        bound = port.bind(1) >= 0;         // (BIND_ABSENT: the export went away; the next switch tries again)
    }
    private void apply(RumbleMapper.Command c) {
        if (paused || stopping) return;
        switch (c.kind) {
            case CANCEL: motor.cancel(); break;
            case ONE_SHOT: motor.oneShot(c.ms, c.amplitude); break;
            case LOOP: motor.loop(c.amplitude); break;
            default: return;
        }
        sent++;
    }
}
