package dev.recompbootstrap.smoke;

/**
 * Update 1 QoL (owner's goal 2026-11-07, item 3): one rule for every side-menu row. A row that changes the game closes the menu once the
 * library reports its request done; browsing, settings and toggles keep it open; a refused request keeps it open with the reason on the
 * status line. The same file in the Thor and the Fold trees; SideMenuApply sends every trainer request through sent(), the game window
 * polls while waiting() (SideMenuApply.poll). tests/SideMenuQolTest.java checks every row of every page against TABLE.
 *
 * The library's words (trainer.h): recomp_trainer_v1 returns the request's sequence number (or ErrOff -1, ErrArgs -2, ErrFull -3; the
 * app's -100 without a trainer); the job's end writes "seq=N ok|fail|blocked|dropped <action> [detail]" (result, finishJob, ageQueue).
 *
 * The Thor's lower-screen trainer never closes after an action: the game stays in view on the main screen (ThorMenus drains it with
 * stayOpen()).
 */
final class ActionClose {
    enum Rule {
        CLOSES,          // closes once the request is done
        CLOSES_SETTLED,  // a value row that changes the game: closes once done and SETTLE_MS after the last step (0 -> 3 stars steps)
        CLOSES_ON_START, // a request whose job runs as long as a game menu is open (17 4, the in-game creator): closes when the library
                         // reports that menu open (state bit 22), stays open with the reason when the job is refused
        STAYS            // keeps the menu open (browsing, settings, toggles that change no world state)
    }
    static final long SETTLE_MS=1200;
    static final String TEXT_WORKING="Working on it...";

    /** The rule, one row per row id (a trailing ':' or '*' = every row with that prefix). Read by rule() and printed by the tests. */
    static final String[][] TABLE={
            // id                 rule              what it is
            {"tr:heal",           "CLOSES",         "Heal And Armour"},
            {"tr:weapons",        "CLOSES",         "Give All Weapons"},
            {"chr:",              "CLOSES",         "Switch To (a story character, Sandbox)"},
            {"tr:fix",            "CLOSES",         "Repair Vehicle"},
            {"veh:",              "CLOSES",         "a vehicle (spawn)"},
            {"tr:waypoint",       "CLOSES",         "Teleport To Waypoint"},
            {"lm:",               "CLOSES",         "a landmark (teleport)"},
            {"tr:clearwx",        "CLOSES",         "Clear Weather"},
            {"wx:",               "CLOSES",         "a weather"},
            {"tr:wanted",         "CLOSES_SETTLED", "Wanted Level (value row)"},
            {"tr:time",           "CLOSES_SETTLED", "Time Of Day (value row)"},
            {"on:char:",          "CLOSES",         "Online character (male, female, back to story)"},
            {"on:place:",         "CLOSES",         "Online place (teleport inside)"},
            {"on:act:",           "CLOSES",         "an activity (start)"},
            {"on:actstop",        "CLOSES",         "Stop Activity"},
            {"on:look:1",         "CLOSES",         "Save Look (ends editing)"},
            {"on:look:2",         "CLOSES",         "Discard Changes (ends editing)"},
            {"on:look:3",         "STAYS",          "Start Editing (the edits follow)"},
            {"on:look:4",         "CLOSES_ON_START","Open Creator (the in-game creator: the game's own menu takes the pad)"},
            {"on:edit:",          "STAYS",          "a creator choice (browsing the look)"},
            {"tr:on",             "STAYS",          "the Trainer switch (a setting)"},
            {"tr:t",              "STAYS",          "a cheat toggle (Invincible ... Freeze Time: a mode, not a change)"},
            {"mm:",               "STAYS",          "My Music: next, previous, find new music (the app's player)"},
            {"on:imenu",          "CLOSES",         "the game's own Interaction Menu (local session): ours closes at once"},
            {"app:diag",          "STAYS",          "Copy Diagnostics (the clipboard; the game goes on)"},
            {"*",                 "STAYS",          "every setting row (Settings, Dual Screens, Touch Controls, Mods, My Music modes)"},
    };

