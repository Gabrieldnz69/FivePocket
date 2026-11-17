package dev.recompbootstrap.smoke;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Whether this device's graphics chip can run the game, from the one line GpuCheck's native probe reports, and the
 * words the launcher says about it. Pure Java: parse() and judge() never throw and never touch Android, so the rule
 * runs in a host test (GpuSupportTest).
 *
 * Why it exists (bug report 2026-11-07: FivePocket "won't open" on a Retroid Pocket 5, Snapdragon 865 / Adreno 650,
 * Android 13). The game's renderer refuses a device it cannot drive, at VdInitializeEngines; the game then stops in its
 * first second and the player saw only "The game has stopped." The renderer's reasons go to the game's standard output
 * (kept in the run's protocol file, never in logcat), and its descriptor-set check refuses without saying anything.
 *
 * The rule is the game's own device gate, item for item, as built into the game code of 1.1.0-alpha.1 (single screen
 * v718, Thor Edition v852) and every later build of that line:
 *   - tools/vulkan_probe/vk11_gate.h plan(): Vulkan 1.2 or newer; or Vulkan 1.1 with VK_EXT_descriptor_indexing;
 *   - the attempts' device.cpp ProbeDevice::create(): each feature the engine requests must be reported;
 *   - vulkan_engine.cpp VulkanEngine::initialize(): runtimeDescriptorArray, shaderClipDistance and
 *     fragmentStoresAndAtomics always; renderer_boundary_trace.cpp bindRendererInlineCapture() requests the native
 *     logical geometry before the device is made (the game code contains that branch: its refusal text is in the
 *     v718 library), which adds vertexPipelineStoresAndAtomics, samplerAnisotropy, sampleRateShading and
 *     independentBlend; create() adds shaderSampledImageArrayDynamicIndexing for the runtime arrays;
 *   - initialize() refuses maxBoundDescriptorSets below 5. (The main draw layout binds 6 sets, generic_renderer.cpp,
 *     so 5 is itself not enough; the launcher still judges by the game's own check and never refuses more than the
 *     game does.)
 * The launcher never stops a start on this alone: it says what is missing and lets the player try anyway, because a
 * newer graphics driver can change any of these answers.
 */
final class GpuSupport {
    private GpuSupport() {}

    static final String PREFIX = "GPU_PROBE_V1";
    /** The fewest descriptor sets the game's own check accepts (VulkanEngine::initialize). */
    static final int GAME_MINIMUM_SETS = 5;
    /** The sets the game's main draw layout binds (generic_renderer.cpp: setLayoutCount=6). Said to the player. */
    static final int GAME_SETS_USED = 6;

    /** Each feature the game's gate requires, with the words the launcher uses for it. Order = the order they are said. */
    static final String[][] REQUIRED_FEATURES = {
        {"runtimeDescriptorArray", "runtime descriptor arrays"},
        {"sampledImageArrayDynamicIndexing", "dynamic indexing of image arrays"},
        {"shaderClipDistance", "clip distances in shaders"},
        {"fragmentStoresAndAtomics", "stores and atomics in pixel shaders"},
        {"vertexPipelineStoresAndAtomics", "stores and atomics in vertex shaders"},
        {"samplerAnisotropy", "anisotropic filtering"},
        {"sampleRateShading", "sample-rate shading"},
        {"independentBlend", "independent blending"},
    };

    /** What the probe reported. error is non-null when the probe itself failed; values are then empty. */
    static final class Caps {
        final String error;
        final String device;
        final int apiMajor, apiMinor;
        final Map<String, Long> values;

        Caps(String error, String device, int apiMajor, int apiMinor, Map<String, Long> values) {
            this.error = error;
            this.device = device;
            this.apiMajor = apiMajor;
            this.apiMinor = apiMinor;
            this.values = values;
        }

        long value(String key) {
            Long v = values.get(key);
            return v == null ? -1 : v;
        }
    }

    /** The judgement: known is false when nothing could be learned (no probe, a failed probe, an unreadable line). */
    static final class Verdict {
        final boolean known;
        final boolean supported;
        final String device;
        /** What is missing, in the launcher's words, in REQUIRED order; empty when supported or unknown. */
        final List<String> missing;

