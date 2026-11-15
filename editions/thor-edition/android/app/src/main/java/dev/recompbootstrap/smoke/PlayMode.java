package dev.recompbootstrap.smoke;

/**
 * Story Mode or Sandbox (Update 1, Sandbox design SB §1.2 and §4). Pure Java: the launcher, the start request and the
 * game window share these words, so the rules run in a host test.
 *
 * The launcher names the mode in the start request's fifth extra, "play_mode" (ALLOCATION.md §3): "story" or
 * "sandbox". A start without the extra (a measurement script's start) names no mode; the game code then plays Story
 * Mode, as it always did. The game window refuses any other value (REJECT_REQUEST), and tells the game code the mode
 * through its export recomp_play_mode_v1 before the game starts (private_trace_jni.cpp, play_mode_gate.h). The game
 * code keeps Sandbox on its own save slot (files/sandbox-mode/g&lt;N&gt;); this app never reads or writes that folder, and
 * never writes the story saves (files/saves, files/profile).
 *
 * A mode holds for the life of the game's process. Every start is a new process (the game window ends its process when
 * it is closed, 25 seconds after it is left, or when Android destroys it; a used start request is never accepted
 * again), and the launcher starts nothing while that process is still listed. So switching between the two modes is
 * always a full restart of the game. If the game code still answers that another mode holds (-1), the game window
 * refuses the start with REJECT_PLAY_MODE_OTHER_MODE and the launcher says so.
 */
final class PlayMode {
    private PlayMode() {}

    /** The start request's extra and its two values. */
    static final String EXTRA = "play_mode";
    static final String STORY = "story", SANDBOX = "sandbox";

    /** What the game window hands the game code (play_mode_gate.h): no mode named, Story, Sandbox. */
    static final int NATIVE_NONE = -1, NATIVE_STORY = 0, NATIVE_SANDBOX = 1;

    /** The refusal codes of play_mode_gate.h (0 = the start may go on). */
    static final int GATE_OK = 0, GATE_NOT_IN_CODE = 1, GATE_OTHER_MODE = 2, GATE_ANSWER = 3;

    /** The game window's status words for those refusals (GameWindowStatus: every refusal begins with REJECT_). */
    static final String WORD_NOT_IN_CODE = "REJECT_PLAY_MODE_NOT_IN_CODE";
    static final String WORD_OTHER_MODE = "REJECT_PLAY_MODE_OTHER_MODE";
    static final String WORD_ANSWER = "REJECT_PLAY_MODE_ANSWER";
    static final String WORD_PREFIX = "REJECT_PLAY_MODE_";

    /** True for a value of the extra the game window accepts; null (no extra) is accepted too: no mode named. */
    static boolean accepted(String extra) {
        return extra == null || STORY.equals(extra) || SANDBOX.equals(extra);
    }

    /** The number the game window hands the game code for an accepted extra. */
    static int nativeValue(String extra) {
        if (extra == null) return NATIVE_NONE;
        if (STORY.equals(extra)) return NATIVE_STORY;
        if (SANDBOX.equals(extra)) return NATIVE_SANDBOX;
        throw new IllegalArgumentException("not a play mode: " + extra);
    }

    /** The status word for a refusal code of play_mode_gate.h; null for GATE_OK. */
    static String refusalWord(int gate) {
        switch (gate) {
            case GATE_OK: return null;
            case GATE_NOT_IN_CODE: return WORD_NOT_IN_CODE;
            case GATE_OTHER_MODE: return WORD_OTHER_MODE;
            default: return WORD_ANSWER;
        }
    }

    /** True for one of the status words above. */
    static boolean refusal(String word) {
        return word != null && word.startsWith(WORD_PREFIX);
    }

    // ------------------------------------------------------------------------------------------ the player's words

    static final String LABEL_STORY = "Story Mode";
    static final String LABEL_SANDBOX = "Sandbox";
    /** The one line under Sandbox when it can be chosen. */
    static final String SANDBOX_LINE = "Free roam on its own save. Your story saves are never changed.";
    /** Sandbox cannot be chosen yet: no story save to start from (SB §1.2, "Seeding precondition"). */
    static final String SANDBOX_NEEDS_SAVE = "Play Story Mode until your first save. Sandbox starts from a copy of your story saves.";
    /** Sandbox cannot be chosen: the installed game code has no Sandbox (no recomp_play_mode_v1 export). */
    static final String SANDBOX_NOT_IN_CODE = "This game code has no Sandbox. Install game code made for this version to use it.";

    /** The game window's first line for a refusal of the mode (its second line is the way back). */
    static final String WINDOW_NOT_IN_CODE = "Sandbox did not start: this game code has no Sandbox. Your story saves were not changed.";
    static final String WINDOW_OTHER_MODE = "The game did not start: it was still open in the other mode. No save was changed.";
    static final String WINDOW_ANSWER = "The game did not start: the mode could not be set. No save was changed.";

    /** The launcher's sentence after such a refusal, with the next step. */
    static final String AFTER_NOT_IN_CODE = "Sandbox did not start: this game code has no Sandbox. Your story saves were not"
            + " changed. Install game code made for this version, or choose Story Mode.";
    static final String AFTER_OTHER_MODE = "Not started: the game was still open in the other mode. No save was changed."
            + " Choose it again.";
    static final String AFTER_ANSWER = "Not started: the mode could not be set. No save was changed. Choose it again.";

    /**
     * The launcher's line when the game is still running in one mode and the player chose the other. The game must
     * be closed first: a mode holds for the whole life of the game's process.
     */
    static String switchWhileRunning(String runningMode) {
        boolean sandbox = SANDBOX.equals(runningMode);
        return "The game is still open in " + (sandbox ? LABEL_SANDBOX : LABEL_STORY) + ". Close it first, then choose "
                + (sandbox ? LABEL_STORY : LABEL_SANDBOX) + ". It closes by itself 25 seconds after you leave it.";
    }

    /** The game window's text for one of the refusal words above, or null for any other word. */
    static String windowText(String word) {
        if (WORD_NOT_IN_CODE.equals(word)) return WINDOW_NOT_IN_CODE;
        if (WORD_OTHER_MODE.equals(word)) return WINDOW_OTHER_MODE;
        if (refusal(word)) return WINDOW_ANSWER;
        return null;
    }

    /** The launcher's sentence for one of the refusal words above, or null for any other word. */
    static String afterStart(String word) {
        if (WORD_NOT_IN_CODE.equals(word)) return AFTER_NOT_IN_CODE;
        if (WORD_OTHER_MODE.equals(word)) return AFTER_OTHER_MODE;
        if (refusal(word)) return AFTER_ANSWER;
        return null;
    }
}
