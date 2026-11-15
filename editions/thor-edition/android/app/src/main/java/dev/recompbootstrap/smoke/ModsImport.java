package dev.recompbootstrap.smoke;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * Copies a mod the player chose on this device into files/mods/&lt;pack&gt;/ (Update 1 P5). Pure Java: the Android side (ModsScreen) only
 * turns the chosen folder (ACTION_OPEN_DOCUMENT_TREE) or zip file (ACTION_OPEN_DOCUMENT) into a Source; every rule is here and host tested.
 *
 * Rules:
 *  - local only: a document provider must be one of LOCAL_PROVIDERS (a cloud provider would download the bytes when they are read), and
 *    a virtual document (no bytes of its own) is refused - Source implementations call refuseCloud();
 *  - every path is checked (clean): no "..", no absolute path, no drive letter, no control byte; hidden names (".DS_Store", "__MACOSX")
 *    are skipped; at most MAX_FILES files and MAX_FOLDERS folder entries, MAX_DEPTH folders deep (files and folders alike), each file at most MAX_FILE_BYTES, all together at most the budget
 *    (min of MAX_TOTAL_BYTES and the free space less RESERVE_BYTES) - counted while copying, so a zip's own size claims do not matter;
 *  - everything is written into a hidden staging folder files/mods/.import-N (the library ignores dot names) and moved into place
 *    only when complete; a failure or a cancel deletes the staging folder and leaves files/mods as it was;
 *  - nothing is written outside files/mods; only regular files and folders are created (no links).
 * Layout: the pack's top must hold a device folder (game/, update/, ...). A single wrapping folder is unwrapped ("MyMod/game/..."); a
 * chosen folder or zip named like a device folder ("game", "game.zip") whose contents have no device folder at their top is wrapped
 * (decided after the copy, so a game.zip that already holds game/ is not wrapped twice).
 */
final class ModsImport {
    private ModsImport() { }

    static final int MAX_FILES = ModsLibrary.MAX_FILES, MAX_DEPTH = ModsLibrary.MAX_DEPTH, MAX_NAME = 48;
    /** Folder entries (a zip's "dir/" entries, a chosen folder's subfolders): each costs time and an inode, so they are capped too. */
    static final int MAX_FOLDERS = 4 * MAX_FILES;
    /** The library refuses a mod file over 8 GiB (mods_glue.inc). */
    static final long MAX_FILE_BYTES = 8L << 30, MAX_TOTAL_BYTES = 16L << 30, RESERVE_BYTES = 1L << 30;
    static final Set<String> LOCAL_PROVIDERS = Collections.unmodifiableSet(new HashSet<String>(Arrays.asList(
            "com.android.externalstorage.documents", "com.android.providers.downloads.documents",
            "com.android.providers.media.documents")));
    static final String STAGING_PREFIX = ".import-";

    static final String TEXT_CLOUD = ModsPage.TEXT_LOCAL_ONLY;
    static final String TEXT_NO_DEVICE = "This has no game folder inside. A mod's files go in a folder called game,"
            + " inside the mod's own folder.";
    static final String TEXT_TOO_MANY = "This mod has more than " + MAX_FILES + " files. The game loads at most " + MAX_FILES + ".";
    static final String TEXT_TOO_DEEP = "This mod's folders go deeper than the game reads.";
    static final String TEXT_TOO_MANY_FOLDERS = "This mod has more folders than the app accepts.";
    static final String TEXT_TOO_LARGE = "This mod is larger than the free space allows.";
    static final String TEXT_BAD_NAME = "This mod holds a file name the app does not accept: ";
    static final String TEXT_CANCELLED = "Adding the mod was stopped. Nothing was kept.";

    // ------------------------------------------------------------------------------------------ pure rules

    /** A document provider that keeps its files on this device. */
    static boolean localProvider(String authority) {
        return authority != null && LOCAL_PROVIDERS.contains(authority);
    }

