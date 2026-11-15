package dev.recompbootstrap.smoke;

import java.nio.charset.StandardCharsets;

/**
 * Update 1 haptics (P2, Android): the words of settings/haptics.txt and the button tick's choice. Pure Java (tests/HapticsWordsTest.java).
 *
 * settings/haptics.txt (ALLOCATION.md section 3, FT section 6): the word "mode=off" or "mode=full"; no mode word (or no file) is
 * Light, the default. Written by RuntimeMapping.plan (launcher Settings, side menu), read by the game activity (Haptics) when the game
 * starts; the side menu also hands a change to the running game at once (SideMenuApply.Effects.hapticsChanged). Other words are ignored
 * by the reader and KEPT by the writer (words(mode, current)), so a later version can add one (iOS adds "triggers=") without this
 * version deleting it on the next settings save. The file is removed only when nothing is left in it (Light and no other word).
 *
 * The tick: one prebaked effect through View.performHapticFeedback, which needs no permission and follows the system's touch feedback
 * switch (the app never passes FLAG_IGNORE_GLOBAL_SETTING). Light = CLOCK_TICK, Full = VIRTUAL_KEY, Off = none. The AYN Thor ignores
 * the composition primitive PRIMITIVE_TICK but plays the prebaked CLICK and TICK (E-H2, update-1/haptics/E-H2-THOR.md); which prebaked
 * effect each constant maps to on that build is a device check (NOTES.md).
 */
final class HapticsWords {
    private HapticsWords() { }

    static final String FILE = "settings/haptics.txt";
    static final String[] IDS = {"off", "light", "full"};

    /** RumbleMapper.OFF / LIGHT / FULL from the file's bytes (null: no file). */
    static int parse(byte[] text) {
        if (text == null) return RumbleMapper.LIGHT;
        final String s = new String(text, StandardCharsets.US_ASCII);
        int mode = RumbleMapper.LIGHT;
        for (String word : s.trim().split("[ \t\r\n]+")) {
            if (!word.startsWith("mode=")) continue;
            final String v = word.substring(5);
            mode = v.equals("off") ? RumbleMapper.OFF : v.equals("full") ? RumbleMapper.FULL : RumbleMapper.LIGHT;
        }
        return mode;
    }

    /** The mode word for a mode; null for Light (the default needs no word). */
    static String modeWord(int mode) {
        return mode == RumbleMapper.OFF ? "mode=off" : mode == RumbleMapper.FULL ? "mode=full" : null;
    }

    /** A word this version keeps from the current file without knowing it: key=value in plain letters, digits and _ . , + - */
    private static final java.util.regex.Pattern OTHER_WORD = java.util.regex.Pattern.compile("[a-z0-9_]+=[A-Za-z0-9_.,+-]+");

    /**
     * The file's new words for a mode (one line, no newline), from the file's current bytes (null: no file or unreadable): the mode word
     * first (none for Light), then every other well-formed word of the current file in its order (a mode word there is dropped, anything
     * malformed too). null when nothing is left: no file.
     */
    static String words(int mode, byte[] current) {
        final StringBuilder b = new StringBuilder();
        final String m = modeWord(mode);
        if (m != null) b.append(m);
        if (current != null) {
            for (String word : new String(current, StandardCharsets.US_ASCII).trim().split("[ \t\r\n]+")) {
                if (word.startsWith("mode=") || !OTHER_WORD.matcher(word).matches()) continue;
                if (b.length() > 0) b.append(' ');
                b.append(word);
            }
        }
        return b.length() == 0 ? null : b.toString();
    }

    /** android.view.HapticFeedbackConstants values (compile-time constants of the platform). */
    static final int VIRTUAL_KEY = 1, CLOCK_TICK = 4, NO_TICK = -1;

    /** The performHapticFeedback constant of a button press in this mode, or NO_TICK. */
    static int tickConstant(int mode) {
        return mode == RumbleMapper.FULL ? VIRTUAL_KEY : mode == RumbleMapper.LIGHT ? CLOCK_TICK : NO_TICK;
    }

    /**
     * One-screen pad (QolTouch): whether the control a finger landed on is a button the pad keeps the finger for. TouchPad.fingerName(id)
     * names it: a control by its id, a stick "left-stick" / "right-stick", or null when the pad took the touch but keeps no finger. That
     * is the case for the HIDE eye and SHOW (the eye again), the first touch that only reveals SKIP, MORE (opens or closes layout B's row)
     * and the app's own buttons (Kind.APP: SAVE "qsave", TRAINER), which padTick covers. The eye's id is "hide" (TouchPad.hideControl),
     * "control" is QolTouch's word for an unknown one. Sticks, the eye and unknown controls do not tick.
     */
    static boolean padButton(String padName) {
        return padName != null && !padName.endsWith("-stick") && !padName.equals("hide") && !padName.equals("control");
    }

    /**
     * One-screen pad (QolTouch), a touch the pad took: whether it ticks. A kept button finger (padButton), an app button the touch
     * acted on (appAction: TouchPad.takeAppAction, "qsave" / "trainer"), or the MORE button (moreToggled: TouchPad.moreOpen() changed)
     * tick; the eye (HIDE / SHOW), the SKIP reveal and sticks do not.
     */
    static boolean padTick(String fingerName, String appAction, boolean moreToggled) {
        return padButton(fingerName) || appAction != null || moreToggled;
    }

    /**
     * Thor lower screen: whether the library shows its Quick Save button (recomp_lower_buttons_v1 bit 0; -100 = no export, 0 = hidden:
     * then a tap there is the map's, no button, no tick).
     */
    static boolean saveButtonShown(int lowerButtons) {
        return lowerButtons > 0 && (lowerButtons & 1) != 0;
    }
}
