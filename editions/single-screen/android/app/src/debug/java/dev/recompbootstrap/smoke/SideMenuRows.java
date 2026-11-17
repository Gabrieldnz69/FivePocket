package dev.recompbootstrap.smoke;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * menu-redesign (2026-10-25): the rows grouped into the pause-menu frame's tabs and sections (tabs(): Settings, Touch, Trainer, Mods);
 * Time Of Day is a value row ("tr:time"), no longer a page.
 * side-menu (SPEC 5): the rows of each SideMenu page, built from the saved settings, the installed game build's capabilities and the
 * trainer's state. Pure Java: tests/SideMenuTest.java runs it on the host.
 *
 * Settings: the launcher's rows in its page order (Display, Frame Rate, World, Controls), each offering only the values this game build
 * supports (Capabilities), with the launcher's one-line description. LIVE: what the game reads again while it runs, from the readers
 * themselves (library v712): the scaler and FSR sharpness (the presenter's poll, every 120th frame), the frame cap and frame generation
 * (the window's switch poll), the frame-rate counter (the same poll), the screen (the app's layout and the library's visible-fraction
 * file) and the touch controls (the app). Everything else is read when the game starts: render resolution (rsLoad), the world sliders
 * (world_load.h launchScope) and the low-delay input option (the game activity's start); those rows say so, and leaving Settings after
 * changing one shows the ALERT ("This setting change requires a restart.").
 */
final class SideMenuRows implements SideMenu.Rows {
    /** The trainer as the menu sees it (recomp_trainer_v1 through the game activity; a fake on the host). */
    interface Trainer {
        boolean present();                 // the game build has the trainer
        int state();                       // recomp_trainer_state_v1: toggles in bits 0..6, enabled 1<<16, gameplay 1<<17, busy 1<<18
        int count(int list);               // trainer.h List: 0 vehicles, 1 landmarks, 2 weathers, 5 characters (Update 1 P7)
        String label(int list,int index);
        String status();                   // the last request's result, or ""
        default String mods(){return "";}  // recomp_mods_status_v1: "enabled=.. found=.. applied=.. skipped=..", or ""
    }
    static final int LIST_VEHICLES=0,LIST_LANDMARKS=1,LIST_WEATHERS=2,LIST_CHARACTERS=5;
    static final int STATE_ENABLED=1<<16,STATE_GAMEPLAY=1<<17;
    /** Update 1 P7: trainer.h StateSandbox. Set only while the game plays Sandbox, so the character rows never show in Story Mode
     *  (where the library refuses the switch anyway) nor with game code that has no switch (no bit, no character list). */
    static final int STATE_SANDBOX=1<<21;   // (ALLOCATION.md §10: bit 21 = StateSandbox; bit 20 is the online character)
    static final String[] TOGGLE_LABELS={"Invincible","Never Wanted","Infinite Ammo","Super Jump","Fast Run","Fast Swim","Freeze Time"};

    static final List<String> LIVE=Arrays.asList(SettingsSchema.SCALER,SettingsSchema.FSR_SHARPNESS,SettingsSchema.SCREEN_FILL,
            SettingsSchema.FRAME_CAP,SettingsSchema.FRAME_GENERATION,SettingsSchema.TOUCH_CONTROLS,
            SettingsSchema.TOUCH_KEEP,SettingsSchema.TOUCH_SIZE,SettingsSchema.TOUCH_OPACITY,SettingsSchema.TOUCH_LAYOUT,
            SettingsSchema.HAPTICS, // (Update 1 haptics: the game window applies it at once, SideMenuApply.Effects.hapticsChanged)
            SettingsSchema.MY_MUSIC,SettingsSchema.MY_MUSIC_VOLUME,SettingsSchema.MY_MUSIC_ORDER); // (Update 1 P6: applied by the app at once)
    /** The Settings tab's sections (menu-redesign: the launcher's categories), each with its settings in the launcher's order. */
    static final List<String> SET_DISPLAY=Arrays.asList(SettingsSchema.SCALER,SettingsSchema.FSR_SHARPNESS,SettingsSchema.RENDER_SCALE,
                    SettingsSchema.SCREEN_FILL),
            SET_FRAME_RATE=Arrays.asList(SettingsSchema.FRAME_CAP,SettingsSchema.FRAME_GENERATION),
            SET_WORLD=Arrays.asList(SettingsSchema.WORLD_TRAFFIC,SettingsSchema.WORLD_PEDESTRIANS,SettingsSchema.WORLD_PARKED_CARS,
                    SettingsSchema.WORLD_SCENARIO_PEDESTRIANS,SettingsSchema.WORLD_DRAW_DISTANCE),
            SET_CONTROLS=Arrays.asList(SettingsSchema.INPUT_UNBUFFERED,SettingsSchema.HAPTICS);
    /** Every setting of the Settings tab (the tests check each is in one section). */
    static final List<String> SETTINGS=Arrays.asList(SettingsSchema.SCALER,SettingsSchema.FSR_SHARPNESS,SettingsSchema.RENDER_SCALE,
            SettingsSchema.SCREEN_FILL,SettingsSchema.FRAME_CAP,SettingsSchema.FRAME_GENERATION,
            SettingsSchema.WORLD_TRAFFIC,SettingsSchema.WORLD_PEDESTRIANS,SettingsSchema.WORLD_PARKED_CARS,
            SettingsSchema.WORLD_SCENARIO_PEDESTRIANS,SettingsSchema.WORLD_DRAW_DISTANCE,SettingsSchema.INPUT_UNBUFFERED,SettingsSchema.HAPTICS);
    static final List<String> TOUCH=Arrays.asList(SettingsSchema.TOUCH_CONTROLS,SettingsSchema.TOUCH_LAYOUT,SettingsSchema.TOUCH_SIZE,
            SettingsSchema.TOUCH_OPACITY,SettingsSchema.TOUCH_KEEP);

    Settings settings=Settings.defaults();
    Capabilities capabilities=Capabilities.of(java.util.Collections.<String>emptySet(),false,false);
    boolean touchShown=true;              // the HIDE eye's state (this session)
    Trainer trainer;                       // null: no trainer in this build
    MyMusicMenu music;                     // Update 1 P6: the player's own music (MyMusicPlayer); null: no My Music tab
    /** Update 1 P6: the My Music page's rows (live: the player applies them at once). */
    static final List<String> MUSIC=Arrays.asList(SettingsSchema.MY_MUSIC,SettingsSchema.MY_MUSIC_VOLUME,SettingsSchema.MY_MUSIC_ORDER);
    int wanted,hour=12;                    // the trainer rows' chosen values (this session)
    final OnlineMenu online=new OnlineMenu(); // Update 1: the Online tab (Sandbox only; its creator values, this session)
    final ActionClose close=new ActionClose(); // Update 1 QoL: which rows close the menu once done; the waiting / refused line
    // Update 1 (menus plan A3, the lead 2026-11-07): the side menu's own settings, in settings/menu.txt (MenuWords), read by the game
    // window when it starts (loadMenuWords) and written at once on a change (SideMenuApply.Effects.menuWords). Not on the launcher's page.
    /** 1.1.5: the single-screen edition keeps Hold Select Opens Menu ON by default (the owner's Off decision is for the Thor); the touch
     *  controls' Interaction Menu button (TouchPad IMENU) gives the game its Select hold in Sandbox. A player's own choice is kept. */
    static final int HOLD_DEFAULT=MenuWords.HOLD_ON;
    int holdSelect=HOLD_DEFAULT,menuStyle=MenuWords.STYLE_FIVEPOCKET;
    static final String ID_HOLD_SELECT="menu:hold",ID_MENU_STYLE="menu:style";
    static final String TEXT_HOLD_SELECT="Holding Select on a controller for a moment opens this menu. A short press still goes to the game.";
    static final String TEXT_MENU_STYLE="Holding Select opens FivePocket's side menu, or the game's own Interaction Menu. The side menu's swipe stays.";
    /** menu.txt and quick-menu.txt as read when the game window starts (null: no file). */
    // Update 1 touch item 3 (the owner's post): menu.txt words; defaults ADS turns ON, fire aims OFF, aim assist STRONG (the lead 2026-11-07)
    boolean adsTurns=true,fireAims=false;int aimAssist=MenuWords.ASSIST_STRONG;
    static final String ID_ADS_TURN="touch:adsturn",ID_FIRE_AIMS="touch:fireaims",ID_AIM_ASSIST="touch:assist";
    boolean cameraSwipe=true,camInvert=false;int camSpeed=1; // (item 4: menu.txt words; Swipe and Medium by default)
    static final String ID_CAMERA="touch:camera",ID_CAM_SPEED="touch:camspeed",ID_CAM_INVERT="touch:caminvert";
    // 2026-11-09 (the owner's Discord report "weapon wheel swapping weapon, fire to aim jank" and his hold-to-ADS / red-reticle auto-fire
    // posts): Aim Mode (Hold default, Tap = the 1-tap latch) and Auto-Fire (default off) are menu.txt words; the camera rows are shown with
    // every library (without the direct turn, Swipe goes through the game's stick at the finger's speed and says so); the rows a library
    // cannot do are shown greyed with the reason, not hidden.
    boolean aimTap=false,autoFire=false;
    static final String ID_AIM_MODE="touch:aimmode",ID_AUTO_FIRE="touch:autofire";
    /** The library answers the red-reticle read (QolTouch's probe, recomp_aim_assist_v1 op -3; false until it has answered). */
    static volatile boolean reticleSignal;
    static final String TEXT_CAMERA_DIRECT="Swipe: the camera follows your finger. Stick: the right side works as a stick.";
    static final String TEXT_CAMERA_STICK_ONLY="Swipe: the camera moves while your finger moves. This game build cannot turn the camera directly, so Swipe goes through the game's stick and feels softer. Stick: the right side works as a stick.";
    static final String TEXT_NEEDS_NEWER_BUILD="Needs a newer game build.";
    private void aimRows(List<SideMenu.Row> l){
        l.add(SideMenu.Row.toggle(ID_ADS_TURN,"ADS Turns Camera",adsTurns,true,false,"While you hold AIM, moving that finger turns the camera."));
        l.add(SideMenu.Row.toggle(ID_FIRE_AIMS,"Fire Also Aims",fireAims,true,false,"One-tap ADS and fire: a tap on FIRE aims and shoots."));
        l.add(SideMenu.Row.choice(ID_AIM_MODE,"Aim Mode",new String[]{"Hold","Tap"},aimTap?1:0,true,false,"Hold: you aim while you hold AIM. Tap: one tap on AIM keeps aiming until the next tap."));
        l.add(SideMenu.Row.choice(ID_CAMERA,"Camera",new String[]{"Stick","Swipe"},cameraSwipe?1:0,true,false,capabilities.cameraTurn()?TEXT_CAMERA_DIRECT:TEXT_CAMERA_STICK_ONLY));
        l.add(SideMenu.Row.choice(ID_CAM_SPEED,"Camera Speed",new String[]{"Slow","Medium","Fast"},camSpeed,true,false,"How far the camera turns for a swipe."));
        l.add(SideMenu.Row.toggle(ID_CAM_INVERT,"Invert Camera Up-Down",camInvert,true,false,"A swipe up looks down."));
        final boolean assist=capabilities.aimAssist();
        l.add(SideMenu.Row.choice(ID_AIM_ASSIST,"Aim Magnetism",new String[]{"Game","Strong"},assist?aimAssist:0,assist,false,
                assist?"How hard your aim pulls onto targets. Strong: the game's assisted aim with full lock-on.":TEXT_NEEDS_NEWER_BUILD));
        l.add(SideMenu.Row.toggle(ID_AUTO_FIRE,"Auto-Fire On Red Reticle",reticleSignal && autoFire,reticleSignal,false,
                reticleSignal?"While you aim, fires whenever the game turns the reticle red (an enemy). Never at bystanders.":TEXT_NEEDS_NEWER_BUILD));
    }
    void loadMenuWords(String menuText,String quickText){
        final byte[] mt=menuText==null?null:menuText.getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        adsTurns=MenuWords.adsTurns(mt);fireAims=MenuWords.fireAims(mt);aimAssist=MenuWords.aimAssist(mt);
        cameraSwipe=MenuWords.cameraSwipe(mt);camSpeed=MenuWords.camSpeed(mt);camInvert=MenuWords.camInvert(mt);
        aimTap=MenuWords.aimTap(mt);autoFire=MenuWords.autoFire(mt); // (2026-11-09)
        holdSelect=MenuWords.holdSelect(menuText==null?null:menuText.getBytes(java.nio.charset.StandardCharsets.US_ASCII),HOLD_DEFAULT);
        menuStyle=MenuWords.style(quickText==null?null:quickText.getBytes(java.nio.charset.StandardCharsets.US_ASCII));
    }
    /** The quick menu in effect: the Interaction Menu only while the game library has it. */
    int effectiveStyle(){return capabilities.interactionMenu()?menuStyle:MenuWords.STYLE_FIVEPOCKET;}
    private void menuRows(List<SideMenu.Row> l){
        // Hold Select belongs to FivePocket's style: in the Interaction Menu style the library opens its menu on the hold
        if(effectiveStyle()==MenuWords.STYLE_FIVEPOCKET)
            l.add(SideMenu.Row.choice(ID_HOLD_SELECT,"Hold Select Opens Menu",new String[]{"Off","On"},holdSelect,true,false,TEXT_HOLD_SELECT));
        if(capabilities.interactionMenu()) // (no row without the library's export: never a dead row)
            l.add(SideMenu.Row.choice(ID_MENU_STYLE,"Quick Menu Style",new String[]{"FivePocket","Interaction Menu"},menuStyle,true,false,TEXT_MENU_STYLE));
    }
    final Favourites favourites=new Favourites(); // Update 1 QoL: the trainer's pinned items (files/trainer-favourites.txt)
    String pinNote; // (the last pin / unpin, shown on the trainer pages until the next row is used)
    /** Update 1 (main 2026-11-07): the touch row that opens the game's own Interaction Menu while a local online session is up
     *  (recomp_imenu_v1(5)); why it did not open, shown on the Online pages until the next row is used. */
    static final String ID_GAME_IMENU="on:imenu",TEXT_GAME_IMENU="Opens the game's own Interaction Menu (the local online session's).";
    static final String TEXT_IMENU_NO_SESSION="The Interaction Menu opens only in a local online session.",TEXT_IMENU_NOT_IN_BUILD="Not in this build.";
    String imenuNote;
    /** Update 1 Diagnostics: the row's id, and the last copy's sentence (Copying..., Done..., or why not), shown on Support. */
    static final String ID_DIAGNOSTICS="app:diag";
    String diagNote;
    /** Update 1 QoL: a trainer item may be held (pinned / unpinned) on the trainer's pages, the lower screen's included. */
    @Override public boolean holdable(SideMenu.Page page,SideMenu.Row row){return page.name().startsWith("TR_") && Favourites.pinnable(row.id);}
    @Override public String holdLabel(SideMenu.Page page,SideMenu.Row row){return favourites.contains(row.id)?"Unpin":"Pin";}

    /** A setting row's values: the setting's own values this build supports (the saved value always among them). */
    private final Map<String,int[]> shownValues=new HashMap<>();
    static boolean live(String key){return LIVE.contains(key);}

    /** SPEC 3.14: the Trainer tab's index (the TRAINER button opens it), or 0. */
    static int trainerTab(List<SideMenu.Tab> tabs){for(int i=0;i<tabs.size();++i)if(tabs.get(i).title.equals("Trainer"))return i;return 0;}
    @Override public List<SideMenu.Tab> tabs(boolean lower){
        final List<SideMenu.Tab> tabs=new ArrayList<>(Arrays.asList(
                new SideMenu.Tab("Settings",new SideMenu.Page[]{SideMenu.Page.SET_DISPLAY,SideMenu.Page.SET_FRAME_RATE,SideMenu.Page.SET_WORLD,
                        SideMenu.Page.SET_CONTROLS,SideMenu.Page.SET_SUPPORT},new String[]{"Display","Frame Rate","World","Controls","Support"}),
                new SideMenu.Tab("Touch",new SideMenu.Page[]{SideMenu.Page.TOUCH},new String[]{"Touch Controls"}),
                new SideMenu.Tab("Trainer",new SideMenu.Page[]{SideMenu.Page.TR_TRAINER,SideMenu.Page.TR_FAVOURITES,SideMenu.Page.TR_PLAYER,SideMenu.Page.TR_VEHICLES,
                        SideMenu.Page.TR_TELEPORT,SideMenu.Page.TR_WORLD},new String[]{"Trainer","Favourites","Player","Vehicles","Teleport","World"}), // (Update 1 QoL: Favourites)
                new SideMenu.Tab("Mods",new SideMenu.Page[]{SideMenu.Page.MODS},new String[]{"Script Mods"})));
        if(music!=null)tabs.add(new SideMenu.Tab("My Music",new SideMenu.Page[]{SideMenu.Page.MY_MUSIC},new String[]{"My Music"})); // (Update 1 P6)
        if(trainer!=null && trainer.present() && OnlineMenu.shown(trainer.state())) // (Update 1: Sandbox only; hidden in Story Mode)
            tabs.add(cashShown()?new SideMenu.Tab("Online",new SideMenu.Page[]{SideMenu.Page.ON_CHARACTER,SideMenu.Page.ON_CREATOR,SideMenu.Page.ON_PLACES,
                    SideMenu.Page.ON_ACTIVITIES,SideMenu.Page.ON_CASH},new String[]{"Character","Creator","Places","Activities","Cash"}) // (2026-11-09: Add Cash)
                    :new SideMenu.Tab("Online",new SideMenu.Page[]{SideMenu.Page.ON_CHARACTER,SideMenu.Page.ON_CREATOR,SideMenu.Page.ON_PLACES,
                    SideMenu.Page.ON_ACTIVITIES},new String[]{"Character","Creator","Places","Activities"}));
        return tabs;
    }
    @Override public List<String> header(){return Arrays.asList("Private Build","In Game");}

    private boolean trainerOn(){return trainer!=null && trainer.present() && (trainer.state()&STATE_ENABLED)!=0;}
    @Override public List<SideMenu.Row> rows(SideMenu.Page page){
        final List<SideMenu.Row> l=new ArrayList<>();
        switch(page){
            case SET_DISPLAY:for(String key:SET_DISPLAY)l.add(setting(key));break;
            case SET_FRAME_RATE:for(String key:SET_FRAME_RATE)l.add(setting(key));break;
            case SET_WORLD:for(String key:SET_WORLD)l.add(setting(key));break;
            case SET_CONTROLS:for(String key:SET_CONTROLS)l.add(setting(key));menuRows(l);break; // (+ Update 1: the side menu's own two)
            case TOUCH:
                l.add(SideMenu.Row.toggle("touch:show","Show Touch Controls",touchShown,true,false,"Hides or shows the on-screen controls (the eye button does the same)."));
                for(String key:TOUCH)l.add(setting(key));
                aimRows(l); // (Update 1 touch item 3)
                break;
            case TR_TRAINER:
                if(trainer!=null && trainer.present())l.add(SideMenu.Row.toggle("tr:on","Trainer",trainerOn(),true,false,"Turns the trainer on. Single player only; it acts only while you play."));
                else l.add(new SideMenu.Row("tr:on","Trainer",SideMenu.Kind.TOGGLE,new String[]{"Off","On"},0,false,false,null,TEXT_NOT_IN_BUILD));
                break;
            case TR_FAVOURITES:if(trainerOn())l.addAll(favourites.rows(this));break; // (Update 1 QoL: the pinned items, Favourites)
            case SET_SUPPORT:l.add(SideMenu.Row.action(ID_DIAGNOSTICS,"Copy Diagnostics",Diagnostics.ROW_HELP));break; // (Update 1: Diagnostics)
            case TR_PLAYER:if(trainerOn())playerRows(l);break;
            case TR_VEHICLES:if(trainerOn()){l.add(SideMenu.Row.action("tr:fix","Repair Vehicle","Repairs and cleans the vehicle you are in."));
                list(l,LIST_VEHICLES,"veh:","Spawns it beside you and puts you in it.");}break;
            case TR_TELEPORT:if(trainerOn()){l.add(SideMenu.Row.action("tr:waypoint","Teleport To Waypoint","Takes you to the map's waypoint."));
                list(l,LIST_LANDMARKS,"lm:","Takes you there.");}break;
            case TR_WORLD:if(trainerOn())worldRows(l);break;
            case MODS:l.add(setting(SettingsSchema.SCRIPT_MODS));break; // (the launcher's own row: settings/mods.txt, read when the game starts)
            case MY_MUSIC:musicRows(l);break; // (Update 1 P6)
            case ON_CHARACTER:if(onlineOn()){ // (Update 1: the Online tab)
                if(MenuWords.localSession(trainer.state()))l.add(SideMenu.Row.action(ID_GAME_IMENU,"Interaction Menu",TEXT_GAME_IMENU)); // (main: the game's own, in a session)
                l.addAll(online.characterRows());}break;
            case ON_CREATOR:if(onlineOn() && (trainer.state()&OnlineMenu.STATE_ONLINE_CHARACTER)!=0)l.addAll(online.creatorRows(capabilities.creatorMenu()));break;
            case ON_PLACES:if(onlineOn())l.addAll(online.placeRows());break;
            case ON_ACTIVITIES:if(onlineOn())l.addAll(online.activityRows(trainer));break;
            case ON_CASH:if(cashShown())l.addAll(online.cashRows(trainer));break; // (2026-11-09: Add Cash, a local Online session only)
            default:break;
        }
        return l;
    }
    @Override public String info(SideMenu.Page page){
        switch(page){
            case TR_FAVOURITES: // (Update 1 QoL)
                if(trainer==null || !trainer.present())return TEXT_NOT_IN_BUILD;
                if(!trainerOn())return TEXT_TRAINER_OFF;
                return favourites.rows(this).isEmpty()?Favourites.TEXT_EMPTY:"";
            case TR_PLAYER:case TR_VEHICLES:case TR_TELEPORT:case TR_WORLD:
                if(trainer==null || !trainer.present())return TEXT_NOT_IN_BUILD;
                return trainerOn()?"":TEXT_TRAINER_OFF;
            case MODS:return trainer==null?"":modsText(trainer.mods());
            case MY_MUSIC:return music==null?"":music.info();
            case ON_CASH:return cashShown()?"":TEXT_CASH_ONLINE; // (the page is listed only while shown; a stale page says why)
            case ON_CHARACTER:case ON_CREATOR:case ON_PLACES:case ON_ACTIVITIES:
                if(!onlineOn())return trainer==null || !trainer.present()?TEXT_NOT_IN_BUILD:!OnlineMenu.shown(trainer.state())?TEXT_ONLINE_SANDBOX:TEXT_TRAINER_OFF;
                if(page==SideMenu.Page.ON_CREATOR && (trainer.state()&OnlineMenu.STATE_ONLINE_CHARACTER)==0)return OnlineMenu.TEXT_NEEDS_CHARACTER;
                if(page==SideMenu.Page.ON_ACTIVITIES && trainer.count(OnlineMenu.LIST_ACTIVITIES)<=0)return TEXT_NOT_IN_BUILD;
                return "";
            default:return "";
        }
    }
    static final String TEXT_NOT_IN_BUILD="Not in the installed game build.";
    static final String TEXT_TRAINER_OFF="The trainer is off. Turn it on under Trainer.";
    static final String TEXT_ONLINE_SANDBOX="Online is part of Sandbox.";
    /** Update 1: the Online tab's rows need Sandbox and the trainer on (the library refuses its online actions otherwise). */
    private boolean onlineOn(){return trainerOn() && OnlineMenu.shown(trainer.state());}
    /** Update 1 P6: Mode, Volume and Order (the launcher's rows, saved at once), then the player's actions. */
    private void musicRows(List<SideMenu.Row> l){
        for(String key:MUSIC)l.add(setting(key));
        if(music==null)return;
        if(settings.get(SettingsSchema.MY_MUSIC)!=SettingsSchema.MY_MUSIC_OFF && music.tracks()>0){
            l.add(SideMenu.Row.action(MyMusicMenu.ID_NEXT,"Next Track","Plays the next song."));
            l.add(SideMenu.Row.action(MyMusicMenu.ID_PREVIOUS,"Previous Track","Plays the song again from the start, or the one before it."));
        }
        l.add(SideMenu.Row.action(MyMusicMenu.ID_RESCAN,"Find New Music","Reads the music folder again after you added songs."));
    }
    private void playerRows(List<SideMenu.Row> l){
        final int s=trainer.state();
        for(int b=0;b<6;++b)l.add(SideMenu.Row.toggle("tr:t"+b,TOGGLE_LABELS[b],((s>>b)&1)!=0,true,false,"Stays on until you turn it off."));
        l.add(SideMenu.Row.choice("tr:wanted","Wanted Level",new String[]{"0 Stars","1 Star","2 Stars","3 Stars","4 Stars","5 Stars"},wanted,true,false,"Sets your wanted level now."));
        l.add(SideMenu.Row.action("tr:heal","Heal And Armour","Full health and armour."));
        l.add(SideMenu.Row.action("tr:weapons","Give All Weapons","Every weapon, with ammo."));
        if((s&STATE_SANDBOX)!=0)characterRows(l);
    }
    /** Update 1 P7 (Sandbox only): one row per story character, from the library's list. */
    private void characterRows(List<SideMenu.Row> l){
        final int n=Math.max(0,trainer.count(LIST_CHARACTERS));
        for(int i=0;i<n;++i){final String name=trainer.label(LIST_CHARACTERS,i);
            if(name==null || name.isEmpty())continue;
            l.add(SideMenu.Row.action("chr:"+i,"Switch To "+name,"Play as "+name+" from where you stand."));}
    }
    private void worldRows(List<SideMenu.Row> l){
        final String[] hours=new String[24];for(int h=0;h<24;++h)hours[h]=hourText(h);
        l.add(SideMenu.Row.choice("tr:time","Time Of Day",hours,hour,true,false,"Sets the clock to this hour now."));
        l.add(SideMenu.Row.toggle("tr:t6",TOGGLE_LABELS[6],((trainer.state()>>6)&1)!=0,true,false,"Stays on until you turn it off."));
        l.add(SideMenu.Row.action("tr:clearwx","Clear Weather","Back to the game's own weather."));
        list(l,LIST_WEATHERS,"wx:","Sets the weather until you clear it.");
    }

    private SideMenu.Row setting(String key){
        final SettingSpec spec=SettingsSchema.spec(key);
        final int saved=settings.get(key);
        final List<Integer> vs=new ArrayList<>();
        for(int v=spec.min;v<=spec.max;v+=spec.step)if(v==saved || capabilities.valueSupported(key,v))vs.add(v);
        final int[] values=new int[vs.size()];final String[] labels=new String[values.length];int at=0;
        for(int i=0;i<values.length;++i){values[i]=vs.get(i);labels[i]=spec.format(values[i]);if(values[i]==saved)at=i;}
        shownValues.put(key,values);
        final boolean enabled=values.length>1;
        final String help=enabled?SettingsPages.explain(key,capabilities):SettingsPages.TEXT_NOT_IN_VERSION;
        final String label=Scene.titleCase(spec.label);
        if(spec.kind==SettingSpec.Kind.TOGGLE)return SideMenu.Row.toggle(key,label,saved==1,enabled,!live(key),help);
        final SideMenu.Row row=SideMenu.Row.choice(key,label,labels,at,enabled,!live(key),help);
        return spec.kind==SettingSpec.Kind.SLIDER && spec.max>0?row.slider(saved/(float)spec.max):row; // (menu-redesign: the game's thin bar)
    }
    /** The setting's value for a CHANGE event's index on its row (as last built); -1 for an unknown row or index. */
    int settingValue(String key,int index){
        final int[] vs=shownValues.get(key);
        if(vs==null)return -1;
        if(SettingsSchema.spec(key).kind==SettingSpec.Kind.TOGGLE)return index==1?1:0;
        return index>=0 && index<vs.length?vs[index]:-1;
    }

    /** The trainer's last result on its pages (the library's "seq=N ok|fail|blocked|dropped <action> [detail]"), in words. */
    @Override public String status(SideMenu.Page page){
        if(page==SideMenu.Page.SET_SUPPORT)return diagNote==null?"":diagNote; // (Update 1: Diagnostics)
        if(trainer!=null && page.name().startsWith("ON_") && imenuNote!=null)return imenuNote; // (Update 1: the game's menu did not open)
        if(trainer!=null && page.name().startsWith("ON_")){final String n=close.note();return n!=null?n:OnlineMenu.statusText(trainer.status());} // (Update 1)
        if(trainer==null || !page.name().startsWith("TR_"))return "";
        if(pinNote!=null)return pinNote; // (Update 1 QoL: the pin just made)
        final String n=close.note(); // (Update 1 QoL: waiting for a request that closes the menu, or refused at the call)
        return n!=null?n:trainerStatusText(trainer.status());
    }
    static String trainerStatusText(String raw){
        if(raw==null)return "";
        final String[] w=raw.trim().split("\\s+");
        if(w.length<3 || !w[0].startsWith("seq="))return "";
        final String what=w[2].replace('-',' ');
        final StringBuilder detail=new StringBuilder();
        for(int i=3;i<w.length;++i)if(w[i].indexOf('=')<0){if(detail.length()>0)detail.append(' ');detail.append(w[i].replace('-',' '));}
            else if(w[i].startsWith("blocked="))detail.append(detail.length()>0?" ":"").append(w[i].substring(8).replace('-',' '));
        final String tail=detail.length()>0?" ("+detail+")":"";
        switch(w[1]){
            case "ok":return "Done: "+what+".";
            case "fail":return "Not done: "+what+tail+".";
            case "blocked":return "Waiting: "+what+tail+".";
            case "dropped":return "Not done, the game was busy: "+what+tail+".";
            default:return "";
        }
    }
    /** The library's mods line ("enabled=1 found=3 applied=2 skipped=1") in words; "" without it. */
    static String modsText(String raw){
        if(raw==null || raw.isEmpty())return "";
        final String live=ModsPage.inGame(raw);if(live!=null)return live; // Update 1: the library's v2 status, per mod
        int enabled=-1,found=0,applied=0,skipped=0;
        for(String w:raw.split("\n")[0].trim().split("\\s+")){
            final int eq=w.indexOf('=');if(eq<=0)continue;
            int v;try{v=Integer.parseInt(w.substring(eq+1));}catch(NumberFormatException no){continue;}
            switch(w.substring(0,eq)){case "enabled":enabled=v;break;case "found":found=v;break;case "applied":applied=v;break;case "skipped":skipped=v;break;default:break;}
        }
        if(enabled!=1)return "";
        if(found==0)return "No mods in the mods folder.";
        return applied+(applied==1?" mod":" mods")+" applied"+(skipped>0?", "+skipped+" skipped":"")+".";
    }
    static String hourText(int h){return String.format(java.util.Locale.ROOT,"%02d:00",h);}
    private void list(List<SideMenu.Row> l,int list,String prefix,String help){
        final int n=trainer==null?0:Math.max(0,trainer.count(list));
        for(int i=0;i<n;++i){final String label=trainer.label(list,i);l.add(SideMenu.Row.action(prefix+i,label==null?"?":label,help));}
    }

    static final String TEXT_CASH_ONLINE="Add Cash is in Online mode.";
    /** 2026-11-09: the Cash page - the Online tab's rows (trainer on, Sandbox), an Online character and a library with "add-cash".
     *  (2026-11-12 qol25e: was "a local Online session (bit 24)"; bit 24 is StateRealMenuAvailable, set only while OUR launcher runs
     *  am_pi_menu, so on the game's own route (freemode starts am_pi_menu itself; t-pad25 runs 2/3, frame b1-cash-main.jpg) it stayed 0 and
     *  the page never showed. Bit 20 (StateOnlineCharacter: a freemode model is the player) is set in the session; the library's add-cash
     *  still refuses outside a local session ("fail add-cash"), so the row can never act on Story or a dead session.) */
    boolean cashShown(){final int s=trainer==null?-100:trainer.state();
        return onlineOn() && s!=-100 && (s&OnlineMenu.STATE_ONLINE_CHARACTER)!=0 && OnlineMenu.cashAction(trainer)>=0;}
}