        Verdict(boolean known, boolean supported, String device, List<String> missing) {
            this.known = known;
            this.supported = supported;
            this.device = device;
            this.missing = Collections.unmodifiableList(missing);
        }

        /** True only when the probe answered and the game's gate would refuse this device. */
        boolean refused() {
            return known && !supported;
        }
    }

    static final Verdict UNKNOWN = new Verdict(false, false, "", new ArrayList<>());

    /** The probe's line as Caps; null when the line is not a probe line at all. Never throws. */
    static Caps parse(String line) {
        if (line == null) {
            return null;
        }
        String text = line.trim();
        if (!text.startsWith(PREFIX + " ")) {
            return null;
        }
        String rest = text.substring(PREFIX.length() + 1);
        if (rest.startsWith("error=")) {
            String error = rest.substring("error=".length()).trim();
            return new Caps(error.isEmpty() ? "unknown" : error, "", -1, -1, new HashMap<>());
        }
        String device = "";
        int at = rest.indexOf(" device=");
        if (rest.startsWith("device=")) {
            device = rest.substring("device=".length()).trim();
            rest = "";
        } else if (at >= 0) {
            device = rest.substring(at + " device=".length()).trim();
            rest = rest.substring(0, at);
        }
        Map<String, Long> values = new HashMap<>();
        int major = -1, minor = -1;
        for (String pair : rest.trim().split(" +")) {
            int eq = pair.indexOf('=');
            if (eq <= 0) {
                continue;
            }
            String key = pair.substring(0, eq), value = pair.substring(eq + 1);
            if (key.equals("api")) {
                String[] parts = value.split("\\.");
                if (parts.length >= 2) {
                    major = (int) Math.min(number(parts[0]), Integer.MAX_VALUE);
                    minor = (int) Math.min(number(parts[1]), Integer.MAX_VALUE);
                }
                if (parts.length >= 3) {
                    long patch = number(parts[2]);
                    if (patch >= 0) values.put("api_patch", patch);
                }
                continue;
            }
            long n = number(value);
            if (n >= 0) {
                values.put(key, n);
            }
        }
        if (major < 0 || minor < 0) {
            return new Caps("no-api-version", device, -1, -1, values);
        }
        return new Caps(null, device, major, minor, values);
    }

    /** The game's device gate applied to caps. A key the probe did not report counts as not known (never as missing). */
    static Verdict judge(Caps caps) {
        if (caps == null || caps.error != null) {
            return UNKNOWN;
        }
        List<String> missing = new ArrayList<>();
        boolean api12 = caps.apiMajor > 1 || (caps.apiMajor == 1 && caps.apiMinor >= 2);
        boolean api11 = caps.apiMajor > 1 || (caps.apiMajor == 1 && caps.apiMinor >= 1);
        if (!api11) {
            missing.add("Vulkan 1.1 (it has Vulkan " + caps.apiMajor + "." + caps.apiMinor + ")");
        } else if (!api12 && caps.value("descriptor_indexing") == 0) {
            missing.add("Vulkan 1.2, or Vulkan 1.1 with descriptor indexing (it has Vulkan " + caps.apiMajor + "."
                    + caps.apiMinor + " without it)");
        }
        for (String[] feature : REQUIRED_FEATURES) {
            if (caps.value(feature[0]) == 0) {
                missing.add(feature[1]);
            }
        }
        long sets = caps.value("maxBoundDescriptorSets");
        if (sets >= 0 && sets < GAME_MINIMUM_SETS) {
            missing.add(GAME_SETS_USED + " descriptor sets at once (it has " + sets + ")");
        }
        return new Verdict(true, missing.isEmpty(), caps.device, missing);
    }

    static Verdict judge(String line) {
        return judge(parse(line));
    }

    // ------------------------------------------------------------------------------------------ words

    static final String NOT_SUPPORTED_YET = "This device isn't supported yet.";
    static final String TRY_ANYWAY = "A: try anyway";

    /** "Adreno (TM) 650" when the probe named it, else "this device's graphics chip". */
    static String chip(Verdict verdict) {
        return verdict.device.isEmpty() ? "this device's graphics chip" : verdict.device;
    }

