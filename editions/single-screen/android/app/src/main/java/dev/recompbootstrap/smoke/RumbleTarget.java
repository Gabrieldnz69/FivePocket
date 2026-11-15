package dev.recompbootstrap.smoke;

/**
 * Update 1 haptics, controller rumble (upd1-wave3 android-dualsense, 2026-11-06): where the game's rumble goes on Android. Pure Java,
 * host-tested (tests/RumbleTargetTest.java); Haptics asks it whenever PrivateGamepadInput gives the game another controller.
 *
 * The rule follows iOS rumble_mapper.h choose() ("a connected controller with haptics, else the phone's own actuator, else nothing"),
 * with Android's "connected controller" read as the controller that PLAYS: ControllerChoice's pick, the one the game is given (iOS uses
 * GCController.current, the controller last used, for the same reason). A controller lying beside the device that is not playing is
 * never rumbled; a playing controller without motors (the Thor's own pad, E-H2) leaves the rumble on the device's own motor, as before.
 * The on-screen pad is not an Android input device (no device number), so it is never a target.
 *
 * The player's Off / Light / Full choice is not decided here: it applies to every target alike (HapticsLoop, RumbleMapper).
 */
final class RumbleTarget {
    private RumbleTarget() { }

    /** No target: nothing vibrates. */
    static final int NONE = -1;
    /** The device's own motor (the system's default vibrator). */
    static final int PHONE = -2;

    /**
     * playing: the Android device number of the controller the game is given (below 0: none, or the on-screen pad); controllerMotors:
     * how many vibrators that controller lists (InputDevice.getVibratorManager().getVibratorIds(), API 31+; 0 before API 31 or when
     * it lists none); phoneMotor: the device has a vibrator. Returns the controller's device number, PHONE or NONE.
     */
    static int choose(int playing, int controllerMotors, boolean phoneMotor) {
        if (playing >= 0 && controllerMotors > 0) return playing;
        return phoneMotor ? PHONE : NONE;
    }

    /** The log's word for a target. */
    static String describe(int target) {
        return target == PHONE ? "phone" : target == NONE ? "none" : "controller";
    }
}
