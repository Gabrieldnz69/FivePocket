package dev.recompbootstrap.smoke;

import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;

/**
 * Thor edition (thor-radio v826): the game window's two menus over one set of rows. The main screen's side menu (left-edge swipe, the
 * touchscreen's Back; SPEC 12 ported from the one-screen edition's v21) and the lower screen's trainer (a two-finger swipe there; root
 * TRAINER, the whole width). Both share SideMenuRows (settings, capabilities, the trainer through PrivateTraceActivity's JNI) and one
 * SideMenuApply. Main thread only. Logs: tag RecompThorMenu.
 */
final class ThorMenus {
    static final String TAG="RecompThorMenu";
    final SideMenuRows rows=new SideMenuRows();
    final SideMenu main=new SideMenu(rows),lower=new SideMenu(rows,true,false); // (menu-redesign: the lower screen's trainer, compact frame)
    SideMenuApply apply;
    TrainerSound sound; // (v832: preloaded with the game window; null before)
    Haptics haptics;    // (Update 1 haptics: the game window's; null before, and in a window without a game)
    private static ThorMenus current;
    static ThorMenus current(){return current;}
    private final File files;
    android.app.Activity activity; // (Update 1 Diagnostics: the game window, for the clipboard; set by PrivateTraceActivity)
    ThorMenus(File files,SettingsStore store){
        current=this;this.files=files;
        // Update 1 QoL: the favourites and where each menu was left, from the last session
        rows.favourites.load(MenuFiles.read(files,MenuFiles.FAVOURITES));
        rows.loadMenuWords(MenuFiles.read(files,MenuWords.FILE),MenuFiles.read(files,MenuWords.QUICK_FILE)); // (Update 1: Hold Select, Quick Menu Style)
        main.restore(MenuFiles.read(files,MenuFiles.MAIN));lower.restore(MenuFiles.read(files,MenuFiles.LOWER));
        rows.trainer=new SideMenuRows.Trainer(){
            public boolean present(){return PrivateTraceActivity.trainerState()!=-100;}
            public int state(){final int s=PrivateTraceActivity.trainerState();return s==-100?0:s;}
            public int count(int list){return Math.max(0,PrivateTraceActivity.trainerList(list,-1,null));}
            public String label(int list,int i){final String[] out=new String[1];PrivateTraceActivity.trainerList(list,i,out);return out[0];}
            public String status(){final String s=PrivateTraceActivity.trainerStatus();return s==null?"":s;}
            public String mods(){final String s=PrivateTraceActivity.modsStatus();return s==null?"":s;}
        };
        apply=new SideMenuApply(rows,new SideMenuApply.Effects(){
            public boolean save(Settings s){
                final SettingsStore.SaveResult r=store.save(s,rows.capabilities);
                Log.i(TAG,"SIDE_MENU save ok="+r.ok+" written="+r.written+" removed="+r.removed+(r.error!=null?" error="+r.error:""));
                return r.ok;
            }
            public void trainerEnabled(boolean on){
                try(FileOutputStream out=new FileOutputStream(new File(files,"settings/trainer.txt"))){
                    out.write((on?"trainer=on\n":"trainer=off\n").getBytes(java.nio.charset.StandardCharsets.US_ASCII));}
                catch(java.io.IOException e){Log.w(TAG,"SIDE_MENU trainer.txt: "+e);}
                Log.i(TAG,"SIDE_MENU trainer="+(on?"on":"off")+" sync="+PrivateTraceActivity.trainerRequest(SideMenuApply.SYNC,0,0,0));
                if(!on && lower.isOpen())lower.close();
            }
            public int trainer(int action,int a0,int a1){
                final int r=PrivateTraceActivity.trainerRequest(action,a0,a1,0);Log.i(TAG,"SIDE_MENU trainer action="+action+" a0="+a0+" a1="+a1+" -> "+r);return r;}
            public void hapticsChanged(int mode){if(haptics!=null)haptics.setMode(mode);}
            public void menuWords(int hold){Log.i(TAG,"SIDE_MENU menu.txt hold="+hold+" written="+MenuFiles.menuWords(files,hold));}
            public void copyDiagnostics(java.util.function.Consumer<String> done){ // (Update 1: off the UI thread, minimum priority)
                final android.app.Activity a=activity;
                if(a==null){done.accept(Diagnostics.failed("no game window"));return;}
                final Thread t=new Thread(()->{
                    String text;try{text=DiagnosticsAndroid.text(a);}catch(RuntimeException|LinkageError e){text=null;}
                    final String report=text;
                    a.runOnUiThread(()->{
                        done.accept(report==null?Diagnostics.failed("the report could not be made"):DiagnosticsAndroid.deliver(a,report,false));
                        Log.i(TAG,"SIDE_MENU diagnostics "+(report==null?"failed":"copied chars="+report.length()));
                        if(changed!=null)changed.run();});
                },"diagnostics");
                t.setPriority(Thread.MIN_PRIORITY);t.start();
            }
            public int gameInteractionMenu(){final int r=PrivateTraceActivity.imenu(5);Log.i(TAG,"SIDE_MENU game interaction menu -> "+r);return r;}
            public void quickMenuStyle(int style){final boolean w=MenuFiles.quickMenu(files,style);
                Log.i(TAG,"SIDE_MENU quick-menu.txt style="+style+" written="+w+" imenu="+PrivateTraceActivity.imenu(MenuWords.imenuOp(style)));}
        });
    }
    /** The trainer may open on the lower screen: the build has it and the side menu's Trainer switch is on (state bit 16). */
    boolean lowerAllowed(){return rows.trainer.present() && (rows.trainer.state()&SideMenuRows.STATE_ENABLED)!=0;}
    /** The events of either menu, applied (and logged). */
    void drain(SideMenu m){
        for(SideMenu.Event e:m.take()){
            Log.i(TAG,(m==lower?"LOWER_TRAINER ":"SIDE_MENU ")+e);apply.apply(e);
            if(m==lower)apply.stayOpen(); // (Update 1 QoL: the lower screen keeps its trainer open; the game is in view above)
            if(e.type==SideMenu.Event.Type.CLOSED)MenuFiles.write(files,m==lower?MenuFiles.LOWER:MenuFiles.MAIN,m.place()); // (where it was left)
        }
        if(rows.favourites.changed){rows.favourites.changed=false;MenuFiles.write(files,MenuFiles.FAVOURITES,rows.favourites.save());}
        watchOnline();
    }
    // Update 1 (owner 2026-11-07): a closing Online request (Character, Places, Activities, Save Look ...) waits for its result; once the
    // library reports it done the side menu closes (SideMenuApply.poll), then the game window's own handler drains and redraws.
    Runnable changed; // (the game window's menuChanged; null before it is set: drained here)
    private final android.os.Handler handler=new android.os.Handler(android.os.Looper.getMainLooper());
    private boolean polling;
    private final Runnable poll=new Runnable(){public void run(){
        polling=false;
        if(apply.poll(main)){Log.i(TAG,"SIDE_MENU online request done: menu closed");if(changed!=null)changed.run();else drain(main);return;}
        watchOnline();
    }};
    /** Update 1 QoL: a quick-strip slot's event (QuickStripView): applied as the side menu's own, nothing to close, results watched. */
    void quick(SideMenu.Event e){Log.i(TAG,"QUICK_STRIP "+e);apply.apply(e);apply.stayOpen();watchOnline();}
    private void watchOnline(){if(!polling && apply.waiting()){polling=true;handler.postDelayed(poll,100);}}
    void close(){handler.removeCallbacks(poll);polling=false;if(sound!=null){sound.release();sound=null;}if(current==this)current=null;}
    // v832: the last lower-screen touch (uptime ms); a touchscreen Back within LOWER_BACK_MS of it came from the lower display
    static final long LOWER_BACK_MS=1500;
    private long lowerTouchAt=-LOWER_BACK_MS-1;
    void lowerTouched(long t){lowerTouchAt=t;}
    boolean backFromLower(long now){return now-lowerTouchAt>=0 && now-lowerTouchAt<=LOWER_BACK_MS;}
}
