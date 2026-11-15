package dev.recompbootstrap.smoke;

/**
 * BUG 02 (2026-10-25, the Fold: a touch on Samsung's taskbar opened another app and onStop ended the game at once, unsaved): a
 * stopped game window gets GRACE_MS to come back before its process ends. onStop posts the end (stopped), onStart takes it back
 * (started); a window that is finishing (B on a refused or stopped start, the system closing it) ends at once, as before.
 * While it is away the frame surface is detached (surfaceDestroyed): the library drops frames without a window and its frame
 * limiter keeps sleeping on the real clock. Coming back attaches the new surface; the game itself is started by the first
 * start only (firstStart), never by a later attach.
 * No Android types: the activity hands in its main-thread Handler (Timer) and the end (the process kill).
 */
final class BackgroundGrace {
    static final long GRACE_MS=25_000;
    interface Timer {void post(Runnable r,long delayMs);void cancel(Runnable r);}
    private final Timer timer;private final Runnable end;private final Runnable due;
    private boolean pending,started;
    BackgroundGrace(Timer timer,Runnable end){this.timer=timer;this.end=end;due=()->{pending=false;end.run();};}
    /** onStop: the end GRACE_MS later (a second stop keeps the first deadline), or at once when the window is finishing. */
    void stopped(boolean finishing){
        if(finishing){cancel();end.run();return;}
        if(!pending){pending=true;timer.post(due,GRACE_MS);}
    }
    /** onStart: takes back a pending end; true when there was one (the player came back in time). */
    boolean started(){if(!pending)return false;cancel();return true;}
    boolean pending(){return pending;}
    private void cancel(){if(pending){pending=false;timer.cancel(due);}}
    /** The game's start: true the first time only (an attach after coming back must not start a second runner). */
    boolean firstStart(){if(started)return false;started=true;return true;}
}