    /** The Source's check for each document: a cloud provider or a virtual document ends the import. */
    static void refuseCloud(String authority, boolean virtual) throws IOException {
        if (!localProvider(authority) || virtual) throw new IOException(TEXT_CLOUD);
    }

    /** Marker for clean(): the entry is hidden or system clutter and is left out. */
    static final String SKIP = "\0skip";

    /**
     * The entry's path inside the mod, '/'-separated, or SKIP, or null when the name is refused (the whole import then stops: a mod
     * with a path-escaping name is not taken in part).
     */
    static String clean(String raw) {
        if (raw == null) return null;
        String p = raw.replace('\\', '/');
        while (p.startsWith("./")) p = p.substring(2);
        if (p.startsWith("/") || (p.length() >= 2 && p.charAt(1) == ':')) return null;
        StringBuilder out = new StringBuilder();
        for (String part : p.split("/")) {
            if (part.isEmpty()) continue;
            if (part.equals("..")) return null;
            if (part.equals(".")) continue;
            for (int i = 0; i < part.length(); ++i) if (part.charAt(i) < 0x20 || part.charAt(i) == 0x7F) return null;
            if (part.startsWith(".") || part.equals("__MACOSX") || part.equalsIgnoreCase("Thumbs.db")) return SKIP;
            if (part.length() > 255) return null;
            out.append(out.length() == 0 ? "" : "/").append(part);
        }
        return out.length() == 0 ? SKIP : out.toString();
    }

    static int depth(String cleanPath) {
        int n = 1;
        for (int i = 0; i < cleanPath.length(); ++i) if (cleanPath.charAt(i) == '/') n++;
        return n;
    }

