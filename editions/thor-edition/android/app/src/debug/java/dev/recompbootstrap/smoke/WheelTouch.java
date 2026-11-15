package dev.recompbootstrap.smoke;

import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;

/**
 * thor-wheel (Thor edition only, 2026-10-22): touches on the lower screen's HUD view drive the game's weapon wheel through the
 * controller state the game already reads (WheelGesture says how). Main thread only: the HUD Presentation and the game
 * activity share the app's main looper, and so do PrivateGamepadInput's key / motion callbacks.
 *
 * Wiring (three calls): SecondScreen sets open(...) as the HUD SurfaceView's touch listener and calls close() when it goes;
 * PrivateGamepadInput passes its pad through merge() in publish() and gives attachPad(this::publish) once. Nothing else
 * changes: no new native entry, the game sees an ordinary pad state (LB OR'd, the right stick replaced while a push lasts).
 * files/settings/second-screen.txt with the word "wheel=main" (the setting "Weapon wheel: Main screen"): LB does not enable touch; the
 * library then leaves the wheel on the main screen. Default (no word): "Lower screen".
 * thor-radio (2026-10-23): the radio wheel the same way - D-pad LEFT held (in a vehicle the game's radio wheel) enables touch steering,
 * unless the word "radio=main" (the setting "Radio wheel: Main screen"). Both words: no touch handling at all.
 * Log tag RecompWheelTouch: one line per gesture state change, labelled "touch" (never "PHYSICAL" or "SYNTHETIC").
 */
final class WheelTouch implements View.OnTouchListener {
    static final String TAG="RecompWheelTouch";
    private static WheelTouch current;   // the HUD view's listener while the second screen is shown
    private static Runnable padChanged;  // PrivateGamepadInput.publish
    private final WheelGesture g=new WheelGesture();
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final Runnable tick=this::tick;
    private long scheduled=WheelGesture.NONE;
    private int pointer=-1,second=-1; // (v663: the pinch's second finger)
    private boolean lastLb;              // (thor-radio: a wheel's button of `mask` held)
    private final int mask;
    private WheelGesture.State lastState=WheelGesture.State.IDLE;
    private boolean lastStick,lastTouchMap;private int lastButtons;
    private int saveTicks; // (Update 1 haptics: WheelGesture.saveRequests() already ticked)

