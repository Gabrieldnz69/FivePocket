package dev.recompbootstrap.smoke;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * The game library's mods status (recomp_mods_status_v2, script_mods.h statusV2; or v1's first line), parsed. Pure Java.
 * Line 1: "enabled=1 found=3 applied=1 skipped=1 v=2 safe=0 packs=2 on=2 conflicts=1 refused=0" (v1: only the first four words).
 * Then "pack <name> on= files= applied= skipped= conflicts= pending= refused= [reason=]" per pack ("*" = the loose files), then
 * "applied|skipped <pack> <key> <detail>" and "conflict <pack> <key> <winner pack>". Names, keys and details are percent-encoded.
 * A line without its line feed (the text was cut by the caller's buffer) is ignored; so is anything not understood.
 */
final class ModsStatus {
    static final String LOOSE = "*";

    static final class Pack {
        /** The folder name, decoded; LOOSE for the files/mods/<device>/ layout. */
        final String name;
        final boolean loose, on;
        final int files, applied, skipped, conflicts, pending, refused;
        /** The first reason a file of this pack was skipped, decoded; "" when none. */
        final String reason;

        Pack(String name, boolean on, int files, int applied, int skipped, int conflicts, int pending, int refused, String reason) {
            this.name = name;
            this.loose = LOOSE.equals(name);
            this.on = on;
            this.files = files;
            this.applied = applied;
            this.skipped = skipped;
            this.conflicts = conflicts;
            this.pending = pending;
            this.refused = refused;
            this.reason = reason;
        }
    }

    static final class Outcome {
        /** "applied", "skipped" or "conflict". */
        final String kind;
        final String pack, key;
        /** applied / skipped: the library's words; conflict: the pack that keeps the file. */
        final String detail;

        Outcome(String kind, String pack, String key, String detail) {
            this.kind = kind;
            this.pack = pack;
            this.key = key;
            this.detail = detail;
        }
    }

    final boolean v2, enabled, safe;
    final int found, applied, skipped, conflicts, refused;
    final List<Pack> packs;
    final List<Outcome> outcomes;

    private ModsStatus(boolean v2, boolean enabled, boolean safe, int found, int applied, int skipped, int conflicts, int refused,
                       List<Pack> packs, List<Outcome> outcomes) {
        this.v2 = v2;
        this.enabled = enabled;
        this.safe = safe;
        this.found = found;
        this.applied = applied;
        this.skipped = skipped;
        this.conflicts = conflicts;
        this.refused = refused;
        this.packs = Collections.unmodifiableList(packs);
        this.outcomes = Collections.unmodifiableList(outcomes);
    }

    /** null for null, empty or a first line without enabled= (no library answer). */
    static ModsStatus parse(String text) {
        if (text == null || text.isEmpty()) return null;
        String[] lines = text.split("\n", -1);
        // the last element is what follows the last line feed: a cut line (or "" when the text ended with one)
        int complete = lines.length - 1;
        String head = lines[0];
        int enabled = number(head, "enabled"), version = number(head, "v");
        if (enabled < 0) return null;
        boolean v2 = version == 2;
        ArrayList<Pack> packs = new ArrayList<Pack>();
        ArrayList<Outcome> outcomes = new ArrayList<Outcome>();
        for (int i = 1; v2 && i < complete; ++i) {
            String[] w = lines[i].split(" ");
            if (w[0].equals("pack") && w.length >= 3) {
                String name = w[1].equals(LOOSE) ? LOOSE : ModsWords.decode(w[1]);
                if (name == null) continue;
                String reasonWord = word(lines[i], "reason");
                String reason = reasonWord == null ? "" : decodeText(reasonWord);
                packs.add(new Pack(name, number(lines[i], "on") == 1, at0(lines[i], "files"), at0(lines[i], "applied"),
                        at0(lines[i], "skipped"), at0(lines[i], "conflicts"), at0(lines[i], "pending"), at0(lines[i], "refused"),
                        reason == null ? "" : reason));
            } else if ((w[0].equals("applied") || w[0].equals("skipped") || w[0].equals("conflict")) && w.length >= 4) {
                String pack = w[1].equals(LOOSE) ? LOOSE : ModsWords.decode(w[1]);
                String key = decodeText(w[2]);
                String detail = w[0].equals("conflict") ? (w[3].equals(LOOSE) ? LOOSE : ModsWords.decode(w[3])) : decodeText(w[3]);
                if (pack != null && key != null && detail != null) outcomes.add(new Outcome(w[0], pack, key, detail));
            }
        }
        return new ModsStatus(v2, enabled == 1, number(head, "safe") == 1, at0(head, "found"), at0(head, "applied"),
                at0(head, "skipped"), at0(head, "conflicts"), at0(head, "refused"), packs, outcomes);
    }

    /** The pack of this name (LOOSE for the loose files), or null. */
    Pack pack(String name) {
        for (Pack p : packs) if (p.name.equals(name)) return p;
        return null;
    }

    /** A key or detail: the encoding allows '/' and '-' unencoded; the decoder is the name decoder without its name rules. */
    private static String decodeText(String text) {
        StringBuilder bytesAsChars = new StringBuilder();
        java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
        for (int i = 0; i < text.length(); ++i) {
            char c = text.charAt(i);
            if (c == '%') {
                if (i + 2 >= text.length()) return null;
                int a = Character.digit(text.charAt(i + 1), 16), b = Character.digit(text.charAt(i + 2), 16);
                if (a < 0 || b < 0) return null;
                bytes.write(a << 4 | b);
                i += 2;
            } else {
                bytes.write(c & 0xFF);
            }
        }
        bytesAsChars.append(new String(bytes.toByteArray(), java.nio.charset.StandardCharsets.UTF_8));
        return bytesAsChars.toString();
    }

    private static String word(String line, String key) {
        for (String w : line.split(" ")) if (w.startsWith(key + "=")) return w.substring(key.length() + 1);
        return null;
    }

    private static int number(String line, String key) {
        String w = word(line, key);
        if (w == null) return -1;
        try {
            return Integer.parseInt(w);
        } catch (NumberFormatException no) {
            return -1;
        }
    }

    private static int at0(String line, String key) {
        return Math.max(0, number(line, key));
    }
}
