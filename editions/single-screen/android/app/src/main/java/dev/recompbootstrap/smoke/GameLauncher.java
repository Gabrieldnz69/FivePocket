package dev.recompbootstrap.smoke;

import android.app.Activity;

/**
 * Starts the game and the controller check. The class that implements it (DebugGameLauncher) lives in the debug
 * source set, because only that source set may name the game activity; the launcher finds it by name at run time.
 */
interface GameLauncher {
    /**
     * Off the UI thread. Prepares a launch request. Returns null on success, else the reason as LaunchRequest.prepare
     * gives it (one of its ERROR_ texts; LaunchAdvice has the sentence the player reads for each):
     * LaunchRequest.UNQUALIFIED when the game code is not the version on record and acceptUnqualified is false.
     */
    String prepare(java.io.File filesDir, boolean acceptUnqualified);
    /**
     * UI thread, after a successful prepare: starts the game activity with the prepared extras and the play mode
     * (PlayMode.STORY or PlayMode.SANDBOX; Update 1), which goes in as the fifth extra.
     */
    void start(Activity activity, String playMode);
    /** UI thread: starts the controller check screen. */
    void startControllerCheck(Activity activity);
}
