package dev.recompbootstrap.smoke;

import android.content.Context;
import android.media.AudioAttributes;
import android.os.Build;
import android.os.CombinedVibration;
import android.os.VibrationAttributes;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.util.Log;
import android.view.InputDevice;
import android.view.View;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;

/**
 * Update 1 haptics (P2, Android; UPDATE-1-FEATURES-DESIGN.md section 1.3): the game's rumble on the device's own motor, and the short
 * tick of on-screen buttons. The game window (PrivateTraceActivity) makes one in onCreate of a game start and drives it: pause() in
 * onPause, resume() in onResume, stop() in onDestroy; the side menu hands a live change to setMode().
 *
 * The motor: the system's default vibrator (API 31+ VibratorManager.getDefaultVibrator(), else Vibrator). E-H2 (Thor): its built-in
 * pad has no vibrator of its own, the system vibrator has amplitude control. Usage: media (API 33+ VibrationAttributes.USAGE_MEDIA;
 * before that the audio usage GAME, which Android files under media), so the system's media-vibration setting and battery saver apply.
 *
 * Controller rumble (upd1-wave3 android-dualsense, API 31+): when the controller the game is given lists vibrators
 * (InputDevice.getVibratorManager(); a DualSense or an Xbox pad over Bluetooth or USB), the rumble goes to the controller instead of
 * the device's own motor, and back to the device's motor when that controller goes or another one without motors plays (RumbleTarget,
 * the iOS rule). PrivateGamepadInput tells controller() on every change. Both of the controller's motors get the same strength
 * (CombinedVibration.createParallel): one curve, the same Off / Light / Full rules and the same 2 s cap as the device's motor. A device
 * without a vibrator of its own still starts no haptics thread (as before), so it has no controller rumble either.
 *
 * The thread "recomp-haptics" runs HapticsLoop: blocked in recomp_rumble_wait_v1 (through rumble_port.h in the JNI) between the game's
 * motor changes; no polling, no work per frame, no allocation except one VibrationEffect per command (a few per second at most).
 * Android cancels a dead process's vibrations, so a crash cannot leave the motor running.
 *
 * Log tag RecompHaptics (proposed in NOTES.md): HAPTICS_START, HAPTICS_BIND, HAPTICS_MODE, HAPTICS_STOP, HAPTICS_MOTOR; nothing per
 * motor change.
 */
final class Haptics {
    static final String TAG = "RecompHaptics";

    // private_trace_jni.cpp (rumble_port.h): recomp_rumble_bind_v1 (-100 before the library is open or without the export) and
    // recomp_rumble_wait_v1 (HapticsLoop.Port's answers).
    static native int rumbleBind(int on);
    static native long rumbleWait(int lastSeq, int timeoutMs);

    /** The tick's mode for the touch surfaces (main thread readers; set here). */
    private static volatile int tickMode = RumbleMapper.LIGHT;

    private final HapticsLoop loop; // null: no vibrator
    private final Thread thread;
    // upd1-wave3 android-dualsense: the device's own motor (the loop's first target) and where the rumble goes now (main thread)
    private final HapticsLoop.Motor phoneMotor; private final boolean phoneAmplitude;
    private int target = RumbleTarget.PHONE;

    private Haptics(HapticsLoop loop, Thread thread, HapticsLoop.Motor phoneMotor, boolean phoneAmplitude) {
        this.loop = loop; this.thread = thread; this.phoneMotor = phoneMotor; this.phoneAmplitude = phoneAmplitude;
    }

    /** The mode of settings/haptics.txt (HapticsWords). */
    static int readMode(File filesDir) {
        final File f = new File(filesDir, HapticsWords.FILE);
        if (!f.isFile()) return RumbleMapper.LIGHT;
        try (FileInputStream in = new FileInputStream(f)) {
            final byte[] b = new byte[256]; final int n = in.read(b);
            return HapticsWords.parse(n > 0 ? java.util.Arrays.copyOf(b, n) : new byte[0]);
        } catch (IOException e) { return RumbleMapper.LIGHT; }
    }

