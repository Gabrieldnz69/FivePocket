package dev.recompbootstrap.smoke;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.MediaPlayer;
import android.media.MediaScannerConnection;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Process;
import android.os.SystemClock;
import android.util.Log;
import java.io.File;
import java.util.List;

/**
 * My Music (Update 1 P6, Android: Thor and Fold): the Android side of the player's own music in the game window's process. The
 * decisions (heartbeat, idle report, fades, focus, errors) are MyMusicEngine's, tested on the host; this class only connects it to
 * MediaPlayer, AudioManager and two threads:
 *  - "MyMusic" (THREAD_PRIORITY_DEFAULT): the engine, the 100 ms tick, the fades and MediaPlayer's callbacks. Default priority, not
 *    background: Android puts background-priority threads in the background cgroup (little cores, a small CPU share) while the game
 *    loads every core, and a tick gap over 1 s would let the game unmute its radio under the player's song. The thread sleeps between
 *    ticks; a tick is a few field reads and one JNI call.
 *  - "MyMusicScan" (background priority): makes the folder on the first start, reads it (up to 10,000 entries) and hands the list to
 *    the engine, so a scan never delays a heartbeat. Look For New Music keeps the song playing when its file is still there.
 * The game side is two library exports (my_music.h) reached through the activity's JNI. The game's own audio stream (AAudio) is not
 * touched: MediaPlayer plays in Android's media server as a separate stream.
 * Files: getExternalFilesDir(null)/My Music (MyMusicLibrary). No network: only local files are opened. Logs: tag RecompMyMusic, one
 * line per change of decision or track (never per tick).
 */
final class MyMusicPlayer implements MyMusicMenu {
    static final String TAG = "RecompMyMusic";

    interface Game extends MyMusicEngine.Game { }

    private final Context context;
    private final HandlerThread thread = new HandlerThread("MyMusic", Process.THREAD_PRIORITY_DEFAULT);
    private final HandlerThread scanThread = new HandlerThread("MyMusicScan", Process.THREAD_PRIORITY_BACKGROUND);
    private final Handler h, scanner;
    private final AudioManager audio;
    private final AudioAttributes attributes = new AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build();
    private final MyMusicEngine engine;
    private MediaPlayer mp;                 // (player thread)
    private AudioFocusRequest focusRequest; // (player thread)

    MyMusicPlayer(Context context, Game game) {
        this.context = context.getApplicationContext();
        this.audio = (AudioManager) this.context.getSystemService(Context.AUDIO_SERVICE);
        thread.start();
        scanThread.start();
        h = new Handler(thread.getLooper());
        scanner = new Handler(scanThread.getLooper());
        engine = new MyMusicEngine(game, new AndroidMedia(), new MyMusicEngine.Scheduler() {
            @Override public void post(Runnable r) { h.post(r); }
            @Override public void postDelayed(Runnable r, long ms) { h.postDelayed(r, ms); }
            @Override public void cancelAll() { h.removeCallbacksAndMessages(null); }
        }, SystemClock::elapsedRealtime, line -> Log.i(TAG, line), SystemClock.elapsedRealtime());
        scan();
    }

    static File folder(Context context) {
        final File base = context.getExternalFilesDir(null);
        return base == null ? null : new File(base, MyMusicLibrary.FOLDER);
    }

    /** Reads the folder on the scan thread (making it the first time, and telling the media store so it shows over USB). */
    private void scan() {
        scanner.post(() -> {
            final File dir = folder(context);
            if (MyMusicLibrary.ensureFolder(dir)) {
                MediaScannerConnection.scanFile(context, new String[] {dir.getAbsolutePath()}, null, null);
                Log.i(TAG, "MY_MUSIC folder made");
            }
            final List<MyMusicLibrary.Track> list = MyMusicLibrary.scan(dir);
            if (dir == null) Log.i(TAG, "MY_MUSIC folder unavailable (no external files directory)");
            h.post(() -> engine.tracks(list));
        });
    }

    // ------------------------------------------------------------------------------------------------ MyMusicMenu (any thread)
    @Override public String info() { return engine.info(); }
    @Override public int tracks() { return engine.trackCount(); }
    @Override public void settings(Settings saved) {
        final MyMusicSettings next = MyMusicSettings.from(saved);
        h.post(() -> engine.settings(next));
    }
    @Override public void action(String id) {
        h.post(() -> engine.action(id));
        if (ID_RESCAN.equals(id)) scan();
    }

    /** The game window started (true) or stopped (false). UI thread. */
    void foreground(boolean on) { engine.foreground(on); }

    /** The window is destroyed: idle to the game at once, then the player and both threads are released. UI thread. */
    void release() {
        engine.release(() -> { thread.quitSafely(); scanThread.quitSafely(); });
    }

    // ------------------------------------------------------------------------------------------------ MediaPlayer and focus (player thread)
    private final class AndroidMedia implements MyMusicEngine.Media {
        @Override public boolean open(File file) {
            try {
                if (mp == null) {
                    mp = new MediaPlayer(); // (created on the player thread: its callbacks come back on this looper)
                    mp.setAudioAttributes(attributes);
                    mp.setOnPreparedListener(p -> engine.prepared());
                    mp.setOnCompletionListener(p -> engine.completed());
                    mp.setOnErrorListener((p, what, extra) -> { Log.w(TAG, "MY_MUSIC error what=" + what + " extra=" + extra); engine.error(); return true; });
                } else mp.reset();
                mp.setDataSource(file.getAbsolutePath());
                mp.prepareAsync();
                return true;
            } catch (java.io.IOException | IllegalStateException | IllegalArgumentException | SecurityException e) {
                Log.w(TAG, "MY_MUSIC cannot open: " + e.getClass().getSimpleName());
                if (mp != null) try { mp.reset(); } catch (IllegalStateException ignored) { }
                return false;
            }
        }
        @Override public void start() { if (mp != null) try { mp.start(); } catch (IllegalStateException e) { Log.w(TAG, "MY_MUSIC start refused"); } }
        @Override public void pause() { if (mp != null) try { mp.pause(); } catch (IllegalStateException ignored) { } }
        @Override public boolean playing() { try { return mp != null && mp.isPlaying(); } catch (IllegalStateException e) { return false; } }
        @Override public long position() { try { return mp != null ? mp.getCurrentPosition() : 0; } catch (IllegalStateException e) { return 0; } }
        @Override public void volume(float gain) { if (mp != null) try { mp.setVolume(gain, gain); } catch (IllegalStateException ignored) { } }
        @Override public void reset() { if (mp != null) try { mp.reset(); } catch (IllegalStateException ignored) { } }
        @Override public void release() { if (mp != null) { mp.release(); mp = null; } }
        @Override public boolean requestFocus() {
            if (audio == null) return true;
            if (focusRequest == null) focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN).setAudioAttributes(attributes)
                    .setWillPauseWhenDucked(false).setOnAudioFocusChangeListener(engine::focus, h).build();
            return audio.requestAudioFocus(focusRequest) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        }
        @Override public void abandonFocus() { if (audio != null && focusRequest != null) audio.abandonAudioFocusRequest(focusRequest); }
    }
}
