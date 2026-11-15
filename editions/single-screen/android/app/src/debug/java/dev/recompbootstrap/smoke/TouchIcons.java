package dev.recompbootstrap.smoke;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import java.util.HashMap;
import java.util.Map;

/**
 * Universal touch controls (SPEC 3.5, the GTA-mobile look, reference/NOTES.md): the icons as white silhouettes with a black outline,
 * our own vector shapes (IconShapes / icons.py); no image file from any game. Drawn like the host preview (TouchRender.swift): per
 * group the outlines, then the white, then the black details; held = the colours swapped.
 */
final class TouchIcons {
    private TouchIcons(){}
    static final float OUTLINE=1.5f; // the black outline, box units each side (24-unit box)
    static final float SHARE=0.70f;  // an icon's half size as a share of its disc's radius
    private static final Map<String,Path[][]> PATHS=new HashMap<>();
    private static final Paint FILL=new Paint(Paint.ANTI_ALIAS_FLAG),STROKE=new Paint(Paint.ANTI_ALIAS_FLAG);
    static {
        FILL.setStyle(Paint.Style.FILL);
        STROKE.setStyle(Paint.Style.STROKE);STROKE.setStrokeCap(Paint.Cap.ROUND);STROKE.setStrokeJoin(Paint.Join.ROUND);
    }
    private static Path[][] paths(String name,IconShapes.Layer[][] g){
        Path[][] p=PATHS.get(name);
        if(p!=null)return p;
        p=new Path[g.length][];
        for(int i=0;i<g.length;++i){
            p[i]=new Path[g[i].length];
            for(int j=0;j<g[i].length;++j){
                final Path q=new Path();q.setFillType(Path.FillType.EVEN_ODD);
                IconShapes.parse(g[i][j].path,new IconShapes.Sink(){
                    @Override public void move(float x,float y){q.moveTo(x,y);}
                    @Override public void line(float x,float y){q.lineTo(x,y);}
                    @Override public void cubic(float x1,float y1,float x2,float y2,float x,float y){q.cubicTo(x1,y1,x2,y2,x,y);}
                    @Override public void quad(float x1,float y1,float x,float y){q.quadTo(x1,y1,x,y);}
                    @Override public void close(){q.close();}
                    @Override public void oval(float l,float t,float r,float b){q.addOval(l,t,r,b,Path.Direction.CW);}
                    @Override public void rect(float l,float t,float r,float b){q.addRect(l,t,r,b,Path.Direction.CW);}
                });
                p[i][j]=q;
            }
        }
        PATHS.put(name,p);return p;
    }
    private static int argb(float a,int rgb){return (Math.round(255*Math.max(0f,Math.min(1f,a)))<<24)|rgb;}
    /** Draws the icon named name centred at (cx, cy), its 24-unit box scaled to 2 * half px; false when there is no such icon. */
    static boolean draw(Canvas c,String name,float cx,float cy,float half,float alpha,boolean held){return draw(c,name,cx,cy,half,alpha,held,held?0x000000:0xFFFFFF);}
    /** As draw(), the glyph in rgb (the WEAPON button: the game's grey). */
    static boolean draw(Canvas c,String name,float cx,float cy,float half,float alpha,boolean held,int rgb){return draw(c,name,cx,cy,half,alpha,held,rgb,OUTLINE);}
    /** As draw(), with the dark edge `outline` box units each side (the WEAPON icon: 0.6, GTA IV's thin edge). */
    static boolean draw(Canvas c,String name,float cx,float cy,float half,float alpha,boolean held,int rgb,float outline){
        final IconShapes.Layer[][] g=IconShapes.icons().get(name);
        if(g==null)return false;
        final Path[][] p=paths(name,g);
        final int fg=argb(alpha,rgb),edge=argb(alpha*0.85f,held?0xFFFFFF:0x000000),detail=argb(alpha,held?0xFFFFFF:0x000000);
        c.save();c.translate(cx,cy);c.scale(half/12f,half/12f);c.translate(-12f,-12f);
        for(int i=0;i<g.length;++i){
            STROKE.setColor(edge);
            for(int j=0;j<g[i].length;++j){final char t=g[i][j].type;
                if(t=='F' || t=='S'){STROKE.setStrokeWidth(t=='F'?2*outline:g[i][j].width+2*outline);c.drawPath(p[i][j],STROKE);}}
            FILL.setColor(fg);STROKE.setColor(fg);
            for(int j=0;j<g[i].length;++j){final char t=g[i][j].type;
                if(t=='F')c.drawPath(p[i][j],FILL);
                else if(t=='S'){STROKE.setStrokeWidth(g[i][j].width);c.drawPath(p[i][j],STROKE);}}
            FILL.setColor(detail);STROKE.setColor(detail);
            for(int j=0;j<g[i].length;++j){final char t=g[i][j].type;
                if(t=='B')c.drawPath(p[i][j],FILL);
                else if(t=='K'){STROKE.setStrokeWidth(g[i][j].width);c.drawPath(p[i][j],STROKE);}}
        }
        c.restore();return true;
    }
}
