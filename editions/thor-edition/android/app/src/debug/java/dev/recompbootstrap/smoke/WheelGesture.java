package dev.recompbootstrap.smoke;

/**
 * thor-wheel (Thor edition only, 2026-10-22): a touch on the Thor's lower screen turned into the controller input the game's
 * own weapon wheel reads (LB held = the wheel open, the right stick's direction = the highlighted slot). Plain Java (no
 * Android types), so the host test runs it (local-analysis/agents/thor-wheel/android/run_test.sh).
 *
 * The lower screen shows the game's wheel (the library replays the wheel's own draws there), centred on the screen. The
 * right stick is pushed fully along the direction from the screen's centre to the finger, the way the PC version's mouse
 * points into the wheel: the game itself then picks the slot that direction names (its own sectors; no slot table here).
 * Inside the dead disc around the centre (the wheel's hub) the stick rests and the game keeps its highlight.
 *
 * Two flows:
 *  STEER  the player holds LB (the wheel is open on the main screen and shown below): the finger moves the highlight; lifting
 *         it leaves the highlight where it is; releasing LB equips, exactly as with the stick (the game's own rule).
 *  OWN    no LB held: a touch held for HOLD_MS opens the wheel (LB is held for the player); after OPEN_MS (the wheel's opening)
 *         the finger steers; lifting it selects: the stick rests, CLOSE_GAP_MS later LB is released and the game equips the
 *         highlighted weapon. A shorter touch (a tap, a resting thumb) does nothing. A finger lifted before OPEN_MS still
 *         selects the slot under its lift point (one push at OPEN_MS).
 * In both, a stick push lasts at least MIN_STICK_MS (four game frames at 30 fps) so that the game reads it.
 * thor-radio (2026-10-23): the same gesture drives the game's RADIO wheel (in a vehicle, held open by D-pad LEFT; the right stick picks
 * the station by its direction, as in the weapon wheel). "LB" below = the wheel's button: the caller passes whether any button of
 * mask() is held (LB for the weapon wheel, LEFT for the radio wheel, each unless its setting says Main screen); the gesture itself
 * never presses LEFT (OWN stays off).
 * The physical pad is merged, never overridden: buttons are OR'd (merge()), the right stick is replaced only while stickOn().
 * Time: any monotonic milliseconds (the app: SystemClock.uptimeMillis). due(): when tick() must run next.
 */
