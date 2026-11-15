package dev.recompbootstrap.smoke;

import java.io.File;

/**
 * 2026-11-09 (lead; trainer-mods' t-onlshop-quick on v1146): the press latch of the pad bridge (native_gamepad_snapshot.h). The game samples
 * the pad once per frame, so a button press shorter than one frame was lost; with the latch every down edge is held until the game has
 * read the pad once. The switch: DEFAULT_ON, or the empty file files/private-trace/diag-pad-latch (read once when the game window starts;
 * the app never creates it). Pure Java.
 */
final class PadLatch {
    static final boolean DEFAULT_ON = true;   // (2026-11-09: on by default; Thor check t-latch2 PASS)
    static final String SWITCH_FILE = "private-trace/diag-pad-latch";

    static boolean on(File filesDir) {
        return DEFAULT_ON || (filesDir != null && new File(filesDir, SWITCH_FILE).isFile());
    }
}
