package dev.recompbootstrap.smoke;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import java.util.function.BooleanSupplier;

/**
 * v714: draws FpsLabel over the game (no touches). Reads PrivateTraceActivity.readFps (the library's recomp_fps_v1) twice a second; the
 * read also tells the library that the app draws the label, so it draws none of its own. Redraws only when the text changes.
 */
final class FpsLabelView extends View {
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.SUBPIXEL_TEXT_FLAG);
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final float[] rates=new float[3];
    private final BooleanSupplier fill;
    private String text="";
    private final Runnable poll=new Runnable(){@Override public void run(){
        final int on=PrivateTraceActivity.readFps(rates);
        final String t=FpsLabel.text(on,rates[0]);
        if(!t.equals(text)){text=t;invalidate();}
        handler.postDelayed(this,500);
    }};

    FpsLabelView(Context context,BooleanSupplier fill){
        super(context);this.fill=fill;setClickable(false);setFocusable(false);setWillNotDraw(false);
        paint.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));paint.setColor(FpsLabel.COLOR);
    }
    @Override public boolean onTouchEvent(android.view.MotionEvent e){return false;}
    @Override protected void onAttachedToWindow(){super.onAttachedToWindow();handler.postDelayed(poll,500);}
    @Override protected void onDetachedFromWindow(){handler.removeCallbacks(poll);super.onDetachedFromWindow();}
    @Override protected void onDraw(Canvas c){
        if(text.isEmpty())return;
        final float[] p=FpsLabel.place(getWidth(),getHeight(),fill.getAsBoolean());
        paint.setTextSize(p[2]);paint.setShadowLayer(5f*p[3],0f,1.5f*p[3],FpsLabel.SHADOW);
        c.drawText(text,p[0],p[1],paint);
    }
}
