package dev.recompbootstrap.smoke;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.view.View;

/**
 * Thor edition (thor-radio v832; android-qol's v714 design, the owner's pick: the shown number alone, "60", small and clean): the frame-rate
 * label over the main screen's top left. Reads PrivateTraceActivity.readFps (the library's recomp_fps_v1) twice a second; the read also
 * tells the library that the app draws the label, so it draws none of its own. White at 78 %, a soft faint shadow, no box.
 */
final class FpsLabelView extends View {
    static final float SIZE=0.0195f,X=0.02f,TOP=8f/720f;static final int COLOR=0xC7FFFFFF,SHADOW=0x73000000;
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.SUBPIXEL_TEXT_FLAG);
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final float[] rates=new float[3];
    private String text="";
    /** The label: the shown rate rounded ("60"), or "" when the counter is off (on != 1) or nothing was measured yet. */
    static String text(int on,float shown){
        if(on!=1 || !(shown>0f) || Float.isInfinite(shown))return "";
        return Integer.toString(Math.round(shown));
    }
    private final Runnable poll=new Runnable(){@Override public void run(){
        final int on=PrivateTraceActivity.readFps(rates);
        final String t=text(on,rates[0]);
        if(!t.equals(text)){text=t;invalidate();}
        handler.postDelayed(this,500);
    }};
    FpsLabelView(Context context){
        super(context);setClickable(false);setFocusable(false);setWillNotDraw(false);
        paint.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));paint.setColor(COLOR);
    }
    @Override public boolean onTouchEvent(android.view.MotionEvent e){return false;}
    @Override protected void onAttachedToWindow(){super.onAttachedToWindow();handler.postDelayed(poll,500);}
    @Override protected void onDetachedFromWindow(){handler.removeCallbacks(poll);super.onDetachedFromWindow();}
    @Override protected void onDraw(Canvas c){
        if(text.isEmpty())return;
        final float h=getHeight(),k=h/720f,size=SIZE*h;
        paint.setTextSize(size);paint.setShadowLayer(5f*k,0f,1.5f*k,SHADOW);
        c.drawText(text,Math.max(8f*k,X*getWidth()),TOP*h+size*0.95f,paint);
    }
}
