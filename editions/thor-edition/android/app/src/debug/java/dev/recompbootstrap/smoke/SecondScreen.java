package dev.recompbootstrap.smoke;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.graphics.Color;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;

/**
 * hud2 (2026-10-22): the game's HUD on a second display (AYN Thor's lower screen). When SecondScreenOption.wanted() and a
 * second display exists, an android.app.Presentation with one SurfaceView is shown there and its surface goes to native
 * code (PrivateTraceActivity.attachHudSurface: a library exporting recomp_set_hud_window_v1 copies the minimap and the
 * wanted stars into it). The window never takes focus (FLAG_NOT_FOCUSABLE): buttons and sticks stay with the game window.
 * Nothing is opened when the option is off or there is no second display; every failure here only logs and leaves the
 * game as it is (tag RecompSecondScreen).
 */
final class SecondScreen {
    static final String TAG="RecompSecondScreen";
    private final Presentation presentation;
    private SecondScreen(Presentation presentation){this.presentation=presentation;}

    /** Null when the option is off, there is no second display, or it could not be shown. */
    static SecondScreen open(Activity activity){
        final boolean dark=SecondScreenOption.off(activity.getFilesDir()); // v820: Lower Screen Off = a dark window (no HUD) on the lower screen
        if(!dark && !SecondScreenOption.wanted(activity.getFilesDir())){Log.i(TAG,"SECOND_SCREEN off");return null;}
        DisplayManager manager=(DisplayManager)activity.getSystemService(Context.DISPLAY_SERVICE);
        if(manager==null){Log.i(TAG,"SECOND_SCREEN none why=no-display-manager");return null;}
        Display[] all=manager.getDisplays();
        Display[] presentationDisplays=manager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        int[] ids=new int[all.length];boolean[] presentation=new boolean[all.length],on=new boolean[all.length];
        StringBuilder seen=new StringBuilder();
        for(int k=0;k<all.length;++k){
            ids[k]=all[k].getDisplayId();on[k]=all[k].getState()!=Display.STATE_OFF;
            for(Display p:presentationDisplays)if(p.getDisplayId()==ids[k])presentation[k]=true;
            seen.append(' ').append(ids[k]).append(presentation[k]?"p":"").append(on[k]?"":"(off)").append(':').append(all[k].getName());
        }
        int id=SecondScreenOption.choose(ids,presentation,on,Display.DEFAULT_DISPLAY);
        Log.i(TAG,"SECOND_SCREEN displays="+all.length+seen+" chosen="+id);
        if(id<0)return null;
        Display display=manager.getDisplay(id);
        if(display==null)return null;
        try{
            if(dark){Dark d=new Dark(activity,display);d.show();Log.i(TAG,"SECOND_SCREEN dark (Lower Screen: Off)");return new SecondScreen(d);}
            Hud hud=new Hud(activity,display);
            hud.show();
            return new SecondScreen(hud);
        }catch(RuntimeException error){ // (WindowManager.InvalidDisplayException, BadTokenException: no HUD, the game goes on)
            Log.w(TAG,"SECOND_SCREEN show-failed "+error.getClass().getSimpleName()+": "+error.getMessage());
            return null;
        }
    }

    void close(){
        WheelTouch.close(); // thor-wheel
        try{presentation.dismiss();}catch(RuntimeException ignored){}
    }

