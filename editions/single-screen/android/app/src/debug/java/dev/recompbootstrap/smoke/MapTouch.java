package dev.recompbootstrap.smoke;

/**
 * thor-maptap (Thor edition only, 2026-10-24, library v667+): touches on the MAIN (top) screen drive the game's pause full map
 * (owner: "the touch controls on the map need to be on the top screen"). Plain Java (no Android types): the host test runs it
 * (local-analysis/agents/thor-wheel/android/run_test.sh, tests/MapTouchTest.java).
 *
 * The gesture travels in the RIGHT stick with the touchpad bit TOUCH (0x0400; map_tap.h): the library gives it to the game only while
 * its pause full map is on screen, else it only rests the right stick for those reads (the player's left stick and triggers are never
 * replaced), and a tap off the map does nothing at all.
 *  drag   one finger moved past TAP_SLOP: the finger's smoothed velocity -> the stick the map should get (the map follows the finger:
 *         the cursor goes the other way); PAN_FULL main-screen px/s = full deflection (the Thor 2026-10-24: the map pans ~16 px of the
 *         1920-wide panel per frame at full deflection, ~480 px/s), PAN_MIN clears the game's dead zone, below PAN_STILL it rests.
 *  pinch  a second finger: the spread's rate (ln/s) -> RX = PINCH, RY = + RT (spreading: zoom in) / - LT (closing: zoom out).
 *  tap    a touch shorter than TAP_MS that did not move: TOUCH | REQUEST for PULSE_MS with RX / RY = the tap's offset from the screen's
 *         centre in guest pixels (1280x720): the library pans the map's cursor onto that point and presses A (the waypoint there).
 * Time: any monotonic milliseconds (the app: MotionEvent.getEventTime / SystemClock.uptimeMillis). due(): when tick() must run next.
 * qol-touch SPEC 3.11 (the owner 2026-10-24: "a double tap on the open map should close it too (waypoint taps can get a short delay)"):
 * with delayTaps (the single-screen edition, while its pause map is shown) a tap's waypoint waits DOUBLE_TAP_MS after its lift; a second
 * short tap whose down comes in that window is a double tap: no waypoint, takeClose() turns true (QolGesture sends the close). A new
 * touch in the window that is not a tap (a drag, a pinch, a hold) drops the waiting waypoint. Drag and pinch act at once as before.
 */
final class MapTouch {
    static final int TOUCH=0x0400,REQUEST=0x0800;
    static final short PINCH=Short.MIN_VALUE; // (map_tap.h PinchCode)
    static final long TAP_MS=300,PULSE_MS=150,LOCK_MS=700,TICK_MS=16,NONE=Long.MAX_VALUE;
    static final float TAP_SLOP=0.03f,PAN_FULL=480f,PAN_STILL=15f,PAN_MIN=0.30f,PAN_SMOOTH_MS=50f,ZOOM_FULL=2.0f,ZOOM_STILL=0.2f,ZOOM_SMOOTH_MS=60f;
    static final int TRIGGER_MIN=80;
    enum State {IDLE,ARMED,DRAG,PINCH,TAP,IGNORED}
    private State state=State.IDLE;
    private float x0,y0,fx,fy,px2,py2,w,h,vx,vy,zr,lastX,lastY;private double lastLogD;
    private long downAt,lastT,next=NONE,lockUntil=Long.MIN_VALUE;
    private int rx,ry,buttons;
    static final long DOUBLE_TAP_MS=300;
    private boolean delayTaps,second,close;private int pendRx,pendRy;private long pendUntil=Long.MIN_VALUE; // (3.11: the waiting waypoint)
    /** SPEC 3.11: waypoint taps wait for a possible second tap (on: the pause map is shown). Off drops a waiting one. */
    void delayTaps(boolean on){if(!on && delayTaps){pendUntil=Long.MIN_VALUE;second=false;if(state==State.IDLE)next=NONE;}delayTaps=on;}
    /** A double tap closed the map (once). */
    boolean takeClose(){final boolean c=close;close=false;return c;}
    private boolean pending(){return pendUntil!=Long.MIN_VALUE;}

    State state(){return state;}
    long due(){return next;}
    int buttons(){return buttons;}
    int rightX(){return rx;}
    int rightY(){return ry;}
    /** Whether this gesture puts anything into the pad now. */
    boolean active(){return buttons!=0;}
    /** The pad the game is given: {buttons, right x, right y}: the gesture's bits OR'd, its right stick while it is active. */
    static int[] merge(MapTouch g,int buttons,int rightX,int rightY){
        if(g==null || !g.active())return new int[]{buttons,rightX,rightY};
        return new int[]{buttons|g.buttons,g.rx,g.ry};
    }
    private static int axis(float v){return Math.round(32767*Math.max(-1f,Math.min(1f,v)));}

