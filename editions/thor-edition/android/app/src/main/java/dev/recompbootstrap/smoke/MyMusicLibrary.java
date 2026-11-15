package dev.recompbootstrap.smoke;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Random;
import java.util.Set;

/**
 * My Music (Update 1 P6): the player's music folder and the rules for adding files to it. Pure Java (java.io only): the host test
 * runs it on a scratch folder.
 *
 * The folder is "My Music" in the app's external files directory (Context.getExternalFilesDir(null), shown over USB as
 * Android/data/&lt;package&gt;/files/My Music), so the player can copy songs in from a computer. MyMusicImportActivity copies files
 * the player picks or shares into the same folder. Nothing is downloaded and nothing is read from a network: a file comes from a
 * local storage provider or it is refused (localProvider).
 */
final class MyMusicLibrary {
    private MyMusicLibrary() { }

    static final String FOLDER = "My Music";
    /** What Android's media framework plays on current devices (platform documentation): MP3, AAC / M4A, FLAC, Ogg Vorbis, Opus, WAV. */
    static final String[] EXTENSIONS = {"mp3", "m4a", "aac", "flac", "ogg", "oga", "opus", "wav"};
    /** The scan stops here: a folder deeper than one level of subfolders is not searched, and at most this many tracks are kept. */
    static final int MAX_DEPTH = 1, MAX_TRACKS = 5000;
    /** Import limits: one file at most MAX_FILE bytes; a copy must leave KEEP_FREE bytes free on the device. */
    static final long MAX_FILE = 512L << 20, KEEP_FREE = 1L << 30;
    /** The suffix of a file being copied (renamed when complete; never played). */
    static final String PART = ".part";

    /** One track: its file and the title shown (the file name without its extension). */
    static final class Track {
        final File file;
        final String title;
        Track(File file, String title) { this.file = file; this.title = title; }
        @Override public String toString() { return title; }
    }

    /** True for a name with one of the supported extensions (case does not matter) and not hidden. (A partial copy, "x.mp3.part",
     *  ends in PART, which is not a supported extension.) */
    static boolean isAudioName(String name) {
        if (name == null || name.startsWith(".")) return false;
        final int dot = name.lastIndexOf('.');
        if (dot <= 0 || dot == name.length() - 1) return false;
        final String ext = name.substring(dot + 1).toLowerCase(Locale.ROOT);
        for (String e : EXTENSIONS) if (e.equals(ext)) return true;
        return false;
    }

    static String title(String name) {
        final int dot = name.lastIndexOf('.');
        return dot > 0 ? name.substring(0, dot) : name;
    }

    /**
     * Makes the music folder when it is missing (the game window's first start, or the first import), so the player finds it over USB.
     * True only when this call made it (the caller then tells Android's media store once). False: it existed, or dir is null, or
     * it could not be made.
     */
    static boolean ensureFolder(File dir) {
        if (dir == null || dir.isDirectory()) return false;
        return dir.mkdirs();
    }

    /** The tracks in the folder and its direct subfolders, sorted by path without regard to case. Empty when the folder is missing. */
    static List<Track> scan(File folder) {
        final List<Track> out = new ArrayList<>();
        walk(folder, 0, out);
        Collections.sort(out, new Comparator<Track>() {
            @Override public int compare(Track a, Track b) {
                final int c = a.file.getPath().compareToIgnoreCase(b.file.getPath());
                return c != 0 ? c : a.file.getPath().compareTo(b.file.getPath());
            }
        });
        return out.size() > MAX_TRACKS ? new ArrayList<>(out.subList(0, MAX_TRACKS)) : out;
    }

    private static void walk(File dir, int depth, List<Track> out) {
        final File[] list = dir == null ? null : dir.listFiles();
        if (list == null) return;
        for (File f : list) {
            if (out.size() >= MAX_TRACKS * 2) return; // (a runaway folder: enough to sort and cut)
            if (f.isDirectory()) {
                if (depth < MAX_DEPTH && !f.getName().startsWith(".")) walk(f, depth + 1, out);
            } else if (f.isFile() && isAudioName(f.getName()) && f.length() > 0) {
                out.add(new Track(f, title(f.getName())));
            }
        }
    }

    /** The play order: 0..n-1, or a Fisher-Yates shuffle of it with the given seed (the same seed gives the same order). */
    static int[] order(int n, boolean shuffle, long seed) {
        final int[] o = new int[Math.max(0, n)];
        for (int i = 0; i < o.length; ++i) o[i] = i;
        if (!shuffle) return o;
        final Random r = new Random(seed);
        for (int i = o.length - 1; i > 0; --i) {
            final int j = r.nextInt(i + 1);
            final int t = o[i]; o[i] = o[j]; o[j] = t;
        }
        return o;
    }

