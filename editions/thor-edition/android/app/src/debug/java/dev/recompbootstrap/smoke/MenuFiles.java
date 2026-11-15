package dev.recompbootstrap.smoke;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * Update 1 QoL (owner's goal 2026-11-07): the side menu's small files in the app's files folder: where each menu was left
 * (side-menu-main.txt, side-menu-lower.txt: SideMenu.place) and the trainer's favourites (trainer-favourites.txt: Favourites.save).
 * Written whole through a temporary file and a rename, so a cut never leaves half a file; a missing or unreadable file reads as null.
 * The same file in the Thor and the Fold trees.
 */
final class MenuFiles {
    static final String MAIN="side-menu-main.txt",LOWER="side-menu-lower.txt",FAVOURITES="trainer-favourites.txt";
    static String read(File dir,String name){
        try{final File f=new File(dir,name);if(!f.isFile() || f.length()>64*1024)return null;
            return new String(Files.readAllBytes(f.toPath()),StandardCharsets.UTF_8);}
        catch(IOException | RuntimeException none){return null;}
    }
    /** settings/menu.txt from the hold-Select value, the file's other words kept (MenuWords); no words left: no file. */
    static boolean menuWords(File dir,int holdSelect){
        final String cur=read(dir,MenuWords.FILE);
        final String w=MenuWords.holdWords(holdSelect,cur==null?null:cur.getBytes(StandardCharsets.US_ASCII));
        if(w==null){final File f=new File(dir,MenuWords.FILE);return !f.exists() || f.delete();}
        return write(dir,MenuWords.FILE,w+"\n");
    }
    /** settings/quick-menu.txt with the style word, the file's other words kept. */
    static boolean quickMenu(File dir,int style){
        final String cur=read(dir,MenuWords.QUICK_FILE);
        return write(dir,MenuWords.QUICK_FILE,MenuWords.styleWords(style,cur==null?null:cur.getBytes(StandardCharsets.US_ASCII))+"\n");
    }
    static boolean write(File dir,String name,String text){
        final File tmp=new File(dir,name+".tmp"),f=new File(dir,name);
        try(FileOutputStream out=new FileOutputStream(tmp)){out.write(text.getBytes(StandardCharsets.UTF_8));out.getFD().sync();}
        catch(IOException e){return false;}
        return tmp.renameTo(f);
    }
}
