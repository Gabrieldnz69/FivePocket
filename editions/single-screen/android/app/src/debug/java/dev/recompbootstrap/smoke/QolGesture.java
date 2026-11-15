package dev.recompbootstrap.smoke;

/**
 * qol-touch (single-screen QoL edition, 2026-10-24; owner: "long press the map for the map toggle, or long press the weapon icon for
 * the weapon wheel, or long press the character select wheel for the select wheel, it doesn't have to draw a new ui either like it
 * does on the second screen, it just has to apply the same touch variables"). Touches on the game's own (only) screen turned into the
 * controller input the game already reads; the game draws its own map and wheels as usual. Plain Java (no Android types), so the
 * host test runs it (local-analysis/single-screen-edition/tests/run_test.sh).
 *
 * Coordinates: the view's touches are taken into the game frame (the 16:9 picture centred in the view with black bars, as
 * direct_present_reference.h layout() puts it); zones are shares of that frame (clamped, so a bar beside a zone counts as the zone).
 * A finger held LONG_MS within LONG_SLOP of where it went down is a long press; what it does depends on where it went down:
 *  MAP        (the minimap, bottom left)      the pause full map: the library's map-tap request (REQUEST for REQUEST_MS, map_tap.h:
 *             on the full map it opened, or any pause screen, the same request = START = back to the game), or without the library's
 *             macro the app's own START, then A (the Thor's fallback).
 *  WEAPON     (the weapon / ammo HUD, top right)  the weapon wheel: LB held while the finger stays down.
 *  RADIO      (the left edge, centre)         the radio wheel (in a vehicle): D-pad LEFT held while the finger stays down.
 *  CHARACTER  (the character wheel's place, bottom right)  the character switch wheel: D-pad DOWN held while the finger stays down.
 * While a wheel's button is held the finger steers the right stick toward the slot, the Thor's WheelGesture rules: from OPEN_MS on
 * (the wheel's opening), once the finger has slid LONG_SLOP from where the long press fired (a press lifted without sliding keeps the
 * game's highlight: the current weapon / station / character), full deflection along the direction from the wheel's centre on the
 * screen to the finger, a rest inside the DEAD disc, every push at least MIN_STICK_MS; lifting = the stick rests, CLOSE_GAP_MS later
 * the button is released and the game selects the highlighted slot. A finger lifted before OPEN_MS after a slide still selects the
 * slot under its lift point (one push at OPEN_MS).
 * LATCHED wheels (owner 2026-10-24: "you shouldn't have to keep your finger on the weapon, it should auto hold it til you make your
 * selection", and a tap on a slot must select it, SPEC 3.6): a wheel's long press lifted WITHOUT a slide keeps its button held
 * (LATCHED; the on-screen pad's WEAPONS / SWITCH / RADIO buttons hand their long hold over the same way, latch()). The next finger is
 * the wheel's: inside the wheel's ring it steers the right stick from the wheel's centre at once (a tap = one push of MIN_STICK_MS) and
 * its lift selects (the stick rests, CLOSE_GAP_MS later the button is released); outside the ring or on its centre = the button released
 * at once with no push (the game keeps the current slot). LATCH_MS without a finger, a physical button press, a context or pause-map
 * change (endLatch(), QolTouch) end it the same way. While the PAD holds LB / D-pad LEFT / D-pad DOWN, a finger inside that wheel's
 * ring steers the right stick the same way (the Thor's WheelTouch rule); the pad's own release selects.
 * v701: a long press with no zone under it = back to the game from the pause map or any pause screen (TOUCH | REQUEST with the stick at
 * (CLOSE, CLOSE), map_tap.h CloseCode; nothing in the game). Only with the library's macro.
 * Everything that is not a long press goes to the Thor's main-screen map touch (MapTouch: drag = pan, pinch = zoom, tap = waypoint),
 * carried on the touchpad bit that the library gives the game only while its pause full map is on screen: in the game a short tap
 * or a drag does nothing, as before this edition (the normal app had no touch layer on the game). Only with the library's macro
 * (nativeMap): without it those bits would reach the game.
 * The physical pad always wins: a finger that lands while the pad holds a wheel's button is ignored; a new physical button press ends
 * the gesture at once (its buttons released, the finger ignored until it lifts); a physical right stick past PAD_DEADZONE replaces
 * the gesture's (merge()). Time: any monotonic milliseconds (the app: MotionEvent.getEventTime / SystemClock.uptimeMillis).
 */
