package dev.recompbootstrap.smoke;

import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.MotionEvent;

/**
 * Update 1 QoL (menus plan A2): the Android side of SideMenuPad, as the launcher's own (BootstrapActivity): NavInput's repeat timer on
 * the main looper, and a joystick MotionEvent with its batched samples as a NavInput.Motion. The same file in the Thor and Fold trees.
 */
final class PadGlue {
    private PadGlue(){}
    static NavInput.Timer timer(){
        final Handler handler=new Handler(Looper.getMainLooper());
        return new NavInput.Timer(){
            private Runnable task;
            @Override public long now(){return SystemClock.uptimeMillis();}
            @Override public void schedule(Runnable t,long delayMs){cancel();task=t;handler.postDelayed(t,delayMs);}
            @Override public void cancel(){if(task!=null){handler.removeCallbacks(task);task=null;}}
        };
    }
    static NavInput.Motion motion(final MotionEvent e){
        return new NavInput.Motion(){
            @Override public int history(){return e.getHistorySize();}
            @Override public float value(int axis,int position){return position<e.getHistorySize()?e.getHistoricalAxisValue(axis,position):e.getAxisValue(axis);}
            @Override public long time(int position){return position<e.getHistorySize()?e.getHistoricalEventTime(position):e.getEventTime();}
        };
    }
}
