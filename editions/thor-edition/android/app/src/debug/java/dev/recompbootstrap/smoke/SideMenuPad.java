package dev.recompbootstrap.smoke;

/**
 * Update 1 QoL (menus plan A2, owner's goal 2026-11-07): a gamepad drives the in-game side menu through the launcher's tested input
 * (NavInput): the D-pad's keys, its HAT axes and the left stick, with hold-to-repeat (FIRST_REPEAT_MS, then REPEAT_MS, then
 * FAST_REPEAT_MS). The same file in the Thor and the Fold trees; the game window hands it the pad's key and joystick events while the
 * menu is open and redraws on changed. Pure Java: tests/SideMenuPadTest.java.
 *
 * What repeats: UP / DOWN (the highlight, the column); LEFT / RIGHT only on a value row with more than two values (a CHOICE: the
 * creator's 255 styles, the hour), never on a toggle (a held direction would flip it on and off). A keeps SideMenu's hold rule (aDown /
 * aHeld / aUp: a trainer item is pinned by holding A). B, L1 / R1 and START act once per press.
 */
final class SideMenuPad implements NavInput.Sink {
    static final int KEY_A=96;   // android.view.KeyEvent.KEYCODE_BUTTON_A
    private final SideMenu menu;private final NavInput nav;private final Runnable changed;private final NavInput.Timer timer;

    SideMenuPad(SideMenu menu,NavInput.Timer timer,Runnable changed){
        this.menu=menu;this.timer=timer;this.changed=changed;this.nav=new NavInput(this,timer);
    }
    /** A pad key while the menu is open: true when the menu took it. */
    boolean key(int keyCode,boolean down,int repeatCount,long time){
        if(!menu.isOpen())return false;
        if(keyCode==KEY_A){
            if(!down)menu.aUp(time);else if(repeatCount>0)menu.aHeld(time);else menu.aDown(time);
            changed.run();return true;
        }
        return nav.key(keyCode,down,repeatCount,time);
    }
    /** A joystick event (the HAT and the left stick, with its batched samples) while the menu is open. */
    boolean motion(NavInput.Motion event){
        if(!menu.isOpen())return false;
        nav.motion(event);
        return true;
    }
    /** The menu opened or closed, the window lost focus: every hold and the repeat are forgotten. */
    void reset(){nav.reset();}

    @Override public void onNav(int command,boolean repeat){
        if(!menu.isOpen()){nav.reset();return;}
        switch(command){
            case NavInput.UP:menu.up();break;
            case NavInput.DOWN:menu.down();break;
            case NavInput.LEFT:if(!repeat || menu.valueRepeats())menu.left();break;
            case NavInput.RIGHT:if(!repeat || menu.valueRepeats())menu.right();break;
            case NavInput.BACK:menu.back();break;
            case NavInput.PAGE_PREV:menu.prevTab();nav.endHold();break;
            case NavInput.PAGE_NEXT:menu.nextTab();nav.endHold();break;
            case NavInput.MENU:menu.close();break;
            case NavInput.ALT:menu.pin();break; // (Y: pin / unpin, the Pin prompt)
            default:return;   // (CONFIRM never comes here: A is SideMenu's hold rule)
        }
        if(!menu.isOpen())nav.reset();
        changed.run();
    }
}