    private WheelTouch(int mask){this.mask=mask;}
    /** The listener for the HUD view, or null when files/settings/second-screen.txt says wheel=main and radio=main. */
    static WheelTouch open(File filesDir){
        final byte[] text=settingsText(filesDir);final int n=text==null?0:text.length;
        final int mask=WheelGesture.mask(text!=null && hasWord(text,n,"wheel=main"),text!=null && hasWord(text,n,"radio=main"),
                text!=null && hasWord(text,n,"character=main")); // (thor-char: + D-pad DOWN unless character=main)
        if(mask==0){Log.i(TAG,"WHEEL_TOUCH off (wheel=main radio=main character=main)");return null;}
        if(current!=null)current.release();
        current=new WheelTouch(mask);
        Log.i(TAG,"WHEEL_TOUCH on buttons="+Integer.toHexString(mask)+" hold_ms="+WheelGesture.HOLD_MS+" open_ms="+WheelGesture.OPEN_MS+" min_stick_ms="+WheelGesture.MIN_STICK_MS+" dead="+WheelGesture.DEAD);
        current.scanMapTap(filesDir);
        return current;
    }
    static void close(){if(current!=null){current.release();current=null;changed();}}
    /** Focus lost / the controller changed (PrivateGamepadInput.clear): the gesture lets go of everything. */
    static void cancelAll(){if(current!=null){current.g.cancel();current.pointer=-1;current.second=-1;current.after();}MAIN.cancel();}
    static void attachPad(Runnable publish){padChanged=publish;}
    /** The pad the game is given: {buttons, right x, right y} (the physical LB is also the gesture's input). */
    static int[] merge(int buttons,int rightX,int rightY){
        final int[] p=merge(buttons,rightX,rightY,0,0,0,0);return new int[]{p[0],p[1],p[2]};
    }
    /** v663: {buttons, right x, right y, left x, left y, LT, RT}: plus the touch map's left stick (drag) and triggers (pinch).
     *  v667: plus the MAIN screen's map gesture (MapTouch: its bits OR'd, its right stick while it is active). */
    static int[] merge(int buttons,int rightX,int rightY,int leftX,int leftY,int leftTrigger,int rightTrigger){
        final int[] p=mergeLower(buttons,rightX,rightY,leftX,leftY,leftTrigger,rightTrigger);
        final int[] m=MapTouch.merge(MAIN.g,p[0],p[1],p[2]);
        p[0]=m[0];p[1]=m[1];p[2]=m[2];return p;
    }
    private static int[] mergeLower(int buttons,int rightX,int rightY,int leftX,int leftY,int leftTrigger,int rightTrigger){
        final WheelTouch w=current;
        if(w==null)return new int[]{buttons,rightX,rightY,leftX,leftY,leftTrigger,rightTrigger};
        final boolean lb=WheelGesture.held(buttons,w.mask); // (thor-radio: LB or D-pad LEFT, per the settings; thor-char: or DOWN)
        w.g.origin(WheelGesture.originX(buttons,w.mask)); // (thor-char: the character wheel's centre while DOWN alone holds a wheel)
        w.g.pad(buttons); // (map-tap: the player's own START / B)
        if(lb!=w.lastLb){w.lastLb=lb;w.g.physical(SystemClock.uptimeMillis(),lb);w.schedule();w.log("pad");}
        return WheelGesture.merge(w.g,buttons,rightX,rightY,leftX,leftY,leftTrigger,rightTrigger);
    }

