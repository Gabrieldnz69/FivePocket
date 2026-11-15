package dev.recompbootstrap.smoke;

import java.nio.charset.StandardCharsets;

/**
 * Update 1 (menus plan A3; Quick Menu Style per ALLOCATION.md / .agents/upd1-imenu/INTEGRATION.md section 5-6): the words of two small
 * files of the app's own, both on RuntimeMapping.ownedFiles() (a launcher Save never writes or removes them: the plan names neither).
 * Pure Java (tests/SideMenuQolTest.java); the same file in the Thor and the Fold trees.
 *   settings/menu.txt        "hold-select=off|on": whether holding Select opens the side menu (350 ms; CaptureCombo). No word = the
 *                            edition's default (SideMenuRows.HOLD_DEFAULT: 1.1.5 owner decision, Off on the Thor, On on the single screen);
 *                            a change in the menu always writes the word, so the player's own choice is kept whatever the default.
 *   settings/quick-menu.txt  "style=fivepocket|imenu": the quick menu (default fivepocket); the library reads it at the first Sandbox
 *                            tick, and the app's change also calls recomp_imenu_v1(3|4) at once.
 * The writers keep the files' other well-formed words (as HapticsWords); menu.txt with no words left is removed.
 */
final class MenuWords {
    private MenuWords() { }

    static final String FILE = "settings/menu.txt", QUICK_FILE = "settings/quick-menu.txt";
    static final int HOLD_OFF = 0, HOLD_ON = 1, STYLE_FIVEPOCKET = 0, STYLE_INTERACTION = 1;
    /** Update 1 (main 2026-11-07; ALLOCATION section 17, trainer-mods v1027): recomp_trainer_state_v1 bit 24 = the game's own Interaction
     *  Menu is available (am_pi_menu runs in the local online session; bit 25 = it is open). A library before v1027 never sets it. */
    static final int STATE_LOCAL_SESSION = 1 << 24;
    /** Does holding Select open the side menu? Only with Hold Select Opens Menu on, the FivePocket style, and NO local online session:
     *  in a session the press goes to the game, whose own Interaction Menu opens on it (main's decision). trainerState -100 = no trainer. */
    static boolean holdOpensSideMenu(int holdSelect, int effectiveStyle, int trainerState) {
        return holdSelect == HOLD_ON && effectiveStyle == STYLE_FIVEPOCKET && !localSession(trainerState);
    }
    static boolean localSession(int trainerState) { return trainerState != -100 && (trainerState & STATE_LOCAL_SESSION) != 0; }
    /** recomp_imenu_v1 ops: 3 style FivePocket (the library's hold-Back opener off), 4 style Interaction Menu (on). */
    static final int IMENU_OP_FIVEPOCKET = 3, IMENU_OP_INTERACTION = 4;

    /** The hold-Select setting in menu.txt's words; no hold-select word (no file, or a file from a build before 1.1.5, which wrote no word
     *  for On) = whenUnset, the edition's default. */
    static int holdSelect(byte[] text, int whenUnset) {
        int v = whenUnset;
        for (String w : words(text)) if (w.startsWith("hold-select=")) v = w.equals("hold-select=off") ? HOLD_OFF : HOLD_ON;
        return v;
    }

    static int style(byte[] text) {
        int v = STYLE_FIVEPOCKET;
        for (String w : words(text)) if (w.startsWith("style=")) v = w.equals("style=imenu") ? STYLE_INTERACTION : STYLE_FIVEPOCKET;
        return v;
    }

    static int imenuOp(int style) { return style == STYLE_INTERACTION ? IMENU_OP_INTERACTION : IMENU_OP_FIVEPOCKET; }

    private static final java.util.regex.Pattern OTHER_WORD = java.util.regex.Pattern.compile("[a-z0-9_-]+=[A-Za-z0-9_.,+-]+");

