package dev.recompbootstrap.smoke;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

/**
 * Whether the launcher can offer Sandbox (Update 1, SB §1.2 and §4). Pure Java, read only: it looks at names and
 * reads a few kilobytes of the game code's header; it never opens a save, never writes, never follows a link.
 * The launcher asks it off the UI thread, with every look at the installed files (LauncherScreen.detect).
 *
 * Two facts:
 *   - storySave: the story slot holds a save Sandbox can start from. Sandbox's first start copies files/saves and
 *     files/profile into its own slot; without a save the game would start a new game there (SB Q1/E8).
 *     The game code's save store (guest_save_store.h) keeps a save at
 *     saves/c/(xuid, 16 hex)/(device, 8 hex)/(type, 8 hex)/(name, hex)/meta.bin, and "its presence IS existence";
 *     the player's identity is profile/identity.txt (guest_local_profile.h). Type 00000001 is a saved game.
 *   - code: the installed game code exports recomp_play_mode_v1 (a defined function in its dynamic symbol table).
 *     The released game code of 1.1.0-alpha.1 does not; the game window would then refuse a Sandbox start.
 */
final class SandboxReady {
    enum Code { SUPPORTED, MISSING, UNKNOWN }

    static final String EXPORT = "recomp_play_mode_v1";
    static final String SAVED_GAME_TYPE = "00000001";
    /** Bounds of the walk: a damaged tree must not keep the look busy. */
    static final int MAX_ENTRIES = 4096;
    /** Bounds of the header read: a section table or symbol table larger than this is not read (UNKNOWN). */
    static final long MAX_TABLE_BYTES = 4L << 20;

    final boolean storySave;
    final Code code;

    SandboxReady(boolean storySave, Code code) {
        this.storySave = storySave;
        this.code = code;
    }

    /** Both facts for the app's files directory (the game code is files/private-trace/private-trace-live.so). */
    static SandboxReady look(File filesDir) {
        File library = new File(new File(filesDir, InstallState.PRIVATE_TRACE), InstallState.LIVE_LIBRARY);
        File root = storyRoot(filesDir);   // (upd1-wave3 S8: the root the game code seeds Sandbox from, not always files/)
        return new SandboxReady(root != null && storySave(root), code(library));
    }

    /** The developer switch the game code reads at its first save access (save_sandbox.h SwitchRelative); absent for every player. */
    static final String SAVE_SWITCH = "diag-save-sandbox";
    /** save_sandbox.h MaximumSwitchBytes. */
    static final int MAX_SWITCH_BYTES = 256;

    /**
     * The folder whose saves/ and profile/ the game code copies into Sandbox's first generation (save_sandbox.h filesRootFor and
     * the play-mode branch: "<base>/sandbox-mode/g<N>, where <base> is what the switch gives"). Read only, no link followed, with
     * the switch's own rules (save_sandbox.h decide):
     *   - no switch file (every player): filesDir;
     *   - "copy [name]": a new sandbox starts as a verified copy of the real saves, so the save is filesDir's;
     *   - "reuse name": filesDir/saves-sandbox/name;
     *   - "empty [name]": starts without a save, so null;
     *   - anything else (unreadable, a link, longer than 256 bytes, more than two words, an unknown mode, a bad name): the game
     *     code has no saves state at all, so null.
     * Before upd1-wave3 S8 the look always used filesDir: on the owner's devices with "reuse thor-1024" the tile followed a save
     * the game would not start from.
     */
    static File storyRoot(File filesDir) {
        try {
            Path sw = filesDir.toPath().resolve(InstallState.PRIVATE_TRACE).resolve(SAVE_SWITCH);
            if (!Files.exists(sw, LinkOption.NOFOLLOW_LINKS)) return filesDir;
            if (!Files.isRegularFile(sw, LinkOption.NOFOLLOW_LINKS) || Files.size(sw) > MAX_SWITCH_BYTES) return null;
            String[] words = new String(Files.readAllBytes(sw), StandardCharsets.US_ASCII).trim().split("[ \t\r\n]+");
            if (words.length == 0 || words[0].isEmpty() || words.length > 2) return null;
            switch (words[0]) {
                case "copy":
                    return words.length == 2 && !validName(words[1]) ? null : filesDir;
                case "empty":
                    return null;
                case "reuse":
                    return words.length == 2 && validName(words[1]) ? new File(new File(filesDir, "saves-sandbox"), words[1]) : null;
                default:
                    return null;
            }
        } catch (IOException | RuntimeException unreadable) {
            return null;
        }
    }

    /** save_sandbox.h isValidName: 1..64 of [A-Za-z0-9._-], the first not '.'. */
    static boolean validName(String name) {
        if (name.isEmpty() || name.length() > 64 || name.charAt(0) == '.') return false;
        for (int at = 0; at < name.length(); ++at) {
            char c = name.charAt(at);
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
        }
        return true;
    }

    // ------------------------------------------------------------------------------------------ the story save

    /** True when root/profile/identity.txt and at least one saved game's meta.bin are there (regular files, no links); root is
     *  filesDir or storyRoot(filesDir). */
    static boolean storySave(File filesDir) {
        try {
            Path files = filesDir.toPath();
            Path profile = files.resolve("profile");
            if (!plainDirectory(profile) || !Files.isRegularFile(profile.resolve("identity.txt"), LinkOption.NOFOLLOW_LINKS)) {
                return false;
            }
            Path contents = files.resolve("saves").resolve("c");
            if (!plainDirectory(files.resolve("saves")) || !plainDirectory(contents)) return false;
            int[] budget = {MAX_ENTRIES};
            for (Path xuid : children(contents, 16, budget)) {
                for (Path device : children(xuid, 8, budget)) {
                    Path type = device.resolve(SAVED_GAME_TYPE);
                    if (!plainDirectory(type)) continue;
                    for (Path content : children(type, -1, budget)) {
                        if (Files.isRegularFile(content.resolve("meta.bin"), LinkOption.NOFOLLOW_LINKS)) return true;
                    }
                }
            }
            return false;
        } catch (IOException | RuntimeException unreadable) {
            return false;   // a save that cannot be seen is a save Sandbox cannot start from
        }
    }