final class QolGesture {
    static final int LB=0x100,LEFT=0x0004,DOWN=0x0002,START=0x0010,A=0x1000,B=0x2000,REQUEST=MapTouch.REQUEST;
    static final int TOUCH=MapTouch.TOUCH;
    // v701 (owner 2026-10-24: "long pressing the map should allow you to go back to gameplay like the thor, only on the main screen"): a long
    // press with no zone under it = TOUCH | REQUEST with the right stick at (CLOSE, CLOSE) (map_tap.h CloseCode): on the pause full map (or any
    // pause screen) the library presses START = back to the game; in the game it does nothing. Only with the library's macro (nativeMap).
    static final int CLOSE=-32768;
    static final long LONG_MS=400,NONE=Long.MAX_VALUE;
    // SPEC 3.11 (owner 2026-10-24: "for map touch make the hold time shorter, and make it work with double tap"): the minimap zone fires after
    // MAP_LONG_MS (the wheels and the no-zone close keep LONG_MS); in the game (not on the pause map) a second tap on the minimap whose down
    // comes within DOUBLE_TAP_MS of the first tap's lift fires the same request at once. On the pause map double taps stay the map's.
    static final long MAP_LONG_MS=250,DOUBLE_TAP_MS=300;
    static final float LONG_SLOP=MapTouch.TAP_SLOP; // (a share of the frame's smaller side: the map touch's drag starts beyond it)
    // The Thor's wheel and map-tap variables (WheelGesture, thor-edition apk-tree; the same values).
    static final long OPEN_MS=250,MIN_STICK_MS=133,CLOSE_GAP_MS=66;
    static final float DEAD=0.10f; // the dead disc's radius, as a share of the frame's smaller side
    static final long REQUEST_MS=150,REQUEST_LOCK_MS=700,FALLBACK_START_MS=100,FALLBACK_A_MS=450,PULSE_MS=120;
    static final int PAD_DEADZONE=8689; // (XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)
    static final long LATCH_MS=10000; // (SPEC 3.6: a latched wheel without a finger lets go after 10 s)
    // SPEC 3.6 v2 (main 2026-10-24: two of the owner's latches were cancelled by his next finger): a near miss (up to NEAR_MISS x the
    // ring's outer radius) selects the slot in its direction; farther, the stick side (x < STICK_SIDE of the view) does nothing and
    // elsewhere only a quick tap (lifted within LATCH_TAP_MS, within the slop) cancels; the centre still cancels at once.
    static final float NEAR_MISS=1.25f,STICK_SIDE=0.45f;static final long LATCH_TAP_MS=250;

