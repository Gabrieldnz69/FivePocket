package dev.recompbootstrap.smoke;

/**
 * Thor edition (thor-radio v828; owner 2026-10-25 on v826: "two finger swipe is too janky, just make the top screen left swipe for the
 * menu, and bottom screen left swipe for the trainer"): the lower screen's opening gesture, the side menu's own rule (SideMenu EDGE /
 * OPEN_DX / OPEN_MS): one finger lands in the left edge strip (EDGE of the width) and moves right OPEN_DX of the width, more across
 * than down (dx > 2 |dy|), within OPEN_MS. Unlike the main screen's strip it takes nothing from the other gestures: every event still
 * reaches the wheel / map / phone gestures until it fires (a tap in the strip is the usual map tap); the caller cancels them then.
 * Pure Java (tests/EdgeSwipeTest.java).
 */
final class EdgeSwipe {
    private int finger=-1;private float x0,y0;private long t0;private boolean done;private float w=1;
    void view(float width){w=Math.max(1f,width);}
    void down(int id,long t,float x,float y){
        if(finger>=0)return; // (one finger: the first in the strip)
        if(x<=SideMenu.EDGE*w){finger=id;x0=x;y0=y;t0=t;done=false;}
    }
    /** A move of finger id: true once, when the swipe is complete. */
    boolean move(int id,long t,float x,float y){
        if(id!=finger || done)return false;
        if(t-t0>SideMenu.OPEN_MS){finger=-1;return false;}
        final float dx=x-x0,dy=Math.abs(y-y0);
        if(dx>=SideMenu.OPEN_DX*w && dx>2f*dy){done=true;return true;}
        return false;
    }
    void up(int id){if(id==finger)finger=-1;}
    void cancel(){finger=-1;}
}
