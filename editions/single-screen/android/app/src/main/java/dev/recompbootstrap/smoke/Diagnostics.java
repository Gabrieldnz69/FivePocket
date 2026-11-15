package dev.recompbootstrap.smoke;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.TimeZone;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Update 1 "Copy diagnostics" (2026-11-07, after a player's "it wouldn't open" with no log to send): the plain-text report a player
 * copies (the launcher's About tab, the side menu's Settings > Support). Pure Java: DiagnosticsAndroid gathers the facts on a
 * background thread when the row is used (nothing runs otherwise; the game's frames never pay for it) and puts the text on the
 * clipboard / the share sheet. Nothing is ever sent by the app: no network.
 *
 * What it holds: app, edition, version; the installed game code (its seal / verified state) and the library's SHA-256; the device,
 * Android version, SoC / GPU strings and free storage; the last starts with their LaunchOutcome and Android's exit record; the game
 * window's refusal word / stopped marker; the tail of the app's own log (its Recomp* tags and crash lines).
 * What it never holds: saves (never read), the gamertag and the profile's xuid (read only to be blanked, see redact), personal file
 * paths (shared storage, documents, the app's data directory) and account names (any e-mail address).
 */
final class Diagnostics {
    private Diagnostics() {}

    static final String TITLE = "FivePocket diagnostics";
    static final String HEADER = TITLE + " (for support: no saves, names or accounts; the app sends nothing)";
    static final int STARTS = 3, LOG_LINES = 80, EXITS = 5, MAX_LINE = 300;
    static final String NAME = "<name>", EMAIL = "<email>", SHARED = "<shared storage>", DOCUMENT = "<document>", APP_DATA = "<app>";

    /** Android's ApplicationExitInfo.getReason() values (API 30+), by name. */
    private static final String[] REASONS = {"UNKNOWN", "EXIT_SELF", "SIGNALED", "LOW_MEMORY", "CRASH", "CRASH_NATIVE", "ANR",
            "INITIALIZATION_FAILURE", "PERMISSION_CHANGE", "EXCESSIVE_RESOURCE_USAGE", "USER_REQUESTED", "USER_STOPPED",
            "DEPENDENCY_DIED", "OTHER", "FREEZER", "PACKAGE_STATE_CHANGE", "PACKAGE_UPDATED"};

    static String reasonName(int reason) {
        return reason >= 0 && reason < REASONS.length ? REASONS[reason] : "REASON_" + reason;
    }

    /** One Android exit record of the game's process (ApplicationExitInfo). */
    static final class Exit {
        final long timestampMs; final int reason, status; final String description;
        Exit(long timestampMs, int reason, int status, String description) {
            this.timestampMs = timestampMs; this.reason = reason; this.status = status; this.description = description == null ? "" : description;
        }
    }

    /** One start the launcher made: its run folder under private-trace, what the game window left there. */
    static final class Start {
        final String id; final long startedMs; final boolean accepted; final String refusal; final boolean stopped;
        Start(String id, long startedMs, boolean accepted, String refusal, boolean stopped) {
            this.id = id; this.startedMs = startedMs; this.accepted = accepted; this.refusal = refusal; this.stopped = stopped;
        }
    }

    /** What DiagnosticsAndroid gathered; any field may be empty when Android does not say. */
    static final class Facts {
        String app = "", edition = "", version = "", packageName = "";
        String gameCode = "";               // the About tab's game-code text (bundle seal / verified state)
        String librarySha = "";             // the installed library's SHA-256 (the bundle's game-library entry, or qualified-code.txt)
        String device = "", android = "", soc = "", gpu = "";
        long freeBytes = -1, totalBytes = -1;
        long nowMs;
        List<Start> starts = new ArrayList<>();
        List<Exit> exits = new ArrayList<>();      // the game process's, newest first
        List<String> log = new ArrayList<>();      // the app's own log lines, oldest first
        List<String> gpuCheck = new ArrayList<>(); // the launcher's GPU check (bugfix-stability's GpuSupport.diagnostics); empty: no block
        Set<String> secrets = new LinkedHashSet<>();   // gamertags and xuids to blank (from identity.txt; never printed)
    }

    // ------------------------------------------------------------------------------------------------ files (pure java.io)

    /** The newest n starts: private-trace/run-(id) folders with a launcher id, newest first. Reads only the game window's markers. */
    static List<Start> starts(File filesDir, int n) {
        File root = new File(filesDir, InstallState.PRIVATE_TRACE);
        File[] runs = root.listFiles();
        List<Start> out = new ArrayList<>();
        if (runs == null) return out;
        List<File> dirs = new ArrayList<>();
        for (File f : runs) {
            String name = f.getName();
            if (name.startsWith("run-") && LaunchRequest.isId(name.substring(4)) && Files.isDirectory(f.toPath(), LinkOption.NOFOLLOW_LINKS)) dirs.add(f);
        }
        Collections.sort(dirs, (a, b) -> Long.compare(started(root, b), started(root, a)));
        for (File d : dirs.subList(0, Math.min(n, dirs.size()))) {
            String id = d.getName().substring(4);
            File claim = new File(root, LaunchOutcome.CLAIM_PREFIX + id);
            String word = LaunchOutcome.refusalWord(new File(d, LaunchOutcome.REFUSAL_FILE));
            out.add(new Start(id, started(root, d), claim.exists(), word == null ? "" : word,
                    Files.isRegularFile(new File(d, LaunchOutcome.STOPPED_FILE).toPath(), LinkOption.NOFOLLOW_LINKS)));
        }
        return out;
    }

    /** When a start began: its claim marker's time (the game window made it while it was created), else the run folder's. */
    private static long started(File root, File runDir) {
        File claim = new File(root, LaunchOutcome.CLAIM_PREFIX + runDir.getName().substring(4));
        long t = claim.lastModified();
        return t > 0 ? t : runDir.lastModified();
    }

    /** The gamertags and xuids of the identity files (the main profile and every save sandbox's), only to blank them in the log. */
    static Set<String> secrets(File filesDir) {
        Set<String> out = new LinkedHashSet<>();
        List<File> files = new ArrayList<>();
        files.add(new File(filesDir, "profile/identity.txt"));
        File[] sandboxes = new File(filesDir, "saves-sandbox").listFiles();
        if (sandboxes != null) for (File s : sandboxes) files.add(new File(s, "profile/identity.txt"));
        for (File f : files) {
            if (!Files.isRegularFile(f.toPath(), LinkOption.NOFOLLOW_LINKS) || f.length() > 4096) continue;
            try {
                for (String line : new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8).split("\n")) {
                    String v = line.startsWith("gamertag=") ? line.substring(9).trim() : line.startsWith("xuid=") ? line.substring(5).trim() : null;
                    if (v != null && v.length() >= 3 && !v.equals("Player") && !v.matches("0+")) out.add(v);
                }
            } catch (IOException unreadable) { /* nothing to blank from it */ }
        }
        return out;
    }

    // ------------------------------------------------------------------------------------------------ redaction

    private static final Pattern EMAILS = Pattern.compile("[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}");
    private static final Pattern DOCUMENTS = Pattern.compile("content://[^\\s\"']+");
    private static final Pattern SHARED_PATHS = Pattern.compile("(?:/storage/|/sdcard/|/mnt/(?:media_rw|user)/)[^\"'\n]*"); // (to the line's end: names hold spaces)
    private static final Pattern APP_PATHS = Pattern.compile("/data/(?:user(?:_de)?/\\d+|data)/[A-Za-z0-9._]+");

    /**
     * Blanks what may name a person: e-mail addresses (accounts), document URIs and shared-storage paths (folder and file names the
     * player chose, e.g. a disc file named after them; a shared path is blanked to the end of its line, since such names hold spaces), the app's data directory prefix (the rest of an app path is the app's own
     * layout), and every given secret (gamertag, xuid) wherever it appears, ignoring case. Control characters become spaces and a
     * line is cut at MAX_LINE.
     */
    static String redact(String text, Set<String> secrets) {
        if (text == null) return "";
        String s = text;
        if (secrets != null) for (String secret : secrets) {
            if (secret == null || secret.length() < 3) continue;
            s = Pattern.compile("(?i)" + Pattern.quote(secret)).matcher(s).replaceAll(Matcher.quoteReplacement(NAME));
        }
        s = EMAILS.matcher(s).replaceAll(EMAIL);
        s = DOCUMENTS.matcher(s).replaceAll(DOCUMENT);
        s = SHARED_PATHS.matcher(s).replaceAll(Matcher.quoteReplacement(SHARED));
        s = APP_PATHS.matcher(s).replaceAll(Matcher.quoteReplacement(APP_DATA));
        StringBuilder out = new StringBuilder(s.length());
        for (String line : s.split("\n", -1)) {
            StringBuilder l = new StringBuilder();
            for (int i = 0; i < line.length(); i++) { char c = line.charAt(i); l.append(c < 0x20 && c != '\t' ? ' ' : c); }
            if (out.length() > 0) out.append('\n');
            out.append(l.length() > MAX_LINE ? l.substring(0, MAX_LINE) + "…" : l);
        }
        return out.toString();
    }

    /** The log lines worth sending: the app's own tags (Recomp*) and crash lines (AndroidRuntime, DEBUG, libc F), the last max. */
    static List<String> logTail(List<String> lines, int max) {
        List<String> keep = new ArrayList<>();
        for (String l : lines) {
            if (l.contains(" Recomp") || l.contains("AndroidRuntime") || l.contains(" DEBUG ") || l.contains(" DEBUG:")
                    || (l.contains(" libc ") && l.contains(" F "))) keep.add(l);
        }
        return keep.size() > max ? keep.subList(keep.size() - max, keep.size()) : keep;
    }

    // ------------------------------------------------------------------------------------------------ the report

    private static String time(long ms, TimeZone zone) {
        if (ms <= 0) return "unknown time";
        SimpleDateFormat f = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT);
        f.setTimeZone(zone);
        int off = zone.getOffset(ms) / 60000;
        return f.format(new Date(ms)) + String.format(Locale.ROOT, " (UTC%+d:%02d)", off / 60, Math.abs(off % 60));
    }

    static String bytes(long b) {
        if (b < 0) return "unknown";
        if (b >= 1L << 30) return String.format(Locale.ROOT, "%.1f GB", b / (double) (1L << 30));
        return String.format(Locale.ROOT, "%d MB", b >> 20);
    }

    /** Android's record for a start: the first exit of the game process at or after it began and before the next start. */
    static Exit exitFor(Start start, long nextStartMs, List<Exit> exits) {
        Exit found = null;
        for (Exit e : exits) {   // newest first: the last match is the earliest one in the window
            if (e.timestampMs >= start.startedMs && (nextStartMs <= 0 || e.timestampMs < nextStartMs)) found = e;
        }
        return found;
    }

    /** What the launcher would say became of the start (LaunchOutcome's words for its kinds). */
    static String outcome(Start s, Exit e) {
        if (!s.accepted) return "NOT_ACCEPTED (the game window never took the start)";
        if (!s.refusal.isEmpty()) return "REFUSED " + s.refusal;
        if (s.stopped) return "STOPPED (the game code returned by itself)";
        if (e == null) return "NOTHING_KNOWN (no Android record: left normally, or still running)";
        switch (LaunchOutcome.exitFor(e.reason)) {
            case CRASH: return "CRASHED";
            case LOW_MEMORY: return "LOW_MEMORY";
            case NOT_RESPONDING: return "NOT_RESPONDING";
            default: return "ENDED (" + reasonName(e.reason) + ")";
        }
    }

    /** The whole report, redacted. */
    static String report(Facts f, TimeZone zone) {
        StringBuilder b = new StringBuilder();
        b.append(HEADER).append('\n').append("Made: ").append(time(f.nowMs, zone)).append("\n\n");
        b.append("[App]\n").append("App: ").append(f.app).append(" - ").append(f.edition).append('\n')
                .append("Version: ").append(f.version).append('\n').append("Package: ").append(f.packageName).append('\n')
                .append("Game code: ").append(f.gameCode.isEmpty() ? "unknown" : f.gameCode.replace("\n", "\n  ")).append('\n')
                .append("Library SHA-256: ").append(f.librarySha.isEmpty() ? "not installed or unknown" : f.librarySha).append("\n\n");
        b.append("[Device]\n").append("Device: ").append(f.device).append('\n').append("Android: ").append(f.android).append('\n')
                .append("SoC: ").append(f.soc).append('\n').append("GPU: ").append(f.gpu).append('\n')
                .append("Free storage: ").append(bytes(f.freeBytes)).append(" of ").append(bytes(f.totalBytes)).append("\n\n");
        if (!f.gpuCheck.isEmpty()) {   // (the GPU check's own words: the chip, its driver and Vulkan, what the game needs and it lacks)
            b.append("[Graphics check]\n");
            for (String l : f.gpuCheck) b.append(l).append('\n');
            b.append('\n');
        }
        b.append("[Last starts, newest first]\n");
        if (f.starts.isEmpty()) b.append("none recorded\n");
        for (int i = 0; i < f.starts.size(); i++) {
            Start s = f.starts.get(i);
            long next = i > 0 ? f.starts.get(i - 1).startedMs : 0;
            Exit e = exitFor(s, next, f.exits);
            b.append(i + 1).append(". ").append(time(s.startedMs, zone)).append(" run ").append(s.id.length() > 8 ? s.id.substring(0, 8) : s.id)
                    .append(": ").append(outcome(s, e)).append('\n');
            if (e != null) b.append("   Android: ").append(reasonName(e.reason)).append(" status=").append(e.status).append(" at ")
                    .append(time(e.timestampMs, zone)).append(e.description.isEmpty() ? "" : " \"" + e.description + "\"").append('\n');
        }
        b.append("\n[Android's exit records of the game process, newest first]\n");
        if (f.exits.isEmpty()) b.append("none (or Android 10 and older, which keep none)\n");
        for (Exit e : f.exits.subList(0, Math.min(EXITS, f.exits.size())))
            b.append(time(e.timestampMs, zone)).append(' ').append(reasonName(e.reason)).append(" status=").append(e.status)
                    .append(e.description.isEmpty() ? "" : " \"" + e.description + "\"").append('\n');
        b.append("\n[The app's log, last ").append(f.log.size()).append(" lines]\n");
        if (f.log.isEmpty()) b.append("none\n");
        for (String l : f.log) b.append(l).append('\n');
        return redact(b.toString(), f.secrets);
    }

    /** The About section's text and the side menu row's help (one wording). */
    static final String ABOUT = "Copy diagnostics: press A here. A text report for support goes to the clipboard and the share sheet opens: "
            + "the app and game code versions, this device, the last starts and how they ended, and the app's own log. "
            + "No saves, names, accounts or personal file paths. The app sends nothing; you choose where to paste it.";
    static final String ROW_HELP = "Copies a text report for support to the clipboard (app, game code, device, last starts, the app's log). "
            + "No saves, names or accounts; nothing is sent.";
    static final String DONE = "Done: diagnostics copied to the clipboard.";
    static String failed(String why) { return "Diagnostics could not be copied" + (why == null || why.isEmpty() ? "." : ": " + why + "."); }
    static List<String> sectionLines(String result) {
        return result == null || result.isEmpty() ? Arrays.asList(ABOUT) : Arrays.asList(ABOUT, "", result);
    }
}
