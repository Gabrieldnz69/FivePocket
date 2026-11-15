package dev.recompbootstrap.smoke;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.View;
import java.util.ArrayList;
import java.util.List;

/**
 * Update 1 QoL (owner's goal 2026-11-07, item 5): draws the lower screen's quick strip (QuickStrip) beside the trainer button, in the
 * button's own look (quick_save.h: black 0.78, radius 10; on = white with black words). Shown exactly while the trainer button is
 * (TrainerButtonView's rule); polled every 250 ms like it. Takes no touches itself: SecondScreen.LowerTouch asks slotAt() and tap().
 */
final class QuickStripView extends View {
    private final ThorMenus menus;private final QuickStrip strip=new QuickStrip();
    private final Paint fill=new Paint(Paint.ANTI_ALIAS_FLAG),text=new Paint(Paint.ANTI_ALIAS_FLAG),stroke=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Handler handler=new Handler(Looper.getMainLooper());private final RectF r=new RectF();
    private boolean shown;private List<QuickStrip.Slot> slots=new ArrayList<>();private int lastPins=-1,lastState=-1;
    private final Runnable poll=new Runnable(){@Override public void run(){
        final boolean s=menus.lowerAllowed() && !menus.lower.isOpen() && PrivateTraceActivity.lowerButtons()>0 && (PrivateTraceActivity.lowerButtons()&1)!=0;
        final boolean closed=strip.tick(SystemClock.uptimeMillis(),s);
        final int pins=menus.rows.favourites.ids().size(),state=menus.rows.trainer==null?0:menus.rows.trainer.state()&(SideMenuRows.STATE_SANDBOX|SideMenuRows.STATE_ENABLED);
        final long now=SystemClock.uptimeMillis();final boolean wasPulsing=strip.pulse(now)>0f;if(s)strip.pins(pins,now); // (counted only while shown: a pin made while hidden pulses when the handle shows)
        if(!wasPulsing && strip.pulse(now+1)>0f){handler.removeCallbacks(beat);handler.post(beat);} // (favourites appeared: the one pulse)
        final boolean changed=pins!=lastPins || state!=lastState;lastPins=pins;lastState=state; // (a pin, Sandbox, the trainer: the handle may come or go)
        if(s!=shown || closed || changed || (s && strip.expanded())){shown=s;relayout();} // (open: its toggles follow the library; at most IDLE_MS)
        handler.postDelayed(this,250);
    }};
    QuickStripView(Context c,ThorMenus menus){
        super(c);this.menus=menus;setClickable(false);setFocusable(false);setWillNotDraw(false);
        text.setTypeface(UiKit.Faces.get(c).regular);text.setTextAlign(Paint.Align.CENTER);
    }
    private void relayout(){
        final List<QuickStrip.Slot> next=shown?strip.layout(menus.rows,getWidth(),getHeight()):new ArrayList<>();
        if(!same(next,slots)){slots=next;invalidate();}
    }
    private static boolean same(List<QuickStrip.Slot> a,List<QuickStrip.Slot> b){
        if(a.size()!=b.size())return false;
        for(int i=0;i<a.size();++i){final QuickStrip.Slot x=a.get(i),y=b.get(i);
            if(!x.id.equals(y.id) || !x.label.equals(y.label) || x.on!=y.on || x.x0!=y.x0)return false;}
        return true;
    }
    /** The slot under a lower-screen point, or null (also while the strip is hidden). */
    QuickStrip.Slot slotAt(float x,float y){return shown?QuickStrip.at(slots,x,y):null;}
    /** A tap on a slot: the handle opens / closes; any other slot's row acts as in the side menu (ThorMenus.quick) and the strip closes. */
    void tap(QuickStrip.Slot s){
        final SideMenu.Event e=strip.tap(s,menus.rows,SystemClock.uptimeMillis());
        android.util.Log.i(ThorMenus.TAG,"QUICK_STRIP tap "+s.id+(s.kind==QuickStrip.Kind.HANDLE?(strip.expanded()?" opened":" closed"):"")); // (every touch on it: the evidence)
        if(e!=null)menus.quick(e);
        relayout();
    }
    @Override public boolean onTouchEvent(android.view.MotionEvent e){return false;}
    @Override protected void onAttachedToWindow(){super.onAttachedToWindow();handler.post(poll);}
    @Override protected void onDetachedFromWindow(){handler.removeCallbacks(poll);handler.removeCallbacks(beat);super.onDetachedFromWindow();}
    @Override protected void onDraw(Canvas c){
        if(!shown || slots.isEmpty())return;
        final float k=Math.min(1f,getHeight()/1080f);
        text.setTextSize(QuickStrip.TEXT_1080*k);
        for(QuickStrip.Slot s:slots){
            final boolean lit=s.on && s.kind!=QuickStrip.Kind.HANDLE; // (an on toggle: white with black words; the open handle stays black)
            r.set(s.x0,s.y0,s.x1,s.y1);fill.setColor(lit?0xC7FFFFFF:0xC7000000);c.drawRoundRect(r,10f*k,10f*k,fill);
            text.setColor(lit?0xFF000000:0xFFFFFFFF);
            if(s.kind==QuickStrip.Kind.HANDLE){handle(c,s,k,strip.pulse(SystemClock.uptimeMillis()));continue;}
            final float base=QuickStrip.TEXT_1080*k;text.setTextSize(base); // (QuickStrip.fit: short names, two lines, smaller rather than cut)
            final QuickStrip.Fit f=QuickStrip.fit(s.label,s.x1-s.x0-10f*k,t->text.measureText(t));
            text.setTextSize(base*f.scale);
            final float lh=base*f.scale*1.2f,cy=(s.y0+s.y1)*0.5f-lh*(f.lines.size()-1)*0.5f;
            for(int i=0;i<f.lines.size();++i)c.drawText(f.lines.get(i),(s.x0+s.x1)*0.5f,cy+i*lh+text.getTextSize()*0.35f,text);
            text.setTextSize(base);
        }
    }
    /** The handle (owner: "make it more prominent"): a whole box in the buttons' look, a bold chevron in its upper half (pointing left
     *  while closed: the strip opens that way; right while open), the pinned count ("\u26053") or "More" below it, a white edge while the
     *  strip is open, and a beating white outline during the one pulse when favourites first appear (QuickStrip.pulse). */
    private void handle(Canvas c,QuickStrip.Slot s,float k,float pulse){
        final float cx=(s.x0+s.x1)*0.5f,mid=(s.y0+s.y1)*0.5f,bh=s.y1-s.y0;
        final float cy=mid-bh*0.14f,dx=Math.min(14f*k,(s.x1-s.x0)*0.16f),dy=Math.min(14f*k,bh*0.16f),dir=s.on?1f:-1f;
        stroke.setStyle(Paint.Style.STROKE);stroke.setStrokeCap(Paint.Cap.ROUND);stroke.setStrokeJoin(Paint.Join.ROUND);
        stroke.setStrokeWidth(Math.max(3f,6f*k));stroke.setColor(0xFFFFFFFF);
        c.drawLine(cx-dir*dx*0.5f,cy-dy,cx+dir*dx*0.5f,cy,stroke);c.drawLine(cx+dir*dx*0.5f,cy,cx-dir*dx*0.5f,cy+dy,stroke);
        final float ts=text.getTextSize();text.setTextSize(QuickStrip.TEXT_1080*1.2f*k);text.setFakeBoldText(true);
        c.drawText(s.label,cx,mid+bh*0.30f,text);
        text.setFakeBoldText(false);text.setTextSize(ts);
        final float edge=s.on?0.9f:pulse*0.95f;
        if(edge>0.01f){
            stroke.setStrokeWidth(Math.max(2f,4f*k));stroke.setColor(((int)(edge*255)<<24)|0xFFFFFF);
            final float in=2f*k;r.set(s.x0+in,s.y0+in,s.x1-in,s.y1-in);c.drawRoundRect(r,9f*k,9f*k,stroke);
        }
    }
    /** While the handle pulses: a redraw every 33 ms, then one more to clear it (nothing runs otherwise). */
    private final Runnable beat=new Runnable(){@Override public void run(){
        invalidate();if(strip.pulse(SystemClock.uptimeMillis())>0f)handler.postDelayed(this,33);
    }};
}