    static Rule rule(String id,SideMenu.Event.Type type){
        if(id==null || (type!=SideMenu.Event.Type.ACTION && type!=SideMenu.Event.Type.CHANGE))return Rule.STAYS;
        for(String[] r:TABLE){
            final String key=r[0];
            final boolean match=key.equals("*") || (key.endsWith(":")?id.startsWith(key):key.equals("tr:t")?id.matches("tr:t[0-9]+"):id.equals(key));
            if(match)return Rule.valueOf(r[1]);
        }
        return Rule.STAYS;
    }

    static final int STATE_CREATOR_MENU=1<<22;   // trainer.h StateCreatorMenu (INTEGRATION.md G3): the in-game creator is open
    private int waitSeq=-1;private long closeAt;private boolean done,onStart;private String note;
    // A character switch (on:char:N) whose own result line may be replaced at once by the Sandbox's follow-up request (Give Weapons after
    // a switch; found by the iOS session 2026-11-07): once a later line shows the switch's job is over, state bit 20 (an online character
    // is the player) tells whether it worked. 0 none, 1 bit 20 on (online male / female), 2 bit 20 off (back to the story character).
    static final int STATE_ONLINE_CHARACTER=1<<20;
    private int wantState;
    /** What recomp_trainer_v1 returned for a request of row id (at time now, ms): kept for the close and the status line. */
    void sent(String id,SideMenu.Event.Type type,int result,long now){
        if(result<0){waitSeq=-1;done=false;note="Not done: "+refusal(result);return;}
        note=null;waitSeq=-1;done=false;
        final Rule r=rule(id,type);
        if(r==Rule.STAYS)return;
        waitSeq=result;note=TEXT_WORKING;closeAt=r==Rule.CLOSES_SETTLED?now+SETTLE_MS:now;onStart=r==Rule.CLOSES_ON_START;
        wantState="on:char:0".equals(id) || "on:char:1".equals(id)?1:"on:char:2".equals(id)?2:0;
    }
    /** The menu was closed (by the player, or here), or the request came from a menu that stays open: nothing is waited for. */
    void menuClosed(){waitSeq=-1;done=false;note=null;}
    boolean waiting(){return waitSeq>=0;}
    /** The library's last result line at time now: true = close the menu. fail / blocked / dropped end the wait with the menu open (the
     *  status line says why); a later sequence number (the waited one's line replaced) ends it without closing. */
    boolean poll(String raw,long now){return poll(raw,now,0);}
    /** As poll(raw, now), with the library's state: a CLOSES_ON_START request is done when STATE_CREATOR_MENU is set. */
    boolean poll(String raw,long now,int state){
        if(waitSeq<0)return false;
        if(onStart && (state&STATE_CREATOR_MENU)!=0){waitSeq=-1;done=false;note=null;return true;}
        if(!done){
            if(raw==null)return false;
            final String[] w=raw.trim().split("\\s+");
            if(w.length<2 || !w[0].startsWith("seq="))return false;
            final int seq;try{seq=Integer.parseInt(w[0].substring(4));}catch(NumberFormatException no){return false;}
            if(seq<waitSeq)return false;
            note=null;
            final boolean switched=wantState==1?(state&STATE_ONLINE_CHARACTER)!=0:wantState==2 && (state&STATE_ONLINE_CHARACTER)==0;
            if(seq>waitSeq && wantState!=0 && switched){done=true;}   // (its line replaced by a later one: the state says it worked)
            else if(seq!=waitSeq || !w[1].equals("ok")){waitSeq=-1;return false;}
            else done=true;
        }
        if(now<closeAt)return false;   // (a value row: settling)
        waitSeq=-1;done=false;
        return true;
    }
    /** The menu's own line (waiting, refused at the call), or null: the library's last result is shown. */
    String note(){return note;}
    static String refusal(int code){
        switch(code){
            case -1:return "turn the trainer on first.";
            case -3:return "too many requests are waiting. Try again in a moment.";
            case -100:return "this game build has no trainer.";
            default:return "this game build does not take that request.";
        }
    }
}
