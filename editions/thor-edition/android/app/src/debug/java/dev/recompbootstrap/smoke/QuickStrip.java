package dev.recompbootstrap.smoke;

import java.util.ArrayList;
import java.util.List;

/**
 * Update 1 QoL (owner's goal 2026-11-07, item 5): the Thor's lower-screen quick strip, a row of the lower screen's own 88 px boxes
 * extending left from the trainer button (WheelGesture.trainerBox: Quick Save's size, its 16 px gap), drawn by QuickStripView and
 * tapped through SecondScreen.LowerTouch. Aligned with the Duo's draft (local-analysis/ios-duo-edition/QUICK-STRIP-DRAFT.md); the spec
 * is local-analysis/.agents/upd1/integration/QUICK-STRIP.md. Pure Java: tests/QuickStripTest.java.
 *
 * Owner 2026-11-07 ("the quick strip handle? make it more prominent"): the handle is a whole box like the trainer button and Quick
 * Save, with a chevron and the number of pinned favourites ("\u26053"; "More" when only the Online shortcut is there), and it pulses once
 * (PULSE_MS) when favourites first appear (the count going from none to some; not at launch). Still absent with nothing to show.
 * Right to left: Quick Save (the library's), the trainer button (the app's), the handle, then - while open - the Online shortcut (Sandbox
 * only: online character / back to story) and the first FAVOURITES trainer favourites. Collapsed by default (the map is not covered: the
 * handle only, half a box, and no handle at all without anything to show). It closes after any slot's action, on the handle, after
 * IDLE_MS without a touch on it, and whenever the trainer button is hidden (wheel, phone, pause, Loading, the trainer off). A slot that
 * would cross the view's left edge (EDGE_1080) is dropped from the favourites' end. Touch only; a gamepad uses the side menu.
 */
final class QuickStrip {
    static final float GAP_1080=16f,EDGE_1080=16f,TEXT_1080=20f;
    static final long PULSE_MS=2400;          // the handle's one pulse when favourites first appear (3 beats of 800 ms)
    static final String STAR="\u2605",MORE="More";
    static final long IDLE_MS=6000;
    static final int FAVOURITES=4;
    static final String ID_HANDLE="qs:handle";
    enum Kind {HANDLE,ONLINE,FAVOURITE}
    static final class Slot {
        final String id,label;final Kind kind;final boolean on;final float x0,y0,x1,y1;
        Slot(String id,String label,Kind kind,boolean on,float x0,float y0,float x1,float y1){
            this.id=id;this.label=label;this.kind=kind;this.on=on;this.x0=x0;this.y0=y0;this.x1=x1;this.y1=y1;}
        boolean contains(float x,float y){return x>=x0 && x<x1 && y>=y0 && y<y1;}
    }

    private boolean expanded;private long lastTouch;
    private int seenPins=-1;private long pulseUntil=Long.MIN_VALUE;
    /** The number of pinned favourites now (QuickStripView's poll): from none to some starts the handle's pulse; the first count (launch,
     *  favourites from the last session) never does. */
    void pins(int n,long now){if(seenPins==0 && n>0)pulseUntil=now+PULSE_MS;seenPins=n;}
    /** The pulse's strength at now, 0..1 (three beats), 0 when not pulsing. */
    float pulse(long now){
        if(now>=pulseUntil || now<pulseUntil-PULSE_MS)return 0f;
        final double phase=(double)(now-(pulseUntil-PULSE_MS))/(PULSE_MS/3.0);
        return (float)(0.5-0.5*Math.cos(2*Math.PI*phase));
    }
    /** The handle's words under its chevron: the pinned favourites' count, or MORE while only the Online shortcut is behind it. */
    static String handleLabel(int pins){return pins>0?STAR+pins:MORE;}
    boolean expanded(){return expanded;}

