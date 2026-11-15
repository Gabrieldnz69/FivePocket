package dev.recompbootstrap.smoke;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;

/**
 * hud2 (2026-10-22): whether the game window shows the HUD (minimap, wanted stars) on a second display, and which display.
 * Plain Java (no Android types), so the host test runs it:
 *   javac -d out android/app/src/debug/java/dev/recompbootstrap/smoke/SecondScreenOption.java android/tests/SecondScreenOptionTest.java
 *   java -cp out dev.recompbootstrap.smoke.SecondScreenOptionTest
 *
 * Thor edition: files/settings/minimap.txt (the Minimap row: both | top | bottom) decides first when it holds one of its words
 * ("top": no lower-screen HUD). Otherwise files/settings/second-screen.txt: its first word "hud" (letters in any case, blanks around
 * it ignored) turns the second screen on; empty, any other first word, or a file of 128 bytes or more = off (the game window is
 * exactly as before); ABSENT = on in the Thor edition (SecondScreenWord). Further words are the library's options (inset=, map=0, stars=0: hud_screen_layout.h) and are not read here.
 */
final class SecondScreenOption {
    static final String FILE="settings/second-screen.txt";
    static final String MINIMAP_FILE=RuntimeMapping.MINIMAP_TXT; // Thor edition: the Minimap row's word file (SecondScreenWord)
    static final int CAP=SecondScreenWord.CAP;
    private SecondScreenOption() {}

    static boolean blank(int c){return SecondScreenWord.blank(c);}

    /** The rule above, on the file's first {@code length} bytes. */
    static boolean wantedText(byte[] text,int length){return SecondScreenWord.hud(text,length);}

    /** Reads files/settings/minimap.txt and second-screen.txt once (SecondScreenWord.lowerScreenOn); any read error = absent. */
    static boolean wanted(File filesDir){
        return SecondScreenWord.lowerScreenOn(read(new File(filesDir,MINIMAP_FILE)),read(new File(filesDir,FILE)));
    }
    /** v820: whether files/settings/lower-screen.txt says "off" (the Lower Screen row: a dark lower screen); absent / unreadable = no. */
    static boolean off(File filesDir){return SecondScreenWord.lowerOff(read(new File(filesDir,RuntimeMapping.LOWER_SCREEN_TXT)));}
    /** A file's first bytes (at most CAP; a file of CAP bytes or more comes back CAP long, which both rules refuse); null when absent. */
    static byte[] read(File file){
        if(!file.isFile())return null;
        byte[] text=new byte[CAP];int length=0;
        try(FileInputStream in=new FileInputStream(file)){
            for(int n;length<CAP && (n=in.read(text,length,CAP-length))>0;)length+=n;
        }catch(IOException error){return null;}
        return java.util.Arrays.copyOf(text,length);
    }

    /**
     * The display for the HUD among the system's displays (ids with whether DisplayManager lists them as presentation
     * displays and whether they are on): the first presentation display that is on and not the default one; else the first
     * other display that is on; else -1 (no second screen: nothing is opened).
     */
    static int choose(int[] ids,boolean[] presentation,boolean[] on,int defaultId){
        return SecondScreenWord.choose(ids,presentation,on,defaultId); // (qol20: the rule moved to main; the launcher's lower screen uses it too)
    }
}
