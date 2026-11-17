package dev.recompbootstrap.smoke;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;

/**
 * 2026-11-17 (owner: "I want the in game menu sound effects as well"): which of the game's own frontend sounds
 * (HUD_FRONTEND_DEFAULT_SOUNDSET) a launcher menu command plays, and which sound files may be played. Pure Java.
 *
 * The sounds are the player's: never in the APK. They are files the player's own setup writes from the player's discs into
 * files/ui/sounds/ (SELECT and BACK by the builder from the disc's frontend sound bank, the synthesized NAV_UP_DOWN,
 * NAV_LEFT_RIGHT and ERROR by the game library's one-time recording). A missing or unusable file is silence, never an error.
 */
final class MenuSounds {
    private MenuSounds() { }

    static final int NONE = -1, NAV_UP_DOWN = 0, NAV_LEFT_RIGHT = 1, SELECT = 2, BACK = 3, ERROR = 4;
    /** The files, by sound: the game's sound names in lower case. */
    static final String[] FILES = {"nav_up_down.wav", "nav_left_right.wav", "select.wav", "back.wav", "error.wav"};
    /** The imported bundle's sounds (builder: SELECT, BACK) first, then files/ui/sounds (the library's recording: NAV, ERROR). */
    static final String[] DIRS = {CodeBundle.UI_DIR + "/sounds", "ui/sounds"};
    /** A menu sound is a fraction of a second; anything bigger is not one of ours. */
    static final long MAX_BYTES = 2L * 1024 * 1024;

    /**
     * The sound for a command the visible screen was given. handled: what Screen.onNav returned; popped: a Back the screen did not
     * handle closed it (the host's own back). The pause menu's rules: a move up or down, a move left or right (and a tab change), Select,
     * Back; Select that nothing took is the error sound. A command nothing reacted to is silent.
     */
    static int soundFor(int command, boolean handled, boolean popped) {
        switch (command) {
            case NavInput.UP: case NavInput.DOWN: return handled ? NAV_UP_DOWN : NONE;
            case NavInput.LEFT: case NavInput.RIGHT: case NavInput.PAGE_PREV: case NavInput.PAGE_NEXT: return handled ? NAV_LEFT_RIGHT : NONE;
            case NavInput.CONFIRM: return handled ? SELECT : ERROR;
            case NavInput.BACK: return handled || popped ? BACK : NONE;
            default: return NONE;
        }
    }

    /** The file for a sound under the app's files folder, when it is a usable sound file (a RIFF WAVE of a sensible size); else null. */
    static File usable(File filesDir, int sound) {
        if (filesDir == null || sound < 0 || sound >= FILES.length) return null;
        for (String dir : DIRS) {
            File f = wave(new File(new File(filesDir, dir), FILES[sound]));
            if (f != null) return f;
        }
        return null;
    }

    private static File wave(File f) {
        if (!f.isFile() || f.length() < 44 || f.length() > MAX_BYTES) return null;
        byte[] head = new byte[12];
        try (FileInputStream in = new FileInputStream(f)) {
            if (in.read(head) != 12) return null;
        } catch (IOException e) {
            return null;
        }
        boolean riff = head[0] == 'R' && head[1] == 'I' && head[2] == 'F' && head[3] == 'F';
        boolean wave = head[8] == 'W' && head[9] == 'A' && head[10] == 'V' && head[11] == 'E';
        return riff && wave ? f : null;
    }
}