    void down(long t,float x,float y,float vw,float vh){
        if(state!=State.IDLE)return;
        x0=fx=lastX=x;y0=fy=lastY=y;w=vw;h=vh;downAt=lastT=t;vx=vy=zr=0;rx=ry=0;buttons=0;
        second=pending() && t-(pendUntil-DOUBLE_TAP_MS)<=DOUBLE_TAP_MS; // (3.11: a second down in the window; the waypoint waits for its lift)
        state=State.ARMED;next=NONE;
    }
    void move(long t,float x,float y){
        if(state==State.ARMED && Math.hypot(x-x0,y-y0)>TAP_SLOP*Math.min(w,h)){pendUntil=Long.MIN_VALUE;second=false; // (3.11: a drag drops a waiting waypoint)
            state=State.DRAG;buttons=TOUCH;lastX=x;lastY=y;lastT=t;next=t+TICK_MS;return;}
        if(state==State.DRAG){drag(t,x,y);next=t+TICK_MS;}
    }
    private void drag(long t,float x,float y){
        final float dt=Math.max(1f,t-lastT),a=Math.min(1f,dt/PAN_SMOOTH_MS);
        vx+=a*((x-lastX)*1000f/dt-vx);vy+=a*((y-lastY)*1000f/dt-vy);lastX=x;lastY=y;lastT=t;
        final float speed=(float)Math.hypot(vx,vy);
        if(speed<PAN_STILL){rx=ry=0;return;}
        final float k=(PAN_MIN+(1f-PAN_MIN)*Math.min(1f,speed/PAN_FULL))/speed; // (the stick's length from the speed, its direction against the finger's)
        rx=axis(-vx*k);ry=axis(vy*k);
    }
    void pointerDown(long t,float x,float y){
        if(state!=State.ARMED && state!=State.DRAG)return;
        pendUntil=Long.MIN_VALUE;second=false; // (3.11: a pinch drops a waiting waypoint)
        state=State.PINCH;buttons=TOUCH;px2=x;py2=y;lastT=t;zr=0;rx=PINCH;ry=0;
        lastLogD=Math.log(Math.max(1.0,Math.hypot(px2-fx,py2-fy)));next=t+TICK_MS;
    }
    void move2(long t,float x1,float y1,float x2,float y2){
        if(state!=State.PINCH)return;
        fx=x1;fy=y1;px2=x2;py2=y2;zoom(t);next=t+TICK_MS;
    }
    private void zoom(long t){
        final double ld=Math.log(Math.max(1.0,Math.hypot(px2-fx,py2-fy)));
        final float dt=Math.max(1f,t-lastT),a=Math.min(1f,dt/ZOOM_SMOOTH_MS);
        zr+=a*((float)((ld-lastLogD)*1000.0/dt)-zr);lastLogD=ld;lastT=t;
        final int v=Math.abs(zr)<ZOOM_STILL?0:Math.round(TRIGGER_MIN+(255-TRIGGER_MIN)*Math.min(1f,Math.abs(zr)/ZOOM_FULL));
        rx=PINCH;ry=zr>0?v:-v;
    }
    /** A finger of the pinch lifted: the pinch ends; the remaining finger is ignored until it lifts. */
    void pointerUp(long t){if(state==State.PINCH){rest();state=State.IGNORED;}}
    void up(long t){
        switch(state){
            case ARMED:
                if(delayTaps && second){second=false;pendUntil=Long.MIN_VALUE;state=State.IDLE;next=NONE; // (3.11: the second tap)
                    if(t-downAt<=TAP_MS)close=true;           // (a double tap: the close, no waypoint)
                    break;}                                    // (a hold: neither; the long press is QolGesture's)
                if(delayTaps && t-downAt<=TAP_MS){ // (3.11: the waypoint waits for a possible second tap)
                    pendRx=Math.round((x0-w*0.5f)*1280f/w);pendRy=Math.round((y0-h*0.5f)*720f/h);pendUntil=t+DOUBLE_TAP_MS;
                    state=State.IDLE;next=pendUntil;break;}
                if(t-downAt<=TAP_MS && t>=lockUntil){ // (a tap: its point as an offset from the screen's centre, in guest pixels)
                    state=State.TAP;buttons=TOUCH|REQUEST;lockUntil=t+LOCK_MS;
                    rx=Math.round((x0-w*0.5f)*1280f/w);ry=Math.round((y0-h*0.5f)*720f/h);next=t+PULSE_MS;
                } else {state=State.IDLE;next=NONE;}
                break;
            case DRAG:case PINCH:case IGNORED:rest();state=State.IDLE;break;
            default:break;
        }
    }
    void tick(long t){
        if(t<next)return;
        if(state==State.IDLE && pending() && t>=pendUntil){ // (3.11: no second tap came: the waypoint now)
            pendUntil=Long.MIN_VALUE;
            if(t>=lockUntil){state=State.TAP;buttons=TOUCH|REQUEST;lockUntil=t+LOCK_MS;rx=pendRx;ry=pendRy;next=t+PULSE_MS;}
            else next=NONE;
            return;
        }
        switch(state){
            case DRAG:drag(t,lastX,lastY);next=t+TICK_MS;break;  // (no move for a while: the velocity decays to rest)
            case PINCH:zoom(t);next=t+TICK_MS;break;
            case TAP:rest();state=State.IDLE;break;
            default:next=NONE;break;
        }
    }
    private void rest(){buttons=0;rx=ry=0;vx=vy=zr=0;next=NONE;}
    void cancel(){rest();state=State.IDLE;pendUntil=Long.MIN_VALUE;second=close=false;}
}
