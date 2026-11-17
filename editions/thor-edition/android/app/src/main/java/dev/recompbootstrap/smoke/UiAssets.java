package dev.recompbootstrap.smoke;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/**
 * Menu revamp M2 (android-qol, 2026-11-07): the game's own menu assets as decoded from the player's discs - for now its fonts, as
 * TrueType files written by the builder from the disc's font_lib_efigs_360.gfx (menu-revamp/host/gfx2ttf.py). The disc archive is
 * encrypted with a key that only the generated game code holds, so the decoding happens where that code is (the builder, on the
 * player's own computer, from the player's own discs), never in this app and never in a published package.
 *
 * The fonts travel with the player's own disc-derived game data (the builder's per-player pack beside the shader packs; on iOS the
 * on-device disc import), never in a published package: the package carries only the fallback face (Nunito Sans). They are kept in
 * the app's private files only. Folders, the first that holds a valid file wins:
 *   files/ui/v2/NAME, then files/ui/NAME - each listed in its folder's manifest.txt ("sha256 size name" per line) and used only
 *   when the file is TrueType and its size and sha256 match. Versioned folders are added, never written over: files/ui itself holds
 *   four empty files on the owner's Thor from a failed copy (2026-11-07), which this check rejects.
 * Nothing valid: the menus keep the bundled Nunito Sans (Scene.Measure.gameFont is false). Pure Java.
 */
final class UiAssets {
    private UiAssets() { }

    static final String DIR = "ui", MANIFEST = "manifest.txt";
    /**
     * Menu revamp M3: the controller glyphs the chip draws (64 x 64 RGBA PNGs from the disc's gfxfontlib_360.xtd, menu-revamp/host/ui_glyphs.py),
     * by the button letter our prompts use.
     */
    static String glyphFile(String glyph) {
        if (glyph == null) return null;
        switch (glyph) {
            case "A": return "glyph-a.png";
            case "B": return "glyph-b.png";
            case "X": return "glyph-x.png";
            case "Y": return "glyph-y.png";
            case "LB": return "glyph-lb.png";
            case "RB": return "glyph-rb.png";
            case "LT": return "glyph-lt.png";
            case "RT": return "glyph-rt.png";
            case "START": return "glyph-start.png";
            case "BACK": case "SELECT": return "glyph-back.png";
            default: return null;
        }
    }

    /** Newest first. */
    /** 1.2.0 (LEDGER 1b): the imported code bundle's ui-asset components first (the builder makes them from the player's discs). */
    static final String[] DIRS = {CodeBundle.UI_DIR, "ui/v4", "ui/v3", "ui/v2", "ui"};
    /** The launcher's backdrop art: the disc's loading-screen picture cut to 16:9 (menu-revamp/host/ui_art.py), drawn sharp. */
    static final String ART_FILE = "menu-art.png";
    /** M6: the landing's tile art per Play item (16:9 crops of the disc's loading-screen backgrounds, host/ui_art.py). */
    static final String TILE_STORY = "tile-story.png", TILE_SANDBOX = "tile-sandbox.png", TILE_ONLINE = "tile-online.png";
    /**
     * 1.2.0 (owner): the Verify Game Data and Import Game Discs tiles' art is the owner's own (not from the disc), shipped in the APK under
     * OWN_ASSETS (assets/ui-own/), drawn before and after setup; without the file the tile keeps the plain ground.
     */
    static final String TILE_VERIFY = "tile-verify-data.png", TILE_IMPORT = "tile-import-discs.png", OWN_ASSETS = "ui-own";
    static final String[] OWN_TILES = {TILE_VERIFY, TILE_IMPORT};
    /** $Font2 (Chalet London): every text of the Enhanced frame (E-F1: 13 template labels within 1 px). */
    static final String GAME_FONT = "chalet-london.ttf";
    /** $Font2_cond (Chalet Comprimé) and $gtaCash (Pricedown): written too, for the landing and in-game pages to come. */
    static final String COND_FONT = "chalet-comprime.ttf", CASH_FONT = "pricedown.ttf";

    /** The first of DIRS whose manifest lists name with this file's exact size and sha256 (and that is TrueType); null otherwise. */
    static File font(File filesDir, String name) {
        if (filesDir == null) return null;
        for (String dir : DIRS) {
            File found = fontIn(new File(filesDir, dir), name);
            if (found != null) return found;
        }
        return null;
    }

    /** dir/name when dir/manifest.txt lists it with this file's exact size and sha256 and it is TrueType; null otherwise. */
    static File fontIn(File dir, String name) {
        File file = new File(dir, name), manifest = new File(dir, MANIFEST);
        if (!file.isFile() || !manifest.isFile()) return null;
        try {
            String want = null;
            long size = -1;
            for (String line : new String(Files.readAllBytes(manifest.toPath()), StandardCharsets.UTF_8).split("\n")) {
                String[] p = line.trim().split(" +");
                if (p.length == 3 && p[2].equals(name)) {
                    want = p[0].toLowerCase();
                    size = Long.parseLong(p[1]);
                }
            }
            if (want == null || file.length() != size || !(name.endsWith(".png") ? isPng(file) : isTrueType(file))) return null;
            return want.equals(sha256(file)) ? file : null;
        } catch (IOException | RuntimeException unreadable) {
            return null;
        }
    }

    /**
     * A TrueType file starts with the sfnt version 0x00010000 and holds at least its table directory (12 bytes): an empty or cut copy
     * listed with its own sha256 (a failed push writes empty files) still falls back to the bundled face instead of reaching
     * Typeface.createFromFile.
     */
    static boolean isTrueType(File file) throws IOException {
        if (file.length() < 12) return false;
        byte[] head = new byte[4];
        try (InputStream in = new FileInputStream(file)) {
            if (in.read(head) != 4) return false;
        }
        return head[0] == 0 && head[1] == 1 && head[2] == 0 && head[3] == 0;
    }

    /** A PNG starts with its 8-byte signature. */
    static boolean isPng(File file) throws IOException {
        if (file.length() < 8) return false;
        byte[] head = new byte[8];
        try (InputStream in = new FileInputStream(file)) {
            if (in.read(head) != 8) return false;
        }
        return (head[0] & 0xFF) == 0x89 && head[1] == 'P' && head[2] == 'N' && head[3] == 'G' && head[4] == 0x0D && head[5] == 0x0A
                && head[6] == 0x1A && head[7] == 0x0A;
    }

    static String sha256(File file) throws IOException {
        MessageDigest digest;
        try {
            digest = MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException impossible) {
            throw new IOException(impossible);
        }
        byte[] buffer = new byte[65536];
        try (InputStream in = new FileInputStream(file)) {
            for (int n; (n = in.read(buffer)) > 0; ) digest.update(buffer, 0, n);
        }
        StringBuilder hex = new StringBuilder(64);
        for (byte b : digest.digest()) hex.append(String.format("%02x", b & 0xFF));
        return hex.toString();
    }
}
