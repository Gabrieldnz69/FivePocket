package dev.recompbootstrap.smoke;

/**
 * Update 1 haptics (P2, Android): the game's motor pair -> what the phone's one actuator does. Pure Java, host-tested
 * (tests/RumbleMapperTest.java), no Android types. One instance per haptics thread; not thread-safe (HapticsLoop holds its lock).
 *
 * Input: packed = left << 16 | right, the two XINPUT_VIBRATION speeds the game sent (recomp_rumble_wait_v1; rumble.h decode).
 * The strength curve is rumble.h's amplitude(packed, mode), the one curve every port uses (iOS rumble_mapper.h follows it too):
 * single-actuator value max(left, 0.6 * right); dead zone below 0x0800; full scale at 0xC000 (the measured ceiling, v953 run A:
 * no game value reached it); Full = 64..255 linear; Light = half of Full, at least 40. The test compares every value of a grid
 * against vectors printed by rumble.h itself, so the two cannot drift apart.
 *
 * Behaviour (UPDATE-1-FEATURES-DESIGN.md section 1.3):
 * - Off: nothing; a running motor is cancelled.
 * - Full: one looping waveform at the current strength; re-issued only when the strength moves by HYSTERESIS or more (so the
 *   actuator is not restarted on every small change); a run that lasts longer than CAP_MS is lowered to 40 % (comfort, motor
 *   heat), at least LightFloor; the game's zero (or a value below the dead zone) cancels.
 * - Light: pulses only: a PULSE_MS one-shot at Light strength on each rising edge, at most one per PULSE_GAP_MS. An edge is a rumble
 *   from nothing, or a jump of JUMP or more (in Full's 0..255 steps) over the last value: a crash on top of a sustained engine or road
 *   rumble still pulses. Sustained or slowly growing rumble gives no continuous buzz.
 * - A motor without amplitude control (hasAmplitudeControl false): Full is on/off (on from 0x4000, a quarter of full scale) and stops
 *   after CAP_MS (it cannot be lowered); Light is NO_AMP_PULSE_MS pulses at the motor's default strength.
 */
final class RumbleMapper {
    static final int OFF = 0, LIGHT = 1, FULL = 2;
    static final int DEAD_ZONE = 0x0800, CEILING = 0xC000, FULL_FLOOR = 64, LIGHT_FLOOR = 40, TOP = 255;
    static final int HYSTERESIS = 16, PULSE_MS = 40, NO_AMP_PULSE_MS = 30;
    /** Light: a rise of this many Full steps over the last value is an edge too (a quarter of the 64..255 span; about 0x2E00 of the left motor). */
    static final int JUMP = 48;
    static final long PULSE_GAP_MS = 250, CAP_MS = 2000;
    static final int NO_AMP_ON = 0x4000;
    /** VibrationEffect.DEFAULT_AMPLITUDE: the motor's own strength (a motor without amplitude control). */
    static final int DEFAULT_AMPLITUDE = -1;
    static final long NO_DEADLINE = Long.MAX_VALUE;

    /** What to do with the actuator. NONE: leave it as it is. */
    enum Kind { NONE, CANCEL, ONE_SHOT, LOOP }

    static final class Command {
        final Kind kind; final int ms; final int amplitude;
        Command(Kind kind, int ms, int amplitude) { this.kind = kind; this.ms = ms; this.amplitude = amplitude; }
        @Override public String toString() { return kind + (kind == Kind.ONE_SHOT ? " " + ms + "ms" : "") + (kind == Kind.ONE_SHOT || kind == Kind.LOOP ? " a=" + amplitude : ""); }
    }
    static final Command NONE = new Command(Kind.NONE, 0, 0), CANCEL = new Command(Kind.CANCEL, 0, 0);
    static Command oneShot(int ms, int amplitude) { return new Command(Kind.ONE_SHOT, ms, amplitude); }
    static Command loop(int amplitude) { return new Command(Kind.LOOP, 0, amplitude); }