    enum Zone {NONE,MAP,WEAPON,RADIO,CHARACTER}
    enum State {IDLE,ARMED,HOLD,CLOSING,MACRO,LATCHED}
    // Zones as shares of the 1280x720 frame: {x0, y0, x1, y1}. The minimap is drawn at ~(40-265, 520-690), the weapon / ammo HUD top
    // right, the character wheel's own viewport 1089,556 125x126 (census char-wheel-1, thor-edition README "Character switch wheel").
    static final float[] MAP_ZONE={0f,0.68f,0.25f,1f},WEAPON_ZONE={0.72f,0f,1f,0.25f},CHARACTER_ZONE={0.80f,0.70f,1f,1f},
                         RADIO_ZONE={0f,0.30f,0.12f,0.62f};
    // touch-controls (SPEC 3.3): while the on-screen pad shows (context mode) the zones make room for its controls: the minimap itself, a
    // smaller weapon HUD corner, a smaller radio edge, and the character zone MOVED to the right edge (its steering is then relative to the
    // long-press point, since the wheel is drawn away from the zone). QolTouch sets padZones; Off / hidden = the zones above.
    // SPEC v2 3.3: with the universal controls showing only the radar and the weapon icon are zones (RADIO and SWITCH are buttons there).
    // (main 2026-10-24: "a thumb resting on the stick must never open the map": with the pad the minimap zone is the radar's DRAWN box,
    // guest ~(40-265, 520-690), + ~15 px: no longer the whole bottom-left corner; a stick touch is the pad's from its down and never a gesture)
    static final float[] PAD_MAP_ZONE={0.02f,0.71f,0.22f,0.97f},PAD_WEAPON_ZONE={0.75f,0f,1f,0.20f},PAD_RADIO_ZONE={2f,2f,2f,2f},
                         PAD_CHARACTER_ZONE={2f,2f,2f,2f}; // (empty: no point of the frame lies there)
    static volatile boolean padZones;
    // The wheels' centres on the frame (shares): the weapon wheel (640, 297) (measured on the Fold 2026-10-24: its slots on a ring of
    // r ~0.27 of the frame's height), the radio wheel (640, 330) (thor-radio RadioCrop), the character wheel (1151.5, 619) (its viewport's centre).
    static final float[] WEAPON_CENTRE={0.5f,0.413f},RADIO_CENTRE={0.5f,330f/720f},CHARACTER_CENTRE={1151.5f/1280f,619f/720f};
    // A latched wheel's tap ring {inner, outer} (shares of the frame's smaller side): a finger between them picks a slot, elsewhere it
    // cancels. The character wheel is small (its viewport 125x126): a small centre and ring.
    static final float[] WEAPON_RING={0.08f,0.45f},RADIO_RING={0.08f,0.45f},CHARACTER_RING={0.025f,0.20f};

    /** The game frame inside a w x h view: {left, top, width, height} (direct_present_reference.h layout(), in floats). */
    static float[] frame(float w,float h){
        if(!(w>0) || !(h>0))return new float[]{0,0,0,0};
        float fw=w,fh=h;
        if(w*720f<=h*1280f)fh=w*720f/1280f;else fw=h*1280f/720f;
        return new float[]{(w-fw)*0.5f,(h-fh)*0.5f,fw,fh};
    }
    private static boolean in(float sx,float sy,float[] z){return sx>=z[0] && sx<=z[2] && sy>=z[1] && sy<=z[3];}
    /** The zone of a point (view coordinates) in a w x h view. */
    static Zone zone(float x,float y,float w,float h){
        final float[] f=frame(w,h);
        if(!(f[2]>0) || !Float.isFinite(x) || !Float.isFinite(y))return Zone.NONE;
        final float sx=Math.max(0f,Math.min(1f,(x-f[0])/f[2])),sy=Math.max(0f,Math.min(1f,(y-f[1])/f[3]));
        final boolean p=padZones;
        if(in(sx,sy,p?PAD_CHARACTER_ZONE:CHARACTER_ZONE))return Zone.CHARACTER;
        if(in(sx,sy,p?PAD_MAP_ZONE:MAP_ZONE))return Zone.MAP;
        if(in(sx,sy,p?PAD_WEAPON_ZONE:WEAPON_ZONE))return Zone.WEAPON;
        if(in(sx,sy,p?PAD_RADIO_ZONE:RADIO_ZONE))return Zone.RADIO;
        return Zone.NONE;
    }
    static int button(Zone z){return z==Zone.WEAPON?LB:z==Zone.RADIO?LEFT:z==Zone.CHARACTER?DOWN:0;}
    static float[] centre(Zone z){return z==Zone.RADIO?RADIO_CENTRE:z==Zone.CHARACTER?CHARACTER_CENTRE:WEAPON_CENTRE;}
    static float[] ring(Zone z){return z==Zone.RADIO?RADIO_RING:z==Zone.CHARACTER?CHARACTER_RING:WEAPON_RING;}
    /** The wheel a pad's buttons hold (LB, then D-pad LEFT, then DOWN), or NONE. */
    static Zone wheelOf(int buttons){return (buttons&LB)!=0?Zone.WEAPON:(buttons&LEFT)!=0?Zone.RADIO:(buttons&DOWN)!=0?Zone.CHARACTER:Zone.NONE;}
    /** The right stick for a finger at (x, y) (frame coordinates) in a fw x fh frame, from the wheel centre (shares cx, cy): {x, y}
     *  (y up, full deflection), or null inside the dead disc (DEAD of the frame's smaller side). */
    static int[] direction(float x,float y,float fw,float fh,float cx,float cy){return direction(x,y,fw,fh,cx,cy,DEAD);}
    static int[] direction(float x,float y,float fw,float fh,float cx,float cy,float dead){
        if(!(fw>0) || !(fh>0) || !Float.isFinite(x) || !Float.isFinite(y))return null;
        final double dx=x-fw*cx,dy=y-fh*cy,r=Math.hypot(dx,dy);
        if(r<dead*Math.min(fw,fh))return null;
        return new int[]{(int)Math.round(32767*dx/r),(int)Math.round(-32767*dy/r)};
    }