    /** "a, b and c". */
    static String list(List<String> items) {
        if (items.isEmpty()) {
            return "";
        }
        if (items.size() == 1) {
            return items.get(0);
        }
        return String.join(", ", items.subList(0, items.size() - 1)) + " and " + items.get(items.size() - 1);
    }

    /** "Your GPU (Adreno (TM) 650) is missing 6 descriptor sets at once (it has 4). This device isn't supported yet." */
    static String reason(Verdict verdict) {
        return "Your GPU (" + chip(verdict) + ") is missing " + list(verdict.missing) + ". " + NOT_SUPPORTED_YET;
    }

    /** The short line on the Play page before a start, when the gate would refuse: the question to try anyway. */
    static String beforeStartLine() {
        return "Your GPU isn't supported yet. " + TRY_ANYWAY;
    }

    /** The detail under it: the whole reason, and what pressing A again does. */
    static String beforeStartDetail(Verdict verdict) {
        return reason(verdict) + " The game will most likely stop at once. Press A on Play again to try anyway, or B"
                + " to leave it. A newer graphics driver for this device may change this. " + HOW_TO_TELL_US;
    }

    /** After a start that ended at once on a device the gate refuses: what happened and why. */
    static String afterStart(Verdict verdict) {
        return "The game could not start. " + reason(verdict) + " " + HOW_TO_TELL_US;
    }

    /**
     * True when the end of a start is the one a refused graphics chip produces, so that the GPU's reason is the right
     * thing to say instead of the general sentence: the game code stopped by itself (the renderer refused the device at
     * VdInitializeEngines and the game returned: TRACE_RETURNED_PROTOCOL_SAVED), the game window's native run failed
     * (REJECT_NATIVE_RUN, REJECT_RUN_SETUP_n, REJECT_EMPTY_PROTOCOL), or the process crashed or stopped responding.
     * A refusal of the files (game code, start settings) and a low-memory close keep their own sentences.
     */
    static boolean explains(Verdict verdict, LaunchOutcome outcome) {
        if (verdict == null || !verdict.refused() || outcome == null) {
            return false;
        }
        switch (outcome.kind) {
            case STOPPED:
            case CRASHED:
            case NOT_RESPONDING:
                return true;
            case REFUSED:
                return outcome.word.equals("REJECT_NATIVE_RUN") || outcome.word.startsWith("REJECT_RUN_SETUP")
                        || outcome.word.equals("REJECT_EMPTY_PROTOCOL");
            default:
                return false;
        }
    }

    // ------------------------------------------------------------------------------------------ diagnostics

    /** The heading of this check's block in the support report (About > Diagnostics, Diagnostics.java in the release trees). */
    static final String GRAPHICS_CHECK_HEADING = "[Graphics check]";
    /** Where the player finds the report (the release trees' About section and its "Copy Diagnostics" prompt). */
    static final String HOW_TO_TELL_US = "About > Diagnostics: A copies a report to paste to us.";

    /** Vulkan's driverVersion in the vendor's own numbering: NVIDIA packs 10.8.8.6 bits, every other vendor 10.10.12. */
    static String driverVersion(long vendor, long raw) {
        if (raw < 0) {
            return "unknown";
        }
        String decoded = vendor == 0x10DE
                ? (raw >>> 22) + "." + ((raw >>> 14) & 0xFF) + "." + ((raw >>> 6) & 0xFF) + "." + (raw & 0x3F)
                : (raw >>> 22) + "." + ((raw >>> 12) & 0x3FF) + "." + (raw & 0xFFF);
        return decoded + " (0x" + Long.toHexString(raw) + ")";
    }

