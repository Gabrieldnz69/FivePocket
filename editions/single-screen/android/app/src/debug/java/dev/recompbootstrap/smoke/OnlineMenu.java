package dev.recompbootstrap.smoke;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Update 1 (Sandbox online content, lead 2026-11-06): the side menu's Online tab: Character, Creator, Places, Activities. Pure Java,
 * the same file in the Thor and the Fold trees; SideMenuRows builds its rows, SideMenuApply sends what it returns. tests/OnlineMenuTest.
 *
 * The library's words (game code v963; trainer.h, character_creator.h, online_world.h, activities.h; ALLOCATION sections 10-11):
 *   15 OnlineCharacter a0 = 0 male, 1 female, 2 back to the story character;
 *   16 OnlinePlace a0 = PlaceList index (the 24 names below: the library lists them in the same order, list 6 is not built);
 *   17 CharacterLook a0 = 0 the saved look (the starting look without one), 1 save the edits, 2 discard them, 3 start editing;
 *   18 CharacterEdit a0 = field << 16 | value (0xFFFF = none for a prop);
 *   19 Activity a0 = list 8 index, a1 = 0 start, 1 stop.
 * Shown only while the library reports Sandbox (state bit 21); the creator only while an online character is the player (bit 20).
 * Face shape and parent fields (1..7) are never sent: they wait for the C2 freeze fix. The library has no read-back of the look and
 * no list 7 (limits) yet, so the menu keeps its own values from the starting look and browses the static ranges
 * (character_creator.h inRange); a number the character does not have is refused by the library and said in words.
 */
final class OnlineMenu {
    static final int ONLINE_CHARACTER=15,ONLINE_PLACE=16,CHARACTER_LOOK=17,CHARACTER_EDIT=18,ACTIVITY=19;
    static final int STATE_ONLINE_CHARACTER=1<<20,STATE_SANDBOX=1<<21;
    static final int LIST_ACTIVITIES=8;
    /** Add Cash (lead 2026-11-09, the owner: "why not give an add money option to the trainer?"; the library side is trainer-mods'
     *  v1153: NETWORK_EARN_FROM_ROCKSTAR(a0)). The action is found by its name in the library's action list (trainer.h List Actions = 4,
     *  actionName "add-cash"), so a library without it never shows a dead row, whatever number it has. a0 = the amount in dollars.
     *  Shown only in a local Online session (MenuWords.STATE_LOCAL_SESSION, bit 24) with the trainer on. */
    static final int LIST_ACTIONS=4;
    static final String CASH_ACTION_NAME="add-cash";
    static final int[] CASH_AMOUNTS={10_000,100_000,1_000_000};
    static final String TEXT_CASH_NEEDS_SESSION="Add Cash works in Online mode only.";
    static final int LOOK_SAVED=0,LOOK_SAVE=1,LOOK_DISCARD=2,LOOK_EDIT=3,LOOK_MENU=4; // (4: the in-game creator, creator_menu.h OpenMenuOp)
    /** character_creator.h Field: OverlayBase 16 (+2 slot index, +1 opacity), PartBase 32 (+3 component), PropBase 72 (+2 anchor). */
    static final int OVERLAY_BASE=16,PART_BASE=32,PROP_BASE=72,FULL_OPACITY=1000,MAX_VALUE=254,NONE=0xFFFF;

    /** online_world.h PlaceList, in its order (the label is the library's own). */
    static final String[] PLACES={"Garage, 2 cars","Garage, 6 cars","Garage, 10 cars","Apartment 1","Apartment 2","Apartment 3",
            "Apartment 4","Apartment 5","Apartment 6","Apartment 7","Apartment 8","Apartment 9","Apartment 10","Apartment 11","Apartment 12",
            "Apartment 13","Apartment 14","Apartment 15","Apartment 16","Apartment 17","Apartment 18","Apartment 19","Apartment 20",
            "Small apartment"};

