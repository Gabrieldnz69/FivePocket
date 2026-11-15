package dev.recompbootstrap.smoke;

/**
 * v714 (owner 2026-10-25: "that fps counter is not clean. it needs to be minimalistic and much smaller"; his pick: the shown number
 * alone, "60", or "30" without frame generation; SPEC 12.5 v2): the frame-rate label the app draws (FpsLabelView) from the library's
 * recomp_fps_v1. Pure Java: tests/FpsLabelTest.java.
 * Look: white at 78 % in a medium sans with a soft faint shadow (black 45 %, blur 5, 1.5 down, in 720p frame pixels), no box, no outline;
 * the text size SIZE of the HUD frame's height (cap height ~1.4 %). Place: the library label's corner (v704): x 0.075 of the shown width,
 * 8 frame pixels below the top of the shown part (in Fill the shown part is the whole screen; in Fit the 16:9 frame).
 */
final class FpsLabel {
    static final float SIZE=0.0195f,X=0.075f,TOP=8f/720f;
    static final int COLOR=0xC7FFFFFF,SHADOW=0x73000000;
    private FpsLabel(){}
    /** The label: the shown rate rounded ("60"), or "" when the counter is off (on != 1) or no window has closed yet. */
    static String text(int on,float shown){
        if(on!=1 || !(shown>0f) || Float.isInfinite(shown))return "";
        return Integer.toString(Math.round(shown));
    }
    /** {x, baseline y, text size, frame pixels per 720p pixel} for a w x h view; fill: the shown part is the whole view. */
    static float[] place(float w,float h,boolean fill){
        final float[] f=QolGesture.frame(w,h);final float k=f[3]/720f,size=SIZE*f[3];
        final float left=fill?0f:f[0],width=fill?w:f[2],top=fill?0f:f[1];
        return new float[]{left+Math.max(8f*k,X*width),top+TOP*f[3]+size*0.95f,size,k};
    }
}
