package dev.recompbootstrap.smoke;

import android.os.Build;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * The launcher's one look at the graphics chip: the native probe (libfivepocket_gpu_check.so, gpu_check_jni.cpp) asked
 * once per process, off the UI thread, and judged by GpuSupport. The probe only reads what Vulkan device 0 reports; it
 * creates no device and loads no game code. When the library or Vulkan is not there, the answer is GpuSupport.UNKNOWN and
 * the launcher behaves exactly as it did before this check existed.
 *
 * The probe is the only native code and the only graphics-driver call in the launcher's process (the game's renderer runs
 * in the game window's own process). A driver that crashes inside vkCreateInstance or a feature query would therefore take
 * the launcher down at every open. So the probe is guarded: a marker file is written before it runs and removed after.
 * A marker found at the next open means the probe never returned; the probe is then not run again while the phone's
 * system build and the app's version stay the same (a system or driver update, or a new app, tries once more), and the
 * answer is the error line "crashed-last-time", which judges as UNKNOWN (nothing asked, nothing refused) and is named in
 * the support report's [Graphics check] block.
 */
final class GpuCheck {
    private GpuCheck() {}

    /** The guard's marker, in the app's files folder: present only while a probe runs, or after one that never returned. */
    static final String MARKER = "gpu-check-running.txt";
    static final String CRASHED_LINE = GpuSupport.PREFIX + " error=crashed-last-time";

    /** Something that asks the driver; the real one is the native probe. */
    interface Probe {
        String run();
    }

    private static final Object LOCK = new Object();
    private static String line;   // the probe's answer, once asked; "" when it could not be asked

    private static native String probe();

    /**
     * The probe's line, asked at most once per process. Call off the UI thread (it creates a Vulkan instance). filesDir: the
     * app's files folder (for the guard's marker); null runs the probe unguarded.
     */
    static String line(File filesDir) {
        synchronized (LOCK) {
            if (line == null) {
                String answer = guarded(filesDir, key(), new Probe() {
                    @Override public String run() {
                        try {
                            System.loadLibrary("fivepocket_gpu_check");
                            return probe();
                        } catch (Throwable unavailable) {   // no library in this build, no Vulkan, a linker refusal
                            return "";
                        }
                    }
                });
                line = answer == null ? "" : answer;
            }
            return line;
        }
    }

    /** The judgement of this device; GpuSupport.UNKNOWN when the probe could not answer. Off the UI thread. */
    static GpuSupport.Verdict verdict(File filesDir) {
        return GpuSupport.judge(line(filesDir));
    }

    /** What a crashed probe is remembered against: the system build (it carries the driver) and the installed APK. */
    private static String key() {
        String apk = ShipApplication.apkPath();
        long installed = apk == null ? 0 : new File(apk).lastModified();
        return Build.FINGERPRINT + " apk=" + installed;
    }

    /**
     * The guard (pure java.io; host-tested). A marker holding this key means the last probe with the same system and app
     * never returned: CRASHED_LINE, and the probe is not run. Otherwise the marker is written (with the key), the probe
     * runs, and the marker is removed. When the marker cannot be written the probe still runs (unguarded, as before).
     */
    static String guarded(File filesDir, String key, Probe probe) {
        if (filesDir == null) {
            return probe.run();
        }
        File marker = new File(filesDir, MARKER);
        byte[] want = key.getBytes(StandardCharsets.UTF_8);
        if (marker.isFile()) {
            try {
                if (java.util.Arrays.equals(Files.readAllBytes(marker.toPath()), want)) {
                    return CRASHED_LINE;
                }
            } catch (IOException | RuntimeException unreadable) {
                return CRASHED_LINE;   // a marker is there and cannot be read: the safe answer is not to probe
            }
        }
        boolean written;
        try (FileOutputStream out = new FileOutputStream(marker)) {
            out.write(want);
            out.getFD().sync();   // on disk before the driver is called: a crash must leave it behind
            written = true;
        } catch (IOException | RuntimeException unwritable) {
            written = false;
        }
        String answer = probe.run();
        if (written) {
            marker.delete();   // (one that cannot be removed reads as a crash next time: unknown, never a refusal)
        }
        return answer;
    }
}