    enum Kind {PART,TEXTURE,PROP,OVERLAY}
    /** One creator choice: the field it edits (for an overlay the index field; the opacity field is the next one). */
    static final class Category {
        final String key,label,unit;final Kind kind;final int field,start;
        Category(String key,String label,String unit,Kind kind,int field,int start){
            this.key=key;this.label=label;this.unit=unit;this.kind=kind;this.field=field;this.start=start;}
        boolean hasNone(){return kind==Kind.PROP || kind==Kind.OVERLAY;}
        int count(){return MAX_VALUE+1+(hasNone()?1:0);}
    }
    static int part(int component){return PART_BASE+3*component;}
    static int prop(int anchor){return PROP_BASE+2*anchor;}
    static int overlay(int slot){return OVERLAY_BASE+2*slot;}
    /** The starting look's values (character_creator.h defaultLook: hair drawable 1, no props, no beard or makeup); -1 = none. */
    static final Category[] CATEGORIES={
            new Category("hair","Hair","Style",Kind.PART,part(2),1),
            new Category("haircolour","Hair Colour","Colour",Kind.TEXTURE,part(2)+1,0),
            new Category("tops","Tops","Style",Kind.PART,part(11),0),
            new Category("legs","Legs","Style",Kind.PART,part(4),0),
            new Category("shoes","Shoes","Style",Kind.PART,part(6),0),
            new Category("hats","Hats","Style",Kind.PROP,prop(0),-1),
            new Category("glasses","Glasses","Style",Kind.PROP,prop(1),-1),
            new Category("beard","Beard","Style",Kind.OVERLAY,overlay(1),-1),
            new Category("makeup","Makeup","Style",Kind.OVERLAY,overlay(4),-1)};

    private final int[] value=new int[CATEGORIES.length];
    private int lastOnline=0; // (the last online character chosen, 0 male / 1 female: the quick strip's Online shortcut; this session)
    int lastOnline(){return lastOnline;}
    private int activity=-1;
    OnlineMenu(){reset();}
    /** Back to the starting look's values (a new character, or a look the library loads that the menu cannot read back). */
    void reset(){for(int i=0;i<CATEGORIES.length;++i)value[i]=CATEGORIES[i].start;}
    int value(String key){final int i=index(key);return i<0?Integer.MIN_VALUE:value[i];}
    static int index(String key){for(int i=0;i<CATEGORIES.length;++i)if(CATEGORIES[i].key.equals(key))return i;return -1;}
    static int encode(int field,int v){return (field<<16)|(v&0xFFFF);}

    static boolean shown(int state){return (state&STATE_SANDBOX)!=0;}

    // ------------------------------------------------------------------------------------------------ Add Cash
    /** The library's number for "add-cash" (its index in the action list), or -1 when the installed game build has no such action. */
    static int cashAction(SideMenuRows.Trainer t){
        if(t==null || !t.present())return -1;
        final int n=t.count(LIST_ACTIONS);
        for(int i=0;i<n;++i)if(CASH_ACTION_NAME.equals(t.label(LIST_ACTIONS,i)))return i;
        return -1;
    }
    /** "$100,000" (the game's own money format). */
    static String dollars(int amount){return "$"+String.format(java.util.Locale.US,"%,d",amount);}
    private int cashAction=-1; // (looked up when the Cash rows are built; the request uses the number the rows were built with)
    List<SideMenu.Row> cashRows(SideMenuRows.Trainer t){
        cashAction=cashAction(t);
        final List<SideMenu.Row> l=new ArrayList<>();
        if(cashAction<0)return l;
        for(int i=0;i<CASH_AMOUNTS.length;++i)
            l.add(SideMenu.Row.action("on:cash:"+i,"Add "+dollars(CASH_AMOUNTS[i]),"Adds "+dollars(CASH_AMOUNTS[i])+" to your bank now. Shops take it from the bank."));
        return l;
    }