    /**
     * The lines of the support report's "[Graphics check]" block (without the heading): what the launcher's probe read from
     * Vulkan device 0 and what the game's gate finds missing, with the probe's own line last so that nothing in the words
     * above can hide what the driver reported. The phone, Android and SoC are in the report's own [Device] block.
     * probeLine is GpuCheck.line() ("" when it could not be asked). Never throws.
     */
    static List<String> graphicsCheck(String probeLine) {
        String line = probeLine == null ? "" : probeLine.trim();
        Caps caps = parse(line);
        Verdict verdict = judge(caps);
        List<String> out = new ArrayList<>();
        if (caps == null) {
            out.add("GPU: not checked (" + (line.isEmpty() ? "the check is not in this build" : "unreadable answer") + ")");
        } else if (caps.error != null && caps.apiMajor < 0 && caps.values.isEmpty()) {
            out.add("GPU: the check failed (" + caps.error + ")");
        } else {
            out.add("GPU: " + (caps.device.isEmpty() ? "unknown" : caps.device));
            out.add("Vulkan: " + (caps.apiMajor < 0 ? "unknown" : caps.apiMajor + "." + caps.apiMinor
                    + (caps.value("api_patch") >= 0 ? "." + caps.value("api_patch") : "")));
            out.add("Driver: " + driverVersion(caps.value("vendor"), caps.value("driver")));
        }
        out.add("Game's graphics needs: " + (!verdict.known ? "not known" : verdict.supported ? "all present"
                : "missing " + list(verdict.missing)));
        out.add("Probe: " + (line.isEmpty() ? "none" : line));
        return out;
    }

    /** A decimal 32-bit unsigned number (Vulkan's uint32_t: a Qualcomm driverVersion is above 2^31); -1 otherwise. */
    private static long number(String text) {
        if (text.isEmpty() || text.length() > 10) {
            return -1;
        }
        long n = 0;
        for (int i = 0; i < text.length(); ++i) {
            char c = text.charAt(i);
            if (c < '0' || c > '9') {
                return -1;
            }
            n = n * 10 + (c - '0');
        }
        return n > 0xFFFFFFFFL ? -1 : n;
    }

    // ------------------------------------------------------------------------------------------ try anyway (v1302)

    /**
     * The opt-in switch (files/private-trace/gpu-try-anyway, default off): with it, the game library's graphics start on a GPU
     * below its descriptor-set minimum instead of refusing (vulkan_engine.cpp, v1302+; read only on such a GPU). The launcher's
     * About > Unsupported GPU turns it on and off; the support report says its state.
     */
    static final String TRY_ANYWAY_SWITCH = "gpu-try-anyway";
    static final String TRY_ANYWAY_ROW = "Try anyway on an unsupported GPU";

    static File tryAnywayFile(File filesDir) {
        return new File(new File(filesDir, InstallState.PRIVATE_TRACE), TRY_ANYWAY_SWITCH);
    }

    static boolean tryAnyway(File filesDir) {
        return filesDir != null && tryAnywayFile(filesDir).isFile();
    }

    /** Turns the switch on or off; returns the state the file now has (unchanged when it could not be written). */
    static boolean setTryAnyway(File filesDir, boolean on) {
        File file = tryAnywayFile(filesDir);
        try {
            if (on) {
                File dir = file.getParentFile();
                if (dir != null && !dir.isDirectory()) dir.mkdirs();
                if (!file.isFile()) new FileOutputStream(file).close();
            } else if (file.isFile()) {
                file.delete();
            }
        } catch (IOException | SecurityException failed) {
            // the state below says what happened
        }
        return tryAnyway(filesDir);
    }

    /** One line for the player: what the launcher's check found for this device. */
    static String status(Verdict verdict) {
        if (verdict == null || !verdict.known) return "This device's graphics chip: not checked";
        return "This device's graphics chip: " + (verdict.supported ? "supported" : "not supported yet (" + list(verdict.missing) + ")");
    }

    /** The About section's text for the switch's state. */
    static String tryAnywayText(boolean on) {
        return TRY_ANYWAY_ROW + ": " + (on ? "On" : "Off") + "\n\n"
                + "Some graphics chips have fewer of the resources the game needs than it asks for, and the game then refuses to"
                + " start. Turned on, it starts anyway: the game may crash or draw wrongly.\n\n"
                + "On a supported graphics chip this changes nothing. A turns it " + (on ? "off." : "on.");
    }

    /** The support report's line for the switch. */
    static String tryAnywayLine(boolean on) {
        return TRY_ANYWAY_ROW + ": " + (on ? "on" : "off");
    }
}