    private final MapTouch map=new MapTouch();
    private boolean nativeMap;
    private State state=State.IDLE;
    private Zone zone=Zone.NONE,held=Zone.NONE;
    private boolean finger,ignored,lifted,slid,mapFinger,mapOpen,padTap;
    private float dead=DEAD; // (the steering's dead disc: DEAD, or a latched wheel's inner ring)
    private float fx,fy,fw,fh,x0,y0,hx,hy;
    private long downAt,openAt,stickSince=-1,next=NONE,lockUntil=Long.MIN_VALUE;
    private int buttons,rx,ry,padNow,padAtDown;private boolean stick;
    private int[] macro;private int macroAt;
    private long mapTapUp=Long.MIN_VALUE/2;private boolean onPauseMap; // (3.11: the last minimap tap's lift; the pause map shown, bit 15)
    /** The pause full map is shown (QolTouch, from the library's bit 15): double taps are then the map's. */
    void pauseMap(boolean shown){onPauseMap=shown;map.delayTaps(shown && nativeMap);}

    /** Whether the library runs the map macro (and the map touch) itself (QolTouch's scan of the library). */
    void nativeMap(boolean on){nativeMap=on;if(!on)map.cancel();}
    boolean nativeMap(){return nativeMap;}
    State state(){return state;}
    Zone held(){return held;}
    MapTouch map(){return map;}
    int buttons(){return buttons;}
    boolean stickOn(){return stick;}
    int rightX(){return rx;}
    int rightY(){return ry;}
    boolean mapOpen(){return mapOpen;}
    long due(){return Math.min(next,nativeMap?map.due():NONE);}
    boolean latched(){return state==State.LATCHED;}
    /** A wheel of this gesture is open (held, latched or selecting): the touch sticks are off meanwhile (SPEC 3.8). */
    boolean wheelOpen(){return button(held)!=0 && (state==State.HOLD || state==State.CLOSING || state==State.LATCHED);}
    /** Whether a new finger is the wheel's (QolTouch gives it to this gesture before the on-screen pad): a latched wheel, or a wheel's
     *  button the physical pad holds. */
    boolean wantsFinger(){return !finger && (state==State.LATCHED || (state==State.IDLE && wheelOf(padNow)!=Zone.NONE));}
    /** The on-screen pad's wheel button was held and lifted without a slide: its button stays held here (LATCHED). */
    void latch(long t,Zone z){
        if(button(z)==0 || (state!=State.IDLE && state!=State.LATCHED && state!=State.HOLD) || (state==State.HOLD && finger))return;
        rest();held=z;buttons=button(z);state=State.LATCHED;next=t+LATCH_MS;padTap=false;relative=false;dead=DEAD;latchAt=t;
    }
    // 2026-11-09 (the owner's "double tap on WEAPON cycles to the next weapon"; libraries without trainer action 22, e.g. the Fold's v956
    // with ActionCount 13): there a WEAPON tap opens the latched wheel at once, and the double tap's second tap landed far from the ring and
    // closed the wheel again ('X'): the double tap did nothing. A far quick tap whose down comes within DOUBLE_TAP_MS of the latch is that
    // second tap ('D'): the wheel stays open so the player picks the next weapon on it.
    private long latchAt=Long.MIN_VALUE/2;private boolean farSecondTap;
    /** A latched wheel lets go at once, without a push (the game keeps the slot it shows): the timeout, a context or pause-map change. */
    void endLatch(long t){if(state==State.LATCHED){rest();buttons=0;state=State.IDLE;held=Zone.NONE;next=NONE;}}