    // ------------------------------------------------------------------------------------------------ rows
    static final String TEXT_NEEDS_CHARACTER="Switch to an online character under Character first.";
    List<SideMenu.Row> characterRows(){
        return Arrays.asList(
                SideMenu.Row.action("on:char:0","Online Male","Play as a male online character."),
                SideMenu.Row.action("on:char:1","Online Female","Play as a female online character."),
                SideMenu.Row.action("on:char:2","Back To Story Character","Back to the character you played before."));
    }
    static final String TEXT_OPEN_CREATOR_HELP="Opens the creator in the game: your character stands still, the camera comes close, and every change shows at once.";
    /** The Creator page. With the in-game creator in the game build (Capabilities.creatorMenu): one row, Open Creator (17 4); its Save &
     *  Exit and Exit Without Saving live in the game's menu. Without it: the menu's own simple rows (the library's 17 3 / 18 / 17 1 / 17 2),
     *  so an older game build keeps a working creator and never shows a dead row. */
    List<SideMenu.Row> creatorRows(boolean inGameCreator){
        if(inGameCreator)return java.util.Collections.singletonList(SideMenu.Row.action("on:look:"+LOOK_MENU,"Open Creator",TEXT_OPEN_CREATOR_HELP));
        final List<SideMenu.Row> l=new ArrayList<>();
        l.add(SideMenu.Row.action("on:look:"+LOOK_EDIT,"Start Editing","Shows your character up close so you can change the look."));
        for(int i=0;i<CATEGORIES.length;++i){
            final Category c=CATEGORIES[i];
            final String[] labels=new String[c.count()];
            int at=0;
            for(int k=0;k<labels.length;++k){
                final int v=c.hasNone()?k-1:k;
                labels[k]=v<0?"None":c.unit+" "+(v+1);
                if(v==value[i])at=k;
            }
            l.add(SideMenu.Row.choice("on:edit:"+c.key,c.label,labels,at,true,false,"Left and right change it now. Save Look keeps it."));
        }
        // owner 2026-11-07: "the save look and saved look should be the same button": one Save Look; the saved look is put on by itself
        // whenever he becomes the online character (afterSwitch), so there is no load row
        l.add(SideMenu.Row.action("on:look:"+LOOK_SAVE,"Save Look","Keeps this look. It is put on whenever you become this online character."));
        l.add(SideMenu.Row.action("on:look:"+LOOK_DISCARD,"Discard Changes","Puts back the look from before you started editing."));
        return l;
    }
    List<SideMenu.Row> placeRows(){
        final List<SideMenu.Row> l=new ArrayList<>();
        for(int i=0;i<PLACES.length;++i)l.add(SideMenu.Row.action("on:place:"+i,PLACES[i],"Takes you inside. On foot only."));
        return l;
    }
    List<SideMenu.Row> activityRows(SideMenuRows.Trainer t){
        final List<SideMenu.Row> l=new ArrayList<>();
        final int n=t==null?0:Math.max(0,t.count(LIST_ACTIVITIES));
        for(int i=0;i<n;++i){final String label=t.label(LIST_ACTIVITIES,i);
            if(label==null || label.isEmpty())continue;
            l.add(SideMenu.Row.action("on:act:"+i,label,"Starts it near you."));}
        if(!l.isEmpty())l.add(SideMenu.Row.action("on:actstop","Stop Activity","Ends the activity you started last."));
        return l;
    }

