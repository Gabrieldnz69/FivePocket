package dev.recompbootstrap.smoke;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.view.PixelCopy;
import android.view.SurfaceView;
import android.view.View;

/**
 * menu-redesign (2026-10-25, local-analysis/menu-redesign/DESIGN.md; the same file in the Thor and the one-screen trees): draws a
 * SideMenu - the in-game menu or the Thor's lower-screen trainer - with the launcher's own painter (UiKit.Painter: Nunito Sans, the
 * game's pause-menu frame built by MenuScenes). Takes no touches itself (QolTouch / MenuRouter route them to the model).
 *
 * Behind the main screen's menu: a blurred copy of the game's picture, taken once when the menu opens (MenuBackdrop: PixelCopy of the
 * game's SurfaceView) and saved for the launcher. The restart ALERT: the owner's capture of the game's alert where the build carries it
 * (owner builds: assets side-menu/alert-restart.png composed, or owner/alert-screen.png composed here); else the drawn notice
 * (MenuScenes.restartAlert: public builds carry no game art).
 */
final class SideMenuView extends View {
    private final SideMenu menu;
    private final UiKit.Painter painter;
    private SurfaceView source;
    private Bitmap backdrop;
    private boolean capturing,captured;
    private final Paint fill=new Paint(Paint.ANTI_ALIAS_FLAG),text=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
    private final RectF r=new RectF();

    SideMenuView(Context context,SideMenu menu){
        super(context);this.menu=menu;painter=new UiKit.Painter(context);menu.measure(painter);
        setClickable(false);setFocusable(false);setWillNotDraw(false);
    }
    /** The game's picture to blur behind the main screen's menu (null: the plain backdrop; the lower screen dims its HUD instead). */
    void source(SurfaceView game){source=game;}
    @Override public boolean onTouchEvent(android.view.MotionEvent e){return false;}
    @Override protected void onSizeChanged(int w,int h,int ow,int oh){menu.view(w,h);menu.density(getResources().getDisplayMetrics().xdpi);}

    @Override protected void onDraw(Canvas c){
        if(!menu.isOpen()){captured=false;return;} // (the next opening takes a new picture)
        if(!captured && !capturing && source!=null && !menu.lower())capture();
        if(SideMenu.live(menu.page()))postInvalidateDelayed(250); // (the trainer's state, the Online results, as the library applies them)
        final int w=getWidth(),h=getHeight();
        if(menu.alertShown() && drawOwnerAlert(c,w,h))return;
        menu.gamepad(UiKit.controllerName()!=null || !UiKit.touchScreen(getContext())); // (the strip, or the icons: owner 2026-10-25)
        final Scene scene=menu.scene();
        final MenuLayout.Frame f=menu.alertShown()?MenuLayout.DEFAULT:menu.frame();
        final float u=menu.alertShown()?Math.min(w/(float)MenuLayout.W,h/(float)MenuLayout.H):menu.unit();
        painter.draw(c,scene,w,h,f.w,f.h,u,backdrop,1f);
    }

    /** One small copy of the game's picture, blurred (MenuBackdrop), drawn behind the menu and saved for the launcher. */
    private void capture(){
        final Bitmap small=Bitmap.createBitmap(MenuBackdrop.W,MenuBackdrop.H,Bitmap.Config.ARGB_8888);
        capturing=true;
        try{
            PixelCopy.request(source,small,result->{
                capturing=false;captured=true;
                if(result!=PixelCopy.SUCCESS)return; // (the plain backdrop stays)
                backdrop=MenuBackdrop.blurred(small);
                MenuBackdrop.save(getContext().getFilesDir(),backdrop);
                invalidate();
            },new Handler(Looper.getMainLooper()));
        }catch(IllegalArgumentException | IllegalStateException unavailable){capturing=false;captured=true;}
    }

    // ------------------------------------------------------------------------------------------------ the owner's ALERT picture
    // owner 2026-10-25: "the alert screen needs to be this exact screen no changes except black out that x button prompt ... you only
    // have to add the text"; layout A (the message between the two rules). Debug assets only (thor-builder keeps them out of public APKs).
    static final float[] ALERT_PROMPT={1730f,1014f,1890f,1070f};       // (source pixels: "Continue" and the X mark)
    static final float ALERT_RULE_X0=885f,ALERT_RULE_X1=1035f,ALERT_RULE_Y=612f,ALERT_RULE_H=3f,ALERT_RULE_DY=47f,ALERT_TEXT_PX=30f;
    private Bitmap composed,raw;private boolean tried;
    private void loadAlert(){
        if(tried)return;
        tried=true;
        try(java.io.InputStream in=getContext().getAssets().open("side-menu/alert-restart.png")){composed=BitmapFactory.decodeStream(in);}
        catch(java.io.IOException | RuntimeException none){composed=null;}
        if(composed!=null)return;
        try(java.io.InputStream in=getContext().getAssets().open("owner/alert-screen.png")){raw=BitmapFactory.decodeStream(in);}
        catch(java.io.IOException | RuntimeException none){raw=null;}
    }
    /** Draws the owner's ALERT picture, fitted on black; false when the build has none. */
    private boolean drawOwnerAlert(Canvas c,int w,int h){
        loadAlert();
        final Bitmap img=composed!=null?composed:raw;
        if(img==null || img.getWidth()<=0 || img.getHeight()<=0)return false;
        final float s=Math.min(w/(float)img.getWidth(),h/(float)img.getHeight()),ox=(w-img.getWidth()*s)*0.5f,oy=(h-img.getHeight()*s)*0.5f;
        fill.setColor(0xFF000000);c.drawRect(0,0,w,h,fill);
        r.set(ox,oy,ox+img.getWidth()*s,oy+img.getHeight()*s);c.drawBitmap(img,null,r,text);
        if(img==composed)return true;
        // the raw capture: its prompt blacked out, the lower rule moved down, the message between the rules
        c.drawRect(ox+ALERT_PROMPT[0]*s,oy+ALERT_PROMPT[1]*s,ox+ALERT_PROMPT[2]*s,oy+ALERT_PROMPT[3]*s,fill);
        c.drawRect(ox+(ALERT_RULE_X0-4f)*s,oy+(ALERT_RULE_Y-3f)*s,ox+(ALERT_RULE_X1+4f)*s,oy+(ALERT_RULE_Y+ALERT_RULE_H+3f)*s,fill);
        fill.setColor(0xFFFFFFFF);
        c.drawRect(ox+ALERT_RULE_X0*s,oy+(ALERT_RULE_Y+ALERT_RULE_DY)*s,ox+ALERT_RULE_X1*s,oy+(ALERT_RULE_Y+ALERT_RULE_DY+ALERT_RULE_H)*s,fill);
        text.setTypeface(UiKit.Faces.get(getContext()).of(Scene.REGULAR));
        text.setTextSize(ALERT_TEXT_PX*s);text.setColor(0xFFFFFFFF);text.setTextAlign(Paint.Align.CENTER);
        c.drawText(SideMenu.ALERT_TEXT,ox+960f*s,oy+(586f+26f+ALERT_TEXT_PX*0.72f)*s,text);
        return true;
    }
}
