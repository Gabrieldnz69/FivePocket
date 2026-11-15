package dev.recompbootstrap.smoke;

/**
 * My Music (Update 1 P6): when the player's music plays, and what the app tells the game (recomp_my_music_v1). Pure Java: the host
 * test runs the whole table. The state word comes from the library's recomp_my_music_state_v1 (my_music.h, bits below; -1 before
 * the game's guest memory exists, -100 when the installed library has no My Music).
 *
 * Everywhere (the console's own custom soundtrack): plays on foot and in vehicles once the player is in the world; pauses in the pause
 * menu, during cutscenes, while the game holds the music for itself (a scene with its own song: the game's 7001A request), while the
 * game window is in the background, and while another app holds Android's audio focus. Station (later): plays only in a vehicle tuned
 * to the chosen station, outside menus, cutscenes and fades.
 * While it pauses the app reports "paused" and the game's own music comes back by itself; while it plays it reports "playing" and the
 * game mutes its music groups. A change from playing to paused or back must hold for DEBOUNCE ticks (100 ms each), except the
 * game's hold, the loss of audio focus, the background and Off, which pause at once (immediate()).
 */
final class MyMusicRules {
    private MyMusicRules() { }

    // my_music.h Bits (keep in step: tests/android/MyMusicTest.java reads the header and compares)
    static final int STATION_MASK = 0xFF, IN_VEHICLE = 1 << 8, RADIO_WHEEL = 1 << 9, FRONTEND = 1 << 10, CUTSCENE = 1 << 11,
            FADED_OUT = 1 << 12, GAME_HOLDS = 1 << 13, CONTROL_OFF = 1 << 14, IN_WORLD = 1 << 15, MUTE_SET = 1 << 16,
            ANSWER_SHIFT = 24, STALE = 1 << 26;
    static final int NO_LIBRARY = -100, NO_GUEST = -1;
    /** recomp_my_music_v1 states (XMP_STATE). */
    static final int REPORT_IDLE = 0, REPORT_PLAYING = 1, REPORT_PAUSED = 2;
    static final int DEBOUNCE = 2;

    enum Why { OFF, NOT_IN_BUILD, NO_TRACKS, ALL_FAILED, WAITING, BACKGROUND, OTHER_APP, MENU, CUTSCENE, GAME_MUSIC, NOT_TUNED, PLAY }

    /** Immediate reasons skip the debounce. */
    static boolean immediate(Why w) {
        return w == Why.GAME_MUSIC || w == Why.OTHER_APP || w == Why.BACKGROUND || w == Why.OFF || w == Why.NOT_IN_BUILD;
    }

    /**
     * The decision for one tick. settings: the player's rows; word: the state word; foreground: the game window is started;
     * tracks: playable tracks found; allFailed: every track failed to play this session; focusLost: another app holds audio focus.
     */
    static Why decide(MyMusicSettings settings, int word, boolean foreground, int tracks, boolean allFailed, boolean focusLost) {
        if (settings.mode == MyMusicSettings.MODE_OFF) return Why.OFF;
        if (word == NO_LIBRARY) return Why.NOT_IN_BUILD;
        if (tracks <= 0) return Why.NO_TRACKS;
        if (allFailed) return Why.ALL_FAILED;
        if (!foreground) return Why.BACKGROUND;
        if (focusLost) return Why.OTHER_APP;
        if (word < 0 || (word & IN_WORLD) == 0) return Why.WAITING;
        if ((word & GAME_HOLDS) != 0) return Why.GAME_MUSIC;
        if ((word & FRONTEND) != 0) return Why.MENU;
        if ((word & CUTSCENE) != 0) return Why.CUTSCENE;
        if (settings.mode == MyMusicSettings.MODE_STATION) {
            final boolean tuned = (word & IN_VEHICLE) != 0 && (word & STATION_MASK) == settings.station && settings.station != MyMusicSettings.NO_STATION;
            if (!tuned || (word & FADED_OUT) != 0) return Why.NOT_TUNED;
        }
        return Why.PLAY;
    }

    /** What the app reports: playing only while its player really plays; paused while My Music is on with music to play; else idle. */
    static int report(boolean playerPlaying, Why why) {
        if (playerPlaying && why == Why.PLAY) return REPORT_PLAYING;
        switch (why) {
            case OFF: case NOT_IN_BUILD: case NO_TRACKS: case ALL_FAILED: return REPORT_IDLE;
            default: return REPORT_PAUSED;
        }
    }

    /** The decision that stands after the debounce: a new decision replaces the standing one after DEBOUNCE equal ticks. */
    static final class Debounce {
        private Why standing = Why.OFF, pending = null;
        private int count;
        Why step(Why w) {
            if (w == standing) { pending = null; count = 0; return standing; }
            // at once: the game's hold, focus loss, background, off; and a change between two pause reasons (only the words change)
            if (immediate(w) || (w != Why.PLAY && standing != Why.PLAY)) { standing = w; pending = null; count = 0; return standing; }
            if (w != pending) { pending = w; count = 1; } else ++count;
            if (count >= DEBOUNCE) { standing = w; pending = null; count = 0; }
            return standing;
        }
        Why standing() { return standing; }
        /** A new decision waits for its second tick (the tick must go on until it stands). */
        boolean settling() { return pending != null; }
    }

    /** The line under the My Music page's title (the side menu's info block). title: the track playing, or "". */
    static String text(Why why, String title, int tracks) {
        switch (why) {
            case OFF: return "My Music is off. Turn it on with Mode.";
            case NOT_IN_BUILD: return "Not in the installed game build.";
            case NO_TRACKS: return "No music yet. Launcher: Settings, My Music shows how to add songs.";
            case ALL_FAILED: return tracks == 1 ? "The music file could not be played." : "None of the " + tracks + " files could be played.";
            case WAITING: return "Starts when you are in the game.";
            case BACKGROUND: return "Paused while the game is in the background.";
            case OTHER_APP: return "Paused: another app is playing sound.";
            case MENU: return "Paused in the pause menu.";
            case CUTSCENE: return "Paused during a cutscene.";
            case GAME_MUSIC: return "Paused while the game plays its own music.";
            case NOT_TUNED: return "Plays in a vehicle tuned to the chosen station.";
            case PLAY: return title == null || title.isEmpty() ? "Playing." : "Playing: " + title;
            default: return "";
        }
    }
}