final class WheelGesture {
    static final int LB=0x100,LEFT=0x0004; // (thor-radio: LEFT = D-pad left, the radio wheel's button)
    static final int DOWN=0x0002; // (thor-char: D-pad down, the character switch wheel's button)
    // thor-char: the character wheel sits in the lower screen's RIGHT half, its centre at (CHAR_CX * w, 0.5 h) (wheel_screen_layout.h
    // charLayout): while only D-pad DOWN holds a wheel open, the touch direction starts there instead of the screen's centre.
    static final float CHAR_CX=0.75f;
    static final long HOLD_MS=200,OPEN_MS=250,MIN_STICK_MS=133,CLOSE_GAP_MS=66,NONE=Long.MAX_VALUE;
    static final float DEAD=0.10f; // the dead disc's radius, as a share of the screen's smaller side
    // lb-only (owner 2026-10-23: "the touch controls are there even when the weapon wheel isnt ... can it turn on only when LB is held?"):
    // OWN off. A touch without LB never opens the wheel; it only steers once LB is held (pressed before or during the touch).
    static final boolean OWN_ENABLED=false;
    // map-tap (owner 2026-10-24: "when you touch the map on the second screen (when not on weapon wheel or radio), it opens the pause
    // menu map screen (pause + a)"): a quick tap without a wheel's button held = START, then A once the menu has opened (the menu opens
    // on its MAP tab; A enters the full map). The next tap closes it again (START). A physical START or B forgets the "open" guess.
    static final boolean MAP_TAP=true;
    static final int START=0x0010,A=0x1000,B=0x2000;
    static final long TAP_MS=300,PULSE_MS=120,MENU_OPEN_MS=250; // (2026-10-24 device: 70 ms presses were missed; A 60 ms after START = MAP tab only, 200 ms = the full map)
    // thor-maptap (2026-10-24, library v658+): the open tap only REQUESTS the map: the reserved XINPUT bit 0x0800 for REQUEST_MS (the
    // JNI passes it, the library removes it before the game reads the pad). The library then presses START and A itself on the pad
    // reads: A is released 2 frames after the menu's MAP tab is first drawn (the MAP tab acts on A's release, ~0.3 s after START's
    // release: device trials 2026-10-24), again if the MAP tab stays, and the MAP-tab frames are replaced by the game's own pause blur.
    // A library without it (no "recomp-map-tap-v1" in private-trace-live.so; WheelTouch scans) gets the app's own presses:
    // START for FALLBACK_START_MS, then A held FALLBACK_A_MS (released ~0.45 s after START's release: 0.3 s was the measured minimum).
    // With the library's macro EVERY quick tap is a request and the library decides (the MAP tab on screen = A, the full map it opened =
    // START = close, otherwise open): the app keeps no open guess of its own (on v658 the guess stayed "open" after a request the game
    // ignored during a phone call). After a request, taps are ignored for REQUEST_LOCK_MS (the library's macro runs ~0.4-0.6 s).
    static final int REQUEST=0x0800;
    static final long REQUEST_MS=150,REQUEST_LOCK_MS=700,FALLBACK_START_MS=100,FALLBACK_A_MS=450;
    private boolean nativeMap;private long lockUntil=Long.MIN_VALUE;
    /** thor-maptap: whether the library runs the map macro itself (WheelTouch's scan of the library). */
    void nativeMap(boolean on){nativeMap=on;}
    // v815 (thor-radio, the phone on the lower screen; library v815+: "recomp-phone-touch-v1", WheelTouch scans): every quick tap is
    // REQUEST | TOUCH with PHONE_TAP in the right stick (y = PHONE_A, or PHONE_BACK on the phone's soft-key bar's right part), every quick
    // swipe REQUEST | TOUCH with PHONE_SWIPE (y = the direction). The library: the phone up = A / B / the D-pad; else a tap = the usual
    // map-tap request and a swipe nothing. (map_tap.h PhoneTapCode / PhoneSwipeCode; wheel_screen_layout.h phoneLayout.)
    static final int PHONE_TAP=-32766,PHONE_SWIPE=-32765,PHONE_A=0,PHONE_BACK=1,SWIPE_UP=1,SWIPE_RIGHT=2,SWIPE_DOWN=3,SWIPE_LEFT=4;
    static final long SWIPE_MS=600,PHONE_LOCK_MS=250;static final float SWIPE_MIN=0.06f;
    static final float PHONE_ASPECT=237f/343f,PHONE_MARGIN_1080=56f,PHONE_BAR_SHARE=0.1f,PHONE_BACK_SHARE=0.4f;
    private boolean phoneTouch;
    void phoneTouch(boolean on){phoneTouch=on;}
    // v828 (owner 2026-10-25: "make it touch like the weapon wheel or the radio so it feels like an actual phone"; library v828+:
    // "recomp-phone-touch-v2"): a tap on the phone (not its Back zone) is REQUEST | TOUCH with PHONE_TAP_AT and the tap's position on the
    // lower screen packed in RY (phoneTapPacked: the x share in the high byte, the y share in the low one; phone_touch.h pack). The library
    // reads the phone screen and selects the tapped icon or row. Without the marker: v815's tap = A.
    static final int PHONE_TAP_AT=-32763;
    private boolean phoneTapAt;
    void phoneTapAt(boolean on){phoneTapAt=on;}
    static int phoneTapPacked(float x,float y,float w,float h){
        final int a=Math.round(Math.max(0f,Math.min(1f,x/w))*255f),b=Math.round(Math.max(0f,Math.min(1f,y/h))*255f);
        return (short)((a<<8)|b);
    }
    /** v815: the phone screen's rectangle on a w x h lower screen {x0, y0, x1, y1} (wheel_screen_layout.h phoneLayout). */
    static float[] phoneRect(float w,float h){
        final float m=Math.round(PHONE_MARGIN_1080*h/1080f);
        float ph=h-2f*m,pw=Math.round(ph*PHONE_ASPECT);
        if(pw>w-2f*m){pw=w-2f*m;ph=Math.round(pw/PHONE_ASPECT);}
        final float x0=(float)Math.floor((w-pw)*0.5f),y0=(float)Math.floor((h-ph)*0.5f);
        return new float[]{x0,y0,x0+pw,y0+ph};
    }
    /** v815: PHONE_BACK for a tap on the phone's soft-key bar's right part, else PHONE_A. */
    static int phoneTapKey(float x,float y,float w,float h){
        final float[] r=phoneRect(w,h);
        final boolean bar=y>=r[3]-(r[3]-r[1])*PHONE_BAR_SHARE && y<r[3]+(r[3]-r[1])*0.05f,right=x>=r[2]-(r[2]-r[0])*PHONE_BACK_SHARE;
        return bar && right?PHONE_BACK:PHONE_A;
    }
    /** v815: the swipe's direction (SWIPE_*) from (x0, y0) to (x1, y1), or 0 when shorter than SWIPE_MIN of the smaller side. */
    static int swipeDir(float x0,float y0,float x1,float y1,float w,float h){
        final float dx=x1-x0,dy=y1-y0;
        if(Math.hypot(dx,dy)<SWIPE_MIN*Math.min(w,h))return 0;
        return Math.abs(dx)>=Math.abs(dy)?(dx>0?SWIPE_RIGHT:SWIPE_LEFT):(dy>0?SWIPE_DOWN:SWIPE_UP);
    }
    // v820 (thor-radio, the lower screen's Quick Save button; library v820+: "recomp-quick-save-v1", WheelTouch scans): a quick tap inside
    // saveRect is REQUEST | TOUCH with SAVE_CODE in the right stick; the library runs the game's own quick save (phone UP, LEFT, A = the
    // Save Game list) while it shows the button, else treats it as the usual map tap (map_tap.h SaveCode; quick_save.h quickSaveHit).
    static final int SAVE_CODE=-32764;
    // (v832: a square floppy button, 88 px; the app's trainer button SAVE_GAP_1080 left of it, the same size - quick_save.h)
    static final float SAVE_W_1080=88f,SAVE_H_1080=88f,SAVE_RIGHT_1080=32f,SAVE_BOTTOM_1080=184f,SAVE_PAD_1080=8f,SAVE_MIN_SCALE=0.95f,SAVE_GAP_1080=16f;
    private boolean saveTouch;
    void saveTouch(boolean on){saveTouch=on;}
    private int saveRequests; // (Update 1 haptics: the quick taps on the Quick Save button sent as a SAVE_CODE request; WheelTouch ticks on each)
    /** Update 1 haptics: how many Quick Save requests this gesture has sent (a quick tap on the button; a drag or a long touch sends none). */
    int saveRequests(){return saveRequests;}
    /** v820: the Quick Save button's touch rectangle on a w x h lower screen {x0, y0, x1, y1} (quick_save.h quickSaveHit), or null. */
    static float[] saveRect(float w,float h){
        if(!(w>0) || !(h>0))return null;
        final float s=Math.min(1f,h/1080f);
        if(s<SAVE_MIN_SCALE)return null;
        final float x1=w-Math.round(SAVE_RIGHT_1080*s),x0=x1-Math.round(SAVE_W_1080*s),y1=h-Math.round(SAVE_BOTTOM_1080*s),y0=y1-Math.round(SAVE_H_1080*s);
        if(x0<0 || y0<0)return null;
        final float p=Math.round(SAVE_PAD_1080*s);
        return new float[]{x0-p,y0-p,x1+p,y1+p};
    }
    /** v832: the trainer button's box {x0, y0, x1, y1} (left of Quick Save's, the same size), or null. */
    static float[] trainerBox(float w,float h){
        final float[] s=saveRect(w,h);if(s==null)return null;
        final float k=Math.min(1f,h/1080f),p=Math.round(SAVE_PAD_1080*k),gap=Math.round(SAVE_GAP_1080*k),side=Math.round(SAVE_W_1080*k);
        final float x1=s[0]+p-gap,y0=s[1]+p;
        return new float[]{x1-side,y0,x1,y0+side};
    }
    static boolean onSave(float x,float y,float w,float h){final float[] r=saveRect(w,h);return r!=null && x>=r[0] && x<r[2] && y>=r[1] && y<r[3];}
    private void phoneMacro(long t,int code,int value){startMacro(t,new int[]{REQUEST|TOUCH,(int)REQUEST_MS});stick=true;rx=code;ry=value;}
    boolean nativeMap(){return nativeMap;}
    static final float TAP_SLOP=0.04f; // a tap moves less than this share of the screen's smaller side
    // touch map (v663, owner 2026-10-24: "drag to pan, pinch to zoom, tap for a waypoint"; library v663+): without a wheel's button,
    // a one-finger drag = the left stick from the finger's smoothed velocity (the map's content follows the finger: the stick points the
    // other way), a two-finger pinch = RT (spread: zoom in) / LT (squeeze: zoom out) from the spread's rate, both with the touchpad bit
    // TOUCH (0x0400, XINPUT's guide bit) so the library passes them to the game only while the pause full map is on screen (a resting stick
    // otherwise). A tap on the Resume button (resume_screen.inc's rectangle) = REQUEST (on a pause screen: START); a tap anywhere else =
    // REQUEST | TOUCH (on the full map: A = the waypoint at the cursor; in the game: open the map as before).
    static final boolean MAP_TOUCH=false; // (v667, owner: the map's touch controls on the TOP screen - MapTouch; the lower screen keeps only the Resume tap)
    static final int TOUCH=0x0400;
    // Gains from the Thor (2026-10-24, v661, diag-stick on the outdoor full map; video/pan1-2.mkv, shots/zoomin-main.png): the map pans at
    // a constant rate per real frame (no acceleration): LS 0.61 = 8 px, LS 1.0 = 16 px of the main screen per frame (~480 px/s at full);
    // RT 255 for 1.5 s zoomed 3390 ft -> 215 ft. So a drag maps to PAN_MIN..1 of the stick (PAN_MIN clears the game's dead zone) as the
    // finger's speed goes from PAN_STILL to PAN_FULL lower-screen px/s; a pinch to TRIGGER_MIN..255 from ZOOM_STILL to ZOOM_FULL ln/s.
    static final float PAN_FULL=320f,PAN_STILL=15f,PAN_MIN=0.30f,PAN_SMOOTH_MS=50f,ZOOM_FULL=2.0f,ZOOM_STILL=0.2f,ZOOM_SMOOTH_MS=60f;
    static final int TRIGGER_MIN=80;
    static final long PAN_TICK_MS=16;
    static final float RESUME_W=900,RESUME_H=92; // (resume_layout.h ResumeRowW / ResumeRowH, scaled down on a smaller screen as there)
    /** Whether (x, y) in a w x h view lies on the Resume button the library draws there. */
    static boolean onResume(float x,float y,float w,float h){
        if(!(w>0) || !(h>0))return false;
        final float s=Math.min(1f,Math.min(w/(RESUME_W+80f),h/(RESUME_H*3f)));
        final float bw=Math.round(RESUME_W*s),bh=Math.round(RESUME_H*s),x0=Math.round((w-bw)*0.5f),y0=Math.round((h-bh)*0.5f);
        return x>=x0 && x<x0+bw && y>=y0 && y<y0+bh;
    }
    private float vx,vy,zr,lastX,lastY,px2,py2;private long lastMoveT;private double lastLogD;private int lx,ly,lt,rt;
    int leftX(){return lx;}
    int leftY(){return ly;}
    int leftTrigger(){return lt;}
    int rightTrigger(){return rt;}
    enum State {IDLE,ARMING,OWN,STEER,CLOSING,MACRO,DRAG,PINCH}