    /** The pad the game is given: {buttons, right x, right y}: the physical state, this gesture's buttons OR'd, its right stick (or the
     *  map touch's channel) while it pushes, unless the physical right stick is past PAD_DEADZONE. */
    static int[] merge(QolGesture g,int buttons,int rightX,int rightY){
        if(g==null)return new int[]{buttons,rightX,rightY};
        final boolean physical=Math.abs(rightX)>PAD_DEADZONE || Math.abs(rightY)>PAD_DEADZONE;
        int b=buttons|g.buttons,x=rightX,y=rightY;
        if(g.stick && (!physical || g.state==State.MACRO)){x=g.rx;y=g.ry;} // (v701: the close code is never replaced: it is no push)
        if(g.nativeMap && g.map.active() && !physical){b|=g.map.buttons();x=g.map.rightX();y=g.map.rightY();}
        return new int[]{b,x,y};
    }

    /** The physical pad's buttons (every publish). A button pressed since the finger went down ends the gesture (the pad wins). */
    void pad(long t,int physicalButtons){
        final int fresh=physicalButtons&~padNow;padNow=physicalButtons;
        if((physicalButtons&(START|B))!=0)mapOpen=false; // (the fallback's open guess: the player's own START / B)
        if(padTap && (physicalButtons&button(held))==0){rest();padTap=false;state=State.IDLE;next=NONE;held=Zone.NONE;if(finger)ignored=true;return;} // (the pad selected)
        if(fresh==0 || (!finger && state!=State.HOLD && state!=State.CLOSING && state!=State.LATCHED))return;
        if(finger && (physicalButtons&~padAtDown)==0)return;
        if(state==State.HOLD || state==State.CLOSING || state==State.LATCHED){rest();buttons=0;state=State.IDLE;next=NONE;held=Zone.NONE;padTap=false;}
        else if(state==State.ARMED){state=State.IDLE;next=NONE;}
        if(mapFinger){map.cancel();mapFinger=false;}
        if(finger)ignored=true;
    }

