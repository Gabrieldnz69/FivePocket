package dev.recompbootstrap.smoke;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.view.View;

/**
 * Thor edition (thor-radio v832; owner 2026-10-25: "create a button like the quick save ... the mod menu could be something like the lester
 * hack icon"; then "I want the EXACT image of lester made into the icon"): the lower screen's trainer button, beside the library's Quick
 * Save (WheelGesture.trainerBox), shown while the Trainer switch is on and the library shows its button row (recomp_lower_buttons_v1 bit 0:
 * the HUD map, no wheel / phone / pause screen). Its icon: assets/owner/trainer-icon.png (the owner's own picture; OWNER BUILDS ONLY), else
 * a dotted skull and crossbones drawn here (DOTS). A tap opens the lower screen's trainer (SecondScreen.LowerTouch). Takes no touches itself.
 */
final class TrainerButtonView extends View {
    static final String[] DOTS={"...#######...","..#########..",".###########.",".##...#...##.",".##...#...##.",".###########.","..####.####..",
            "...#######...","...#.#.#.#...","#...........#",".##.......##.","...##...##...",".##.......##.","#...........#"};
    private final ThorMenus menus;private final Paint fill=new Paint(Paint.ANTI_ALIAS_FLAG),img=new Paint(Paint.FILTER_BITMAP_FLAG|Paint.ANTI_ALIAS_FLAG);
    private final Handler handler=new Handler(Looper.getMainLooper());private final RectF r=new RectF();
    private Bitmap icon;private boolean shown;
    private final Runnable poll=new Runnable(){@Override public void run(){
        final boolean s=menus.lowerAllowed() && !menus.lower.isOpen() && PrivateTraceActivity.lowerButtons()>0 && (PrivateTraceActivity.lowerButtons()&1)!=0;
        if(s!=shown){shown=s;invalidate();}
        handler.postDelayed(this,250);
    }};
    TrainerButtonView(Context c,ThorMenus menus){
        super(c);this.menus=menus;setClickable(false);setFocusable(false);setWillNotDraw(false);
        try(java.io.InputStream in=c.getAssets().open("owner/trainer-icon.png")){icon=BitmapFactory.decodeStream(in);}catch(java.io.IOException | RuntimeException none){icon=null;}
    }
    boolean hit(float x,float y,float w,float h){
        if(!shown)return false;
        final float[] b=WheelGesture.trainerBox(w,h);if(b==null)return false;
        final float p=Math.round(WheelGesture.SAVE_PAD_1080*Math.min(1f,h/1080f));
        return x>=b[0]-p && x<b[2]+p && y>=b[1]-p && y<b[3]+p;
    }
    @Override public boolean onTouchEvent(android.view.MotionEvent e){return false;}
    @Override protected void onAttachedToWindow(){super.onAttachedToWindow();handler.post(poll);}
    @Override protected void onDetachedFromWindow(){handler.removeCallbacks(poll);super.onDetachedFromWindow();}
    @Override protected void onDraw(Canvas c){
        if(!shown)return;
        final float[] b=WheelGesture.trainerBox(getWidth(),getHeight());if(b==null)return;
        final float k=Math.min(1f,getHeight()/1080f);
        r.set(b[0],b[1],b[2],b[3]);fill.setColor(0xC7000000);c.drawRoundRect(r,10f*k,10f*k,fill); // (quick_save.h: black 0.78, radius 10)
        final float side=72f*k,x0=(b[0]+b[2]-side)*0.5f,y0=(b[1]+b[3]-side)*0.5f;
        if(icon!=null){r.set(x0,y0,x0+side,y0+side);c.drawBitmap(icon,null,r,img);return;}
        final float cell=side/14f,dot=cell*0.74f; // the dotted skull (public builds)
        for(int j=0;j<DOTS.length;++j)for(int i=0;i<DOTS[j].length();++i)if(DOTS[j].charAt(i)=='#'){
            fill.setColor(0xFFFFFFFF);final float x=x0+(side-13f*cell)*0.5f+i*cell+(cell-dot)*0.5f,y=y0+j*cell+(cell-dot)*0.5f;c.drawRect(x,y,x+dot,y+dot,fill);}
        fill.setColor(0xFFEB1F1F);
        for(float col:new float[]{4.5f,8.5f}){final float cx=x0+(side-13f*cell)*0.5f+col*cell,cy=y0+4f*cell;c.drawRect(cx-dot*0.6f,cy-dot*0.6f,cx+dot*0.6f,cy+dot*0.6f,fill);}
    }
}
