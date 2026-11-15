package dev.recompbootstrap.smoke;

import java.util.Locale;

/**
 * qol-fill (owner 2026-10-25: "I know the fold still does not have a fill screen option"; Settings > Display > Screen, settings/screen.txt
 * "fill"): the geometry of the Fill screen. The game's SurfaceView is laid out as a 16:9 picture that covers the screen, centred, so the
 * system crops what lies past the screen's edges: the world fills the screen and keeps its shape (the Fold's inner screen: about a third
 * of the width cut, half each side). Its buffer stays the size of Fit's picture (the presenter draws the same pixels as in Fit; the
 * display scales the layer), or with files/private-trace/qol-fill-native the layout's own size (one buffer pixel per screen pixel). The
 * library (v712 qol_hud_fit.h) reads the visible fraction from files/private-trace/qol-screen-visible and shrinks the HUD by the smaller
 * one about the centre (the iPad's ios-hud-fit), so the HUD lands where Fit puts it: QolGesture.frame() of the screen stays the HUD's
 * frame and every touch zone keeps its place. Pure Java: tests/ScreenFillTest.java runs it on the host.
 */
final class ScreenFill {
    static final String SETTING="settings/screen.txt",VISIBLE="private-trace/qol-screen-visible",NATIVE="private-trace/qol-fill-native";
    private ScreenFill(){}

    /** Fill was chosen: the setting file's word is "fill" (anything else, or no file, is Fit). */
    static boolean wanted(byte[] setting){
        return setting!=null && new String(setting,java.nio.charset.StandardCharsets.US_ASCII).trim().equals("fill");
    }
    /** The SurfaceView's size for a w x h screen: {16 n, 9 n}, the smallest whole 16:9 that covers it (Fit: the screen itself). */
    static int[] layout(int w,int h,boolean fill){
        if(!fill || w<1 || h<1)return new int[]{w,h};
        final int n=Math.max((w+15)/16,(h+8)/9);
        return new int[]{16*n,9*n};
    }
    /** The buffer: Fit's picture, {16 m, 9 m} with the largest m inside the screen; with nativeSize the layout's own size. */
    static int[] buffer(int w,int h,boolean nativeSize){
        if(nativeSize)return layout(w,h,true);
        final int m=Math.max(1,Math.min(w/16,h/9));
        return new int[]{16*m,9*m};
    }
    /** The visible fraction {vx, vy} of the picture on a w x h screen (1 1 in Fit). */
    static float[] visible(int w,int h,boolean fill){
        final int[] l=layout(w,h,fill);
        if(!fill || l[0]<1 || l[1]<1)return new float[]{1f,1f};
        return new float[]{Math.min(1f,w/(float)l[0]),Math.min(1f,h/(float)l[1])};
    }
    /** The line the library reads (qol_hud_fit.h parseVisible: "vx vy"). */
    static String visibleText(float[] v){return String.format(Locale.ROOT,"%.4f %.4f\n",v[0],v[1]);}
}