    // ------------------------------------------------------------------------------------------------ requests
    /** The requests {action, a0, a1} a menu event sends; none for anything that is not an Online row. */
    List<int[]> requests(SideMenu.Event e){
        final List<int[]> out=new ArrayList<>();
        if(e.id==null || !e.id.startsWith("on:"))return out;
        if(e.type==SideMenu.Event.Type.CHANGE && e.id.startsWith("on:edit:")){
            final int i=index(e.id.substring(8));
            if(i<0)return out;
            final Category c=CATEGORIES[i];
            if(e.value<0 || e.value>=c.count())return out;
            final int v=c.hasNone()?e.value-1:e.value;
            switch(c.kind){
                case PART:case TEXTURE:out.add(new int[]{CHARACTER_EDIT,encode(c.field,v),0});break;
                case PROP:out.add(new int[]{CHARACTER_EDIT,encode(c.field,v<0?NONE:v),0});break;
                case OVERLAY:
                    if(v<0)out.add(new int[]{CHARACTER_EDIT,encode(c.field+1,0),0});   // none: opacity 0 (the disc's off)
                    else{out.add(new int[]{CHARACTER_EDIT,encode(c.field,v),0});out.add(new int[]{CHARACTER_EDIT,encode(c.field+1,FULL_OPACITY),0});}
                    break;
            }
            value[i]=v;
            // a new drawable starts at its first texture (character_creator.h editLook): the colour row follows
            if(c.kind==Kind.PART)for(int k=0;k<CATEGORIES.length;++k)if(CATEGORIES[k].kind==Kind.TEXTURE && CATEGORIES[k].field==c.field+1)value[k]=0;
            return out;
        }
        if(e.type!=SideMenu.Event.Type.ACTION)return out;
        if(e.id.equals("on:actstop")){if(activity>=0)out.add(new int[]{ACTIVITY,activity,1});return out;}
        final int colon=e.id.lastIndexOf(':');
        final int n;try{n=Integer.parseInt(e.id.substring(colon+1));}catch(NumberFormatException no){return out;}
        switch(e.id.substring(0,colon)){
            case "on:char":if(n>=0 && n<=2){out.add(new int[]{ONLINE_CHARACTER,n,0});reset();if(n<2)lastOnline=n;}break;
            case "on:look":if(n>=LOOK_SAVE && n<=LOOK_MENU){out.add(new int[]{CHARACTER_LOOK,n,0});if(n!=LOOK_SAVE)reset();}break; // (0: no row; afterSwitch)
            case "on:place":if(n>=0 && n<PLACES.length)out.add(new int[]{ONLINE_PLACE,n,0});break;
            case "on:act":if(n>=0){out.add(new int[]{ACTIVITY,n,0});activity=n;}break;
            case "on:cash":if(cashAction>=0 && n>=0 && n<CASH_AMOUNTS.length)out.add(new int[]{cashAction,CASH_AMOUNTS[n],0});break;
            default:break;
        }
        return out;
    }

