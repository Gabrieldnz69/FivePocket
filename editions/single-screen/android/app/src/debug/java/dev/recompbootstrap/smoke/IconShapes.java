package dev.recompbootstrap.smoke;

import java.util.HashMap;
import java.util.Map;

/**
 * Universal touch controls (SPEC 3.5, the GTA-mobile look): the icons' shapes, plain Java (the host test reads them). The source is
 * local-analysis/touch-controls-spec/icons/icons.py -> icons.txt (shared with iOS and the host preview, TouchRender.swift) ->
 * TouchIconData.java (generated). An icon is groups of layers in a 24 x 24 box: F = white fill, S<w> = white stroke of width w (both
 * outlined in black), B = black fill, K<w> = black stroke (details). Paths: M L C Q Z (absolute), O cx cy r, E cx cy rx ry, R x y w h.
 */
final class IconShapes {
    private IconShapes(){}
    /** A path's commands, in the box's units. */
    interface Sink {
        void move(float x,float y);void line(float x,float y);void cubic(float x1,float y1,float x2,float y2,float x,float y);
        void quad(float x1,float y1,float x,float y);void close();void oval(float l,float t,float r,float b);void rect(float l,float t,float r,float b);
    }
    static final class Layer {
        final char type;final float width;final String path;
        Layer(char type,float width,String path){this.type=type;this.width=width;this.path=path;}
    }
    private static Map<String,Layer[][]> icons;
    /** Every icon by name (parsed once). */
    static synchronized Map<String,Layer[][]> icons(){
        if(icons!=null)return icons;
        final Map<String,Layer[][]> m=new HashMap<>();
        for(String[] e:TouchIconData.ICONS){
            final String[] groups=e[1].split("\\|\\|");final Layer[][] g=new Layer[groups.length][];
            for(int i=0;i<groups.length;++i){
                final String[] layers=groups[i].split(";");g[i]=new Layer[layers.length];
                for(int j=0;j<layers.length;++j){
                    final int k=layers[j].indexOf(':');final String head=layers[j].substring(0,k);
                    g[i][j]=new Layer(head.charAt(0),head.length()>1?Float.parseFloat(head.substring(1)):0f,layers[j].substring(k+1));
                }
            }
            m.put(e[0],g);
        }
        return icons=m;
    }
    /** Feeds a path's commands to k; an unknown command or a missing number throws IllegalArgumentException. */
    static void parse(String path,Sink k){
        final String[] t=path.trim().split("\\s+");
        final float[] v=new float[6];int i=0;
        while(i<t.length){
            final String c=t[i++];
            final int n;
            switch(c){case "M":case "L":n=2;break;case "C":n=6;break;case "Q":case "R":case "E":n=4;break;case "O":n=3;break;case "Z":n=0;break;
                default:throw new IllegalArgumentException("command "+c);}
            if(i+n>t.length)throw new IllegalArgumentException("numbers for "+c);
            for(int j=0;j<n;++j)v[j]=Float.parseFloat(t[i++]);
            switch(c){
                case "M":k.move(v[0],v[1]);break;
                case "L":k.line(v[0],v[1]);break;
                case "C":k.cubic(v[0],v[1],v[2],v[3],v[4],v[5]);break;
                case "Q":k.quad(v[0],v[1],v[2],v[3]);break;
                case "Z":k.close();break;
                case "O":k.oval(v[0]-v[2],v[1]-v[2],v[0]+v[2],v[1]+v[2]);break;
                case "E":k.oval(v[0]-v[2],v[1]-v[3],v[0]+v[2],v[1]+v[3]);break;
                default:k.rect(v[0],v[1],v[0]+v[2],v[1]+v[3]);break;
            }
        }
    }
}