    /** The slots to draw now in a lower view of w x h (empty: nothing beyond today's trainer button and Quick Save). */
    List<Slot> layout(SideMenuRows rows,float w,float h){
        final List<Slot> out=new ArrayList<>();
        final float[] t=WheelGesture.trainerBox(w,h);
        if(t==null || rows==null)return out;
        // No visible control that does nothing (owner via the lead, 2026-11-07, the "<" that "does nothing"): the handle shows only when
        // opening it shows at least one slot. The rows are built here; QuickStripView lays out only when the strip's visibility changes,
        // when it closes, or while it is open, so this costs nothing per frame.
        final List<Slot> content=content(rows);
        if(content.isEmpty()){expanded=false;return out;}
        final float k=Math.min(1f,h/1080f),gap=Math.round(GAP_1080*k),edge=Math.round(EDGE_1080*k),side=t[2]-t[0];
        final float hx1=t[0]-gap,hx0=hx1-side; // (a whole box, as the trainer button and Quick Save)
        if(hx0<edge)return out;
        out.add(new Slot(ID_HANDLE,handleLabel(rows.favourites.ids().size()),Kind.HANDLE,expanded,hx0,t[1],hx1,t[3])); // (on = open: the chevron points right)
        if(!expanded)return out;
        float x1=hx0-gap;Kind last=null;
        for(Slot c:content){
            if(last==Kind.ONLINE && c.kind==Kind.FAVOURITE)x1-=gap; // (the Online shortcut set apart by one more gap)
            final float x0=x1-side;
            if(x0<edge)break; // (dropped from the favourites' end)
            out.add(new Slot(c.id,c.label,c.kind,c.on,x0,t[1],x1,t[3]));
            x1=x0-gap;last=c.kind;
        }
        return out;
    }
    /** What the strip holds when open, in order (no rectangles yet). */
    private static List<Slot> content(SideMenuRows rows){
        final List<Slot> l=new ArrayList<>();
        if(rows.trainer==null || !rows.trainer.present())return l;
        final int s=rows.trainer.state();
        if((s&SideMenuRows.STATE_ENABLED)==0)return l;
        if(OnlineMenu.shown(s)){
            final boolean online=(s&OnlineMenu.STATE_ONLINE_CHARACTER)!=0;
            l.add(new Slot(online?"on:char:2":"on:char:"+rows.online.lastOnline(),online?"Story Character":"Online Character",Kind.ONLINE,online,0,0,0,0));
        }
        int n=0;
        for(SideMenu.Row r:rows.favourites.rows(rows)){
            if(n++>=FAVOURITES)break;
            l.add(new Slot(r.id,shortLabel(r),Kind.FAVOURITE,r.kind==SideMenu.Kind.TOGGLE && r.value==1,0,0,0,0));
        }
        return l;
    }
    /** A favourite's words on its box (main 2026-11-07: "Give All Weap..." was cut): the trainer's own rows have short names (Heal, Weapons,
     *  Repair, Wanted 2\u2605, Time 18:00); any other row its label without a leading "Switch To" / "Teleport To"; a value row with its value.
     *  What still does not fit one line wraps onto two in a smaller size (fit). */
    static String shortLabel(SideMenu.Row r){
        switch(r.id){
            case "tr:heal":return "Heal";
            case "tr:weapons":return "Weapons";
            case "tr:fix":return "Repair";
            case "tr:waypoint":return "Waypoint";
            case "tr:clearwx":return "Clear Sky";
            case "tr:wanted":{final String v=r.shown();return "Wanted "+(v.isEmpty()?"":v.charAt(0)+STAR);}
            case "tr:time":return "Time "+r.shown();
            default:break;
        }
        String s=r.label;
        for(String p:new String[]{"Switch To ","Teleport To "})if(s.startsWith(p)){s=s.substring(p.length());break;}
        if(r.kind==SideMenu.Kind.CHOICE)s=s+" "+r.shown();
        return s;
    }
    /** How a box's words are drawn: lines at scale (of TEXT_1080). */
    static final class Fit {final float scale;final List<String> lines;Fit(float scale,List<String> lines){this.scale=scale;this.lines=lines;}}
    static final float[] FIT_SCALES={1f,0.9f,0.8f,0.7f};
    /** The largest scale at which the words fit width in at most two lines (word wrap, never mid-word); measure gives a text's width at
     *  scale 1. Only when even the smallest scale cannot fit them is the second line cut with an ellipsis (a single very long word). */
    static Fit fit(String s,float width,java.util.function.ToDoubleFunction<String> measure){
        for(float k:FIT_SCALES){
            final List<String> l=wrap(s,width/k,measure);
            boolean ok=l.size()<=2;for(String x:l)ok&=measure.applyAsDouble(x)<=width/k;
            if(ok)return new Fit(k,l);
        }
        final float k=FIT_SCALES[FIT_SCALES.length-1];final List<String> l=wrap(s,width/k,measure);
        final List<String> two=new ArrayList<>(l.subList(0,Math.min(2,l.size())));
        if(l.size()>2)for(int i=2;i<l.size();++i)two.set(1,two.get(1)+" "+l.get(i));
        for(int i=0;i<two.size();++i){String x=two.get(i);while(x.length()>1 && measure.applyAsDouble(x)>width/k)x=x.substring(0,x.length()-2)+"\u2026";two.set(i,x);}
        return new Fit(k,two);
    }
    private static List<String> wrap(String s,float width,java.util.function.ToDoubleFunction<String> measure){
        final List<String> out=new ArrayList<>();String line="";
        for(String w:s.split(" ")){
            if(w.isEmpty())continue;
            final String t=line.isEmpty()?w:line+" "+w;
            if(line.isEmpty() || measure.applyAsDouble(t)<=width){line=t;continue;}
            out.add(line);line=w;
        }
        if(!line.isEmpty())out.add(line);
        return out;
    }
    static Slot at(List<Slot> slots,float x,float y){for(Slot s:slots)if(s.contains(x,y))return s;return null;}

    /** A tap on slot s at time now: the side-menu event it sends (null for the handle, which opens or closes the strip). Any slot's
     *  action closes the strip. A favourite acts as its row would on its page: an action acts, a toggle flips, a value row steps once. */
    SideMenu.Event tap(Slot s,SideMenuRows rows,long now){
        lastTouch=now;
        if(s==null)return null;
        if(s.kind==Kind.HANDLE){expanded=!expanded;return null;}
        expanded=false;
        if(s.kind==Kind.ONLINE)return new SideMenu.Event(SideMenu.Event.Type.ACTION,s.id,0,null);
        for(SideMenu.Row r:rows.favourites.rows(rows)){
            if(!r.id.equals(s.id) || !r.enabled)continue;
            switch(r.kind){
                case ACTION:return new SideMenu.Event(SideMenu.Event.Type.ACTION,r.id,0,null);
                case TOGGLE:return new SideMenu.Event(SideMenu.Event.Type.CHANGE,r.id,r.value==1?0:1,null);
                case CHOICE:return r.values==null || r.values.length==0?null:new SideMenu.Event(SideMenu.Event.Type.CHANGE,r.id,(r.value+1)%r.values.length,null);
                default:return null;
            }
        }
        return null;
    }
    /** Called with the trainer button's visibility at time now (QuickStripView's poll): true when the strip closed. */
    boolean tick(long now,boolean shown){
        if(!expanded)return false;
        if(!shown || now-lastTouch>=IDLE_MS){expanded=false;return true;}
        return false;
    }
}