    /** thor-radio v828: a wheel's button (LB / D-pad LEFT / DOWN, per the settings) is held: the lower screen's edge swipe steers it. */
    boolean wheelHeld(){return lastLb;}
    @Override public boolean onTouch(View view,MotionEvent e){
        final long t=e.getEventTime();
        final float w=view.getWidth(),h=view.getHeight();
        switch(e.getActionMasked()){
            case MotionEvent.ACTION_DOWN:
                pointer=e.getPointerId(0);second=-1;
                g.down(t,e.getX(0),e.getY(0),w,h,lastLb);break;
            case MotionEvent.ACTION_POINTER_DOWN:{ // (v663: a second finger = the touch map's pinch; more fingers are ignored)
                final int k=e.getActionIndex();
                if(pointer>=0 && second<0 && e.getPointerId(k)!=pointer){second=e.getPointerId(k);g.pointerDown(t,e.getX(k),e.getY(k));}
                break;}
            case MotionEvent.ACTION_MOVE:{
                final int i=pointer<0?-1:e.findPointerIndex(pointer),j=second<0?-1:e.findPointerIndex(second);
                if(i>=0 && j>=0)g.move2(t,e.getX(i),e.getY(i),e.getX(j),e.getY(j));
                else if(i>=0)g.move(t,e.getX(i),e.getY(i),w,h);
                break;}
            case MotionEvent.ACTION_POINTER_UP:{
                final int id=e.getPointerId(e.getActionIndex());
                if(id==second || id==pointer)g.pointerUp(t); // (v663: either finger of a pinch lifted: the pinch ends)
                if(id==second)second=-1;
                if(id==pointer){pointer=-1;g.up(t);} // (the tracked finger lifted; the others are ignored)
                break;}
            case MotionEvent.ACTION_UP:case MotionEvent.ACTION_CANCEL:
                if(pointer>=0)g.up(t);
                pointer=-1;second=-1;break;
            default:break;
        }
        // Update 1 haptics: the Quick Save button's tick, when the tap was classified as a save request (not on the finger's down: a drag
        // or a long touch on the button saves nothing) and the library shows the button (else the library takes the tap as the map's)
        if(g.saveRequests()!=saveTicks){saveTicks=g.saveRequests();if(HapticsWords.saveButtonShown(PrivateTraceActivity.lowerButtons()))Haptics.tick(view);}
        after();
        return true;
    }
    private void tick(){scheduled=WheelGesture.NONE;g.tick(SystemClock.uptimeMillis());after();}
    private void after(){schedule();log("touch");if(outputChanged())changed();}
    private void schedule(){
        final long due=g.due();
        if(due==scheduled)return;
        handler.removeCallbacks(tick);scheduled=due;
        if(due!=WheelGesture.NONE)handler.postAtTime(tick,due);
    }
    private boolean outputChanged(){
        final boolean touchMap=g.state()==WheelGesture.State.DRAG || g.state()==WheelGesture.State.PINCH; // (v663: every drag / pinch step is published)
        final boolean c=g.buttons()!=lastButtons || g.stickOn()!=lastStick || g.stickOn() || touchMap || lastTouchMap;
        lastTouchMap=touchMap;
        lastButtons=g.buttons();lastStick=g.stickOn();
        return c;
    }
    private static void changed(){if(padChanged!=null)padChanged.run();}
    private void log(String why){
        if(g.state()==lastState)return;
        lastState=g.state();
        Log.i(TAG,"WHEEL_TOUCH state="+g.state()+" lb="+((g.buttons()&WheelGesture.LB)!=0?1:0)+" stick="+(g.stickOn()?g.rightX()+","+g.rightY():"rest")+
                " slot8="+(g.stickOn()?WheelGesture.slot(g.rightX(),g.rightY(),8):-1)+" by="+why+" (touch input)");
    }
    private void release(){handler.removeCallbacks(tick);scheduled=WheelGesture.NONE;g.cancel();pointer=-1;}
    /** thor-maptap: whether the game library runs the map macro itself (its marker string), looked up once per screen on a worker
     *  thread (the library is read to its end, ~1 s); until then, and when absent or unreadable, the app presses START and A. */
    private void scanMapTap(File filesDir){
        final File so=new File(filesDir,CapabilityScanner.LIBRARY);
        final Thread scan=new Thread(()->{
            java.util.Set<String> found=java.util.Collections.emptySet();
            try(FileInputStream in=new FileInputStream(so)){found=CapabilityScanner.scan(in,java.util.Arrays.asList(MAP_TAP_MARKER,PHONE_MARKER,SAVE_MARKER,PHONE_MARKER_V2));}
            catch(IOException|RuntimeException error){found=java.util.Collections.emptySet();}
            final boolean on=found.contains(MAP_TAP_MARKER),phone=on && found.contains(PHONE_MARKER),save=on && found.contains(SAVE_MARKER),
                    tapAt=phone && found.contains(PHONE_MARKER_V2);
            handler.post(()->{g.nativeMap(on);g.phoneTouch(phone);g.saveTouch(save);g.phoneTapAt(tapAt);Log.i(TAG,"WHEEL_TOUCH map_tap="+(on?"library":"app")+" ("+MAP_TAP_MARKER+(on?" found)":" not found)")+
                " phone_touch="+(phone?1:0)+" quick_save="+(save?1:0)+" phone_tap_at="+(tapAt?1:0));});
        },"maptap-scan");
        scan.setDaemon(true);scan.start();
    }
    static final String MAP_TAP_MARKER="recomp-map-tap-v1"; // (map_tap.h Marker)
    static final String PHONE_MARKER="recomp-phone-touch-v1"; // (v815: map_tap.h PhoneMarker)
    static final String SAVE_MARKER="recomp-quick-save-v1"; // (v820: quick_save.h SaveMarker)
    static final String PHONE_MARKER_V2="recomp-phone-touch-v2"; // (v828: map_tap.h PhoneTouchMarker - a tap selects the tapped item)

