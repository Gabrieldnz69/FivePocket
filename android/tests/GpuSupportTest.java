package dev.recompbootstrap.smoke;

import static dev.recompbootstrap.smoke.LauncherTestSupport.ID_A;
import static dev.recompbootstrap.smoke.LauncherTestSupport.check;
import static dev.recompbootstrap.smoke.LauncherTestSupport.detected;
import static dev.recompbootstrap.smoke.LauncherTestSupport.equal;
import static dev.recompbootstrap.smoke.LauncherTestSupport.newFilesDir;
import static dev.recompbootstrap.smoke.LauncherTestSupport.privateTrace;
import static dev.recompbootstrap.smoke.LauncherTestSupport.run;
import static dev.recompbootstrap.smoke.LauncherTestSupport.write;

import java.io.File;
import java.util.Arrays;

/**
 * Host test of GpuSupport and of what the launcher does with its verdict (bug report 2026-11-07: FivePocket "won't
 * open" on a Retroid Pocket 5, Adreno 650). Run with java -ea.
 *
 * Contracts:
 *   - the probe line is a format between gpu_check_jni.cpp and Java: a device name with spaces and '=' survives, an
 *     error line is "not known", never "refused";
 *   - the verdict refuses exactly what the game's own device gate refuses, item by item, and never more: a fact the
 *     probe did not report is not counted as missing;
 *   - the launcher puts the GPU question before a start on a refused chip, a second Play tries anyway, and that answer
 *     never accepts game code that is not the qualified build;
 *   - after a start, the GPU's reason replaces only the sentences a refused chip produces.
 */
public final class GpuSupportTest {
    /** Every fact the game's gate asks, all reported as present: the line a supported chip gives. */
    private static final String ALL_PRESENT = "GPU_PROBE_V1 api=1.3.128 driver=2149842944 vendor=20803 device_id=1124073472"
            + " descriptor_indexing=1 runtimeDescriptorArray=1 sampledImageArrayDynamicIndexing=1 shaderClipDistance=1"
            + " fragmentStoresAndAtomics=1 vertexPipelineStoresAndAtomics=1 samplerAnisotropy=1 sampleRateShading=1"
            + " independentBlend=1 maxBoundDescriptorSets=32 device=Adreno (TM) 740";

    private static String with(String key, String value) {
        String line = ALL_PRESENT.replaceAll(" " + key + "=[^ ]+", " " + key + "=" + value);
        check(!line.equals(ALL_PRESENT) || ALL_PRESENT.contains(" " + key + "=" + value + " "), "fixture changed " + key);
        return line;
    }

