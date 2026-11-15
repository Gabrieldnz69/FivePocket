package dev.recompbootstrap.smoke;

import java.util.ArrayList;
import java.util.List;

/**
 * Update 1 QoL (owner's goal 2026-11-07, item 2): the trainer's favourites. Any trainer item is pinned or unpinned by holding A on it or
 * by touching and holding it (SideMenu HOLD); the pinned items are listed first, in the Trainer tab's Favourites section, in the order
 * they were pinned, as the same rows (same ids: they act exactly as on their own page). Saved in files/trainer-favourites.txt (one id
 * per line) by the game window. The same file in the Thor and the Fold trees. tests/SideMenuQolTest.java.
 */
final class Favourites {
    static final int MAX=40;
    static final String TEXT_EMPTY="Hold A on a trainer item, or touch and hold it, to pin it here.";
    static final String TEXT_PINNED="Done: pinned to Favourites.",TEXT_UNPINNED="Done: removed from Favourites.";
    /** The pages whose rows can be pinned (their rows, in this order, are where the pinned rows are found). */
    static final SideMenu.Page[] SOURCES={SideMenu.Page.TR_PLAYER,SideMenu.Page.TR_VEHICLES,SideMenu.Page.TR_TELEPORT,SideMenu.Page.TR_WORLD};

    private final List<String> ids=new ArrayList<>();
    boolean changed;   // (the game window saves when set, then clears it)

    /** A trainer item that can be pinned: every row of the trainer's pages except the Trainer switch. */
    static boolean pinnable(String id){
        if(id==null)return false;
        return id.equals("tr:heal") || id.equals("tr:weapons") || id.equals("tr:fix") || id.equals("tr:waypoint") || id.equals("tr:clearwx")
                || id.equals("tr:wanted") || id.equals("tr:time") || id.matches("tr:t[0-9]+")
                || id.matches("(veh|lm|wx|chr):[0-9]+");
    }
    boolean contains(String id){return ids.contains(id);}
    List<String> ids(){return new ArrayList<>(ids);}
    /** Pins or unpins: true when it is pinned now. */
    boolean toggle(String id){
        if(!pinnable(id))return false;
        changed=true;
        if(ids.remove(id))return false;
        if(ids.size()>=MAX)ids.remove(0);
        ids.add(id);
        return true;
    }
    String save(){final StringBuilder b=new StringBuilder();for(String id:ids)b.append(id).append('\n');return b.toString();}
    void load(String text){
        ids.clear();
        if(text==null)return;
        for(String line:text.split("\n")){final String id=line.trim();if(pinnable(id) && !ids.contains(id) && ids.size()<MAX)ids.add(id);}
    }
    /** The pinned rows as their pages build them now, in pin order; a pin whose row is not there now (another build, Story Mode for a
     *  character) is skipped and kept. */
    List<SideMenu.Row> rows(SideMenu.Rows source){
        final List<SideMenu.Row> all=new ArrayList<>();
        for(SideMenu.Page p:SOURCES)all.addAll(source.rows(p));
        final List<SideMenu.Row> out=new ArrayList<>();
        for(String id:ids)for(SideMenu.Row r:all)if(r.id.equals(id)){out.add(r);break;}
        return out;
    }
}