    /** v820: Lower Screen Off: a black window at the window's lowest brightness (its own LayoutParams only), no HUD surface. */
    private static final class Dark extends Presentation {
        Dark(Context outer,Display display){super(outer,display);}
        @Override protected void onCreate(Bundle savedInstanceState){
            super.onCreate(savedInstanceState);
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
            getWindow().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(Color.BLACK));
            WindowManager.LayoutParams lp=getWindow().getAttributes();lp.screenBrightness=BrightnessMatch.OFF;getWindow().setAttributes(lp);
            setContentView(new android.view.View(getContext()));
        }
    }

    /**
     * v820: the lower screen's window brightness = the main panel's (BrightnessMatch), followed while the window is up (a read-only
     * observer of the brightness setting). Only this window's LayoutParams.screenBrightness is set; it goes with the window.
     */
    private static final class Brightness extends android.database.ContentObserver {
        private final android.view.Window window;private final android.content.ContentResolver resolver;
        Brightness(android.view.Window window,android.content.ContentResolver resolver){super(new android.os.Handler(android.os.Looper.getMainLooper()));this.window=window;this.resolver=resolver;}
        void start(){
            apply();
            try{
                resolver.registerContentObserver(android.provider.Settings.System.getUriFor(android.provider.Settings.System.SCREEN_BRIGHTNESS),false,this);
                resolver.registerContentObserver(android.provider.Settings.System.getUriFor(android.provider.Settings.System.SCREEN_BRIGHTNESS_MODE),false,this);
            }catch(RuntimeException error){Log.w(TAG,"SECOND_SCREEN brightness observer refused: "+error.getMessage());}
        }
        void stop(){try{resolver.unregisterContentObserver(this);}catch(RuntimeException ignored){}}
        @Override public void onChange(boolean selfChange){apply();}
        void apply(){
            Float f=null;int i=-1;boolean auto=false;
            try{f=android.provider.Settings.System.getFloat(resolver,"screen_brightness_float");}catch(Exception unreadable){f=null;}
            try{i=android.provider.Settings.System.getInt(resolver,android.provider.Settings.System.SCREEN_BRIGHTNESS);}catch(Exception unreadable){i=-1;}
            try{auto=android.provider.Settings.System.getInt(resolver,android.provider.Settings.System.SCREEN_BRIGHTNESS_MODE)==android.provider.Settings.System.SCREEN_BRIGHTNESS_MODE_AUTOMATIC;}catch(Exception unreadable){auto=false;}
            final float b=BrightnessMatch.windowBrightness(f,i,auto);
            WindowManager.LayoutParams lp=window.getAttributes();
            if(lp.screenBrightness!=b){lp.screenBrightness=b;window.setAttributes(lp);}
            Log.i(TAG,"SECOND_SCREEN brightness="+b+" main_float="+f+" main_int="+i+" auto="+auto);
        }
    }

    private static final class Hud extends Presentation {
        private Brightness brightness; // (v820)
        Hud(Context outer,Display display){super(outer,display);}
        @Override protected void onStart(){super.onStart();if(brightness==null)brightness=new Brightness(getWindow(),getContext().getContentResolver());brightness.start();}
        @Override protected void onStop(){if(brightness!=null)brightness.stop();super.onStop();}
        @Override protected void onCreate(Bundle savedInstanceState){
            super.onCreate(savedInstanceState);
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            // (v820: the main panel's brightness - Brightness, registered in onStart, removed in onStop)
            SurfaceView view=new SurfaceView(getContext());
            // (2026-10-22 first device run: the HUD presented 30/s but SurfaceFlinger showed its layer fully covered by this window's
            // opaque black background; a SurfaceView sits BELOW its window by default. v601..v824: on top (setZOrderOnTop). v826: below, under
            // a TRANSPARENT window background, so the lower screen's trainer (a view) can draw over the HUD picture.)
            final ThorMenus menus=ThorMenus.current();
            if(menus!=null)getWindow().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
            else {getWindow().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(Color.BLACK));view.setZOrderOnTop(true);}
            // thor-wheel (Thor edition): the weapon wheel by touch (WheelTouch / WheelGesture; null = second-screen.txt wheel=main)
            final WheelTouch wheel=WheelTouch.open(getContext().getFilesDir());
            final SideMenuView trainerView=menus!=null?new SideMenuView(getContext(),menus.lower):null;
            final TrainerButtonView trainerButton=menus!=null?new TrainerButtonView(getContext(),menus):null; // (v832)
            final QuickStripView quickStrip=menus!=null?new QuickStripView(getContext(),menus):null; // (Update 1 QoL: the quick strip)
            if(menus!=null)view.setOnTouchListener(new LowerTouch(menus,wheel,trainerView,trainerButton,quickStrip));
            if(menus!=null && android.os.Build.VERSION.SDK_INT>=29) // (v832: the left edge is ours - the system Back gesture does not start there)
                view.addOnLayoutChangeListener((vv,l,t,r,b,ol,ot,or,ob)->vv.setSystemGestureExclusionRects(java.util.Collections.singletonList(new android.graphics.Rect(0,0,Math.round(0.05f*(r-l)),b-t))));
            else if(wheel!=null)view.setOnTouchListener(wheel);
            view.getHolder().addCallback(new SurfaceHolder.Callback(){
                private boolean attached;
                @Override public void surfaceCreated(SurfaceHolder holder){
                    attached=PrivateTraceActivity.attachHudSurface(holder.getSurface());
                    Log.i(TAG,"SECOND_SCREEN surface="+(attached?"attached":"refused")+" size="+holder.getSurfaceFrame().width()+"x"+holder.getSurfaceFrame().height());
                }
                @Override public void surfaceChanged(SurfaceHolder holder,int format,int width,int height){
                    if(attached){PrivateTraceActivity.changedHudSurface();Log.i(TAG,"SECOND_SCREEN surface=changed size="+width+"x"+height);}
                }
                @Override public void surfaceDestroyed(SurfaceHolder holder){
                    if(attached){PrivateTraceActivity.detachHudSurface();attached=false;Log.i(TAG,"SECOND_SCREEN surface=detached");}
                }
            });
            if(trainerView==null){setContentView(view);return;}
            final android.widget.FrameLayout root=new android.widget.FrameLayout(getContext());
            root.addView(view,new android.widget.FrameLayout.LayoutParams(-1,-1));
            root.addView(trainerButton,new android.widget.FrameLayout.LayoutParams(-1,-1)); // (v832: the trainer button; takes no touches itself)
            root.addView(quickStrip,new android.widget.FrameLayout.LayoutParams(-1,-1)); // (Update 1 QoL: the quick strip; takes no touches itself)
            root.addView(trainerView,new android.widget.FrameLayout.LayoutParams(-1,-1)); // (draws only while open; takes no touches itself)
            setContentView(root);
        }
    }

    /**
     * thor-radio v826 / v828: the lower screen's touches. The trainer (open) takes every finger; otherwise a left-edge swipe (EdgeSwipe,
     * v828: the owner found v826's two-finger swipe janky) opens it when the side menu's Trainer switch is on and no wheel is held; every
     * touch also goes to the wheel / map / phone gestures (WheelTouch) until the swipe fires, then they are cancelled.
     */
    private static final class LowerTouch implements android.view.View.OnTouchListener {
        private final ThorMenus menus;private final WheelTouch wheel;private final SideMenuView view;private final MenuRouter router;
        private final EdgeSwipe swipe=new EdgeSwipe();private boolean held;private final TrainerButtonView button;private final QuickStripView strip;
        LowerTouch(ThorMenus menus,WheelTouch wheel,SideMenuView view,TrainerButtonView button,QuickStripView strip){
            this.menus=menus;this.wheel=wheel;this.view=view;this.button=button;this.strip=strip;
            router=new MenuRouter(menus.lower,()->{menus.drain(menus.lower);view.invalidate();});
        }
        @Override public boolean onTouch(android.view.View v,android.view.MotionEvent e){
            swipe.view(v.getWidth());
            menus.lowerTouched(android.os.SystemClock.uptimeMillis()); // (v832: a system Back from here never opens the side menu)
            if(menus.lower.isOpen()){router.route(e);return true;}
            if(e.getActionMasked()==android.view.MotionEvent.ACTION_DOWN && strip!=null) { // (Update 1 QoL: a quick-strip slot, never the map's)
                final QuickStrip.Slot s=strip.slotAt(e.getX(),e.getY());
                if(s!=null){Haptics.tick(v);strip.tap(s);held=true;return true;}
            }
            if(e.getActionMasked()==android.view.MotionEvent.ACTION_DOWN && button!=null && button.hit(e.getX(),e.getY(),v.getWidth(),v.getHeight())) { // v832
                if(menus.lowerAllowed()){Haptics.tick(v); // (Update 1 haptics: the button's tick)
                    menus.lower.view(v.getWidth(),v.getHeight());menus.lower.openMenu();menus.drain(menus.lower);view.invalidate();held=true;
                    if(menus.sound!=null)menus.sound.play(); // (v832: the owner's sound, once per open)
                    Log.i(ThorMenus.TAG,"LOWER_TRAINER button");return true;}
            }
            final int a=e.getActionMasked(),k=e.getActionIndex(),id=e.getPointerId(k);
            if(a==android.view.MotionEvent.ACTION_DOWN || a==android.view.MotionEvent.ACTION_POINTER_DOWN)swipe.down(id,e.getEventTime(),e.getX(k),e.getY(k));
            else if(a==android.view.MotionEvent.ACTION_MOVE) {
                for(int i=0;i<e.getPointerCount();++i)if(swipe.move(e.getPointerId(i),e.getEventTime(),e.getX(i),e.getY(i))) {
                    final boolean allowed=menus.lowerAllowed() && (wheel==null || !wheel.wheelHeld());
                    Log.i(ThorMenus.TAG,"LOWER_TRAINER edge-swipe allowed="+(allowed?1:0));
                    if(allowed) {
                        if(wheel!=null){final android.view.MotionEvent c=android.view.MotionEvent.obtain(e);c.setAction(android.view.MotionEvent.ACTION_CANCEL);wheel.onTouch(v,c);c.recycle();}
                        menus.lower.view(v.getWidth(),v.getHeight());menus.lower.openMenu();menus.drain(menus.lower);view.invalidate();held=true;return true;
                    }
                }
            } else if(a==android.view.MotionEvent.ACTION_UP || a==android.view.MotionEvent.ACTION_CANCEL){swipe.cancel();if(held){held=false;return true;}}
            else if(a==android.view.MotionEvent.ACTION_POINTER_UP)swipe.up(id);
            if(held)return true; // (the rest of the gesture that opened it)
            return wheel!=null && wheel.onTouch(v,e);
        }
    }
}