    /**
     * The folder name for a new pack from what the player chose ("Gun Pack.zip" -> "Gun Pack"): letters, digits, spaces and . _ - ( ) +
     * kept, anything else '_', no leading dot or space, at most MAX_NAME characters, not a device name, not taken (any case) by
     * existing (" (2)", " (3)", ... appended).
     */
    static String packName(String chosen, List<String> existing) {
        String base = chosen == null ? "" : chosen.trim();
        if (base.toLowerCase(Locale.ROOT).endsWith(".zip")) base = base.substring(0, base.length() - 4);
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < base.length(); ++i) {
            char c = base.charAt(i);
            boolean ok = Character.isLetterOrDigit(c) || c == ' ' || c == '.' || c == '_' || c == '-' || c == '(' || c == ')' || c == '+';
            b.append(ok ? c : '_');
        }
        String name = b.toString().replaceAll(" +", " ");
        while (name.startsWith(".") || name.startsWith(" ")) name = name.substring(1);
        if (name.length() > MAX_NAME) name = name.substring(0, MAX_NAME);
        name = name.trim();
        if (name.isEmpty()) name = "Mod";
        if (ModsLibrary.deviceName(name)) name = name + " mod";
        String candidate = name;
        for (int n = 2; taken(candidate, existing); ++n) candidate = name + " (" + n + ")";
        return candidate;
    }

    private static boolean taken(String name, List<String> existing) {
        for (String e : existing) if (e.equalsIgnoreCase(name)) return true;
        return false;
    }

    // ------------------------------------------------------------------------------------------ the copy

    /** One file or folder of the chosen mod. data is read once, for a file; the copier closes it. */
    static final class Entry {
        final String path;
        final boolean directory;
        final InputStream data;

        Entry(String path, boolean directory, InputStream data) {
            this.path = path;
            this.directory = directory;
            this.data = data;
        }
    }

    interface Source {
        /** The next entry, or null at the end. IOException(TEXT_CLOUD) for a document that is not on this device. */
        Entry next() throws IOException;
    }

    interface Progress {
        void update(int files, long bytes, String current);
        boolean cancelled();
    }

    static final class Result {
        final boolean ok;
        final String pack;
        final int files;
        final long bytes;
        /** The sentence for the page: what was added, or why nothing was. */
        final String sentence;

        Result(boolean ok, String pack, int files, long bytes, String sentence) {
            this.ok = ok;
            this.pack = pack;
            this.files = files;
            this.bytes = bytes;
            this.sentence = sentence;
        }
    }

    /**
     * Copies the source into a new pack. chosenName: the folder's or zip's display name. freeBytes: the space left on the app's storage
     * (File.getUsableSpace). Never throws; the staging folder is gone afterwards whatever happened.
     */
    static Result run(File filesDir, String chosenName, Source source, long freeBytes, Progress progress) {
        File root = new File(filesDir, ModsLibrary.MODS_DIR);
        File staging = null;
        int files = 0, folders = 0;
        long bytes = 0;
        try {
            if (Files.isSymbolicLink(root.toPath())) throw new IOException("The mods folder is a link; nothing is written.");
            if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Could not create the mods folder.");
            staging = new File(root, STAGING_PREFIX + System.nanoTime());
            if (!staging.mkdir()) throw new IOException("Could not create a folder for the new mod.");
            String stagingPath = staging.getCanonicalPath() + File.separator;
            long budget = Math.max(0, Math.min(MAX_TOTAL_BYTES, freeBytes - RESERVE_BYTES));
            String bare = chosenName == null ? "" : chosenName.replaceAll("(?i)\\.zip$", "");
            boolean mayWrap = ModsLibrary.deviceName(bare);   // (wrapped after the copy only if no device folder stands at the top)
            byte[] buffer = new byte[1 << 16];
            for (Entry e = source.next(); e != null; e = source.next()) {
                if (progress != null && progress.cancelled()) throw new IOException(TEXT_CANCELLED);
                String path = clean(e.path);
                if (path == null) {
                    close(e);
                    throw new IOException(TEXT_BAD_NAME + e.path);
                }
                if (SKIP.equals(path)) {
                    close(e);
                    continue;
                }
                if (depth(path) + (mayWrap ? 1 : 0) > MAX_DEPTH + 2) {
                    close(e);
                    throw new IOException(TEXT_TOO_DEEP);
                }
                File target = new File(staging, path);
                if (!target.getCanonicalPath().startsWith(stagingPath)) {   // (clean() already refuses this; checked again where it matters)
                    close(e);
                    throw new IOException(TEXT_BAD_NAME + e.path);
                }
                if (e.directory) {
                    if (++folders > MAX_FOLDERS) throw new IOException(TEXT_TOO_MANY_FOLDERS);
                    if (!target.isDirectory() && !target.mkdirs()) throw new IOException("Could not create a folder for the new mod.");
                    continue;
                }
                if (++files > MAX_FILES) {
                    close(e);
                    throw new IOException(TEXT_TOO_MANY);
                }
                File parent = target.getParentFile();
                if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Could not create a folder for the new mod.");
                if (target.exists()) {   // (two entries for one name, e.g. a zip with duplicates: the first stays)
                    close(e);
                    files--;
                    continue;
                }
                long size = 0;
                try (InputStream in = e.data; FileOutputStream out = new FileOutputStream(target)) {
                    for (int n; (n = in.read(buffer)) > 0; ) {
                        size += n;
                        bytes += n;
                        if (size > MAX_FILE_BYTES || bytes > budget) throw new IOException(TEXT_TOO_LARGE);
                        out.write(buffer, 0, n);
                        if (progress != null && progress.cancelled()) throw new IOException(TEXT_CANCELLED);
                    }
                    out.getFD().sync();
                }
                if (progress != null) progress.update(files, bytes, path);
            }
            File top = packRoot(staging);
            if (top == null && mayWrap) top = wrap(staging, bare.toLowerCase(Locale.ROOT));
            if (top == null) throw new IOException(TEXT_NO_DEVICE);
            List<String> existing = new ArrayList<String>();
            String[] present = root.list();
            if (present != null) existing.addAll(Arrays.asList(present));
            String name = packName(top == staging ? chosenName : top.getName(), existing);
            if (!new File(top, ModsLibrary.IMPORT_MARK).createNewFile()) throw new IOException("Could not mark the new mod.");
            Files.move(top.toPath(), new File(root, name).toPath(), StandardCopyOption.ATOMIC_MOVE);
            if (top != staging) ModsLibrary.deleteTree(staging.toPath());   // (what was left around the unwrapped folder)
            staging = null;
            return new Result(true, name, files, bytes, "Added " + name + ": " + (files == 1 ? "1 file" : files + " files") + ".");
        } catch (IOException | RuntimeException failed) {
            String why = failed.getMessage() == null ? failed.getClass().getSimpleName() : failed.getMessage();
            return new Result(false, null, 0, 0, why.startsWith("Adding") || why.startsWith("This") || why.startsWith("Only")
                    ? why : "The mod could not be added (" + why + ").");
        } finally {
            if (staging != null && Files.isDirectory(staging.toPath(), LinkOption.NOFOLLOW_LINKS)) {
                try {
                    ModsLibrary.deleteTree(staging.toPath());
                } catch (IOException ignored) {
                    // a later import uses another name; the library never reads dot folders
                }
            }
        }
    }

    /**
     * The chosen "game" folder or "game.zip" held the device folder's contents themselves: they move into staging/&lt;device&gt;/ (through
     * a hidden folder, so a file that is itself called "game" can stay inside). Returns packRoot afterwards, or null.
     */
    static File wrap(File staging, String device) throws IOException {
        String[] names = staging.list();
        if (names == null) return null;
        File holder = new File(staging, ".wrap");
        if (!holder.mkdir()) throw new IOException("Could not create a folder for the new mod.");
        for (String n : names) {
            if (n.startsWith(".")) continue;
            Files.move(new File(staging, n).toPath(), new File(holder, n).toPath(), StandardCopyOption.ATOMIC_MOVE);
        }
        Files.move(holder.toPath(), new File(staging, device).toPath(), StandardCopyOption.ATOMIC_MOVE);
        return packRoot(staging);
    }

    /** The folder that becomes the pack: staging itself when it holds a device folder, else its one wrapping folder that does; or null. */
    static File packRoot(File staging) {
        if (holdsDevice(staging)) return staging;
        String[] names = staging.list();
        File only = null;
        if (names != null) {
            for (String n : names) {
                if (n.startsWith(".")) continue;
                // A file beside the one wrapping folder (a README, a licence, a picture) is not a second pack: it stays behind with
                // the staging folder (hunt-android H4). The library would not read a file at a pack's top anyway.
                if (!Files.isDirectory(new File(staging, n).toPath(), LinkOption.NOFOLLOW_LINKS)) continue;
                if (only != null) return null;
                only = new File(staging, n);
            }
        }
        return only != null && Files.isDirectory(only.toPath(), LinkOption.NOFOLLOW_LINKS) && holdsDevice(only) ? only : null;
    }

    private static boolean holdsDevice(File folder) {
        String[] names = folder.list();
        if (names == null) return false;
        for (String n : names) {
            if (ModsLibrary.deviceName(n) && Files.isDirectory(new File(folder, n).toPath(), LinkOption.NOFOLLOW_LINKS)) return true;
        }
        return false;
    }

    private static void close(Entry e) {
        if (e.data == null) return;
        try {
            e.data.close();
        } catch (IOException ignored) {
            // nothing to do
        }
    }

    /**
     * A Source over a zip file's bytes, as the Mods page reads a chosen zip. An entry name with the zip's UTF-8 flag (general purpose
     * bit 11) is UTF-8; one without it is code page 437, as the zip format (APPNOTE 4.4.4 and appendix D) says. Older Windows tools
     * write such names: read as UTF-8, one accented name failed the whole import (hunt-android H5).
     */
    static Source zip(InputStream raw) {
        return zip(new java.util.zip.ZipInputStream(raw, CP437));
    }

    /** Code page 437 (the zip format's names without the UTF-8 flag): one byte, one character, so no name is ever malformed. */
    static final java.nio.charset.Charset CP437 = new Cp437();

    private static final class Cp437 extends java.nio.charset.Charset {
        /** Bytes 0x80..0xFF; 0x00..0x7F are ASCII. */
        static final String HIGH = "\u00c7\u00fc\u00e9\u00e2\u00e4\u00e0\u00e5\u00e7\u00ea\u00eb\u00e8\u00ef\u00ee\u00ec\u00c4\u00c5\u00c9\u00e6\u00c6\u00f4\u00f6\u00f2\u00fb\u00f9\u00ff\u00d6\u00dc\u00a2\u00a3\u00a5\u20a7\u0192\u00e1\u00ed\u00f3\u00fa\u00f1\u00d1\u00aa\u00ba\u00bf\u2310\u00ac\u00bd\u00bc\u00a1\u00ab\u00bb\u2591\u2592\u2593\u2502\u2524\u2561\u2562\u2556\u2555\u2563\u2551\u2557\u255d\u255c\u255b\u2510\u2514\u2534\u252c\u251c\u2500\u253c\u255e\u255f\u255a\u2554\u2569\u2566\u2560\u2550\u256c\u2567\u2568\u2564\u2565\u2559\u2558\u2552\u2553\u256b\u256a\u2518\u250c\u2588\u2584\u258c\u2590\u2580\u03b1\u00df\u0393\u03c0\u03a3\u03c3\u00b5\u03c4\u03a6\u0398\u03a9\u03b4\u221e\u03c6\u03b5\u2229\u2261\u00b1\u2265\u2264\u2320\u2321\u00f7\u2248\u00b0\u2219\u00b7\u221a\u207f\u00b2\u25a0\u00a0";

        Cp437() {
            super("x-zip-cp437", new String[0]);
        }

        @Override public boolean contains(java.nio.charset.Charset cs) {
            return cs instanceof Cp437 || java.nio.charset.StandardCharsets.US_ASCII.equals(cs);
        }

        @Override public java.nio.charset.CharsetDecoder newDecoder() {
            return new java.nio.charset.CharsetDecoder(this, 1f, 1f) {
                @Override protected java.nio.charset.CoderResult decodeLoop(java.nio.ByteBuffer in, java.nio.CharBuffer out) {
                    while (in.hasRemaining()) {
                        if (!out.hasRemaining()) return java.nio.charset.CoderResult.OVERFLOW;
                        int b = in.get() & 0xff;
                        out.put(b < 0x80 ? (char) b : HIGH.charAt(b - 0x80));
                    }
                    return java.nio.charset.CoderResult.UNDERFLOW;
                }
            };
        }

        @Override public java.nio.charset.CharsetEncoder newEncoder() {
            return new java.nio.charset.CharsetEncoder(this, 1f, 1f) {
                @Override protected java.nio.charset.CoderResult encodeLoop(java.nio.CharBuffer in, java.nio.ByteBuffer out) {
                    while (in.hasRemaining()) {
                        char c = in.get(in.position());
                        int b = c < 0x80 ? c : HIGH.indexOf(c) < 0 ? -1 : 0x80 + HIGH.indexOf(c);
                        if (b < 0) return java.nio.charset.CoderResult.unmappableForLength(1);
                        if (!out.hasRemaining()) return java.nio.charset.CoderResult.OVERFLOW;
                        in.get();
                        out.put((byte) b);
                    }
                    return java.nio.charset.CoderResult.UNDERFLOW;
                }
            };
        }
    }

    /** A Source over a zip stream (java.util.zip). Entries come in the zip's order; sizes are counted by run(), not trusted. */
    static Source zip(final java.util.zip.ZipInputStream in) {
        return new Source() {
            @Override public Entry next() throws IOException {
                java.util.zip.ZipEntry z = in.getNextEntry();
                if (z == null) return null;
                // the entry's bytes are the stream up to the next entry: a view that does not close the zip itself
                InputStream data = z.isDirectory() ? null : new java.io.FilterInputStream(in) {
                    @Override public void close() { }
                };
                return new Entry(z.getName(), z.isDirectory(), data);
            }
        };
    }
}
