package dev.recompbootstrap.smoke;

/**
 * OWNER ORDER 2026-11-10 ("STRONG AIM ASSIST IS FOR THE TOUCH CONTROLS", never for a controller); the library side is v1171+
 * (touch_exports.h): Strong is in effect only while touch is the active input. The Fold's on-screen pad and a real controller share one
 * gamepad stream, so only the app can tell them apart. It tells the library at each change of the winning input, once per change:
 *  - a real controller press won (QolTouch.merge: TouchPad.freshPhysical on the controller's own values): recomp_aim_assist_v1(-5), and
 *    the library restores the game's targeting mode;
 *  - touch took over again (a finger down on the screen): recomp_aim_assist_v1(preference), i.e. the Aim Magnetism choice (0 game, 1
 *    strong), which arms Strong when it is chosen; -6 (touch input, the library's stored choice) when the app has no choice yet.
 * Pure Java: tests/AimInputTest.java.
 */
final class AimInput {
    static final int CONTROLLER = -5, TOUCH = -6;
    private static final int NONE = 0, BY_TOUCH = 1, BY_CONTROLLER = 2;
    private int last = NONE;

    /** A real controller press won: the code for recomp_aim_assist_v1, or 0 when the controller was already the active input. */
    int controller() {
        if (last == BY_CONTROLLER) return 0;
        last = BY_CONTROLLER;
        return CONTROLLER;
    }

    /** A finger went down: the code for recomp_aim_assist_v1 (the choice 0 / 1, or TOUCH without one), or 0 when touch was already active. */
    int touch(int preference) {
        if (last == BY_TOUCH) return 0;
        last = BY_TOUCH;
        return preference == 0 || preference == 1 ? preference : TOUCH;
    }
}
