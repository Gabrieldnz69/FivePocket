package dev.recompbootstrap.smoke;

import android.content.Intent;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;
import java.io.File;
import java.io.IOException;

/**
 * 2026-11-17 (owner: "no silent first menu"): setup's last step (MenuSoundsPrep). Creates the pass switch, starts the game as a normal
 * Story start (off the UI thread, as LauncherScreen.play) - the game window covers itself with the same page (MenuSoundPassOverlay) and
 * ends itself when the library has answered - then polls files/ui/sounds, removes the switch and the answer, and closes when the step
 * has ended (A or B then opens the menu below it). No launch helper, a refused start, a failure or the time limit all end the step.
 */
final class MenuSoundsScreen implements Screen {
    static final String TAG = "RecompSetup";
    private static final String LAUNCH_HELPER = "dev.recompbootstrap.smoke.DebugGameLauncher";
    private final MenuSoundsPrep prep = new MenuSoundsPrep();
    private ScreenHost host;
    private UiKit.SceneView root;
    private boolean launched, shown, cleaned;

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (!shown || prep.ended()) return;
            File filesDir = host.filesDir();
            prep.update(SystemClock.elapsedRealtime(), MenuSoundsPrep.found(filesDir), MenuSoundsPrep.done(filesDir), host.gameRunning());
            if (prep.ended()) { Log.i(TAG, "MENU_SOUNDS_STEP end state=" + prep.state + " found=" + prep.found + " reason=" + prep.reason); cleanUp(); }
            render();
            if (!prep.ended()) root.postDelayed(this, MenuSoundsPrep.POLL_MS);
        }
    };

    @Override public View createView(ScreenHost screenHost) {
        host = screenHost;
        root = new UiKit.SceneView(host.activity(), host.unit(), new UiKit.Listener() {
            @Override public void onHit(int id) {
                if (id / 100 * 100 == MenuLayout.HIT_PROMPT) onNav(id % 100, false);
            }

            @Override public void onScroll(int lines) {
            }
        });
        return root;
    }

    @Override public void onShown() {
        shown = true;
        render();
        if (!launched) launch();
        else { root.removeCallbacks(poll); root.post(poll); }
    }

    @Override public void onHidden() {
        shown = false;
        root.removeCallbacks(poll);
    }

    @Override public void onAppBack() {
        if (shown) { root.removeCallbacks(poll); root.post(poll); }
    }

    @Override public boolean onNav(int command, boolean repeat) {
        if (repeat) return true;
        if (prep.ended()) {
            if (command == NavInput.CONFIRM || command == NavInput.BACK) host.pop();
            return true;
        }
        if (command == NavInput.BACK) {
            prep.skip();
            cleanUp();
            render();
            return true;
        }
        return false;
    }

    @Override public void onActivityResult(int requestCode, int resultCode, Intent data) {
    }

    private void launch() {
        launched = true;
        final File filesDir = host.filesDir();
        final GameLauncher launcher = find();
        if (launcher == null) { prep.notStarted("no game window in this build"); render(); return; }
        final String[] error = new String[1];
        final boolean[] unsupported = new boolean[1];
        host.background(new Runnable() {
            @Override public void run() {
                if (!MenuSoundsPrep.libraryRecords(filesDir)) { unsupported[0] = true; Log.i(TAG, "MENU_SOUNDS_STEP skipped: the library has no pass"); return; }   // (the large file: off the UI thread)
                File old = MenuSoundsPrep.doneFile(filesDir), sw = MenuSoundsPrep.switchFile(filesDir);
                if (old.exists() && !old.delete()) { error[0] = "an old answer could not be removed"; return; }
                try {
                    if (!sw.isFile() && !sw.createNewFile()) { error[0] = "the switch could not be made"; return; }
                } catch (IOException e) {
                    error[0] = "the switch could not be made";
                    return;
                }
                error[0] = launcher.prepare(filesDir, false);
            }
        }, new Runnable() {
            @Override public void run() {
                if (unsupported[0]) { prep.skip(); cleaned = true; if (shown) host.pop(); return; }   // this game code has no pass: no step
                if (error[0] != null) { Log.i(TAG, "MENU_SOUNDS_STEP not started: " + error[0]); prep.notStarted(error[0]); cleanUp(); render(); return; }
                try {
                    Log.i(TAG, "MENU_SOUNDS_STEP start switch=" + MenuSoundsPrep.switchFile(host.filesDir()).isFile());
                    launcher.start(host.activity(), PlayMode.STORY);   // the contract: a normal Story start, the switch tells the pass
                    prep.started(SystemClock.elapsedRealtime());
                } catch (RuntimeException refused) {
                    prep.notStarted(refused.getClass().getSimpleName());
                    cleanUp();
                }
                render();
                root.removeCallbacks(poll);
                root.postDelayed(poll, MenuSoundsPrep.POLL_MS);
            }
        });
    }

    /** The pass is over for the app: the switch and the answer go (the sound files stay). */
    private void cleanUp() {
        if (cleaned) return;
        cleaned = true;
        File filesDir = host.filesDir();
        MenuSoundsPrep.switchFile(filesDir).delete();
        MenuSoundsPrep.doneFile(filesDir).delete();
    }

    private static GameLauncher find() {
        try {
            return (GameLauncher) Class.forName(LAUNCH_HELPER).getDeclaredConstructor().newInstance();
        } catch (ReflectiveOperationException | RuntimeException | LinkageError absent) {
            return null;
        }
    }

    private void render() {
        if (root != null) SetupFrame.show(root, prep.page());
    }
}