    // ---------------------------------------------------------------------------------------------------------- adding files

    /**
     * The content authorities a file may come from: Android's own local storage providers (the system picker's Internal storage /
     * SD card, Downloads, and the media store) and the phone makers' file managers that share local files. A cloud provider's
     * authority is refused, so no file is pulled over the network (UPDATE-1-REVIEW section 5, "No network").
     */
    static final String[] LOCAL_AUTHORITIES = {
            "com.android.externalstorage.documents", "com.android.providers.downloads.documents",
            "com.android.providers.media.documents", "media", "com.sec.android.app.myfiles.FileProvider",
            "com.google.android.apps.nbu.files.provider"}; // (Files by Google: shares files stored on the device)

    /** The words for a file from any other provider (a cloud drive, another app's own files). */
    static final String NOT_LOCAL = "files shared from that app cannot be added; share them from a file app instead.";   // (player-copy music.import.cloud)

    static boolean localProvider(String scheme, String authority) {
        if (!"content".equals(scheme) || authority == null) return false;
        for (String a : LOCAL_AUTHORITIES) if (a.equals(authority)) return true;
        return false;
    }

    /**
     * A safe file name for a copy: the display name without folders, control characters or a leading dot, at most 120 characters
     * before the extension; " (2)", " (3)" ... before the extension when the name is taken. null when the name has no supported
     * extension.
     */
    static String safeName(String display, Set<String> taken) {
        if (display == null) return null;
        String name = display;
        final int slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
        if (slash >= 0) name = name.substring(slash + 1);
        final StringBuilder b = new StringBuilder();
        for (int i = 0; i < name.length(); ++i) {
            final char c = name.charAt(i);
            if (c < 32 || c == 127 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') b.append('_');
            else b.append(c);
        }
        name = b.toString().trim();
        while (name.startsWith(".")) name = name.substring(1);
        if (!isAudioName(name)) return null;
        final int dot = name.lastIndexOf('.');
        String stem = name.substring(0, dot).trim();
        final String ext = name.substring(dot).toLowerCase(Locale.ROOT);
        if (stem.isEmpty()) stem = "Track";
        if (stem.length() > 120) stem = stem.substring(0, 120).trim();
        String candidate = stem + ext;
        for (int n = 2; contains(taken, candidate); ++n) candidate = stem + " (" + n + ")" + ext;
        return candidate;
    }

    private static boolean contains(Set<String> taken, String name) {
        for (String t : taken) if (t.equalsIgnoreCase(name)) return true; // (case-insensitive file systems)
        return false;
    }

    /** Why a file of this size cannot be copied now (the words shown), or null when it can. size < 0: unknown. */
    static String refusal(long size, long freeBytes) {
        if (size > MAX_FILE) return TOO_LARGE;
        final long need = size < 0 ? 0 : size;
        if (freeBytes - need < KEEP_FREE) return "not enough free space on this device.";
        return null;
    }
    static final String TOO_LARGE = "too large (over 512 MB).";

    /**
     * Reserves the copy's final name in dir: an empty file made with createNewFile, which is atomic, so two imports running at once
     * never get the same name and an existing file is never chosen (a taken name gets " (2)", " (3)" ...). The empty reservation is
     * not a track (scan skips empty files). Returns null when the name has no supported extension.
     */
    static File reserve(File dir, String display, Set<String> taken) throws IOException {
        final Set<String> seen = new HashSet<>(taken);
        for (int tries = 0; tries < 1000; ++tries) {
            final String name = safeName(display, seen);
            if (name == null) return null;
            final File f = new File(dir, name);
            if (f.createNewFile()) { taken.add(name); return f; }
            seen.add(name);
        }
        throw new IOException("no free name");
    }

    /**
     * Puts a finished copy in place of its reservation. Only an empty reservation is replaced (it is ours: reserve() made it), and only
     * by a copy with content; false otherwise, and then nothing was renamed.
     */
    static boolean commit(File part, File reserved) {
        if (!reserved.isFile() || reserved.length() != 0 || !part.isFile() || part.length() == 0) return false;
        return part.renameTo(reserved);
    }

    /** A failed copy: its partial file goes, and its reservation too while it is still empty (never a file with content). */
    static void abandon(File part, File reserved) {
        if (part != null && part.isFile()) part.delete();
        if (reserved != null && reserved.isFile() && reserved.length() == 0) reserved.delete();
    }
}
