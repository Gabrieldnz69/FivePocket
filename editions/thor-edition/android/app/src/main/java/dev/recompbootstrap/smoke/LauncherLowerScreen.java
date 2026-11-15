package dev.recompbootstrap.smoke;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;

/**
 * qol20 (owner 2026-11-08: "yes the menu should run on the lower screen as well. just even a black screen or something"): while the
 * launcher menu is in front, the Thor's lower screen is ours - a black window - instead of Android's home screen with its app grid.
 *
 * Owned by BootstrapActivity in launcher mode: show() in onResume, hide() in onStop and onDestroy. Not hidden in onPause: when the
 * game starts, the launcher pauses before the game process exists; the game window opens its own lower screen (SecondScreen) in
 * its onCreate, and the launcher stops only after the game window is up, so the lower screen never shows Android's home in between.
 *
 * Only while the menu is on the main display; the display is chosen as the game window chooses its HUD display
 * (SecondScreenWord.choose). A single-screen device has no other display: nothing is opened. The window never takes focus (FLAG_NOT_FOCUSABLE),
 * so the controller stays with the menu; touches on it are taken and ignored, so nothing on the home screen under it can be
 * started blind. Every failure only logs (tag RecompLauncherLower) and leaves the menu as it is.
 */
final class LauncherLowerScreen {
    static final String TAG = "RecompLauncherLower";
    private final Activity activity;
    private Presentation shown;

    LauncherLowerScreen(Activity activity) {
        this.activity = activity;
    }

    void show() {
        if (shown != null) return;
        Display own = activity.getWindowManager().getDefaultDisplay();   // the display this activity's window is on
        DisplayManager manager = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        if (manager == null || own == null) return;
        if (own.getDisplayId() != Display.DEFAULT_DISPLAY) return;   // the menu itself on the lower screen: the top screen is not ours to cover
        Display[] all = manager.getDisplays();
        Display[] presentationDisplays = manager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        int[] ids = new int[all.length];
        boolean[] presentation = new boolean[all.length], on = new boolean[all.length];
        for (int k = 0; k < all.length; ++k) {
            ids[k] = all[k].getDisplayId();
            on[k] = all[k].getState() != Display.STATE_OFF;
            for (Display p : presentationDisplays) if (p.getDisplayId() == ids[k]) presentation[k] = true;
        }
        int id = SecondScreenWord.choose(ids, presentation, on, own.getDisplayId());
        if (id < 0) return;   // single screen: nothing (no log: every resume would repeat it)
        Display display = manager.getDisplay(id);
        if (display == null) return;
        try {
            Black black = new Black(activity, display);
            black.show();
            shown = black;
            Log.i(TAG, "LAUNCHER_LOWER shown display=" + id + " (" + display.getName() + ")");
        } catch (RuntimeException error) {   // (InvalidDisplayException, BadTokenException: Android's screen stays, the menu goes on)
            Log.w(TAG, "LAUNCHER_LOWER show-failed " + error.getClass().getSimpleName() + ": " + error.getMessage());
        }
    }

    void hide() {
        if (shown == null) return;
        try {
            shown.dismiss();
        } catch (RuntimeException ignored) {
        }
        shown = null;
        Log.i(TAG, "LAUNCHER_LOWER hidden");
    }

    /** A black window over the whole lower screen; it takes touches and does nothing with them. */
    private static final class Black extends Presentation {
        Black(Context outer, Display display) {
            super(outer, display);
        }

        @Override
        protected void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
            getWindow().setBackgroundDrawable(new ColorDrawable(Color.BLACK));
            View view = new View(getContext());
            view.setOnTouchListener(new View.OnTouchListener() {
                @Override public boolean onTouch(View v, MotionEvent event) {
                    return true;
                }
            });
            setContentView(view);
        }
    }
}