    public static void main(String[] args) throws Exception {
        LauncherTestSupport.requireAssertions();

        run("GpuSupport.theProbeLineIsReadWithTheDeviceNameWhole", () -> {
            GpuSupport.Caps caps = GpuSupport.parse("GPU_PROBE_V1 api=1.1.128 maxBoundDescriptorSets=4"
                    + " device=Adreno (TM) 650 rev=2 x");
            equal(null, caps.error, "a probe line");
            equal("Adreno (TM) 650 rev=2 x", caps.device, "the name is the rest of the line, spaces and '=' kept");
            equal(1, caps.apiMajor, "api major");
            equal(1, caps.apiMinor, "api minor");
            equal(4L, caps.value("maxBoundDescriptorSets"), "the limit");
            equal(-1L, caps.value("rev"), "nothing of the name is read as a fact");
            equal(null, GpuSupport.parse("VULKAN_PROBE_V1 api=4206592"), "another line is not a probe line");
            equal(null, GpuSupport.parse(null), "no line");
        });

        run("GpuSupport.aFailedProbeIsNotKnownAndNeverARefusal", () -> {
            for (String line : new String[] {"GPU_PROBE_V1 error=create-instance--9", "GPU_PROBE_V1 error=no-device", "",
                    "GPU_PROBE_V1 maxBoundDescriptorSets=1 device=x", "garbage"}) {
                GpuSupport.Verdict v = GpuSupport.judge(line);
                check(!v.known && !v.refused(), "not known, not refused: '" + line + "'");
            }
        });

        run("GpuSupport.aChipWithEveryFeatureIsSupported", () -> {
            GpuSupport.Verdict v = GpuSupport.judge(ALL_PRESENT);
            check(v.known && v.supported && !v.refused(), "supported");
            equal(0, v.missing.size(), "nothing missing");
            // The Galaxy Z Fold4 (Adreno 730, driver 0615.98) reports Vulkan 1.1 with descriptor indexing: the game runs.
            check(GpuSupport.judge(with("api", "1.1.128")).supported, "Vulkan 1.1 with descriptor indexing (the Fold4)");
            // The game's check is "at least 5"; the launcher never refuses more than the game does.
            check(GpuSupport.judge(with("maxBoundDescriptorSets", "5")).supported, "5 sets: the game's own check passes");
        });

        run("GpuSupport.eachItemOfTheGamesGateIsRefusedAloneAndNamed", () -> {
            for (String[] feature : GpuSupport.REQUIRED_FEATURES) {
                GpuSupport.Verdict v = GpuSupport.judge(with(feature[0], "0"));
                check(v.refused(), feature[0] + "=0 is refused");
                equal(Arrays.asList(feature[1]), v.missing, "only " + feature[0] + " is named");
            }
            GpuSupport.Verdict sets = GpuSupport.judge(with("maxBoundDescriptorSets", "4"));
            equal(Arrays.asList("6 descriptor sets at once (it has 4)"), sets.missing, "4 sets (Adreno 5xx/6xx drivers)");
            GpuSupport.Verdict old = GpuSupport.judge(with("api", "1.1.128").replace("descriptor_indexing=1", "descriptor_indexing=0"));
            equal(Arrays.asList("Vulkan 1.2, or Vulkan 1.1 with descriptor indexing (it has Vulkan 1.1 without it)"), old.missing,
                    "Vulkan 1.1 without descriptor indexing");
            check(GpuSupport.judge(with("api", "1.0.61")).missing.get(0).startsWith("Vulkan 1.1"), "Vulkan 1.0");
            // On Vulkan 1.2 descriptor indexing is core: the extension's absence from the list is not a refusal.
            check(GpuSupport.judge(ALL_PRESENT.replace("descriptor_indexing=1", "descriptor_indexing=0")).supported,
                    "1.2+ without the extension name");
        });

        run("GpuSupport.aFactTheProbeDidNotReportIsNeverCountedMissing", () -> {
            String partial = "GPU_PROBE_V1 api=1.3.0 device=Mali-G710";
            GpuSupport.Verdict v = GpuSupport.judge(partial);
            check(v.known && v.supported, "only the version reported: nothing is called missing");
        });

        run("GpuSupport.theReasonNamesTheChipAndEveryMissingItem", () -> {
            GpuSupport.Verdict v = GpuSupport.judge(with("shaderClipDistance", "0").replace("maxBoundDescriptorSets=32",
                    "maxBoundDescriptorSets=4").replace("Adreno (TM) 740", "Adreno (TM) 650"));
            equal("Your GPU (Adreno (TM) 650) is missing clip distances in shaders and 6 descriptor sets at once (it has 4)."
                    + " This device isn't supported yet.", GpuSupport.reason(v), "the player's sentence");
            GpuSupport.Verdict nameless = GpuSupport.judge(with("shaderClipDistance", "0").replace(" device=Adreno (TM) 740", ""));
            check(GpuSupport.reason(nameless).startsWith("Your GPU (this device's graphics chip) is missing"), "no name");
        });

        final InstallState ready = detected(LauncherTestSupport.Install.READY);
        final GpuSupport.Verdict refused = GpuSupport.judge(with("maxBoundDescriptorSets", "4"));
        final GpuSupport.Verdict supported = GpuSupport.judge(ALL_PRESENT);

        run("LauncherMenu.aRefusedChipIsAskedAboutBeforeAnythingIsPrepared", () -> {
            LauncherMenu menu = new LauncherMenu(true);
            menu.setInstall(ready);
            menu.setGpu(refused);
            equal(LauncherMenu.Action.NONE, menu.confirm(false), "the first Play prepares nothing");
            check(menu.asking(), "the question stands");
            equal(GpuSupport.beforeStartLine(), menu.secondLine(LauncherMenu.PRIMARY), "short line under Play");
            equal(GpuSupport.beforeStartDetail(refused), menu.failure(), "the whole reason in the right panel");
            check(!menu.failureIsError(), "a question, not a failure");
            // B (or moving away) answers no: the next Play asks again.
            menu.leavePlayItem();
            check(!menu.asking(), "B answers no");
            equal(LauncherMenu.Action.NONE, menu.confirm(false), "asked again after a no");
            equal(LauncherMenu.Action.PLAY, menu.confirm(false), "the second Play tries anyway");
            check(!menu.acceptsUnqualified(), "trying anyway on the GPU never accepts unqualified game code");
            check(menu.prepared(null, false, true), "and it starts");
            equal(LauncherMenu.Action.PLAY, menu.confirm(false), "not asked again after a yes");
        });

        run("LauncherMenu.aSupportedOrUnknownChipChangesNothing", () -> {
            for (GpuSupport.Verdict v : new GpuSupport.Verdict[] {supported, GpuSupport.UNKNOWN, null}) {
                LauncherMenu menu = new LauncherMenu(true);
                menu.setInstall(ready);
                menu.setGpu(v);
                equal(LauncherMenu.Action.PLAY, menu.confirm(false), "Play prepares at once");
                check(!menu.asking(), "no question");
            }
        });

        run("LauncherMenu.afterAStartTheGpuReasonReplacesOnlyWhatARefusedChipCauses", () -> {
            File files = newFilesDir();
            write(new File(privateTrace(files), "used-" + ID_A), new byte[0]);
            File run = new File(privateTrace(files), "run-" + ID_A);
            // The game code stopped by itself: what a refused chip gives (VdInitializeEngines rejected, the game returns).
            write(new File(run, LaunchOutcome.STOPPED_FILE), "TRACE_RETURNED_PROTOCOL_SAVED\n");
            LaunchOutcome stopped = LaunchOutcome.read(files, ID_A, LaunchOutcome.Exit.UNKNOWN_OR_ORDINARY, -1);
            equal(LaunchOutcome.Kind.STOPPED, stopped.kind, "fixture: stopped");
            LauncherMenu menu = new LauncherMenu(true);
            menu.setInstall(ready);
            menu.setGpu(refused);
            menu.startOutcome(stopped);
            equal(GpuSupport.afterStart(refused), menu.failure(), "a stop on a refused chip is said with the GPU's reason");
            LauncherMenu fine = new LauncherMenu(true);
            fine.setInstall(ready);
            fine.setGpu(supported);
            fine.startOutcome(stopped);
            equal(LaunchAdvice.OUTCOME_STOPPED, fine.failure(), "a stop on a supported chip keeps its sentence");
            // A refusal of the files is about the files, whatever the chip.
            write(new File(run, LaunchOutcome.REFUSAL_FILE), "REJECT_HASH\n");
            LaunchOutcome files_ = LaunchOutcome.read(files, ID_A, LaunchOutcome.Exit.UNKNOWN_OR_ORDINARY, -1);
            check(!GpuSupport.explains(refused, files_), "REJECT_HASH is not the GPU");
            write(new File(run, LaunchOutcome.REFUSAL_FILE), "REJECT_RUN_SETUP_2\n");
            check(GpuSupport.explains(refused, LaunchOutcome.read(files, ID_A, LaunchOutcome.Exit.UNKNOWN_OR_ORDINARY, -1)),
                    "the native run's failure is");
            new File(run, LaunchOutcome.REFUSAL_FILE).delete();
            new File(run, LaunchOutcome.STOPPED_FILE).delete();
            check(!GpuSupport.explains(refused, LaunchOutcome.read(files, ID_A, LaunchOutcome.Exit.LOW_MEMORY, 9000)),
                    "a low-memory close keeps its own sentence");
            check(GpuSupport.explains(refused, LaunchOutcome.read(files, ID_A, LaunchOutcome.Exit.CRASH, 900)), "a crash is");
            LauncherTestSupport.discard(files);
        });

        run("GpuSupport.theGraphicsCheckBlockCarriesTheChipTheDriverAndWhatIsMissing", () -> {
            // The Thor's real probe line (device-runs/gpucheck-20261107/probe.txt). Its driverVersion is above 2^31:
            // Qualcomm's 512.676.53 = 0x802a4035 (major << 22 | minor << 12 | patch), worked out by hand.
            String thor = "GPU_PROBE_V1 api=1.3.128 driver=2150252597 vendor=20803 device_id=1124403713 descriptor_indexing=1"
                    + " runtimeDescriptorArray=1 sampledImageArrayDynamicIndexing=1 shaderClipDistance=1"
                    + " fragmentStoresAndAtomics=1 vertexPipelineStoresAndAtomics=1 samplerAnisotropy=1 sampleRateShading=1"
                    + " independentBlend=1 maxBoundDescriptorSets=7 device=Adreno (TM) 740";
            equal(Arrays.asList("GPU: Adreno (TM) 740", "Vulkan: 1.3.128", "Driver: 512.676.53 (0x802a4035)",
                    "Game's graphics needs: all present", "Probe: " + thor), GpuSupport.graphicsCheck(thor), "the whole block");

            String rp5 = with("api", "1.1.128").replace("maxBoundDescriptorSets=32", "maxBoundDescriptorSets=4")
                    .replace("shaderClipDistance=1", "shaderClipDistance=0").replace("Adreno (TM) 740", "Adreno (TM) 650");
            java.util.List<String> refusedBlock = GpuSupport.graphicsCheck(rp5);
            check(refusedBlock.contains("Game's graphics needs: missing clip distances in shaders and 6 descriptor sets at once"
                    + " (it has 4)"), "the missing items, in the launcher's words: " + refusedBlock);
            equal("Probe: " + rp5, refusedBlock.get(refusedBlock.size() - 1), "the driver's own line, unchanged, last");

            for (String line : new String[] {"", "GPU_PROBE_V1 error=no-device", "garbage", null}) {
                check(GpuSupport.graphicsCheck(line).contains("Game's graphics needs: not known"),
                        "never 'missing' without an answer: " + line);
            }
            equal("GPU: the check failed (no-device)", GpuSupport.graphicsCheck("GPU_PROBE_V1 error=no-device").get(0),
                    "the probe's own failure is named");
        });

        run("GpuCheck.aProbeThatNeverReturnedIsNotRunAgainOnTheSameSystemAndApp", () -> {
            // The probe is the launcher process's only driver call: a driver crash in it must cost one open, not every open.
            File dir = newFilesDir();
            int[] runs = {0};
            GpuCheck.Probe answers = () -> { runs[0]++; return ALL_PRESENT; };
            GpuCheck.Probe crashes = () -> { runs[0]++; throw new IllegalStateException("the driver took the process down"); };
            equal(ALL_PRESENT, GpuCheck.guarded(dir, "sys-1 apk=1", answers), "a probe that returns: its answer");
            check(!new File(dir, GpuCheck.MARKER).exists(), "and no marker is left");
            try {
                GpuCheck.guarded(dir, "sys-1 apk=1", crashes);
                check(false, "the crash stands for a process that died");
            } catch (IllegalStateException died) {
                // what a native crash leaves: the marker, written before the call
            }
            check(new File(dir, GpuCheck.MARKER).isFile(), "a probe that never returned leaves the marker");
            runs[0] = 0;
            equal(GpuCheck.CRASHED_LINE, GpuCheck.guarded(dir, "sys-1 apk=1", answers), "the next open does not probe");
            equal(0, runs[0], "the driver is not called again");
            check(!GpuSupport.judge(GpuCheck.CRASHED_LINE).known && !GpuSupport.judge(GpuCheck.CRASHED_LINE).refused(),
                    "and the launcher behaves as without the check (unknown, never refused)");
            equal("GPU: the check failed (crashed-last-time)", GpuSupport.graphicsCheck(GpuCheck.CRASHED_LINE).get(0),
                    "the support report names it");
            equal(ALL_PRESENT, GpuCheck.guarded(dir, "sys-2 apk=1", answers), "a system (driver) update tries once more");
            equal(1, runs[0], "one probe");
            check(!new File(dir, GpuCheck.MARKER).exists(), "and it returned: no marker");
            LauncherTestSupport.discard(dir);
        });

        LauncherTestSupport.finish("GpuSupportTest");
    }
}