    /** rumble.h combined(): the heavy left motor as it is, the light right one at 60 %. */
    static int combined(int packed) {
        final int left = packed >>> 16, right = (packed & 0xFFFF) * 3 / 5;
        return Math.max(left, right);
    }

    /** rumble.h amplitude(packed, mode): 0..255. */
    static int amplitude(int packed, int mode) {
        final int v = combined(packed);
        if (mode == OFF || v < DEAD_ZONE) return 0;
        final int c = Math.min(v, CEILING);
        final int full = FULL_FLOOR + ((TOP - FULL_FLOOR) * (c - DEAD_ZONE) + (CEILING - DEAD_ZONE) / 2) / (CEILING - DEAD_ZONE);
        if (mode == FULL) return full;
        final int light = (full + 1) / 2;
        return Math.max(light, LIGHT_FLOOR);
    }

    /** Full's strength after CAP_MS of continuous rumble: 40 %, at least LightFloor. */
    static int capped(int level) { return Math.max(LIGHT_FLOOR, (level * 2 + 2) / 5); }

    private final boolean amplitudeControl;
    private boolean looping, capped;
    private int applied, level, previous;
    private long runStart, lastPulse = Long.MIN_VALUE / 2;

    RumbleMapper(boolean amplitudeControl) { this.amplitudeControl = amplitudeControl; }

    /** Forget the actuator's state (it was cancelled from outside: pause, a mode change). The next rumble counts as a rising edge. */
    void reset() { looping = false; capped = false; applied = 0; level = 0; previous = 0; }

    boolean looping() { return looping; }

    /** Light after a look (resume, a mode switch): remember the game's current value without a pulse. */
    void prime(int packed) { level = amplitude(packed, FULL); previous = level; }

    /** The game sent a new value. */
    Command update(int packed, int mode, long now) {
        final int was = previous;
        level = amplitude(packed, FULL);
        previous = level;
        if (mode == OFF) return stop();
        if (mode == LIGHT) {
            if (looping) { reset(); previous = level; return CANCEL; } // (a loop left from Full: Light never loops)
            final boolean edge = was == 0 || level - was >= JUMP;
            if (!edge || level == 0 || now - lastPulse < PULSE_GAP_MS) return NONE;
            lastPulse = now;
            return amplitudeControl ? oneShot(PULSE_MS, amplitude(packed, LIGHT)) : oneShot(NO_AMP_PULSE_MS, DEFAULT_AMPLITUDE);
        }
        if (!amplitudeControl) {
            final boolean on = combined(packed) >= NO_AMP_ON;
            if (!on) return stop();
            if (looping) return NONE; // (on, or stopped by the cap until the game lets go)
            if (capped) return NONE;
            looping = true; runStart = now; applied = DEFAULT_AMPLITUDE;
            return loop(DEFAULT_AMPLITUDE);
        }
        if (level == 0) return stop();
        if (!looping && !capped) { looping = true; runStart = now; applied = level; return loop(level); }
        if (!looping) { capped = false; looping = true; runStart = now; applied = level; return loop(level); }
        final int target = capped ? capped(level) : level;
        if (Math.abs(target - applied) < HYSTERESIS) return NONE;
        applied = target;
        return loop(target);
    }

    /** When tick() has something to do (the cap), or NO_DEADLINE. */
    long deadline() { return looping && !capped ? runStart + CAP_MS : NO_DEADLINE; }

    /** Time passed without a new value: applies the cap when it is due. */
    Command tick(long now) {
        if (!looping || capped || now < runStart + CAP_MS) return NONE;
        capped = true;
        if (!amplitudeControl) { looping = false; return CANCEL; }
        final int target = capped(level);
        if (Math.abs(target - applied) < HYSTERESIS) return NONE;
        applied = target;
        return loop(target);
    }

    private Command stop() {
        final boolean was = looping;
        looping = false; capped = false; applied = 0;
        return was ? CANCEL : NONE;
    }
}
