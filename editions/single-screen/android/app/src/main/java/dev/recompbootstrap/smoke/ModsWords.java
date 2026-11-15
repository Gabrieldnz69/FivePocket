package dev.recompbootstrap.smoke;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/**
 * The words of files/settings/mods.txt, the one file the game library reads for mods (script_mods.h parseConfig; Update 1 P5).
 * Pure Java. Words, whitespace separated, the key in any case:
 *   scripts=on|off             mods on or off (the Settings row "Script mods"; unchanged meaning since v826)
 *   disabled=<pack>|<pack>     packs switched off on the Mods page; names percent-encoded with encode(name, false)
 *   safe=1                     the next start with every mod off (the crash guard's "Start once without mods"); removed after that start
 * An older library reads only scripts=, so the other words change nothing there. render() writes the canonical form:
 * "scripts=on\n" alone stays byte for byte what RuntimeMapping wrote before Update 1 (no drift for an existing install).
 */
final class ModsWords {
    /** The library reads the first 4095 bytes (mods_glue.inc); an older one the first 511, where scripts= always stands first. */
    static final int MAX_BYTES = 4000;

    final boolean scripts;
    final boolean safe;
    /** Switched-off pack names, decoded, in the order they were switched off. Unmodifiable. */
    final List<String> disabled;

    private ModsWords(boolean scripts, boolean safe, List<String> disabled) {
        this.scripts = scripts;
        this.safe = safe && scripts;   // (a safe start without mods on means nothing: the word is dropped)
        this.disabled = Collections.unmodifiableList(new ArrayList<String>(disabled));
    }

    static final ModsWords NONE = new ModsWords(false, false, Collections.<String>emptyList());

    /** The words of a mods.txt (null or unreadable: none, which is "off"). Malformed names are dropped, as the library drops them. */
    static ModsWords parse(byte[] content) {
        if (content == null) return NONE;
        boolean scripts = false, safe = false;
        ArrayList<String> disabled = new ArrayList<String>();
        String text = new String(content, StandardCharsets.ISO_8859_1);   // (bytes as they are: names are percent-encoded ASCII)
        for (String word : text.split("[ \t\r\n]+")) {
            int eq = word.indexOf('=');
            if (eq <= 0) continue;
            String key = word.substring(0, eq).toLowerCase(Locale.ROOT), value = word.substring(eq + 1);
            if (key.equals("scripts")) scripts = yes(value);
            else if (key.equals("safe")) safe = yes(value);
            else if (key.equals("disabled")) {
                for (String part : value.split("\\|", -1)) {
                    String name = decode(part);
                    if (name != null && !disabled.contains(name)) disabled.add(name);
                }
            }
        }
        return new ModsWords(scripts, safe, disabled);
    }

    private static boolean yes(String value) {
        String v = value.toLowerCase(Locale.ROOT);
        return v.equals("on") || v.equals("1") || v.equals("true") || v.equals("yes");
    }

    /**
     * The file's content for these words, or null when the file should be absent (mods off and nothing else to keep: what the
     * Settings row did before Update 1).
     */
    byte[] render() {
        if (!scripts && disabled.isEmpty()) return null;
        StringBuilder out = new StringBuilder(scripts ? "scripts=on" : "scripts=off");
        if (!disabled.isEmpty()) {
            out.append("\ndisabled=");
            for (int i = 0; i < disabled.size(); ++i) out.append(i == 0 ? "" : "|").append(encode(disabled.get(i), false));
        }
        if (safe) out.append("\nsafe=1");
        return out.append('\n').toString().getBytes(StandardCharsets.US_ASCII);
    }

    /** True when render() stays within MAX_BYTES (the page refuses a change that would not). */
    boolean fits() {
        byte[] r = render();
        return r == null || r.length <= MAX_BYTES;
    }

    boolean packOn(String name) {
        return !disabled.contains(name);
    }

    ModsWords withScripts(boolean on) {
        return new ModsWords(on, safe, disabled);
    }

    ModsWords withSafe(boolean on) {
        return new ModsWords(scripts, on, disabled);
    }

    /** One pack switched on or off. */
    ModsWords withPack(String name, boolean on) {
        ArrayList<String> next = new ArrayList<String>(disabled);
        next.remove(name);
        if (!on) next.add(name);
        return new ModsWords(scripts, safe, next);
    }

    /** A removed pack leaves no word behind (a later pack of the same name starts switched on). */
    ModsWords without(String name) {
        return withPack(name, true);
    }

    /** Only the names that still exist (the page tidies the list on its next write). */
    ModsWords keepOnly(List<String> present) {
        ArrayList<String> next = new ArrayList<String>();
        for (String name : disabled) if (present.contains(name)) next.add(name);
        return new ModsWords(scripts, safe, next);
    }

    // ------------------------------------------------------------------------------------------ the encoding (script_mods.h encodeName)

    /** A-Z a-z 0-9 . _ ~ stay (path: also '/' and '-'); every other UTF-8 byte becomes %XX in upper-case hex. */
    static String encode(String name, boolean path) {
        StringBuilder out = new StringBuilder();
        for (byte b : name.getBytes(StandardCharsets.UTF_8)) {
            int c = b & 0xFF;
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '~'
                    || (path && (c == '/' || c == '-'))) {
                out.append((char) c);
            } else {
                out.append('%').append("0123456789ABCDEF".charAt(c >> 4)).append("0123456789ABCDEF".charAt(c & 15));
            }
        }
        return out.toString();
    }

    /**
     * The inverse for a pack name, or null when malformed: a bad escape, a control byte, a space or '|', invalid UTF-8, or a name
     * that is empty, ".", "..", or holds '/' or NUL (no such folder can be a pack). The library applies the same rule.
     */
    static String decode(String text) {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        for (int i = 0; i < text.length(); ++i) {
            char c = text.charAt(i);
            if (c == '%') {
                if (i + 2 >= text.length()) return null;
                int a = Character.digit(text.charAt(i + 1), 16), b = Character.digit(text.charAt(i + 2), 16);
                if (a < 0 || b < 0) return null;
                bytes.write(a << 4 | b);
                i += 2;
                continue;
            }
            if (c <= 0x20 || c == 0x7F || c > 0xFF || c == '|') return null;   // (a raw byte >= 0x80 passes, as in the library)
            bytes.write(c);
        }
        byte[] raw = bytes.toByteArray();
        String name;
        try {
            name = StandardCharsets.UTF_8.newDecoder().decode(java.nio.ByteBuffer.wrap(raw)).toString();
        } catch (java.nio.charset.CharacterCodingException invalid) {
            return null;
        }
        if (name.isEmpty() || name.equals(".") || name.equals("..") || name.indexOf('/') >= 0 || name.indexOf('\0') >= 0) return null;
        return name;
    }
}