    /** Starts the haptics thread for a game start (after System.loadLibrary). Never throws; nor do the methods below, nor the thread. */
    static Haptics start(Context context, File filesDir) {
        final int mode = readMode(filesDir);
        tickMode = mode;
        Vibrator vibrator = null;
        try {
            if (Build.VERSION.SDK_INT >= 31) {
                final VibratorManager m = context.getSystemService(VibratorManager.class);
                vibrator = m != null ? m.getDefaultVibrator() : null;
            } else vibrator = (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
        } catch (RuntimeException e) { vibrator = null; }
        if (vibrator == null || !vibrator.hasVibrator()) {
            Log.i(TAG, "HAPTICS_START mode=" + HapticsWords.IDS[mode] + " vibrator=0 (button ticks only)");
            return new Haptics(null, null, null, false);
        }
        final boolean amplitude = vibrator.hasAmplitudeControl();
        final HapticsLoop.Motor phone = new DeviceMotor(vibrator);
        // (guarded: a LinkageError from the natives reads as "no rumble in this library", on this thread and on the main thread)
        final HapticsLoop loop = new HapticsLoop(HapticsLoop.guarded(new HapticsLoop.Port() {
            @Override public int bind(int on) { final int r = rumbleBind(on); Log.i(TAG, "HAPTICS_BIND on=" + on + " -> " + r); return r; }
            @Override public long waitChange(int lastSeq, int timeoutMs) { return rumbleWait(lastSeq, timeoutMs); }
        }), phone, android.os.SystemClock::uptimeMillis, amplitude, mode);
        final Thread t = new Thread(() -> {
            try {
                // Normal priority, not BACKGROUND: nice 10 can also put the thread in the background group (small cores, a low CPU
                // share) while the game saturates the cores, which would make a stop late; it wakes only a few times a second.
                android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_DEFAULT);
                loop.run(); // (never throws; a failure is kept in loop.failure())
                final Throwable failure = loop.failure();
                if (failure != null) Log.w(TAG, "HAPTICS_STOP error " + failure);
                Log.i(TAG, "HAPTICS_STOP thread ended bound=" + (loop.bound() ? 1 : 0) + " commands=" + loop.sent());
            } catch (Throwable e) {
                Log.w(TAG, "HAPTICS_STOP error " + e); // (an uncaught Throwable on any thread would end the game's process)
            }
        }, "recomp-haptics");
        t.setDaemon(true);
        t.start();
        Log.i(TAG, "HAPTICS_START mode=" + HapticsWords.IDS[mode] + " vibrator=1 amplitude=" + (amplitude ? 1 : 0) + " api=" + Build.VERSION.SDK_INT);
        return new Haptics(loop, t, phone, amplitude);
    }

    /**
     * upd1-wave3 android-dualsense: the controller the game is given changed (PrivateGamepadInput.onController; main thread; below 0:
     * none or the on-screen pad). The rumble goes to that controller when it lists vibrators, else to the device's own motor
     * (RumbleTarget). One HAPTICS_TARGET line per change of target; never throws.
     */
    void controller(int deviceId) {
        if (loop == null) return;
        try {
            Object manager = null; int motors = 0; boolean amplitude = true; String name = "";
            if (deviceId >= 0 && Build.VERSION.SDK_INT >= 31) {
                final InputDevice device = InputDevice.getDevice(deviceId);
                if (device != null) {
                    final VibratorManager m = device.getVibratorManager();
                    final int[] ids = m.getVibratorIds();
                    for (int id : ids) amplitude &= m.getVibrator(id).hasAmplitudeControl();
                    motors = ids.length; manager = m; name = device.getName();
                }
            }
            final int next = RumbleTarget.choose(deviceId, motors, true); // (a loop exists only with the device's own vibrator)
            if (next == target) return;
            target = next;
            if (next == RumbleTarget.PHONE) loop.retarget(phoneMotor, phoneAmplitude);
            else loop.retarget(new ControllerMotor((VibratorManager) manager), amplitude);
            Log.i(TAG, "HAPTICS_TARGET " + RumbleTarget.describe(next) + (next >= 0 ? " id=" + deviceId + " motors=" + motors + " amplitude="
                    + (amplitude ? 1 : 0) + " name=" + name : ""));
        } catch (RuntimeException | LinkageError e) { Log.w(TAG, "HAPTICS_TARGET error " + e); }
    }