    private static boolean plainDirectory(Path path) {
        return Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS);
    }

    /**
     * The plain directories in dir whose names are hexLength lower-case hex digits (any length of at least one
     * hex digit pair for -1: a content's encoded name). A set-aside tree (name.new, name.old) does not count.
     */
    private static java.util.List<Path> children(Path dir, int hexLength, int[] budget) throws IOException {
        java.util.List<Path> found = new java.util.ArrayList<Path>();
        try (DirectoryStream<Path> stream = Files.newDirectoryStream(dir)) {
            for (Path child : stream) {
                if (--budget[0] < 0) throw new IOException("too many entries");
                String name = child.getFileName().toString();
                if (hex(name, hexLength) && plainDirectory(child)) found.add(child);
            }
        }
        return found;
    }

    private static boolean hex(String name, int length) {
        if (length < 0 ? (name.isEmpty() || name.length() % 2 != 0) : name.length() != length) return false;
        for (int at = 0; at < name.length(); ++at) {
            char c = name.charAt(at);
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        }
        return true;
    }

    // ------------------------------------------------------------------------------------------ the game code

    /**
     * Whether an ELF64 little-endian shared library defines the function recomp_play_mode_v1 in its dynamic symbol
     * table (what dlsym finds). UNKNOWN when the file is missing, is not such a library, or its tables cannot be read
     * within the bounds; the launcher then offers Sandbox and the game window's own check decides.
     */
    static Code code(File library) {
        try (FileChannel in = FileChannel.open(library.toPath(), StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS)) {
            long size = in.size();
            ByteBuffer header = read(in, 0, 64, size);
            if (header == null || header.getInt(0) != 0x464c457f || header.get(4) != 2 || header.get(5) != 1) return Code.UNKNOWN;
            long sectionTable = header.getLong(0x28);
            int entrySize = header.getShort(0x3A) & 0xffff, count = header.getShort(0x3C) & 0xffff;
            if (entrySize < 64 || count == 0 || (long) entrySize * count > MAX_TABLE_BYTES) return Code.UNKNOWN;
            ByteBuffer sections = read(in, sectionTable, (long) entrySize * count, size);
            if (sections == null) return Code.UNKNOWN;
            for (int index = 0; index < count; ++index) {
                int at = index * entrySize;
                if (sections.getInt(at + 4) != 11) continue;   // SHT_DYNSYM
                long symbolsAt = sections.getLong(at + 24), symbolsSize = sections.getLong(at + 32);
                long symbolSize = sections.getLong(at + 56);
                int link = sections.getInt(at + 40);
                if (symbolSize != 24 || link <= 0 || link >= count || symbolsSize > MAX_TABLE_BYTES) return Code.UNKNOWN;
                int strings = link * entrySize;
                if (sections.getInt(strings + 4) != 3) return Code.UNKNOWN;   // SHT_STRTAB
                long stringsAt = sections.getLong(strings + 24), stringsSize = sections.getLong(strings + 32);
                if (stringsSize > MAX_TABLE_BYTES) return Code.UNKNOWN;
                ByteBuffer symbols = read(in, symbolsAt, symbolsSize, size);
                ByteBuffer names = read(in, stringsAt, stringsSize, size);
                if (symbols == null || names == null) return Code.UNKNOWN;
                byte[] wanted = EXPORT.getBytes(StandardCharsets.US_ASCII);
                for (int symbol = 0; symbol + 24 <= symbols.capacity(); symbol += 24) {
                    long name = symbols.getInt(symbol) & 0xffffffffL;
                    int info = symbols.get(symbol + 4) & 0xff;
                    int sectionIndex = symbols.getShort(symbol + 6) & 0xffff;
                    boolean definedFunction = sectionIndex != 0 && (info & 0xf) == 2 && (info >> 4) != 0;   // FUNC, not LOCAL
                    if (definedFunction && named(names, name, wanted)) return Code.SUPPORTED;
                }
                return Code.MISSING;
            }
            return Code.UNKNOWN;   // no dynamic symbol table: not a library the game window could use
        } catch (IOException | RuntimeException unreadable) {
            return Code.UNKNOWN;
        }
    }

    private static boolean named(ByteBuffer names, long at, byte[] wanted) {
        if (at < 0 || at + wanted.length >= names.capacity()) return false;
        for (int index = 0; index < wanted.length; ++index) {
            if (names.get((int) at + index) != wanted[index]) return false;
        }
        return names.get((int) at + wanted.length) == 0;
    }

    /** length bytes at offset, little-endian; null when they do not lie inside the file. */
    private static ByteBuffer read(FileChannel in, long offset, long length, long size) throws IOException {
        if (offset < 0 || length < 0 || length > MAX_TABLE_BYTES || offset > size - length) return null;
        ByteBuffer buffer = ByteBuffer.allocate((int) length).order(ByteOrder.LITTLE_ENDIAN);
        while (buffer.hasRemaining()) {
            if (in.read(buffer, offset + buffer.position()) <= 0) return null;
        }
        buffer.flip();
        return buffer;
    }
}