    private State state=State.IDLE;
    private boolean finger,ignored,lifted;     // a finger is down / that finger is ignored until its lift / (OWN, STEER) it lifted
    private float fx,fy,fw,fh;                 // the finger and the view's size
    private long openAt,stickSince=-1,next=NONE;
    private int buttons,rx,ry;private boolean stick;
    private long downAt;private float dx0,dy0;private boolean moved,mapOpen; // (map-tap)
    private int[] macro;private int macroAt; // (map-tap: {buttons, ms} pairs; pulses with gaps)

    State state(){return state;}
    int buttons(){return buttons;}
    boolean stickOn(){return stick;}
    int rightX(){return rx;}
    int rightY(){return ry;}
    long due(){return next;}
    boolean mapOpen(){return mapOpen;}
    /** map-tap: the physical pad (every publish): START or B pressed by the player = the menu's state is theirs again. */
    void pad(int physicalButtons){if((physicalButtons&(START|B))!=0)mapOpen=false;}
    private void startMacro(long t,int[] steps){macro=steps;macroAt=0;state=State.MACRO;buttons=steps[0];next=t+steps[1];}

    /** The right stick for a finger at (x, y) in a view of w x h: {x, y} (y up, full deflection), or null inside the dead disc. */
    static int[] direction(float x,float y,float w,float h){return direction(x,y,w,h,0.5f);}
    /** thor-char: the same from (shareX * w, h / 2). */
    static int[] direction(float x,float y,float w,float h,float shareX){
        if(!(w>0) || !(h>0) || !Float.isFinite(x) || !Float.isFinite(y))return null;
        final double dx=x-w*shareX,dy=y-h*0.5,r=Math.hypot(dx,dy);
        if(r<DEAD*Math.min(w,h))return null;
        return new int[]{(int)Math.round(32767*dx/r),(int)Math.round(-32767*dy/r)};
    }
    /** thor-radio: the buttons that let a touch steer: LB unless the weapon wheel is on the main screen, LEFT unless the radio wheel is. */
    static int mask(boolean weaponMain,boolean radioMain){return (weaponMain?0:LB)|(radioMain?0:LEFT);}
    /** thor-char: plus DOWN unless the character wheel is on the main screen. */
    static int mask(boolean weaponMain,boolean radioMain,boolean characterMain){return mask(weaponMain,radioMain)|(characterMain?0:DOWN);}
    /** thor-char: the x of the touch origin as a share of the width: CHAR_CX while DOWN is the only wheel button held, else the centre. */
    static float originX(int buttons,int mask){final int b=buttons&mask;return b==DOWN?CHAR_CX:0.5f;}
    private float ox=0.5f;
    /** thor-char: where directions start (a share of the width); WheelTouch sets it from the pad before physical(). */
    void origin(float shareX){ox=shareX;}
    /** Whether the pad holds a wheel's button (the gesture's "physical LB"). */
    static boolean held(int buttons,int mask){return (buttons&mask)!=0;}
    /** The slot (0 = up, then clockwise; n slots) a direction names: for the log only (the game decides from the stick). */
    static int slot(int sx,int sy,int n){
        if(n<=0 || (sx==0 && sy==0))return -1;
        double a=Math.atan2(sx,sy); // (0 = up, clockwise positive)
        if(a<0)a+=2*Math.PI;
        return (int)Math.floor(a/(2*Math.PI/n)+0.5)%n;
    }
    /** The pad the game is given: {buttons, right x, right y}: the physical state, this gesture's buttons OR'd, its stick while it pushes. */
    static int[] merge(WheelGesture g,int buttons,int rightX,int rightY){
        final int[] p=merge(g,buttons,rightX,rightY,0,0,0,0);return new int[]{p[0],p[1],p[2]};
    }
    /** v663: {buttons, right x, right y, left x, left y, LT, RT}: as above, plus the drag's left stick and the pinch's triggers. */
    static int[] merge(WheelGesture g,int buttons,int rightX,int rightY,int leftX,int leftY,int leftTrigger,int rightTrigger){
        if(g==null)return new int[]{buttons,rightX,rightY,leftX,leftY,leftTrigger,rightTrigger};
        final boolean drag=g.state==State.DRAG,pinch=g.state==State.PINCH;
        return new int[]{buttons|g.buttons,g.stick?g.rx:rightX,g.stick?g.ry:rightY,drag?g.lx:pinch?0:leftX,drag?g.ly:pinch?0:leftY,
                         pinch?g.lt:leftTrigger,pinch?g.rt:rightTrigger};
    }
    private static int axis(float v){return Math.round(32767*Math.max(-1f,Math.min(1f,v)));}
    private void touchRest(){lx=ly=lt=rt=0;vx=vy=zr=0;}
    private void drag(long t,float x,float y){ // (DRAG: the finger's velocity, smoothed, -> the left stick)
        final float dt=Math.max(1f,t-lastMoveT),a=Math.min(1f,dt/PAN_SMOOTH_MS);
        vx+=a*((x-lastX)*1000f/dt-vx);vy+=a*((y-lastY)*1000f/dt-vy);
        lastX=x;lastY=y;lastMoveT=t;
        final float speed=(float)Math.hypot(vx,vy);
        if(speed<PAN_STILL){lx=ly=0;return;}
        final float k=(PAN_MIN+(1f-PAN_MIN)*Math.min(1f,speed/PAN_FULL))/speed; // (the stick's length from the speed, its direction the finger's)
        lx=axis(-vx*k);ly=axis(vy*k);
    }
    private void zoom(long t,float x1,float y1,float x2,float y2){ // (PINCH: the spread's rate, smoothed, -> RT / LT)
        final double d=Math.max(1.0,Math.hypot(x2-x1,y2-y1)),ld=Math.log(d);
        final float dt=Math.max(1f,t-lastMoveT),a=Math.min(1f,dt/ZOOM_SMOOTH_MS);
        zr+=a*((float)((ld-lastLogD)*1000.0/dt)-zr);lastLogD=ld;lastMoveT=t;
        final int v=Math.abs(zr)<ZOOM_STILL?0:Math.round(TRIGGER_MIN+(255-TRIGGER_MIN)*Math.min(1f,Math.abs(zr)/ZOOM_FULL));
        rt=zr>0?v:0;lt=zr<0?v:0;
    }