    // ------------------------------------------------------------------------------------------------ after a switch
    // Closing after a done action (owner 2026-11-07) and the refused / waiting line moved to ActionClose (the same rule for every row).
    /** What recomp_trainer_v1 returned for a request of row id: a 15 0 / 15 1 switch is followed by the saved look (afterSwitch). */
    void sent(String id,int result){
        if("on:char:0".equals(id) || "on:char:1".equals(id) || "on:char:2".equals(id))switchSeq=result>=0 && !"on:char:2".equals(id)?result:-1;
        if(id!=null && id.startsWith("on:cash:"))cashSeq=result>=0?result:-1;
    }
    // Add Cash's notice (trainer-mods 2026-11-09: NETWORK_EARN_FROM_ROCKSTAR posts no feed line that they can see; the app says it)
    private int cashSeq=-1;
    boolean cashPending(){return cashSeq>=0;}
    /** "+$100,000" once the waited Add Cash request's own result is "ok add-cash <amount>" (the amount the library reports); null while
     *  it runs; null and the wait ended when it failed or its line was replaced by a later request's. */
    String cashDone(String raw){
        if(cashSeq<0 || raw==null)return null;
        final String[] w=raw.trim().split("\\s+");
        if(w.length<3 || !w[0].startsWith("seq="))return null;
        final int seq;try{seq=Integer.parseInt(w[0].substring(4));}catch(NumberFormatException no){return null;}
        if(seq<cashSeq)return null;
        final boolean ok=seq==cashSeq && w[1].equals("ok") && w[2].equals(CASH_ACTION_NAME) && w.length>=4;
        cashSeq=-1;
        if(!ok)return null;
        try{return "+"+dollars(Integer.parseInt(w[3]))+" (bank)";}catch(NumberFormatException no){return null;} // (trainer-mods 2026-11-10: NETWORK_EARN_FROM_ROCKSTAR credits the bank; the HUD shows the wallet)
    }
    // The saved look on every switch to an online character (owner 2026-11-07). v963 puts it back after a respawn only (character_
    // creator.h noteRespawn): a 15 0 / 15 1 switch records its new ped as seen, so nothing re-applies it. Until the library does, the
    // app asks for it: 17 0 (ApplySaved: the saved look of this model, else the starting look) once the switch reports ok. Not cleared
    // by the menu closing: he may close it himself before the switch is done.
    private int switchSeq=-1;
    boolean switching(){return switchSeq>=0;}
    /** The request to send after a switch, read from the library's last result line: {17, 0, 0} once the waited 15 0 / 15 1 is ok;
     *  null otherwise (still running, failed, or its line replaced by a later result). */
    int[] afterSwitch(String raw){return afterSwitch(raw,0);}
    /** As afterSwitch(raw), with the library's state: a later line (the switch's own replaced by the Sandbox's Give Weapons) counts as
     *  done when an online character is the player now (state bit 20). */
    int[] afterSwitch(String raw,int state){
        if(switchSeq<0 || raw==null)return null;
        final String[] w=raw.trim().split("\\s+");
        if(w.length<2 || !w[0].startsWith("seq="))return null;
        final int seq;try{seq=Integer.parseInt(w[0].substring(4));}catch(NumberFormatException no){return null;}
        if(seq<switchSeq)return null;
        final boolean ok=seq==switchSeq?w[1].equals("ok"):(state&STATE_ONLINE_CHARACTER)!=0;
        switchSeq=-1;
        return ok?new int[]{CHARACTER_LOOK,LOOK_SAVED,0}:null;
    }
    // ------------------------------------------------------------------------------------------------ the result line
    /** The library's last result ("seq=N ok|fail|blocked|dropped <action> [detail]") in words for the Online pages. */
    static String statusText(String raw){
        if(raw==null)return "";
        final String[] w=raw.trim().split("\\s+");
        if(w.length<3 || !w[0].startsWith("seq="))return "";
        if(w[1].equals("fail") || w[1].equals("blocked")){
            if(w[2].equals(CASH_ACTION_NAME))for(int i=3;i<w.length;++i)if(w[i].equals("needs-session"))return "Not done: "+TEXT_CASH_NEEDS_SESSION;
            for(int i=3;i<w.length;++i){final String why=reason(w[i]);if(why!=null)return "Not done: "+why;}
        }
        final String text=SideMenuRows.trainerStatusText(raw);
        // "blocked" is final (trainer.h finishJob: the gate closed and the job was given up), not a wait: said as such here
        if(w[1].equals("blocked") && text.startsWith("Waiting: "))return "Not done, it was interrupted: "+text.substring(9);
        return text;
    }
    private static String reason(String word){
        switch(word){
            case "no-online-character":return "switch to an online character under Character first.";
            case "not-editing":return "choose Start Editing first.";
            case "no-such-drawable":case "no-such-texture":case "no-such-prop":case "no-such-overlay":
                return "this character has no item with that number.";
            case "in-vehicle":return "get out of the vehicle first.";
            case "busy":case "character-busy":return "still busy with the last change.";
            case "needs-session":return "this activity needs an online session.";
            case "already-story":return "you already play the story character.";
            case "sandbox-only":return "only in Sandbox.";
            default:return null;
        }
    }
}
