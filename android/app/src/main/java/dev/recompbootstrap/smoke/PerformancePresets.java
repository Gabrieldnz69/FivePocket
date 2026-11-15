package dev.recompbootstrap.smoke;

/**
 * The Performance page's presets (Update 1 W1-9; docs/fable/UPDATE-1-FEATURES-DESIGN.md §4.2 with review C10). A preset
 * sets four existing rows together - Frame Rate Cap, Render Resolution, Scaler, Frame Generation - and nothing else: the
 * World rows stay as the player set them (default = the original game; no content changes).
 *
 * The preset row (SettingsSchema.PERFORMANCE_PRESET) is derived, never stored: Settings.with calls follow(), so choosing a
 * preset writes its four values, any other change to one of the four shows the preset they now match (or Custom), and every
 * producer of Settings (the menu, the side menu, a decoded file, a test) stays consistent. SettingsJson neither writes nor
 * reads the key; RuntimeMapping maps it to no file, so every game file still comes from the four rows as set by hand.
 * A preset the installed build cannot honour is not offered (Capabilities.reason gives the first covered row's reason), and
 * the row's line names it: never left out silently. Pure Java.
 *
 * Names: no preset is called "Balanced" (the Render Resolution row already uses that word for 88 %). Battery keeps the
 * original 100 % render size: the only measured saving is the cap (the schema's 75 % text says "little or no power saved").
 */
final class PerformancePresets {
    private PerformancePresets() { }

    static final int CUSTOM = 0, BATTERY = 1, ADAPTIVE = 2, MAX = 3;
    static final String[] IDS = {"custom", "battery", "adaptive", "max"};
    static final String[] LABELS = {"Custom", "Battery", "Adaptive", "Max (default)"};

    /** The rows a preset sets, in this order in VALUES; no other row is ever written by a preset. */
    static final String[] KEYS = {SettingsSchema.FRAME_CAP, SettingsSchema.RENDER_SCALE, SettingsSchema.SCALER,
            SettingsSchema.FRAME_GENERATION};
    private static final int[][] VALUES = {
            null,
            {SettingsSchema.CAP_30, SettingsSchema.RENDER_SCALE_100, SettingsSchema.SCALER_FSR1, SettingsSchema.FG_OFF},
            {SettingsSchema.CAP_AUTO, SettingsSchema.RENDER_SCALE_100, SettingsSchema.SCALER_FSR1, SettingsSchema.FG_OFF},
            {SettingsSchema.CAP_60, SettingsSchema.RENDER_SCALE_100, SettingsSchema.SCALER_FSR1, SettingsSchema.FG_OFF}};

    // PLACEHOLDER texts until the preset runs (experiment E-P1): they say what each preset sets, never what it saves.
    // (one line with the names of unavailable presets after it: at most 55 characters each)
    static final String[] NOTES = {
            "Your own cap, render size, scaler and frame generation.",
            "30 fps cap (original console cap), 100 % render, FSR 1.",
            "Automatic cap, 100 % render, FSR 1.",
            "60 fps cap, 100 % render, FSR 1: the default."};
    static final String TEXT_WORLD_KEPT = "World rows are not changed by a preset.";
    static final String TEXT_UNAVAILABLE = "Not in this game build: ";

    /** The value a preset gives one of KEYS. IllegalArgumentException for Custom or a key a preset does not set. */
    static int value(int preset, String key) {
        if (preset <= CUSTOM || preset >= IDS.length) throw new IllegalArgumentException("not a preset with values: " + preset);
        for (int i = 0; i < KEYS.length; ++i) if (KEYS[i].equals(key)) return VALUES[preset][i];
        throw new IllegalArgumentException(key + " is not set by a preset");
    }

    /**
     * Settings.with's rule, on its fresh copy of the values (schema order) after `key` changed: the preset row applies its
     * preset to the four rows (Custom applies nothing); a change to one of the four sets the preset row to what they match.
     * Any other key leaves the values as they are.
     */
    static int[] follow(int[] values, String key) {
        int at = SettingsSchema.indexOf(SettingsSchema.PERFORMANCE_PRESET);
        if (key.equals(SettingsSchema.PERFORMANCE_PRESET)) {
            int preset = values[at];
            if (preset != CUSTOM) for (int i = 0; i < KEYS.length; ++i) values[SettingsSchema.indexOf(KEYS[i])] = VALUES[preset][i];
        } else if (!covers(key)) {
            return values;
        }
        values[at] = CUSTOM;
        for (int preset = CUSTOM + 1; preset < IDS.length && values[at] == CUSTOM; ++preset) {
            boolean all = true;
            for (int i = 0; i < KEYS.length && all; ++i) all = values[SettingsSchema.indexOf(KEYS[i])] == VALUES[preset][i];
            if (all) values[at] = preset;
        }
        return values;
    }

    /** True for the four rows a preset sets. */
    static boolean covers(String key) {
        for (String k : KEYS) if (k.equals(key)) return true;
        return false;
    }

    /**
     * Why the installed build cannot honour a preset ("Frame rate cap: (reason)"), or null. A preset is honoured when each
     * of its four values is supported; Custom when at least one of the four rows can change at all (in a build that reads
     * none of their files the rows hold their defaults: Max, and the preset row is disabled like them).
     */
    static String reason(Capabilities capabilities, int preset) {
        if (preset == CUSTOM) {
            for (String key : KEYS) if (capabilities.anyChoice(key)) return null;
            return Capabilities.REASON_NOT_IN_BUILD;
        }
        for (int i = 0; i < KEYS.length; ++i) {
            String why = capabilities.reason(KEYS[i], VALUES[preset][i]);
            if (why != null) return SettingsSchema.spec(KEYS[i]).label + ": " + why;
        }
        return null;
    }

    /** The preset row's line: what the preset sets, then the presets this build cannot honour (one line: about 100 characters). */
    static String line(Capabilities capabilities, int preset) {
        StringBuilder missing = new StringBuilder();
        for (int other = CUSTOM + 1; other < IDS.length; ++other) {
            if (reason(capabilities, other) != null) missing.append(missing.length() == 0 ? "" : ", ").append(LABELS[other]);
        }
        return missing.length() == 0 ? NOTES[preset] : NOTES[preset] + " " + TEXT_UNAVAILABLE + missing + ".";
    }
}
