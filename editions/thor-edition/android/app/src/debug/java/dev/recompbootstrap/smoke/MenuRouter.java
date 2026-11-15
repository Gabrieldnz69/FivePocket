package dev.recompbootstrap.smoke;

import android.view.MotionEvent;

/**
 * Thor edition (thor-radio v826): a SideMenu's share of a view's touch events (the one-screen edition's QolTouch.routeMenu, without its
 * pad): the edge strip's finger while the menu is closed, every finger while it is open. route() is true when the event is the menu's
 * (the caller then gives it to nothing else); after(): what runs after each gesture (the events applied, the view redrawn).
 */
final class MenuRouter {
    private final SideMenu menu;private final Runnable after;private int finger=-1;
    MenuRouter(SideMenu menu,Runnable after){this.menu=menu;this.after=after;}
    boolean route(MotionEvent e){
        final long t=e.getEventTime();final int a=e.getActionMasked();
        final boolean wasOpen=menu.isOpen();
        boolean mine;
        if(a==MotionEvent.ACTION_DOWN || a==MotionEvent.ACTION_POINTER_DOWN){
            final int k=e.getActionIndex(),id=e.getPointerId(k);
            if(finger<0 && (a==MotionEvent.ACTION_DOWN || wasOpen) && menu.down(id,t,e.getX(k),e.getY(k))){finger=id;mine=true;}
            else mine=wasOpen;
        } else if(finger>=0){
            final int k=e.findPointerIndex(finger);
            if(a==MotionEvent.ACTION_MOVE && k>=0){menu.move(finger,t,e.getX(k),e.getY(k));mine=wasOpen;}
            else if((a==MotionEvent.ACTION_UP || a==MotionEvent.ACTION_POINTER_UP) && e.getPointerId(e.getActionIndex())==finger){
                menu.up(finger,t,e.getX(e.getActionIndex()),e.getY(e.getActionIndex()));finger=-1;mine=true;}
            else if(a==MotionEvent.ACTION_CANCEL){menu.cancel();finger=-1;mine=wasOpen;}
            else mine=wasOpen;
        } else mine=wasOpen;
        if(a==MotionEvent.ACTION_UP || a==MotionEvent.ACTION_CANCEL)finger=-1;
        if(after!=null)after.run();
        return mine || menu.isOpen() || wasOpen;
    }
}