    // The game window's calls (main thread). None of them throws: the port is guarded and DeviceMotor catches its own failures; the
    // catch below is the last line for anything else (an exception here would end the game's process).
    void setMode(int mode) {
        tickMode = mode;
        try { if (loop != null) loop.setMode(mode); } catch (RuntimeException | LinkageError e) { Log.w(TAG, "HAPTICS_MODE error " + e); }
        Log.i(TAG, "HAPTICS_MODE " + HapticsWords.IDS[mode]);
    }
    void pause() {
        try {
            if (loop == null) return;
            // Update 1 QA (device evidence): the motor commands the game caused before this pause (pause's own cancel not counted);
            // one line per onPause, never per motor change. HAPTICS_STOP cannot serve: onDestroy does not run on a force-stop.
            final int before = loop.sent();
            loop.pause();
            Log.i(TAG, "HAPTICS_PAUSE game_commands=" + before + " bound=" + (loop.bound() ? 1 : 0));
        } catch (RuntimeException | LinkageError e) { Log.w(TAG, "HAPTICS_MOTOR pause error " + e); }
    }
    void resume() { if (loop != null) loop.resume(); }
    /** The game window goes: motor off, unbound; the thread ends within HapticsLoop.IDLE_MS (it is a daemon; nothing waits for it). */
    void stop() { try { if (loop != null) loop.stop(); } catch (RuntimeException | LinkageError e) { Log.w(TAG, "HAPTICS_STOP error " + e); } }

    /** A button press on an on-screen control: one prebaked effect, unless the player chose Off (the system's own switch applies too). */
    static void tick(View view) {
        final int c = HapticsWords.tickConstant(tickMode);
        if (c != HapticsWords.NO_TICK && view != null) view.performHapticFeedback(c);
    }

    /**
     * The device vibrator. Called under HapticsLoop's lock; vibrate / cancel are binder calls that return without waiting for the motor
     * (the main thread's pause() takes the same lock, so the haptics thread runs at normal priority: see start()).
     */
    private static final long LOOP_SEGMENT_MS = 1000;
    private static final class DeviceMotor implements HapticsLoop.Motor {
        private final Vibrator vibrator; private final Object attributes; private boolean broken;
        DeviceMotor(Vibrator vibrator) {
            this.vibrator = vibrator;
            attributes = Build.VERSION.SDK_INT >= 33 ? VibrationAttributes.createForUsage(VibrationAttributes.USAGE_MEDIA)
                    : new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build();
        }
        @Override public void cancel() { if (!broken) try { vibrator.cancel(); } catch (RuntimeException e) { fail(e); } }
        @Override public void oneShot(int ms, int amplitude) { play(VibrationEffect.createOneShot(ms, amplitude)); }
        @Override public void loop(int amplitude) {
            play(VibrationEffect.createWaveform(new long[] {LOOP_SEGMENT_MS}, new int[] {amplitude}, 0));
        }
        @SuppressWarnings("deprecation")
        private void play(VibrationEffect effect) {
            if (broken) return;
            try {
                if (Build.VERSION.SDK_INT >= 33) vibrator.vibrate(effect, (VibrationAttributes) attributes);
                else vibrator.vibrate(effect, (AudioAttributes) attributes);
            } catch (RuntimeException e) { fail(e); }
        }
        private void fail(RuntimeException e) { broken = true; Log.w(TAG, "HAPTICS_MOTOR off: " + e); }
    }

    /**
     * upd1-wave3 android-dualsense: a controller's vibrators (API 31+ InputDevice.getVibratorManager()), both given the same effect
     * (CombinedVibration.createParallel). Called under HapticsLoop's lock like DeviceMotor; vibrate / cancel are binder calls that do
     * not wait for the motors. A failure (the controller went away between its removal and the retarget) silences this motor only; the
     * next retarget makes a new one. Android cancels a dead process's input-device vibrations as it does the device's own.
     */
    @android.annotation.TargetApi(31)
    private static final class ControllerMotor implements HapticsLoop.Motor {
        private final VibratorManager manager; private boolean broken;
        ControllerMotor(VibratorManager manager) { this.manager = manager; }
        @Override public void cancel() { if (!broken) try { manager.cancel(); } catch (RuntimeException e) { fail(e); } }
        @Override public void oneShot(int ms, int amplitude) { play(VibrationEffect.createOneShot(ms, amplitude)); }
        @Override public void loop(int amplitude) {
            play(VibrationEffect.createWaveform(new long[] {LOOP_SEGMENT_MS}, new int[] {amplitude}, 0));
        }
        private void play(VibrationEffect effect) {
            if (broken) return;
            try { manager.vibrate(CombinedVibration.createParallel(effect)); } catch (RuntimeException e) { fail(e); }
        }
        private void fail(RuntimeException e) { broken = true; Log.w(TAG, "HAPTICS_MOTOR controller off: " + e); }
    }
}