    // v667 (owner 2026-10-24: "the touch controls on the map need to be on the top screen"): the game view's touches on the MAIN screen
    // -> MapTouch (drag / pinch / tap for the pause full map; the library ignores them outside the map). Main thread, as the rest.
    static final MainTouch MAIN=new MainTouch();
    /** The game view's touch listener (PrivateTraceActivity sets it on the main SurfaceView). */
    static View.OnTouchListener mainListener(){return MAIN;}
    static final class MainTouch implements View.OnTouchListener {
        final MapTouch g=new MapTouch();
        private final Handler handler=new Handler(Looper.getMainLooper());
        private final Runnable tick=this::tick;
        private long scheduled=MapTouch.NONE;private int pointer=-1,second=-1;private MapTouch.State lastState=MapTouch.State.IDLE;
        @Override public boolean onTouch(View view,MotionEvent e){
            final long t=e.getEventTime();
            switch(e.getActionMasked()){
                case MotionEvent.ACTION_DOWN:pointer=e.getPointerId(0);second=-1;g.down(t,e.getX(0),e.getY(0),view.getWidth(),view.getHeight());break;
                case MotionEvent.ACTION_POINTER_DOWN:{final int k=e.getActionIndex();
                    if(pointer>=0 && second<0 && e.getPointerId(k)!=pointer){second=e.getPointerId(k);g.pointerDown(t,e.getX(k),e.getY(k));}break;}
                case MotionEvent.ACTION_MOVE:{final int i=pointer<0?-1:e.findPointerIndex(pointer),j=second<0?-1:e.findPointerIndex(second);
                    if(i>=0 && j>=0)g.move2(t,e.getX(i),e.getY(i),e.getX(j),e.getY(j));else if(i>=0)g.move(t,e.getX(i),e.getY(i));break;}
                case MotionEvent.ACTION_POINTER_UP:{final int id=e.getPointerId(e.getActionIndex());
                    if(id==second || id==pointer)g.pointerUp(t);if(id==second)second=-1;if(id==pointer){pointer=-1;g.up(t);}break;}
                case MotionEvent.ACTION_UP:case MotionEvent.ACTION_CANCEL:if(pointer>=0)g.up(t);pointer=-1;second=-1;break;
                default:break;
            }
            after();
            return true;
        }
        private void tick(){scheduled=MapTouch.NONE;g.tick(SystemClock.uptimeMillis());after();}
        private void after(){
            final long due=g.due();
            if(due!=scheduled){handler.removeCallbacks(tick);scheduled=due;if(due!=MapTouch.NONE)handler.postAtTime(tick,due);}
            if(g.state()!=lastState){lastState=g.state();Log.i(TAG,"MAP_TOUCH state="+g.state()+" rx="+g.rightX()+" ry="+g.rightY()+" (main-screen touch input)");}
            changed();
        }
        void cancel(){g.cancel();pointer=second=-1;handler.removeCallbacks(tick);scheduled=MapTouch.NONE;changed();}
    }

    /** files/settings/second-screen.txt's first bytes (the same 128-byte file SecondScreenOption reads); null when absent or unreadable. */
    static byte[] settingsText(File filesDir){
        final File f=new File(filesDir,SecondScreenOption.FILE);
        if(!f.isFile())return null;
        final byte[] text=new byte[SecondScreenOption.CAP];int n=0;
        try(FileInputStream in=new FileInputStream(f)){for(int k;n<text.length && (k=in.read(text,n,text.length-n))>0;)n+=k;}catch(IOException error){return null;}
        return java.util.Arrays.copyOf(text,n);
    }
    static boolean hasWord(byte[] text,int length,String word){
        int i=0;
        while(i<length){
            while(i<length && SecondScreenOption.blank(text[i]))++i;
            final int s=i;while(i<length && !SecondScreenOption.blank(text[i]))++i;
            if(i-s==word.length()){boolean same=true;for(int k=0;k<word.length() && same;++k)same=text[s+k]==word.charAt(k);if(same)return true;}
        }
        return false;
    }
}