    private void aim(long t){
        final int[] d=direction(fx,fy,fw,fh,ox); // (thor-char: from the character wheel's centre while DOWN holds it)
        if(d==null){rest();return;} // (the stick rests: the game keeps its highlight)
        if(!stick)stickSince=t;
        stick=true;rx=d[0];ry=d[1];
    }
    private void rest(){stick=false;rx=ry=0;stickSince=-1;}
    /** When the current push may end: at least MIN_STICK_MS after it started (t when there is none). */
    private long pushEnds(long t){return stick?Math.max(t,stickSince+MIN_STICK_MS):t;}
    private void close(long t){rest();state=State.CLOSING;next=t+CLOSE_GAP_MS;}
    private void idle(){rest();state=State.IDLE;next=NONE;if(finger)ignored=true;}

    void down(long t,float x,float y,float w,float h,boolean physicalLB){
        if(finger)return; // (one finger at a time)
        finger=true;fx=x;fy=y;fw=w;fh=h;lifted=false;downAt=t;dx0=x;dy0=y;moved=false;
        if(state!=State.IDLE){ignored=true;return;} // (closing: this finger waits for its lift)
        ignored=false;
        if(physicalLB){state=State.STEER;next=NONE;aim(t);}
        else {state=State.ARMING;next=OWN_ENABLED?t+HOLD_MS:NONE;} // (lb-only: ARMING waits for the player's LB, never opens the wheel itself)
    }
    void move(long t,float x,float y,float w,float h){
        if(!finger || ignored)return;
        fx=x;fy=y;fw=w;fh=h;
        if(Math.hypot(x-dx0,y-dy0)>TAP_SLOP*Math.min(w,h))moved=true; // (map-tap: a drag is no tap)
        if(state==State.ARMING && moved && nativeMap && MAP_TOUCH){state=State.DRAG;buttons=TOUCH;touchRest();lastX=x;lastY=y;lastMoveT=t;next=t+PAN_TICK_MS;return;}
        if(state==State.DRAG){drag(t,x,y);next=t+PAN_TICK_MS;return;}
        if(state==State.STEER || (state==State.OWN && t>=openAt+OPEN_MS))aim(t);
    }
    /** v663: a second finger down at (x, y) while the first one has not steered a wheel: the pinch. */
    void pointerDown(long t,float x,float y){
        if(!finger || ignored || !nativeMap || !MAP_TOUCH || (state!=State.ARMING && state!=State.DRAG))return;
        state=State.PINCH;buttons=TOUCH;touchRest();moved=true;px2=x;py2=y;lastMoveT=t;
        lastLogD=Math.log(Math.max(1.0,Math.hypot(x-fx,y-fy)));next=t+PAN_TICK_MS;
    }
    /** v663: both fingers of the pinch moved (the tracked finger at x1, y1). */
    void move2(long t,float x1,float y1,float x2,float y2){
        if(state!=State.PINCH || ignored)return;
        fx=x1;fy=y1;px2=x2;py2=y2;zoom(t,x1,y1,x2,y2);next=t+PAN_TICK_MS;
    }
    /** v663: one finger of the pinch lifted: the pinch ends, the remaining finger is ignored until it lifts. */
    void pointerUp(long t){
        if(state!=State.PINCH)return;
        touchRest();buttons=0;state=State.IDLE;next=NONE;if(finger)ignored=true;
    }
    void up(long t){
        if(!finger)return;
        finger=false;
        if(ignored){ignored=false;return;}
        lifted=true;
        switch(state){
            case DRAG:case PINCH:touchRest();buttons=0;state=State.IDLE;next=NONE;break;
            case ARMING:
                if(phoneTouch && nativeMap && t>=lockUntil && t-downAt<=SWIPE_MS && moved) { // v815: a swipe (the phone's D-pad)
                    final int dir=swipeDir(dx0,dy0,fx,fy,fw,fh);
                    if(dir!=0){lockUntil=t+PHONE_LOCK_MS;phoneMacro(t,PHONE_SWIPE,dir);}else{state=State.IDLE;next=NONE;}
                } else if(MAP_TAP && !moved && t-downAt<=TAP_MS && t>=lockUntil) { // (map-tap: open the pause map, or close it; on the lift, so a touch that is then joined by LB / LEFT still steers a wheel)
                    if(saveTouch && nativeMap && onSave(dx0,dy0,fw,fh)){lockUntil=t+REQUEST_LOCK_MS;phoneMacro(t,SAVE_CODE,0);++saveRequests;} // (v820)
                    else if(phoneTouch && nativeMap){lockUntil=t+PHONE_LOCK_MS;final int key=phoneTapKey(dx0,dy0,fw,fh); // (v815; v828: tap on an item)
                        if(phoneTapAt && key==PHONE_A)phoneMacro(t,PHONE_TAP_AT,phoneTapPacked(dx0,dy0,fw,fh));else phoneMacro(t,PHONE_TAP,key);}
                    else if(nativeMap){lockUntil=t+REQUEST_LOCK_MS; // (thor-maptap: the library decides open / close and presses; v663: TOUCH = a tap off the Resume button)
                        startMacro(t,new int[]{MAP_TOUCH && !onResume(dx0,dy0,fw,fh)?REQUEST|TOUCH:REQUEST,(int)REQUEST_MS});}
                    else if(mapOpen){mapOpen=false;startMacro(t,new int[]{START,(int)PULSE_MS,0,(int)PULSE_MS});}
                    else {mapOpen=true;startMacro(t,new int[]{START,(int)FALLBACK_START_MS,A,(int)FALLBACK_A_MS,0,(int)PULSE_MS});}
                } else {state=State.IDLE;next=NONE;} // (a long touch or a drag without a wheel's button, or a tap right after a request: nothing)
                break;
            case STEER:{final long e=pushEnds(t);if(e<=t)idle();else next=e;break;}
            case OWN:
                if(t<openAt+OPEN_MS)break;          // (tick() at OPEN time pushes toward the lift point, then selects)
                {final long e=pushEnds(t);if(e<=t)close(t);else next=e;}
                break;
            default:break;
        }
    }
    /** The physical LB, whenever the pad changes. */
    void physical(long t,boolean physicalLB){
        if((state==State.DRAG || state==State.PINCH) && physicalLB){touchRest();buttons=0;state=State.ARMING;} // (v663: a wheel's button wins)
        if(state==State.ARMING && physicalLB){state=State.STEER;next=NONE;aim(t);}
        else if(state==State.STEER && !physicalLB)idle(); // (the wheel closed: the player equipped; a finger still down is ignored)
    }
    void tick(long t){
        if(t<next)return;
        switch(state){
            case ARMING:state=State.OWN;openAt=t;buttons=LB;rest();next=t+OPEN_MS;break;
            case OWN:
                if(!lifted){aim(t);next=NONE;break;}       // (the wheel is open: the finger steers from now on)
                if(!stick){aim(t);if(stick){next=stickSince+MIN_STICK_MS;break;}} // (lifted before OPEN: one push at the lift point)
                close(t);break;
            case STEER:idle();break;                       // (a lifted finger's push lasted MIN_STICK_MS)
            case CLOSING:buttons=0;state=State.IDLE;next=NONE;if(finger)ignored=true;break;
            case DRAG:drag(t,lastX,lastY);next=t+PAN_TICK_MS;break; // (v663: no move for a while = the velocity decays to rest)
            case PINCH:zoom(t,fx,fy,px2,py2);next=t+PAN_TICK_MS;break;
            case MACRO:
                macroAt+=2;
                if(macroAt>=macro.length){buttons=0;state=State.IDLE;next=NONE;macro=null;rest();if(finger)ignored=true;break;} // (v815: rest = the phone code's stick released)
                buttons=macro[macroAt];next=t+macro[macroAt+1];break;
            default:next=NONE;break;
        }
    }
    /** Focus lost / the screen went away: everything released at once. */
    void cancel(){rest();touchRest();buttons=0;state=State.IDLE;finger=ignored=lifted=false;next=NONE;macro=null;}
}
