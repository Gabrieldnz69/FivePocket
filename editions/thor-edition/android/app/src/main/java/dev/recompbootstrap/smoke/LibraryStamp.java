package dev.recompbootstrap.smoke;

import java.io.File;

/**
 * qol21 (owner 2026-11-08 21:1x on the Thor: "Online does not show on the first menu screen only sandbox and story mode"): the live game
 * code was replaced (v1116 -> v1115) while the launcher stayed in front, so no resume came and the Play tab kept the answer of the old
 * library (no Online export). The launcher now notes the stamp of the library its look read and, while it is in front, compares the file's
 * stamp every LauncherScreen.LIBRARY_WATCH_MS; a different stamp means a new look (SandboxReady and the rest re-read). Size and
 * modification time only: one stat, no read. Pure Java (LibraryStampTest).
 */
final class LibraryStamp {
    /** -1 / -1: no regular file there. */
    final long size, modified;

    private LibraryStamp(long size, long modified) {
        this.size = size;
        this.modified = modified;
    }

    static LibraryStamp of(File library) {
        try {
            if (library == null || !library.isFile()) return new LibraryStamp(-1, -1);
            return new LibraryStamp(library.length(), library.lastModified());
        } catch (RuntimeException unreadable) {
            return new LibraryStamp(-1, -1);
        }
    }

    /** True when other is the same file state (null: never looked, so not the same). */
    boolean same(LibraryStamp other) {
        return other != null && other.size == size && other.modified == modified;
    }
}
