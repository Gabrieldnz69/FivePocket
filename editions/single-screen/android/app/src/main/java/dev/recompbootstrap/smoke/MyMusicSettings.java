package dev.recompbootstrap.smoke;

import java.util.Locale;

/**
 * My Music (Update 1 P6): the player's choice for their own music, as the three Settings rows (SettingsSchema.MY_MUSIC,
 * MY_MUSIC_VOLUME, MY_MUSIC_ORDER) and as the words of settings/my-music.txt (ALLOCATION section 3), which RuntimeMapping writes
 * and the iOS / Duo shells read the same way. Pure Java, immutable.
 *
 * Words (one line, any order, unknown words ignored): mode=off|everywhere|station, volume=0..100, order=shuffle|inorder,
 * station=0..254 (Station mode only; Update 1 offers Off and Everywhere). The file is absent when every row is at its default
 * (mode off), as RuntimeMapping's rule of least change asks.
 */
final class MyMusicSettings {
    /**
     * Update 1: My Music is merged but not offered until the mute gate E-SR1 passes on the Thor (lead, 2026-11-06). While false:
     * no My Music category or rows in the launcher's Settings (MenuModel, SettingsPages), no player and no side-menu tab in the
     * game window (PrivateTraceActivity), and AndroidManifest.xml keeps MyMusicImportActivity disabled. To offer it: true here,
     * and android:enabled="true" on that activity.
     */
    static final boolean SHOWN = false;

    /** The launcher rows that stay hidden while SHOWN is false. */
    static boolean hiddenRow(String key) {
        return !SHOWN && (SettingsSchema.MY_MUSIC.equals(key) || SettingsSchema.MY_MUSIC_VOLUME.equals(key)
                || SettingsSchema.MY_MUSIC_ORDER.equals(key));
    }

    static final int MODE_OFF = 0, MODE_EVERYWHERE = 1, MODE_STATION = 2;
    static final String[] MODE_IDS = {"off", "everywhere", "station"};
    /** The volume row's steps (percent). Default 80: the player's music sits a little under the game's own mix. */
    static final int[] VOLUMES = {20, 40, 60, 80, 100};
    static final int VOLUME_DEFAULT_INDEX = 3;
    static final int NO_STATION = 255;

    final int mode;
    final int volume;      // percent, 0..100
    final boolean shuffle;
    final int station;     // 0..254, NO_STATION when none chosen

    MyMusicSettings(int mode, int volume, boolean shuffle, int station) {
        this.mode = mode >= MODE_OFF && mode <= MODE_STATION ? mode : MODE_OFF;
        this.volume = Math.max(0, Math.min(100, volume));
        this.shuffle = shuffle;
        this.station = station >= 0 && station < NO_STATION ? station : NO_STATION;
    }

    static final MyMusicSettings DEFAULTS = new MyMusicSettings(MODE_OFF, VOLUMES[VOLUME_DEFAULT_INDEX], true, NO_STATION);

    /** The three rows of a Settings value (their indexes as SettingsSchema defines them). */
    static MyMusicSettings from(Settings s) {
        final int mode = s.get(SettingsSchema.MY_MUSIC);
        final int vi = s.get(SettingsSchema.MY_MUSIC_VOLUME);
        final int volume = VOLUMES[vi >= 0 && vi < VOLUMES.length ? vi : VOLUME_DEFAULT_INDEX];
        return new MyMusicSettings(mode, volume, s.get(SettingsSchema.MY_MUSIC_ORDER) == SettingsSchema.MY_MUSIC_SHUFFLE, NO_STATION);
    }

    /** The volume row's index for a percentage (the nearest step). */
    static int volumeIndex(int percent) {
        int best = VOLUME_DEFAULT_INDEX;
        for (int i = 0; i < VOLUMES.length; ++i) if (Math.abs(VOLUMES[i] - percent) < Math.abs(VOLUMES[best] - percent)) best = i;
        return best;
    }

    boolean isDefault() {
        return mode == MODE_OFF && volume == DEFAULTS.volume && shuffle && station == NO_STATION;
    }

    /** The file's line (without the line feed), or null when the file should not exist (every value at its default). */
    String words() {
        if (isDefault()) return null;
        final StringBuilder b = new StringBuilder();
        b.append("mode=").append(MODE_IDS[mode]).append(" volume=").append(volume).append(" order=").append(shuffle ? "shuffle" : "inorder");
        if (station != NO_STATION) b.append(" station=").append(station);
        return b.toString();
    }

    /** The settings a file says; missing or malformed words keep their defaults. null text = no file = DEFAULTS. */
    static MyMusicSettings parse(String text) {
        if (text == null) return DEFAULTS;
        int mode = DEFAULTS.mode, volume = DEFAULTS.volume, station = NO_STATION;
        boolean shuffle = true;
        for (String w : text.trim().split("\\s+")) {
            final int eq = w.indexOf('=');
            if (eq <= 0) continue;
            final String key = w.substring(0, eq).toLowerCase(Locale.ROOT), value = w.substring(eq + 1).toLowerCase(Locale.ROOT);
            switch (key) {
                case "mode":
                    for (int i = 0; i < MODE_IDS.length; ++i) if (MODE_IDS[i].equals(value)) mode = i;
                    break;
                case "volume": {
                    final int v = number(value);
                    if (v >= 0 && v <= 100) volume = v;
                    break;
                }
                case "order":
                    if (value.equals("shuffle")) shuffle = true;
                    else if (value.equals("inorder")) shuffle = false;
                    break;
                case "station": {
                    final int v = number(value);
                    if (v >= 0 && v < NO_STATION) station = v;
                    break;
                }
                default:
                    break;
            }
        }
        return new MyMusicSettings(mode, volume, shuffle, station);
    }

    private static int number(String s) {
        if (s.isEmpty() || s.length() > 3) return -1;
        for (int i = 0; i < s.length(); ++i) if (s.charAt(i) < '0' || s.charAt(i) > '9') return -1;
        return Integer.parseInt(s);
    }

    /**
     * The MediaPlayer gain (0..1) for this volume: the square of the fraction (closer to how loud the steps sound), times 0.3
     * while Android asks the app to duck (a notification sound, navigation speech).
     */
    float gain(boolean ducked) {
        final float f = volume / 100f;
        return f * f * (ducked ? 0.3f : 1f);
    }

    @Override public boolean equals(Object o) {
        if (!(o instanceof MyMusicSettings)) return false;
        final MyMusicSettings m = (MyMusicSettings) o;
        return m.mode == mode && m.volume == volume && m.shuffle == shuffle && m.station == station;
    }

    @Override public int hashCode() {
        return ((mode * 131 + volume) * 2 + (shuffle ? 1 : 0)) * 257 + station;
    }

    @Override public String toString() {
        final String w = words();
        return w == null ? "mode=off (default)" : w;
    }
}