    void down(long t,float x,float y,float w,float h){
        if(finger)return; // (one finger at a time; a second one is the map's pinch: pointerDown)
        final float[] f=frame(w,h);
        finger=true;lifted=false;slid=false;mapFinger=false;
        fw=f[2];fh=f[3];fx=x0=x-f[0];fy=y0=y-f[1];downAt=t;padAtDown=padNow;
        if(fw>0 && state==State.LATCHED){ // (SPEC 3.6: the latched wheel's finger: a slot, a near miss, the centre, or far)
            ignored=false;final float rel=ringRel(held);++latchTouches;lastLatchRel=rel;
            if(rel<0){lastLatchOutcome='C';endLatch(t);ignored=true;return;}          // (the centre: closed without a change)
            if(rel<=NEAR_MISS){lastLatchOutcome=rel<=1f?'S':'N';steer(t,false);return;} // (a slot, or a near miss: its direction)
            farStickSide=x<STICK_SIDE*w;farMoved=false;latchFar=true;farSecondTap=t-latchAt<=DOUBLE_TAP_MS;lastLatchOutcome=farStickSide?'I':farSecondTap?'D':'F';return; // (decided at the lift)
        }
        if(fw>0 && state==State.IDLE && wheelOf(padNow)!=Zone.NONE){ // (the pad holds a wheel's button: a finger on its ring steers)
            held=wheelOf(padNow);ignored=!onRing(held);
            if(ignored){held=Zone.NONE;return;}
            buttons=0;steer(t,true);return;
        }
        if(state!=State.IDLE || !(fw>0)){ignored=true;return;} // (a gesture still ending)
        ignored=false;
        zone=zone(x,y,w,h);state=State.ARMED;next=zone==Zone.NONE && !nativeMap?NONE:t+(zone==Zone.MAP?MAP_LONG_MS:LONG_MS); // (v701: no zone = the pause screen's close)
        if(zone==Zone.MAP && !onPauseMap && t-mapTapUp<=DOUBLE_TAP_MS){mapTapUp=Long.MIN_VALUE/2;fire(t);return;} // (3.11: the double tap)
        if(nativeMap && map.state()==MapTouch.State.IDLE){map.down(t,fx,fy,fw,fh);mapFinger=true;}
    }
    private boolean latchFar,farStickSide,farMoved;
    int latchTouches;float lastLatchRel;char lastLatchOutcome; // (QolTouch logs each latched-wheel touch: where, and what it did)
    /** The finger's distance from the wheel's centre as a share of its ring's outer radius; -1 inside the inner (centre) radius. */
    private float ringRel(Zone z){
        final float[] c=centre(z),g=ring(z);final double d=Math.hypot(fx-fw*c[0],fy-fh*c[1]),m=Math.min(fw,fh);
        return d<g[0]*m?-1f:(float)(d/(g[1]*m));
    }
    private boolean onRing(Zone z){
        final float[] c=centre(z),g=ring(z);final double d=Math.hypot(fx-fw*c[0],fy-fh*c[1]),m=Math.min(fw,fh);
        return d>=g[0]*m && d<=g[1]*m;
    }
    /** The wheel is already open: this finger steers from its first point (HOLD, slid), the pad's tap or a latched wheel's. */
    private void steer(long t,boolean pad){
        state=State.HOLD;padTap=pad;slid=true;lifted=false;openAt=t-OPEN_MS;relative=false;dead=ring(held)[0];hx=fx;hy=fy;next=NONE;aim(t);
    }
    void move(long t,float x,float y,float w,float h){
        if(!finger || ignored)return;
        final float[] f=frame(w,h);
        fx=x-f[0];fy=y-f[1];
        if(latchFar){if(Math.hypot(fx-x0,fy-y0)>LONG_SLOP*Math.min(fw,fh))farMoved=true;return;} // (a far finger on a latched wheel steers nothing)
        if(mapFinger)map.move(t,fx,fy);
        switch(state) {
            case ARMED:if(Math.hypot(fx-x0,fy-y0)>LONG_SLOP*Math.min(fw,fh)){state=State.IDLE;next=NONE;}break; // (a drag: no long press)
            case HOLD:
                if(!slid && Math.hypot(fx-hx,fy-hy)>LONG_SLOP*Math.min(fw,fh))slid=true;
                if(slid && !lifted && t>=openAt+OPEN_MS)aim(t);
                break;
            default:break;
        }
    }
    /** A second finger (frame-independent: the map's pinch); a long press not yet fired is cancelled. */
    void pointerDown(long t,float x,float y,float w,float h){
        if(!finger || ignored)return;
        if(state==State.ARMED){state=State.IDLE;next=NONE;}
        if(mapFinger){final float[] f=frame(w,h);map.pointerDown(t,x-f[0],y-f[1]);}
    }
    void move2(long t,float x1,float y1,float x2,float y2,float w,float h){
        if(!finger || ignored || !mapFinger)return;
        final float[] f=frame(w,h);map.move2(t,x1-f[0],y1-f[1],x2-f[0],y2-f[1]);
    }
    void pointerUp(long t){if(mapFinger)map.pointerUp(t);}
    void up(long t){
        if(!finger)return;
        finger=false;
        if(ignored){ignored=false;return;}
        if(latchFar){latchFar=false; // (3.6 v2: only a quick tap away from the stick side cancels; anything else leaves the wheel latched)
            if(!farStickSide && !farMoved && !farSecondTap && t-downAt<=LATCH_TAP_MS){lastLatchOutcome='X';endLatch(t);}
            return;}
        lifted=true;
        if(mapFinger){map.up(t);mapFinger=false; // (a short tap: the map's waypoint tap; off the map the library does nothing)
            if(map.takeClose() && t>=lockUntil){lockUntil=t+REQUEST_LOCK_MS;startMacro(t,new int[]{TOUCH|REQUEST,(int)REQUEST_MS});stick=true;rx=ry=CLOSE;
                state=State.MACRO;return;}} // (3.11: a double tap on the open map = the close, as v701's long press)
        switch(state) {
            case ARMED:state=State.IDLE;next=NONE;if(zone==Zone.MAP)mapTapUp=t;break; // (shorter than the hold: nothing of this gesture's own; 3.11: a minimap tap)
            case HOLD:
                if(!slid && !padTap){latch(t,held);break;} // (SPEC 3.6: no slide = the wheel stays open for a tap)
                if(t<openAt+OPEN_MS)break;          // (tick() at OPEN time pushes toward the lift point if the finger slid, then selects)
                {final long e=pushEnds(t);if(e<=t)close(t);else next=e;}
                break;
            default:break;
        }
    }
    void tick(long t){
        if(nativeMap && t>=map.due())map.tick(t);
        if(t<next)return;
        switch(state) {
            case ARMED:fire(t);break;
            case HOLD:
                if(!lifted){if(slid)aim(t);next=NONE;break;}       // (the wheel is open: the finger steers from now on, once it slides)
                if(slid && !stick){aim(t);if(stick){next=stickSince+MIN_STICK_MS;break;}} // (lifted before OPEN: one push at the lift point)
                close(t);break;
            case CLOSING:buttons=0;state=State.IDLE;next=NONE;held=Zone.NONE;padTap=false;if(finger)ignored=true;break;
            case LATCHED:endLatch(t);break; // (LATCH_MS without a finger)
            case MACRO:
                macroAt+=2;
                if(macroAt>=macro.length){buttons=0;rest();state=State.IDLE;next=NONE;macro=null;held=Zone.NONE;if(finger)ignored=true;break;}
                buttons=macro[macroAt];next=t+macro[macroAt+1];break;
            default:next=NONE;break;
        }
    }
    /** The long press fires (zone != NONE, the finger still within LONG_SLOP): the map touch lets go of this finger. */
    private void fire(long t){
        if(mapFinger){map.cancel();mapFinger=false;}
        held=zone;hx=fx;hy=fy;
        if(zone==Zone.NONE) { // v701: back to the game from a pause screen (the library ignores it in the game)
            if(t<lockUntil){state=State.IDLE;next=NONE;ignored=true;return;} // (without the library down() never arms it)
            lockUntil=t+REQUEST_LOCK_MS;startMacro(t,new int[]{TOUCH|REQUEST,(int)REQUEST_MS});
            stick=true;rx=ry=CLOSE;return;
        }
        if(zone==Zone.MAP) {
            if(t<lockUntil){state=State.IDLE;next=NONE;ignored=true;return;} // (right after a request: the library's macro still runs)
            lockUntil=t+REQUEST_LOCK_MS;
            if(nativeMap)startMacro(t,new int[]{REQUEST,(int)REQUEST_MS});
            else if(mapOpen){mapOpen=false;startMacro(t,new int[]{START,(int)PULSE_MS,0,(int)PULSE_MS});}
            else {mapOpen=true;startMacro(t,new int[]{START,(int)FALLBACK_START_MS,A,(int)FALLBACK_A_MS,0,(int)PULSE_MS});}
            return;
        }
        state=State.HOLD;buttons=button(zone);openAt=t;rest();next=t+OPEN_MS;relative=padZones && zone==Zone.CHARACTER;dead=DEAD;padTap=false;
    }
    private void startMacro(long t,int[] steps){macro=steps;macroAt=0;state=State.MACRO;buttons=steps[0];next=t+steps[1];}
    private boolean relative; // (SPEC 3.3: the moved character zone steers from the long-press point)
    private void aim(long t){
        final float[] c=centre(held);
        final int[] d=relative?direction(fx-hx+fw*0.5f,fy-hy+fh*0.5f,fw,fh,0.5f,0.5f):direction(fx,fy,fw,fh,c[0],c[1],dead);
        if(d==null){rest();return;} // (the stick rests: the game keeps its highlight)
        if(!stick)stickSince=t;
        stick=true;rx=d[0];ry=d[1];
    }
    private void rest(){stick=false;rx=ry=0;stickSince=-1;}
    /** When the current push may end: at least MIN_STICK_MS after it started (t when there is none). */
    private long pushEnds(long t){return stick?Math.max(t,stickSince+MIN_STICK_MS):t;}
    private void close(long t){rest();state=State.CLOSING;next=t+CLOSE_GAP_MS;}
    /** Focus lost / the screen went away: everything released at once. */
    void cancel(){rest();map.cancel();buttons=0;state=State.IDLE;held=Zone.NONE;finger=ignored=lifted=mapFinger=padTap=latchFar=false;next=NONE;macro=null;}
}
