package dev.recompbootstrap.smoke;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * The mods on this device as the app sees them, and the app's few writes for the Mods page (Update 1 P5). Pure Java (java.io/nio).
 *
 * What it reads: files/mods (one folder per pack; folders named after a guest device are the older loose layout), files/settings/mods.txt.
 * What it writes, and nothing else:
 *   - files/settings/mods.txt, only through SettingsStore.apply (the fixed list's gate: no symbolic link, temporary file + rename);
 *   - files/mods/&lt;pack&gt;/ when a pack the app imported is removed (remove(): only a folder holding IMPORT_MARK, never through a link);
 *   - files/private-trace/run-&lt;id&gt;/mods-at-start.txt, made when a start is made: "mods-on=N" (the packs that start loads) and
 *     a copy of mods.txt (the crash guard's record);
 *   - files/mods/.import-* staging folders left by an import that was cut short (sweepStaging; never through a link).
 * Saves, the profile and the game's data are never opened for writing here.
 *
 * Every read-modify-write of mods.txt (the page's changes, the safe word's consumption, the Mods switch) holds WORDS_LOCK and starts
 * from the file as it is on disk, never from what a page showed earlier: two writers cannot undo each other.
 */
final class ModsLibrary {
    private ModsLibrary() { }

    static final String MODS_DIR = "mods";
    /** Inside a pack the app imported (hidden: the library ignores dot names). Only such a pack can be removed from the app. */
    static final String IMPORT_MARK = ".fivepocket-import";
    static final String AT_START = "mods-at-start.txt";
    /** The game window's copy of the library's v2 status when it is left (optional: see NOTES, PrivateTraceActivity hook). */
    static final String STATUS_FILE = "mods-status.txt";
    /** The library's limits (mods_glue.inc): 512 files in all, folders 12 deep. Counting stops a little past them. */
    static final int MAX_FILES = 512, MAX_DEPTH = 12;
    /** Held by every read-modify-write of mods.txt in this process (the launcher's threads and the Mods page's executor). */
    static final Object WORDS_LOCK = new Object();
    /** The first line of the crash guard's record: how many packs that start loads. */
    static final String MODS_ON_WORD = "mods-on=";

    /** One pack on disk. */
    static final class Installed {
        /** The folder name; ModsStatus.LOOSE for the loose layout (all device-named folders together). */
        final String name;
        final boolean loose;
        /** Files the library would read (regular files inside a device folder, not hidden, not linked, not too deep). */
        final int files;
        /** Files it will refuse: at the pack's top, outside any device folder, or too deep. */
        final int refused;
        final long bytes;
        /** The app imported it (IMPORT_MARK): the page offers Remove. */
        final boolean imported;

        Installed(String name, boolean loose, int files, int refused, long bytes, boolean imported) {
            this.name = name;
            this.loose = loose;
            this.files = files;
            this.refused = refused;
            this.bytes = bytes;
            this.imported = imported;
        }
    }

    /** The device folder names of the library (script_mods.h deviceName), lower case. */
    static boolean deviceName(String name) {
        String n = name.toLowerCase(Locale.ROOT);
        return n.equals("game") || n.equals("update") || n.equals("cache") || n.equals("gamecache");
    }

    /** The library's apply order: byte order of the UTF-8 names (std::string comparison), the loose files first. */
    static final Comparator<String> ORDER = new Comparator<String>() {
        @Override public int compare(String a, String b) {
            if (a.equals(b)) return 0;
            if (a.equals(ModsStatus.LOOSE)) return -1;
            if (b.equals(ModsStatus.LOOSE)) return 1;
            byte[] x = a.getBytes(StandardCharsets.UTF_8), y = b.getBytes(StandardCharsets.UTF_8);
            for (int i = 0; i < Math.min(x.length, y.length); ++i) {
                int d = (x[i] & 0xFF) - (y[i] & 0xFF);
                if (d != 0) return d;
            }
            return x.length - y.length;
        }
    };

    /** Every pack in files/mods, in the library's order. Never writes; an unreadable folder counts as empty. */
    static List<Installed> list(File filesDir) {
        File root = new File(filesDir, MODS_DIR);
        String[] names = root.list();
        if (names == null || !Files.isDirectory(root.toPath(), LinkOption.NOFOLLOW_LINKS)) return Collections.emptyList();
        Arrays.sort(names, ORDER);
        ArrayList<Installed> out = new ArrayList<Installed>();
        int[] loose = new int[2];
        long[] looseBytes = new long[1];
        boolean anyLoose = false;
        for (String name : names) {
            if (name.startsWith(".")) continue;
            File folder = new File(root, name);
            if (!Files.isDirectory(folder.toPath(), LinkOption.NOFOLLOW_LINKS)) continue;
            if (deviceName(name)) {
                anyLoose = true;
                count(folder, 1, true, loose, looseBytes);
            }
        }
        if (anyLoose) out.add(new Installed(ModsStatus.LOOSE, true, loose[0], loose[1], looseBytes[0], false));
        for (String name : names) {
            if (name.startsWith(".") || deviceName(name)) continue;
            File folder = new File(root, name);
            if (!Files.isDirectory(folder.toPath(), LinkOption.NOFOLLOW_LINKS)) continue;
            int[] counts = new int[2];
            long[] bytes = new long[1];
            String[] inside = folder.list();
            if (inside != null) {
                for (String child : inside) {
                    if (child.startsWith(".")) continue;
                    File c = new File(folder, child);
                    if (Files.isDirectory(c.toPath(), LinkOption.NOFOLLOW_LINKS)) count(c, 1, deviceName(child), counts, bytes);
                    else if (Files.isRegularFile(c.toPath(), LinkOption.NOFOLLOW_LINKS)) counts[1]++;
                }
            }
            out.add(new Installed(name, false, counts[0], counts[1], bytes[0], new File(folder, IMPORT_MARK).isFile()));
        }
        return out;
    }

    /** The folder names of the packs (not the loose files), for ModsWords.keepOnly. */
    static List<String> names(List<Installed> packs) {
        ArrayList<String> out = new ArrayList<String>();
        for (Installed p : packs) if (!p.loose) out.add(p.name);
        return out;
    }

    /** counts[0] += readable files, counts[1] += refused ones (outside a device folder, or deeper than the library goes). */
    private static void count(File folder, int depth, boolean device, int[] counts, long[] bytes) {
        String[] names = folder.list();
        if (names == null || counts[0] + counts[1] > 4 * MAX_FILES) return;
        for (String name : names) {
            if (name.startsWith(".")) continue;
            File f = new File(folder, name);
            if (Files.isDirectory(f.toPath(), LinkOption.NOFOLLOW_LINKS)) {
                if (depth >= MAX_DEPTH) counts[1]++;
                else count(f, depth + 1, device, counts, bytes);
            } else if (Files.isRegularFile(f.toPath(), LinkOption.NOFOLLOW_LINKS)) {
                if (device && f.length() > 0) {
                    counts[0]++;
                    bytes[0] += f.length();
                } else {
                    counts[1]++;
                }
            }
        }
    }

    // ------------------------------------------------------------------------------------------ mods.txt

    static ModsWords readWords(File filesDir) {
        File file = new File(filesDir, RuntimeMapping.MODS_TXT);
        try {
            if (!Files.isRegularFile(file.toPath(), LinkOption.NOFOLLOW_LINKS)) return ModsWords.NONE;
            return ModsWords.parse(readUpTo(file, 4096));
        } catch (IOException | RuntimeException unreadable) {
            return ModsWords.NONE;
        }
    }

    /**
     * Writes the words (null content: the file is removed), through the settings store's gate. Refuses while the game runs (it read the
     * file when it started; a change now would only confuse the next look at its status) and a list that does not fit.
     */
    static void writeWords(File filesDir, ModsWords words, SettingsStore.GameRunning running) throws IOException {
        if (running.check()) throw new IOException(ModsPage.TEXT_GAME_RUNNING);
        if (!words.fits()) throw new IOException(ModsPage.TEXT_TOO_MANY_OFF);
        File settings = new File(filesDir, "settings");
        if (!settings.isDirectory() && !settings.mkdir()) throw new IOException("Could not create the settings folder");
        Map<String, byte[]> plan = new HashMap<String, byte[]>();
        plan.put(RuntimeMapping.MODS_TXT, words.render());
        new SettingsStore(filesDir, running).apply(plan, new ArrayList<String>(), new ArrayList<String>());
    }

    /**
     * One change from the Mods page, applied to mods.txt as it is on disk now (not to the words the page loaded): pack (null: the safe
     * word) switched on or off. Words of packs no longer in files/mods are dropped on the way (keepOnly), so a pack deleted outside
     * the app leaves nothing behind and a later pack of that name arrives switched on. Returns the words written.
     */
    static ModsWords change(File filesDir, String pack, boolean on, SettingsStore.GameRunning running) throws IOException {
        synchronized (WORDS_LOCK) {
            ModsWords now = readWords(filesDir);
            ModsWords next = (pack == null ? now.withSafe(on) : now.withPack(pack, on)).keepOnly(names(list(filesDir)));
            writeWords(filesDir, next, running);
            return next;
        }
    }

    /** Remove from the page: the pack (remove()'s checks), then its word and those of other vanished packs. */
    static void removePack(File filesDir, String name, SettingsStore.GameRunning running) throws IOException {
        synchronized (WORDS_LOCK) {
            if (running.check()) throw new IOException(ModsPage.TEXT_GAME_RUNNING);
            remove(filesDir, name);
            ModsWords now = readWords(filesDir);
            ModsWords next = now.keepOnly(names(list(filesDir)));
            if (!next.disabled.equals(now.disabled)) writeWords(filesDir, next, running);
        }
    }

    // ------------------------------------------------------------------------------------------ remove

    /**
     * Removes one pack the app imported: files/mods/&lt;name&gt; and everything in it. Refused (IOException, nothing removed) for the loose
     * files, a hidden or path-like name, a pack without IMPORT_MARK (somebody else put it there) and a folder that is a link. Links inside
     * are removed as links, never followed.
     */
    static void remove(File filesDir, String name) throws IOException {
        if (name == null || name.equals(ModsStatus.LOOSE) || name.isEmpty() || name.startsWith(".") || name.contains("/") || deviceName(name)) {
            throw new IOException("This mod cannot be removed from the app.");
        }
        File root = new File(filesDir, MODS_DIR).getCanonicalFile();
        File folder = new File(root, name);
        Path path = folder.toPath();
        if (!Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS) || !folder.getCanonicalFile().getParentFile().equals(root)) {
            throw new IOException("This mod is not there any more.");
        }
        if (!Files.isRegularFile(new File(folder, IMPORT_MARK).toPath(), LinkOption.NOFOLLOW_LINKS)) {
            throw new IOException("This mod was not added by the app, so the app leaves it alone.");
        }
        deleteTree(path);
    }

    /** Deletes a folder this app created (an import's staging folder, or a pack after remove()'s checks). Never follows links. */
    static void deleteTree(Path top) throws IOException {
        Files.walkFileTree(top, new SimpleFileVisitor<Path>() {
            @Override public FileVisitResult visitFile(Path file, BasicFileAttributes attributes) throws IOException {
                Files.delete(file);   // (a link is deleted as a link: walkFileTree does not follow links by default)
                return FileVisitResult.CONTINUE;
            }

            @Override public FileVisitResult postVisitDirectory(Path dir, IOException failed) throws IOException {
                if (failed != null) throw failed;
                Files.delete(dir);
                return FileVisitResult.CONTINUE;
            }
        });
    }

    /**
     * Deletes the staging folders (files/mods/.import-*) of imports that were cut short (the process was killed during a copy). Only
     * real folders, never through a link. The caller guarantees that no import runs: ModsScreen calls it on the one thread that runs
     * every import. Returns how many were removed.
     */
    static int sweepStaging(File filesDir) {
        File root = new File(filesDir, MODS_DIR);
        if (!Files.isDirectory(root.toPath(), LinkOption.NOFOLLOW_LINKS)) return 0;
        String[] names = root.list();
        int removed = 0;
        if (names == null) return 0;
        for (String name : names) {
            if (!name.startsWith(ModsImport.STAGING_PREFIX)) continue;
            Path path = root.toPath().resolve(name);
            if (!Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS)) continue;
            try {
                deleteTree(path);
                removed++;
            } catch (IOException | RuntimeException left) {
                // tried again at the next look
            }
        }
        return removed;
    }

    // ------------------------------------------------------------------------------------------ the crash guard

    /**
     * When a start is made: the number of packs that start loads ("mods-on=N", the crash warning's N: packs added or removed later do
     * not change it) and a copy of mods.txt as it stands, in that run's folder (nothing when the folder is missing).
     */
    static void recordStart(File filesDir, String runId) {
        if (!LaunchRequest.isId(runId)) return;
        File run = new File(new File(filesDir, InstallState.PRIVATE_TRACE), "run-" + runId);
        File words = new File(filesDir, RuntimeMapping.MODS_TXT);
        try {
            if (!Files.isDirectory(run.toPath(), LinkOption.NOFOLLOW_LINKS)) return;
            byte[] content = Files.isRegularFile(words.toPath(), LinkOption.NOFOLLOW_LINKS) ? readUpTo(words, 4096) : new byte[0];
            ModsWords now = ModsWords.parse(content);
            int on = 0;
            if (now.scripts && !now.safe) {
                for (Installed pack : list(filesDir)) if (pack.files > 0 && now.packOn(pack.name)) on++;
            }
            byte[] head = (MODS_ON_WORD + on + "\n").getBytes(StandardCharsets.US_ASCII);
            byte[] record = Arrays.copyOf(head, head.length + content.length);
            System.arraycopy(content, 0, record, head.length, content.length);
            File target = new File(run, AT_START);
            if (Files.isSymbolicLink(target.toPath())) return;
            AtomicFiles.write(target, record);
        } catch (IOException | RuntimeException unwritable) {
            // no record: the guard then says nothing about this start
        }
    }

    /** The words that were in force when that start was made; null when no record was made. */
    static ModsWords wordsAtStart(File filesDir, String runId) {
        if (!LaunchRequest.isId(runId)) return null;
        File record = new File(new File(new File(filesDir, InstallState.PRIVATE_TRACE), "run-" + runId), AT_START);
        try {
            if (!Files.isRegularFile(record.toPath(), LinkOption.NOFOLLOW_LINKS)) return null;
            return ModsWords.parse(readUpTo(record, 8192));
        } catch (IOException | RuntimeException unreadable) {
            return null;
        }
    }

    /** The record's "mods-on=N" (the packs that start loaded); -1 when there is no record or it was made before that word existed. */
    static int modsOnAtStart(File filesDir, String runId) {
        if (!LaunchRequest.isId(runId)) return -1;
        File record = new File(new File(new File(filesDir, InstallState.PRIVATE_TRACE), "run-" + runId), AT_START);
        try {
            if (!Files.isRegularFile(record.toPath(), LinkOption.NOFOLLOW_LINKS)) return -1;
            String text = new String(readUpTo(record, 64), StandardCharsets.US_ASCII);
            if (!text.startsWith(MODS_ON_WORD)) return -1;
            int end = text.indexOf('\n');
            if (end < 0) return -1;
            int n = Integer.parseInt(text.substring(MODS_ON_WORD.length(), end));
            return n < 0 ? -1 : n;
        } catch (IOException | RuntimeException unreadable) {
            return -1;
        }
    }

    /** The game window's saved status of a run (STATUS_FILE), or null. */
    static ModsStatus statusOf(File filesDir, String runId) {
        if (!LaunchRequest.isId(runId)) return null;
        File file = new File(new File(new File(filesDir, InstallState.PRIVATE_TRACE), "run-" + runId), STATUS_FILE);
        try {
            if (!Files.isRegularFile(file.toPath(), LinkOption.NOFOLLOW_LINKS)) return null;
            return ModsStatus.parse(new String(readUpTo(file, 65536), StandardCharsets.US_ASCII));
        } catch (IOException | RuntimeException unreadable) {
            return null;
        }
    }

    /** The game window, when it is left: the library's status of this run (ASCII by construction). Never throws. */
    static void saveStatus(File filesDir, String runId, String status) {
        if (!LaunchRequest.isId(runId) || status == null || status.isEmpty()) return;
        File run = new File(new File(filesDir, InstallState.PRIVATE_TRACE), "run-" + runId);
        try {
            if (!Files.isDirectory(run.toPath(), LinkOption.NOFOLLOW_LINKS)) return;
            File target = new File(run, STATUS_FILE);
            if (Files.isSymbolicLink(target.toPath())) return;
            byte[] bytes = status.getBytes(StandardCharsets.US_ASCII);
            AtomicFiles.write(target, bytes.length > 65536 ? Arrays.copyOf(bytes, 65536) : bytes);
        } catch (IOException | RuntimeException unwritable) {
            // the page then shows what is on disk only
        }
    }

    /** What the guard concludes about one judged start. */
    static final class AfterStart {
        /** Packs that were on for that start when it ended badly (the warning's N); 0: no warning. */
        final int warnMods;
        /** The start ran with safe=1 and the word was removed from mods.txt now. */
        final boolean safeCleared;

        AfterStart(int warnMods, boolean safeCleared) {
            this.warnMods = warnMods;
            this.safeCleared = safeCleared;
        }
    }

    /**
     * Called once per judged start (LauncherScreen, after LaunchOutcome.read), off the UI thread. The warning stands for a start that
     * CRASHED, stopped by itself or stopped answering while mods were on (and it was not the safe start); low memory is not blamed on
     * mods. N is the count recorded when the start was made (recordStart). It also tries consumeSafe, which does not depend on this
     * call: the launcher refresh and the Mods page try it too.
     */
    static AfterStart afterStart(File filesDir, String runId, LaunchOutcome.Kind kind, SettingsStore.GameRunning running) {
        ModsWords then = wordsAtStart(filesDir, runId);
        if (then == null) return new AfterStart(0, false);
        int warn = 0;
        if (then.scripts && !then.safe && ModsPage.blames(kind)) {
            warn = modsOnAtStart(filesDir, runId);
            if (warn < 0) {   // (a record made before mods-on= existed: the packs as they are now)
                warn = 0;
                for (Installed pack : list(filesDir)) if (pack.files > 0 && then.packOn(pack.name)) warn++;
            }
        }
        return new AfterStart(warn, consumeSafe(filesDir, running));
    }

    /**
     * Removes safe=1 from mods.txt once the safe start it was meant for is over. Independent of the launcher's in-memory watch (which
     * a judge while the game still runs, or the launcher's process being killed, would lose): called on every launcher refresh, every
     * Mods page load and every judged start. Removes the word only when all hold:
     *  - the newest launcher run's record (recordStart) says safe=1: that start was made with the word;
     *  - the game took that start (its claim marker exists) and the game is not running now: that start is over;
     *  - mods.txt is not newer than that record: the player has not chosen the safe start again since.
     * The library reads mods.txt when its process starts, so the word is never removed before then. True when removed now.
     */
    static boolean consumeSafe(File filesDir, SettingsStore.GameRunning running) {
        synchronized (WORDS_LOCK) {
            String run = LaunchRequest.newestLauncherRun(filesDir);
            ModsWords then = wordsAtStart(filesDir, run);
            if (then == null || !then.safe) return false;
            File trace = new File(filesDir, InstallState.PRIVATE_TRACE);
            Path claim = new File(trace, LaunchOutcome.CLAIM_PREFIX + run).toPath();
            Path record = new File(new File(trace, "run-" + run), AT_START).toPath();
            Path words = new File(filesDir, RuntimeMapping.MODS_TXT).toPath();
            try {
                if (!Files.exists(claim, LinkOption.NOFOLLOW_LINKS) || !Files.isRegularFile(words, LinkOption.NOFOLLOW_LINKS)) return false;
                if (refusedBeforeGameCode(LaunchOutcome.refusalWord(new File(new File(trace, "run-" + run), LaunchOutcome.REFUSAL_FILE)))) {
                    return false;   // that start never reached the game code, so the start without mods is still to come (hunt-android H3)
                }
                if (Files.getLastModifiedTime(words, LinkOption.NOFOLLOW_LINKS).compareTo(
                        Files.getLastModifiedTime(record, LinkOption.NOFOLLOW_LINKS)) > 0) {
                    return false;
                }
                if (running.check()) return false;
                ModsWords now = readWords(filesDir);
                if (!now.safe) return false;
                writeWords(filesDir, now.withSafe(false), running);
                return true;
            } catch (IOException | RuntimeException unwritable) {
                return false;   // stays set: the page shows "off for the next start"; the next refresh tries again
            }
        }
    }

    /**
     * True for the game window's refusal words (start-refused.txt) of a start that ended before the game code ran, so before the
     * library read mods.txt: the window's own checks (REJECT_PROCESS, REJECT_SURFACE, REJECT_LIBRARY_*, REJECT_HASH, REJECT_MANIFEST_*),
     * the play-mode gate (REJECT_PLAY_MODE_*) and REJECT_NATIVE_RUN (runTrace gave nothing: refused on loading, before the entry is
     * called). REJECT_RUN_SETUP_* and REJECT_EMPTY_PROTOCOL come from the entry's own return, after the game code ran: that start was
     * made, so its safe word is spent as usual. null (no refusal file, or not a refusal word): false.
     */
    static boolean refusedBeforeGameCode(String word) {
        return word != null && !word.startsWith("REJECT_RUN_SETUP") && !word.equals("REJECT_EMPTY_PROTOCOL");
    }

    static byte[] readUpTo(File file, int limit) throws IOException {
        FileInputStream in = new FileInputStream(file);
        try {
            byte[] buffer = new byte[limit];
            int size = 0;
            while (size < limit) {
                int read = in.read(buffer, size, limit - size);
                if (read < 0) break;
                size += read;
            }
            return Arrays.copyOf(buffer, size);
        } finally {
            in.close();
        }
    }
}
