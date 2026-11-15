package dev.recompbootstrap.smoke;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * The in-game menu (menu-redesign 2026-10-25, local-analysis/menu-redesign/DESIGN.md 7.3; the same file in the Thor and the one-screen
 * trees): the model of the menu that a swipe in from the left edge opens over the game, shown as the game's own pause menu - the
 * header, a strip of tabs, a column of sections and the pane of the section's rows (MenuScenes, the launcher's own drawing). With
 * lower = true it is the Thor's lower-screen trainer: a compact frame (no header, no column), one section per tab. Pure Java (no Android
 * types): tests/SideMenuTest.java runs it on the host.
 *
 * Opening (SPEC 12.1, unchanged): a finger that lands in the left edge strip (EDGE of the view's width) and moves right OPEN_DX of the
 * width, more across than down, within OPEN_MS; or the touchscreen's Back. A touch that starts in the strip belongs to the menu's gesture
 * only, unless an on-screen control lies under it (controls). The game keeps running; its picture behind the menu is a blurred copy
 * taken when the menu opens (SideMenuView). A gamepad drives the menu and the game gets none of its input.
 *
 * Navigation (the game's): two levels, the column and the pane. Column: UP / DOWN choose the section, RIGHT or A enter its pane, B
 * closes. Pane: UP / DOWN choose the row, LEFT / RIGHT step a value, A acts, B back to the column. L1 / R1 change the tab from anywhere,
 * START closes. Touch: a tab, a section or a row is tapped directly; a value row takes two taps (the first highlights it, then the
 * highlighted row's left half steps back and its right half forward), an action row (the trainer's) acts on one; the prompts are
 * buttons; a drag on the pane moves through the rows; a swipe left closes.
 *
 * A setting that applies at the next start (restart) shows the ALERT once when the tab is left or the menu closes after it changed; a
 * tap, A or B on the ALERT continues with what led to it. The rows come from the host (Rows); this model keeps the tab, the section,
 * the highlight and the gestures, and reports what the player did as Events (ids unchanged since v21: SideMenuApply).
 */
final class SideMenu {
    static final float EDGE=0.035f,OPEN_DX=0.06f;
    static final long OPEN_MS=600;
    /** A tap moves at most this share of the view's smaller side; a drag beyond it on the pane moves through the rows. */
    static final float TAP_SLOP=0.02f;

    /** The sections (a tab's column items). TR_LOWER_PLAYER: the lower screen's Player tab (the trainer's switch, then Player's rows). */
    enum Page {SET_DISPLAY,SET_FRAME_RATE,SET_WORLD,SET_CONTROLS,SCR_LOWER,SCR_WHEELS,SCR_PHONE,TOUCH,
        TR_TRAINER,TR_PLAYER,TR_VEHICLES,TR_TELEPORT,TR_WORLD,TR_LOWER_PLAYER,MODS,MY_MUSIC,
        ON_CHARACTER,ON_CREATOR,ON_PLACES,ON_ACTIVITIES,   // (Update 1: the Online tab, OnlineMenu)
        TR_FAVOURITES,   // (Update 1 QoL: the trainer's pinned items, Favourites)
        SET_SUPPORT}     // (Update 1: Settings > Support, Copy Diagnostics)
    enum Kind {ACTION,PAGE,CHOICE,TOGGLE}
    /** One row. value / values: a CHOICE's index into its labels; a TOGGLE's 0 / 1. help: what it does, or why it is disabled. */
    static final class Row {
        final String id,label,help;final Kind kind;final String[] values;final int value;final boolean enabled,restart;final Page target;
        /** A slider setting's position 0..1 (the game's thin bar is drawn); negative for every other row. */
        final float fraction;
        Row(String id,String label,Kind kind,String[] values,int value,boolean enabled,boolean restart,Page target,String help){
            this(id,label,kind,values,value,enabled,restart,target,help,-1f);}
        Row(String id,String label,Kind kind,String[] values,int value,boolean enabled,boolean restart,Page target,String help,float fraction){
            this.id=id;this.label=label;this.kind=kind;this.values=values;this.value=value;this.enabled=enabled;this.restart=restart;this.target=target;this.help=help;
            this.fraction=fraction;}
        /** The same row drawn as a slider at fraction (0..1). */
        Row slider(float fraction){return new Row(id,label,kind,values,value,enabled,restart,target,help,fraction);}
        static Row action(String id,String label,String help){return new Row(id,label,Kind.ACTION,null,0,true,false,null,help);}
        static Row toggle(String id,String label,boolean on,boolean enabled,boolean restart,String help){
            return new Row(id,label,Kind.TOGGLE,new String[]{"Off","On"},on?1:0,enabled,restart,null,help);}
        static Row choice(String id,String label,String[] values,int value,boolean enabled,boolean restart,String help){
            return new Row(id,label,Kind.CHOICE,values,value,enabled,restart,null,help);}
        /** The value shown: a choice's label or On / Off; "" for actions. */
        String shown(){return values==null || value<0 || value>=values.length?"":values[value];}
    }
    /** A tab: its name and its sections with their names in the column. */
    static final class Tab {
        final String title;final Page[] pages;final String[] names;
        Tab(String title,Page[] pages,String[] names){this.title=title;this.pages=pages;this.names=names;}
    }
    /** What the player did. CHANGE: row id and its new value; ACTION: row id; OPENED / CLOSED; ALERT_SHOWN; PAGE: the section shown;
     *  HOLD: row id held (A held, or a touch held, HOLD_MS) on a row the host lets be held (Rows.holdable: pin / unpin). */
    static final class Event {
        enum Type {OPENED,CLOSED,PAGE,CHANGE,ACTION,ALERT_SHOWN,HOLD}
        final Type type;final String id;final int value;final Page page;
        Event(Type type,String id,int value,Page page){this.type=type;this.id=id;this.value=value;this.page=page;}
        @Override public String toString(){return type+(id!=null?" "+id:"")+(type==Type.CHANGE?"="+value:"")+(page!=null?" "+page:"");}
    }
    interface Rows {
        /** The tabs of the main screen's menu, or of the lower screen's trainer. */
        List<Tab> tabs(boolean lower);
        List<Row> rows(Page page);
        /** A result line under the help (the trainer's last request), or "". Words starting "Done" are drawn in the good colour. */
        default String status(Page page){return "";}
        /** The info block at the top of the section's pane (why it is empty, the mods' state), or "". */
        default String info(Page page){return "";}
        /** The small lines at the header's right. */
        default List<String> header(){return Arrays.asList("In Game");}
        /** Update 1 QoL: may this row be held (A held / touch held: HOLD instead of acting)? Then A acts when it is let go. */
        default boolean holdable(Page page,Row row){return false;}
        /** The prompt for Y on a row that may be held (Pin / Unpin). */
        default String holdLabel(Page page,Row row){return "Pin";}
    }
    /** Whether an on-screen control lies under a point (TouchPad): a finger there is the control's, also inside the edge strip. */
    interface Controls {boolean at(float x,float y);}
    private Controls controls=(x,y)->false;
    void controls(Controls c){controls=c==null?(x,y)->false:c;}

    private final Rows source;private final boolean lower,edgeOpens;
    private final List<Event> events=new ArrayList<>();
    private boolean open,armed,alert,inPane;
    private int tab;
    private final int[] section=new int[16];
    private final int[] highlight=new int[Page.values().length],first=new int[Page.values().length];
    /** Update 1 QoL (menus plan A4): the highlighted row of each page by id; its index follows the id when the list changes. */
    private final String[] highlightId=new String[Page.values().length];
    private boolean restartPending;
    private boolean wasInPane;   // (Update 1 QoL: the menu reopens where it was left, the pane included)
    private String pendingTab;private final java.util.Map<String,Integer> pendingSections=new java.util.HashMap<>();
    private int finger=-1;private float downX,downY,lastY,dragged;private long downAt;private boolean moved;
    private float vw=1,vh=1,xdpi=0;
    private Scene.Measure measure=(text,size,face)->text.length()*size*0.5f;
    private Scene shown;

    SideMenu(Rows source){this(source,false,true);}
    /** lower: the Thor's lower-screen trainer (compact frame, one section per tab, opened by its own gesture, not the edge swipe). */
    SideMenu(Rows source,boolean lower,boolean edgeOpens){this.source=source;this.lower=lower;this.edgeOpens=edgeOpens;}

    void view(float w,float h){vw=Math.max(1f,w);vh=Math.max(1f,h);}
    /** The screen's density (DisplayMetrics.xdpi): the lower screen's frame is drawn at the touch size (MenuLayout.touchUnit). */
    void density(float dotsPerInch){xdpi=dotsPerInch;}
    /** The text measure of the painter that draws the menu (UiKit.Painter), so that what is built is what is shown. */
    void measure(Scene.Measure m){if(m!=null)measure=m;}
    boolean isOpen(){return open;}
    /** A page whose lines follow the library as it applies requests (the trainer's and the Online tab's): redrawn while shown. */
    static boolean live(Page p){return p!=null && (p.name().startsWith("TR_") || p.name().startsWith("ON_"));}
    boolean alertShown(){return alert;}
    boolean lower(){return lower;}
    int tab(){return tab;}
    boolean inPane(){return inPane;}
    Page page(){final Tab t=tabs().get(tab);return t.pages[Math.min(section[tab],t.pages.length-1)];}
    int highlighted(){return index(page(),rows());}
    /** The page's highlighted row in rs: the row with its remembered id when that row is there, else the remembered position (within
     *  the list); the id is kept, so a row that comes back is found again. */
    private int index(Page p,List<Row> rs){
        final String id=highlightId[p.ordinal()];
        if(id!=null)for(int i=0;i<rs.size();++i)if(rs.get(i).id.equals(id)){highlight[p.ordinal()]=i;return i;}
        return Math.min(highlight[p.ordinal()],Math.max(0,rs.size()-1));
    }
    private void mark(Page p,List<Row> rs,int i){highlight[p.ordinal()]=i;highlightId[p.ordinal()]=i>=0 && i<rs.size()?rs.get(i).id:null;}
    List<Row> rows(){return source.rows(page());}
    List<Tab> tabs(){return source.tabs(lower);}
    private boolean single(){return tabs().get(tab).pages.length<=1;}
    /** The events since the last call (the host applies them). */
    List<Event> take(){final List<Event> out=new ArrayList<>(events);events.clear();return out;}

    // ------------------------------------------------------------------------------------------------ opening and closing
    void openMenu(){
        if(open)return;
        open=true;alert=false;after=After.NONE;restartPending=false;
        applyPending();
        if(tab>=tabs().size())tab=0;
        inPane=lower || single() || (wasInPane && !rows().isEmpty()); // (Update 1 QoL: back on the row it was left on; a one-section tab has no column, as toTab)
        events.add(new Event(Event.Type.OPENED,null,0,null));
    }
    /** Close (START, the Resume prompt, a swipe left, B at the top level): after a restart setting changed, the ALERT first. */
    void close(){
        if(!open)return;
        if(restartPending){showAlert(After.CLOSE,0);return;}
        wasInPane=inPane;aDownAt=-1;
        open=false;alert=false;after=After.NONE;finger=-1;
        events.add(new Event(Event.Type.CLOSED,null,0,null));
    }
    /** Back (B, the touchscreen's Back, the Back prompt): Continue on the ALERT; from the pane to the column; else close. */
    void back(){
        if(!open)return;
        if(alert){dismissAlert();return;}
        if(inPane && !single() && !lower){inPane=false;return;}
        close();
    }
    private enum After {NONE,CLOSE,TAB}
    private After after=After.NONE;private int afterTab;
    private void showAlert(After then,int toTab){alert=true;after=then;afterTab=toTab;events.add(new Event(Event.Type.ALERT_SHOWN,null,0,null));}
    /** TEST ONLY (the debug build's shell broadcast, for a screenshot of the ALERT without changing a saved setting): open, ALERT shown. */
    void alertForTest(){openMenu();showAlert(After.NONE,0);}
    /** Continue on the ALERT (a tap anywhere, A, B, Back): then what led to it (closing, or the other tab). */
    void dismissAlert(){
        if(!alert)return;
        alert=false;restartPending=false;
        final After then=after;after=After.NONE;
        if(then==After.CLOSE)close();else if(then==After.TAB)toTab(afterTab);
    }

    // ------------------------------------------------------------------------------------------------ tabs and sections
    void prevTab(){if(open && !alert)toTab(tab-1);}
    void nextTab(){if(open && !alert)toTab(tab+1);}
    /** Another tab (wrapping): after a restart setting changed on this one, the ALERT first. */
    void toTab(int t){
        final int n=tabs().size();if(n==0)return;
        t=((t%n)+n)%n;if(t==tab)return;
        if(restartPending){showAlert(After.TAB,t);return;}
        tab=t;inPane=lower || single();
        events.add(new Event(Event.Type.PAGE,null,0,page()));
    }
    private void toSection(int s){
        final Tab t=tabs().get(tab);
        s=((s%t.pages.length)+t.pages.length)%t.pages.length;
        if(s==section[tab])return;
        section[tab]=s;
        events.add(new Event(Event.Type.PAGE,null,0,page()));
    }

    // ------------------------------------------------------------------------------------------------ gamepad / keys
    void up(){if(alert)return;if(inPane)step(-1);else toSection(section[tab]-1);}
    void down(){if(alert)return;if(inPane)step(+1);else toSection(section[tab]+1);}
    void left(){if(alert || !inPane)return;final Row r=current();if(r!=null && r.enabled && (r.kind==Kind.CHOICE || r.kind==Kind.TOGGLE))activate(r,-1);}
    void right(){
        if(alert)return;
        if(!inPane){enterPane();return;}
        final Row r=current();if(r!=null && r.enabled && (r.kind==Kind.CHOICE || r.kind==Kind.TOGGLE))activate(r,+1);
    }
    void select(){
        if(alert){dismissAlert();return;}
        if(!inPane){enterPane();return;}
        final Row r=current();if(r!=null && r.enabled)activate(r,+1);
    }
    private void enterPane(){if(!rows().isEmpty())inPane=true;}
    /** Update 1 QoL (menus plan A2): does a held LEFT / RIGHT keep stepping the highlighted row? Only a value row with more than two
     *  values (a CHOICE); a toggle or a two-value choice changes once per press. */
    boolean valueRepeats(){
        if(alert || !inPane)return false;
        final Row r=current();
        return r!=null && r.enabled && r.kind==Kind.CHOICE && r.values!=null && r.values.length>2;
    }
    private Row current(){final List<Row> rs=rows();final int i=index(page(),rs);return i>=0 && i<rs.size()?rs.get(i):null;}
    private void step(int d){
        final List<Row> rs=rows();final int n=rs.size();if(n==0)return;
        mark(page(),rs,((index(page(),rs)+d)%n+n)%n);
    }

    private void activate(Row r,int dir){
        switch(r.kind){
            case PAGE:break;
            case ACTION:events.add(new Event(Event.Type.ACTION,r.id,0,null));break;
            case TOGGLE:change(r,r.value==1?0:1);break;
            case CHOICE:{final int n=r.values.length;if(n==0)break;change(r,((r.value+dir)%n+n)%n);break;}
        }
    }
    private void change(Row r,int v){
        events.add(new Event(Event.Type.CHANGE,r.id,v,null));
        if(r.restart)restartPending=true;
    }
    /** The help strip's first line: the highlighted row's help, a disabled row's reason, a restart row's note. */
    String description(){
        final Row r=inPane?current():null;
        if(r==null)return "";
        if(!r.enabled)return r.help;
        return r.restart?r.help+" Applies the next time the game starts.":r.help;
    }

    // ------------------------------------------------------------------------------------------------ the drawn frame
    /** Device pixels per panel pixel: the main screen's panel fits the view (as the launcher's); the lower screen's is the touch size. */
    float unit(){
        if(!lower)return Math.min(vw/MenuLayout.W,vh/MenuLayout.H);
        return Math.min(MenuLayout.touchUnit(xdpi),vw/700f);
    }
    MenuLayout.Frame frame(){
        if(!lower)return MenuLayout.DEFAULT;
        final float u=unit();
        return MenuLayout.Frame.compact((int)(vw/u),(int)(vh/u),u);
    }
    static final String TITLE="FivePocket"; // owner 2026-10-25

    /** What the frame shows now, as the launcher's own page description. */
    MenuScenes.Page page(MenuLayout.Frame f){
        final MenuScenes.Page p=new MenuScenes.Page();
        final List<Tab> ts=tabs();final Tab t=ts.get(tab);
        p.title=TITLE;
        p.header=!lower;
        for(String s:source.header())p.status.add(new MenuScenes.Line(s,MenuScenes.TONE_NORMAL));
        p.tabs=new String[ts.size()];for(int i=0;i<ts.size();++i)p.tabs[i]=ts.get(i).title;
        p.tab=tab;p.shoulders=!lower;
        p.backdrop=!lower;
        if(!lower && t.pages.length>1){
            p.column=t.names;p.columnSelected=section[tab];p.columnLit=true;
            p.columnEnabled=new boolean[t.pages.length];
            for(int i=0;i<t.pages.length;++i)p.columnEnabled[i]=!source.rows(t.pages[i]).isEmpty() || !source.info(t.pages[i]).isEmpty();
        }
        final Page pg=page();
        final String info=source.info(pg);
        if(!info.isEmpty())p.info.add(new MenuScenes.Line(info,MenuScenes.TONE_NORMAL));
        final List<Row> rs=source.rows(pg);
        for(Row r:rs){
            final MenuScenes.Row m=new MenuScenes.Row();
            m.label=r.label;m.enabled=r.enabled;m.restart=r.restart;m.fraction=r.fraction;
            if(r.kind==Kind.CHOICE || r.kind==Kind.TOGGLE){
                m.value=r.shown();m.arrows=true;m.halves=true;
                m.canLeft=m.canRight=r.values!=null && r.values.length>1;
            }
            p.rows.add(m);
        }
        p.rowFocused=inPane && !rs.isEmpty()?index(pg,rs):-1;
        p.rowFirst=first[pg.ordinal()];
        p.counter=true; // (Update 1 QoL: "n / m" on a long list's scroll row)
        final String help=description(),status=source.status(pg);
        if(!help.isEmpty())p.help.add(new MenuScenes.Line(help,MenuScenes.TONE_NORMAL));
        if(!status.isEmpty())p.help.add(new MenuScenes.Line(status,status.startsWith("Done")?MenuScenes.TONE_GOOD:MenuScenes.TONE_WARNING));
        if(!lower){ // (the lower screen: its compact frame draws the X and check buttons instead of prompts, MenuScenes.touchButtons)
            p.prompts.add(new MenuScenes.Prompt("Resume","START",NavInput.MENU));
            p.prompts.add(new MenuScenes.Prompt("Back","B",NavInput.BACK));
            // Update 1 QoL (menus plan A5): honest prompts: A says what it does on this row (Select an action, Change a value), none on a
            // disabled row; Y pins / unpins a trainer item (as holding A does)
            final Row cur=inPane?current():null;
            if(inPane){if(cur!=null && cur.enabled)p.prompts.add(new MenuScenes.Prompt(cur.kind==Kind.ACTION?"Select":"Change","A",NavInput.CONFIRM));}
            else if(!rs.isEmpty())p.prompts.add(new MenuScenes.Prompt("Select","A",NavInput.CONFIRM));
            if(cur!=null && cur.enabled && source.holdable(pg,cur))p.prompts.add(new MenuScenes.Prompt(source.holdLabel(pg,cur),"Y",NavInput.ALT));
        }
        return p;
    }
    /** Builds the frame to draw (and to hit-test the next taps against): the restart notice while the ALERT stands, else the menu. */
    Scene scene(){
        if(alert){shown=MenuScenes.restartAlert(ALERT_TEXT,measure);return shown;}
        final MenuLayout.Frame f=frame();
        final MenuScenes.Page p=page(f);
        first[page().ordinal()]=MenuScenes.firstRow(p,measure,f);
        p.rowFirst=first[page().ordinal()];
        shown=MenuScenes.build(p,measure,f);
        return shown;
    }
    static final String ALERT_TEXT="This setting change requires a restart.";

    // ------------------------------------------------------------------------------------------------ where it was left (Update 1 QoL)
    /** The tab, each tab's section, each page's highlighted row and whether the pane had focus, as text (the game window saves it in
     *  files/side-menu-*.txt when the menu closes and gives it back with restore() at the next start). Tabs are named by title: the
     *  tabs shown depend on the game (My Music, Online in Sandbox). */
    String place(){
        final StringBuilder b=new StringBuilder();final List<Tab> ts=tabs();
        if(tab<ts.size())b.append("tab=").append(ts.get(tab).title).append('\n');
        for(int i=0;i<ts.size() && i<section.length;++i)b.append("section=").append(ts.get(i).title).append('=').append(section[i]).append('\n');
        for(Page p:Page.values())if(highlight[p.ordinal()]>0)b.append("row=").append(p.name()).append('=').append(highlight[p.ordinal()]).append('\n');
        for(Page p:Page.values())if(highlightId[p.ordinal()]!=null)b.append("rowid=").append(p.name()).append('=').append(highlightId[p.ordinal()]).append('\n');
        b.append("pane=").append((open?inPane:wasInPane)?1:0).append('\n');
        return b.toString();
    }
    /** Back to a place(): rows at once; the tab and the sections when the menu next opens (the tabs are known then). Unknown or bad
     *  lines are skipped. */
    void restore(String text){
        if(text==null)return;
        for(String line:text.split("\n")){
            final int eq=line.indexOf('=');if(eq<0)continue;
            final String k=line.substring(0,eq),v=line.substring(eq+1).trim();
            try{
                switch(k){
                    case "tab":pendingTab=v;break;
                    case "pane":wasInPane=v.equals("1");break;
                    case "section":{final int e2=v.lastIndexOf('=');if(e2>0)pendingSections.put(v.substring(0,e2),Math.max(0,Integer.parseInt(v.substring(e2+1))));break;}
                    case "row":{final int e2=v.lastIndexOf('=');if(e2>0)highlight[Page.valueOf(v.substring(0,e2)).ordinal()]=Math.max(0,Integer.parseInt(v.substring(e2+1)));break;}
                    case "rowid":{final int e2=v.indexOf('=');if(e2>0 && e2<v.length()-1)highlightId[Page.valueOf(v.substring(0,e2)).ordinal()]=v.substring(e2+1);break;} // (ids hold ':', never '=')
                    default:break;
                }
            }catch(IllegalArgumentException bad){/* skipped */}
        }
    }
    private void applyPending(){
        if(pendingTab==null && pendingSections.isEmpty())return;
        final List<Tab> ts=tabs();
        for(int i=0;i<ts.size() && i<section.length;++i){final Integer s=pendingSections.get(ts.get(i).title);
            if(s!=null)section[i]=Math.min(s,ts.get(i).pages.length-1);}
        if(pendingTab!=null)for(int i=0;i<ts.size();++i)if(ts.get(i).title.equals(pendingTab))tab=i;
        pendingTab=null;pendingSections.clear(); // (once: a tab not shown at the first opening is not jumped to later)
    }

    // ------------------------------------------------------------------------------------------------ holding A or a row (Update 1 QoL)
    static final long HOLD_MS=600;
    private long aDownAt=-1;
    /** A pressed (the gamepad's): on a row that may be held it acts when let go (aUp), or HOLDs after HOLD_MS; elsewhere at once. */
    void aDown(long t){
        if(!alert && inPane){final Row r=current();if(r!=null && r.enabled && source.holdable(page(),r)){aDownAt=t;return;}}
        select();
    }
    /** A still down (its key repeats): HOLD once HOLD_MS have passed. */
    void aHeld(long t){if(aDownAt>=0 && t-aDownAt>=HOLD_MS){aDownAt=-1;hold();}}
    void aUp(long t){
        if(aDownAt<0)return;
        final boolean held=t-aDownAt>=HOLD_MS;aDownAt=-1;
        if(held)hold();else select();
    }
    /** Y, or the Pin prompt: the highlighted row HELD (pinned / unpinned) when it may be. */
    void pin(){if(!alert && inPane)hold();}
    private void hold(){final Row r=current();if(open && r!=null && r.enabled && source.holdable(page(),r))events.add(new Event(Event.Type.HOLD,r.id,0,null));}
    /** A touch held HOLD_MS on a row and lifted there: that row highlighted and HELD when it may be; else a plain tap. */
    void holdAt(float x,float y){
        if(shown==null)scene();
        final float u=unit();final MenuLayout.Frame f=frame();
        final int id=shown.hitAt((x-(vw-f.w*u)/2f)/u,(y-(vh-f.h*u)/2f)/u);
        final int i=id>=MenuLayout.HIT_ROW && id<MenuLayout.HIT_ARROW_LEFT?id-MenuLayout.HIT_ROW
                :id>=MenuLayout.HIT_ARROW_LEFT && id<MenuLayout.HIT_ARROW_RIGHT?id-MenuLayout.HIT_ARROW_LEFT
                :id>=MenuLayout.HIT_ARROW_RIGHT && id<MenuLayout.HIT_PROMPT?id-MenuLayout.HIT_ARROW_RIGHT:-1;
        final List<Row> rs=rows();
        if(!open || alert || i<0 || i>=rs.size() || !rs.get(i).enabled || !source.holdable(page(),rs.get(i))){hit(id);return;}
        inPane=true;mark(page(),rs,i);
        events.add(new Event(Event.Type.HOLD,rs.get(i).id,0,null));
    }

    // ------------------------------------------------------------------------------------------------ touches
    /** A finger down at (x, y) in the view: true when the menu takes it (the edge strip, or anywhere while the menu is open). */
    boolean down(int id,long t,float x,float y){
        if(open){finger=id;downX=x;downY=lastY=y;downAt=t;moved=false;dragged=0;return true;}
        if(edgeOpens && x<=EDGE*vw && !controls.at(x,y)){finger=id;downX=x;downY=lastY=y;downAt=t;moved=false;armed=true;return true;}
        return false;
    }
    void move(int id,long t,float x,float y){
        if(id!=finger)return;
        if(armed){
            final float dx=x-downX,dy=Math.abs(y-downY);
            if(t-downAt<=OPEN_MS && dx>=OPEN_DX*vw && dx>2f*dy){armed=false;finger=-1;openMenu();}
            return;
        }
        if(!open || alert)return;
        if(!moved && Math.hypot(x-downX,y-downY)>TAP_SLOP*Math.min(vw,vh))moved=true;
        if(moved && Math.abs(x-downX)<Math.abs(y-downY)){
            // a vertical drag moves the highlight through the pane's rows, one row per row pitch
            dragged+=lastY-y;
            final float pitch=MenuLayout.ROW_PITCH*unit();
            while(Math.abs(dragged)>=pitch){final int d=dragged>0?1:-1;dragged-=d*pitch;if(!inPane)enterPane();if(inPane)step(d);}
        }
        lastY=y;
    }
    void up(int id,long t,float x,float y){
        if(id!=finger)return;
        finger=-1;
        if(armed){armed=false;return;} // (an edge touch that did not open: nothing)
        if(!open)return;
        if(alert){dismissAlert();return;}
        if(moved){if(x-downX<=-OPEN_DX*vw && Math.abs(x-downX)>2f*Math.abs(y-downY))close();return;} // (a swipe left closes)
        if(t-downAt>=HOLD_MS){holdAt(x,y);return;} // (Update 1 QoL: touch and hold a row)
        tap(x,y);
    }
    void cancel(){finger=-1;armed=false;}

    /** A tap at a view point: what the frame drawn last has there (Scene.hitAt). */
    void tap(float x,float y){
        if(shown==null)scene();
        final float u=unit();final MenuLayout.Frame f=frame();
        final float px=(x-(vw-f.w*u)/2f)/u,py=(y-(vh-f.h*u)/2f)/u;
        hit(shown.hitAt(px,py));
    }
    /** A touch rectangle of the frame (MenuLayout.HIT_*). */
    void hit(int id){
        if(id<0 || !open)return;
        if(alert){dismissAlert();return;}
        if(id>=MenuLayout.HIT_PROMPT && id<MenuLayout.HIT_ALERT_BUTTON){
            switch(id-MenuLayout.HIT_PROMPT){
                case NavInput.BACK:back();return;
                case NavInput.CONFIRM:select();return;
                case NavInput.MENU:close();return;
                case NavInput.PAGE_PREV:prevTab();return;
                case NavInput.PAGE_NEXT:nextTab();return;
                case NavInput.ALT:pin();return; // (Update 1 QoL: the Pin / Unpin prompt)
                default:return;
            }
        }
        if(id>=MenuLayout.HIT_TAB && id<MenuLayout.HIT_COLUMN){toTab(id-MenuLayout.HIT_TAB);return;}
        if(id>=MenuLayout.HIT_COLUMN && id<MenuLayout.HIT_ROW){
            final int s=id-MenuLayout.HIT_COLUMN;
            if(s==section[tab] && !inPane)enterPane();else{toSection(s);inPane=false;}
            return;
        }
        final boolean leftHalf=id>=MenuLayout.HIT_ARROW_LEFT && id<MenuLayout.HIT_ARROW_RIGHT,rightHalf=id>=MenuLayout.HIT_ARROW_RIGHT && id<MenuLayout.HIT_PROMPT;
        final int i=leftHalf?id-MenuLayout.HIT_ARROW_LEFT:rightHalf?id-MenuLayout.HIT_ARROW_RIGHT:id-MenuLayout.HIT_ROW;
        final List<Row> rs=rows();
        if(i<0 || i>=rs.size())return;
        final boolean wasHighlighted=inPane && index(page(),rs)==i;
        inPane=true;mark(page(),rs,i);
        final Row r=rs.get(i);
        if(!r.enabled)return; // (highlighted: its reason shows in the help strip; nothing changes)
        // Two taps for a value (owner 2026-10-25, after one stray tap turned the Thor's lower screen off): a tap on a value row that
        // is not the highlighted one only highlights it; the left or right half of the highlighted row then steps it. An action row
        // (the trainer's) acts on one tap.
        if((r.kind==Kind.CHOICE || r.kind==Kind.TOGGLE) && !wasHighlighted)return;
        activate(r,leftHalf?-1:+1);
    }
}
