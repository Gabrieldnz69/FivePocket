package dev.recompbootstrap.smoke;

/**
 * v820 (thor-radio, Thor edition; owner 2026-10-25: "a brightness match between the panels"): the lower screen's window brightness from
 * the main panel's brightness setting. App-scoped only: the result goes into the lower screen window's
 * WindowManager.LayoutParams.screenBrightness (it applies while that window is shown, and goes with it); no system or display setting is
 * ever written (main 2026-10-25: never Settings.System). Plain Java, so the host test runs it:
 *   javac -d out android/app/src/debug/java/dev/recompbootstrap/smoke/BrightnessMatch.java android/tests/BrightnessMatchTest.java
 *   java -cp out dev.recompbootstrap.smoke.BrightnessMatchTest
 * Both Thor panels map brightness to light through the same table (dumpsys display 2026-10-25: identical mBacklight / mNits for
 * local:...667 and local:...084), so the same value gives the same nominal light. Measured before: main 0.651 (the user's setting),
 * lower 0.598 (its default: nothing set it).
 */
final class BrightnessMatch {
    /** WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE: the window leaves the display's own brightness alone. */
    static final float NONE = -1f;
    /** The dark lower screen (Lower Screen: Off): the window's lowest brightness. */
    static final float OFF = 0f;
    private BrightnessMatch() {}

    /**
     * The window brightness (0..1, or NONE) for the main panel's settings: floatSetting = Settings.System "screen_brightness_float" when
     * readable (else null), intSetting = Settings.System.SCREEN_BRIGHTNESS (1..255, or -1 unreadable), automatic = SCREEN_BRIGHTNESS_MODE
     * is automatic (the main panel's light then follows the light sensor, which the setting does not show: NONE, nothing is matched).
     */
    static float windowBrightness(Float floatSetting, int intSetting, boolean automatic) {
        if (automatic) return NONE;
        if (floatSetting != null && floatSetting >= 0f && floatSetting <= 1f) return floatSetting;
        if (intSetting >= 1 && intSetting <= 255) return (intSetting - 1) / 254f; // (Android's int-to-float brightness mapping)
        return NONE;
    }
}