    /** menu.txt's words (one line): the hold word first (always written since 1.1.5: the player's explicit choice, On or Off, survives a
     *  change of the edition's default), then the current file's others. */
    static String holdWords(int hold, byte[] current) {
        final StringBuilder b = new StringBuilder(hold == HOLD_OFF ? "hold-select=off" : "hold-select=on");
        keep(b, current, "hold-select=");
        return b.length() == 0 ? null : b.toString();
    }

    /** quick-menu.txt's words (one line): the style word always (the library reads it), then the current file's others. */
    static String styleWords(int style, byte[] current) {
        final StringBuilder b = new StringBuilder(style == STYLE_INTERACTION ? "style=imenu" : "style=fivepocket");
        keep(b, current, "style=");
        return b.toString();
    }

    // Update 1 touch aim toggles (TOUCH-DESIGN.md item 3), words of menu.txt: ads-turn=off, fire-aims=on, aim-assist=game
    static final String ADS_TURN = "ads-turn", FIRE_AIMS = "fire-aims", AIM_ASSIST = "aim-assist";
    static final int ASSIST_GAME = 0, ASSIST_STRONG = 1;   // (recomp_aim_assist_v1's mode)

    /** The value of key in the text (the last such word), or null. */
    static String value(byte[] text, String key) {
        String v = null;
        for (String w : words(text)) if (w.startsWith(key + "=")) v = w.substring(key.length() + 1);
        return v;
    }

    /** The text's words with key set to value (null: the word removed), others kept; null when no words are left. */
    static String withWord(byte[] current, String key, String value) {
        final StringBuilder b = new StringBuilder();
        keep(b, current, key + "=");
        if (value != null) b.append(b.length() > 0 ? " " : "").append(key).append('=').append(value);
        return b.length() == 0 ? null : b.toString();
    }

    // Update 1 touch item 4 (the player's "moving camera just by swiping"): camera=stick (default swipe), cam-speed=slow|fast, cam-invert=on
    static final String CAMERA = "camera", CAM_SPEED = "cam-speed", CAM_INVERT = "cam-invert";
    static final float[] CAM_DEG_PER_CM = {4f, 8f, 12f};   // Slow, Medium (default), Fast
    static boolean cameraSwipe(byte[] text) { return !"stick".equals(value(text, CAMERA)); }
    static int camSpeed(byte[] text) { final String v = value(text, CAM_SPEED); return "slow".equals(v) ? 0 : "fast".equals(v) ? 2 : 1; }
    static boolean camInvert(byte[] text) { return "on".equals(value(text, CAM_INVERT)); }

    static boolean adsTurns(byte[] text) { return !"off".equals(value(text, ADS_TURN)); }          // default on
    static boolean fireAims(byte[] text) { return "on".equals(value(text, FIRE_AIMS)); }           // default off
    static int aimAssist(byte[] text) { return "game".equals(value(text, AIM_ASSIST)) ? ASSIST_GAME : ASSIST_STRONG; } // default strong
    // 2026-11-09 (the owner: hold to ADS by default, the 1-tap latch as a choice; auto-fire on the red reticle, default off): aim-mode=tap,
    // auto-fire=on (no word = Hold / Off)
    static final String AIM_MODE = "aim-mode", AUTO_FIRE = "auto-fire";
    static boolean aimTap(byte[] text) { return "tap".equals(value(text, AIM_MODE)); }      // default hold
    static boolean autoFire(byte[] text) { return "on".equals(value(text, AUTO_FIRE)); }    // default off

    private static void keep(StringBuilder b, byte[] current, String ours) {
        for (String w : words(current)) {
            if (w.startsWith(ours) || !OTHER_WORD.matcher(w).matches()) continue;
            b.append(b.length() > 0 ? " " : "").append(w);
        }
    }

    private static String[] words(byte[] text) {
        if (text == null) return new String[0];
        final String s = new String(text, StandardCharsets.US_ASCII).trim();
        return s.isEmpty() ? new String[0] : s.split("[ \t\r\n]+");
    }
}
