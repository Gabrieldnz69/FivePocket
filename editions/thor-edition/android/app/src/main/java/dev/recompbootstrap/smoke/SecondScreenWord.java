package dev.recompbootstrap.smoke;

/**
 * Thor edition: the two small file rules of the lower screen, shared by the settings code (main source set, every build type) and
 * SecondScreenOption (the game window, debug source set). Plain Java.
 *  - settings/second-screen.txt: its first word "hud" (letters in any case, blanks around it ignored) turns the lower screen on; an
 *    empty file, any other first word, or 128 bytes or more = off. ABSENT = ON: the Thor edition shows its lower screen by default
 *    (the owner's "Both", 2026-10-22); the normal app had it off without the file.
 *  - settings/minimap.txt (the Minimap row): "both" | "top" | "bottom" (RuntimeMapping.parseMinimapWord); when it holds one of the
 *    three words it decides instead of second-screen.txt: "top" = no lower-screen HUD, "both" and "bottom" = the lower screen on.
 */
final class SecondScreenWord {
    static final int CAP = 128;
    private SecondScreenWord() { }

    static boolean blank(int c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }

    /** second-screen.txt's rule on its first {@code length} bytes. */
    static boolean hud(byte[] text, int length) {
        if (text == null || length < 0 || length >= CAP || length > text.length) return false;
        int i = 0;
        while (i < length && blank(text[i])) ++i;
        int start = i;
        while (i < length && !blank(text[i])) ++i;
        if (i - start != 3) return false;
        return lower(text[start]) == 'h' && lower(text[start + 1]) == 'u' && lower(text[start + 2]) == 'd';
    }

    private static int lower(byte b) {
        int c = b & 255;
        return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
    }

    /** v820: settings/lower-screen.txt's rule: its first word "off" (any case, blanks around it ignored) = a dark lower screen. */
    static boolean lowerOff(byte[] text) {
        if (text == null || text.length >= CAP) return false;
        int i = 0;
        while (i < text.length && blank(text[i])) ++i;
        int start = i;
        while (i < text.length && !blank(text[i])) ++i;
        return i - start == 3 && lower(text[start]) == 'o' && lower(text[start + 1]) == 'f' && lower(text[start + 2]) == 'f';
    }

    /**
     * Whether the lower screen shows the HUD: minimap.txt's word when it holds one (top: no; both, bottom: yes), else second-screen.txt.
     * minimap / secondScreen: the files' first bytes, or null when a file is absent or unreadable.
     */
    static boolean lowerScreenOn(byte[] minimap, byte[] secondScreen) {
        int word = minimap == null ? -1 : RuntimeMapping.parseMinimapWord(minimap);
        if (word >= 0) return word != SettingsSchema.MINIMAP_TOP;
        return secondScreen == null || hud(secondScreen, secondScreen.length);
    }

    /**
     * The lower screen among the system's displays (ids with whether DisplayManager lists them as presentation displays and whether
     * they are on): the first presentation display that is on and not {@code ownId} (the display the caller's window is on); else the
     * first other display that is on; else -1 (no second screen: nothing is opened). Used by the game window's HUD
     * (SecondScreenOption) and the launcher's lower screen (LauncherLowerScreen, qol20).
     */
    static int choose(int[] ids, boolean[] presentation, boolean[] on, int ownId) {
        if (ids == null || presentation == null || on == null || presentation.length != ids.length || on.length != ids.length) return -1;
        for (int k = 0; k < ids.length; ++k) if (ids[k] != ownId && presentation[k] && on[k]) return ids[k];
        for (int k = 0; k < ids.length; ++k) if (ids[k] != ownId && on[k]) return ids[k];
        return -1;
    }
}
