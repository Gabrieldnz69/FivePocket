#pragma once
// imenu_draw.h (PROPOSED, Update 1 menus package item 6; lane upd1-imenu/draw-glue, 2026-11-07). The Sandbox replica of the disc's
// interaction menu (am_pi_menu, the 2013 launch version), drawn, sounded and gated the way the disc's own script does it, through the
// trainer's native path (trainer::Natives: script thread, once per game frame, the PLAYER_PED_ID safe point). Route B of MENUS-PLAN.md.
// Pure: no guest memory except one documented game word (the text-command guard), no files, no allocation (fixed members only).
//
// Evidence (OBSERVED = read from the disc's own code; am_pi_menu / freemode code offsets, decoded with tools/dis2.py, and the whole
// per-frame body RUN on the host by tools/imi_reference.py - tests/imenu_reference.cmds is that run's command stream, which this file's
// output must match frame by frame; see ../NOTES.md):
//   timers  freemode 0xaa11 / am_pi_menu 0x13c6b: started on the first call (0x9214 / 0x13cc8, GET_GAME_TIMER), true once elapsed >= delay
//   open    freemode 0xcb2e5: IS_DISABLED_CONTROL_PRESSED(0,0) held, timer started on the first pressed frame, fires at >= 350 ms
//           (0xcb253) and the can-open check 0xcb3ed passes -> DISABLE_CONTROL_ACTION(0,0); still held past 350 ms without opening:
//           control 0 stays disabled (0xcb377).
//   stream  am_pi_menu 0x148 state 0: REQUEST_STREAMED_TEXTURE_DICT("MPInteraction") until loaded, then init (0x2a2e1 / 0x2a410),
//           DISABLE_CONTROL_ACTION(0,0) on those frames.
//   frame   0x2bc95: SET_INPUT_EXCLUSIVE(2, 174 175 162 161 163 164), DISABLE_CONTROL_ACTION(0, 24 ids) (+ 22 on the inventory page);
//           0x2a173: CommonMenu + instructional_buttons every frame; 0x2989d: navigation + NAV_UP_DOWN / NAV_LEFT_RIGHT;
//           HIDE_HELP_TEXT_THIS_FRAME; header 0x24171 (MPInteraction/menubar, the player's name, the crew line); the library draw
//           0x21175 (menu_draw.h with the am_pi_menu Style); accept 0x13e30 (SELECT); HIDE_HELP_TEXT_THIS_FRAME; back / close
//           0x138f6 (BACK on a sub page, CANCEL when it closes; Back held > 350 ms after a release closes too).
//   close   next frame: 0x2bc95 cool-down controls first, then RESET_HUD_COMPONENT_VALUES(15), feed height 0, CommonMenu and the movie
//           released; until 1000 ms after that frame (0x2bde3) SET_INPUT_EXCLUSIVE(2,175) + DISABLE_CONTROL_ACTION(0, 43 52 73).
// Deliberate differences from the disc (each one listed in NOTES.md 3, with its reason): no rank badge (owner default), no crew
// emblem (offline there is none: the disc draws none either), no network / stat / freemode globals, MPInteraction released on close,
// the instructional buttons re-sent only when they change (the disc re-sends identical sets on page rebuilds), widths measured once.
//
// Frame rate: Story mode never reaches this file (the glue calls it only in Sandbox mode). Closed: ONE native per frame
// (IS_DISABLED_CONTROL_PRESSED(0,0)), plus 4 control natives per frame during the 1 s cool-down, plus the can-open queries (up to 8)
// on each frame Back stays held past 350 ms without opening (as the disc). Open: the draw (about 60-90 natives,
// the same work am_pi_menu does in Online) plus 11 input reads and 6 gate queries.
#include "menu_draw.h"
#include <atomic>

namespace recomp::imenudraw {
using trainer::Natives;
using trainer::joaat;
using trainer::bits;

namespace n {
constexpr std::uint32_t Align=joaat("SET_SCRIPT_GFX_ALIGN"),AlignParams=joaat("SET_SCRIPT_GFX_ALIGN_PARAMS"),AlignReset=joaat("RESET_SCRIPT_GFX_ALIGN"),
    DrawOrder=joaat("SET_SCRIPT_GFX_DRAW_ORDER"),RequestTxd=joaat("REQUEST_STREAMED_TEXTURE_DICT"),TxdLoaded=joaat("HAS_STREAMED_TEXTURE_DICT_LOADED"),
    ReleaseTxd=joaat("SET_STREAMED_TEXTURE_DICT_AS_NO_LONGER_NEEDED"),DrawSprite=joaat("DRAW_SPRITE"),
    TextFont=joaat("SET_TEXT_FONT"),TextScale=joaat("SET_TEXT_SCALE"),TextColour=joaat("SET_TEXT_COLOUR"),TextJust=joaat("SET_TEXT_JUSTIFICATION"),
    BeginText=joaat("BEGIN_TEXT_COMMAND_DISPLAY_TEXT"),NextColour=joaat("SET_COLOUR_OF_NEXT_TEXT_COMPONENT"),
    AddLiteral=joaat("ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME"),EndText=joaat("END_TEXT_COMMAND_DISPLAY_TEXT"),
    LabelText=joaat("GET_FILENAME_FOR_AUDIO_CONVERSATION"),   // (2013 name collision: returns the localised text of a GXT key)
    PlayerName=joaat("GET_PLAYER_NAME"),PlayerId=joaat("PLAYER_ID"),PlayerPed=joaat("PLAYER_PED_ID"),
    FadedOut=joaat("IS_SCREEN_FADED_OUT"),FadingOut=joaat("IS_SCREEN_FADING_OUT"),FadingIn=joaat("IS_SCREEN_FADING_IN"),
    FrontendFading=joaat("IS_FRONTEND_FADING"),FadedIn=joaat("IS_SCREEN_FADED_IN"),PauseMenu=joaat("IS_PAUSE_MENU_ACTIVE"),
    Switching=joaat("IS_PLAYER_SWITCH_IN_PROGRESS"),Warning=joaat("IS_WARNING_MESSAGE_ACTIVE"),HideHelp=joaat("HIDE_HELP_TEXT_THIS_FRAME"),
    Exclusive=joaat("SET_INPUT_EXCLUSIVE"),Disable=joaat("DISABLE_CONTROL_ACTION"),Pressed=joaat("IS_CONTROL_PRESSED"),
    JustPressed=joaat("IS_CONTROL_JUST_PRESSED"),DisabledPressed=joaat("IS_DISABLED_CONTROL_PRESSED"),
    Playing=joaat("IS_PLAYER_PLAYING"),ControlOn=joaat("IS_PLAYER_CONTROL_ON"),Ragdoll=joaat("IS_PED_RAGDOLL"),
    Threads=joaat("GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH"),Sound=joaat("PLAY_SOUND_FRONTEND");
}
inline constexpr menudraw::Named Used[]{
    {"SET_SCRIPT_GFX_ALIGN",n::Align},{"SET_SCRIPT_GFX_ALIGN_PARAMS",n::AlignParams},{"RESET_SCRIPT_GFX_ALIGN",n::AlignReset},
    {"SET_SCRIPT_GFX_DRAW_ORDER",n::DrawOrder},{"REQUEST_STREAMED_TEXTURE_DICT",n::RequestTxd},{"HAS_STREAMED_TEXTURE_DICT_LOADED",n::TxdLoaded},
    {"SET_STREAMED_TEXTURE_DICT_AS_NO_LONGER_NEEDED",n::ReleaseTxd},{"DRAW_SPRITE",n::DrawSprite},{"SET_TEXT_FONT",n::TextFont},
    {"SET_TEXT_SCALE",n::TextScale},{"SET_TEXT_COLOUR",n::TextColour},{"SET_TEXT_JUSTIFICATION",n::TextJust},
    {"BEGIN_TEXT_COMMAND_DISPLAY_TEXT",n::BeginText},{"SET_COLOUR_OF_NEXT_TEXT_COMPONENT",n::NextColour},
    {"ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME",n::AddLiteral},{"END_TEXT_COMMAND_DISPLAY_TEXT",n::EndText},
    {"GET_FILENAME_FOR_AUDIO_CONVERSATION",n::LabelText},{"GET_PLAYER_NAME",n::PlayerName},{"PLAYER_ID",n::PlayerId},
    {"PLAYER_PED_ID",n::PlayerPed},{"IS_SCREEN_FADED_OUT",n::FadedOut},{"IS_SCREEN_FADING_OUT",n::FadingOut},
    {"IS_SCREEN_FADING_IN",n::FadingIn},{"IS_FRONTEND_FADING",n::FrontendFading},{"IS_SCREEN_FADED_IN",n::FadedIn},
    {"IS_PAUSE_MENU_ACTIVE",n::PauseMenu},{"IS_PLAYER_SWITCH_IN_PROGRESS",n::Switching},{"IS_WARNING_MESSAGE_ACTIVE",n::Warning},
    {"HIDE_HELP_TEXT_THIS_FRAME",n::HideHelp},{"SET_INPUT_EXCLUSIVE",n::Exclusive},{"DISABLE_CONTROL_ACTION",n::Disable},
    {"IS_CONTROL_PRESSED",n::Pressed},{"IS_CONTROL_JUST_PRESSED",n::JustPressed},{"IS_DISABLED_CONTROL_PRESSED",n::DisabledPressed},
    {"IS_PLAYER_PLAYING",n::Playing},{"IS_PLAYER_CONTROL_ON",n::ControlOn},{"IS_PED_RAGDOLL",n::Ragdoll},
    {"GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH",n::Threads},{"PLAY_SOUND_FRONTEND",n::Sound}};
inline constexpr std::uint32_t UsedNatives[]{
    n::Align,n::AlignParams,n::AlignReset,n::DrawOrder,n::RequestTxd,n::TxdLoaded,n::ReleaseTxd,n::DrawSprite,n::TextFont,n::TextScale,
    n::TextColour,n::TextJust,n::BeginText,n::NextColour,n::AddLiteral,n::EndText,n::LabelText,n::PlayerName,n::PlayerId,n::PlayerPed,
    n::FadedOut,n::FadingOut,n::FadingIn,n::FrontendFading,n::FadedIn,n::PauseMenu,n::Switching,n::Warning,n::HideHelp,n::Exclusive,
    n::Disable,n::Pressed,n::JustPressed,n::DisabledPressed,n::Playing,n::ControlOn,n::Ragdoll,n::Threads,n::Sound};
static_assert(std::size(Used)==std::size(UsedNatives));

// ---- what the disc says (OBSERVED; am_pi_menu offsets unless "fm" = freemode) ----
constexpr std::uint32_t HoldMs=350;                     // fm 0xcb253 (open), am 0x13aa3 (close by holding Back again)
constexpr std::uint32_t CooldownMs=1000;                // 0x2bde3
// OURS (no disc counterpart: am_pi_menu's state 0 waits for MPInteraction without a limit, and freemode ends the script by other means
// we do not replicate). Streaming that long, or the player no longer playing / in control while it streams, closes the menu, so a
// dictionary that never loads cannot keep control 0 disabled for the rest of the session (review P2).
constexpr std::uint32_t StreamTimeoutMs=3000;
constexpr int BackGroup=0,BackControl=0;                // fm 0xcb2e7 IS_DISABLED_CONTROL_PRESSED(0,0)
constexpr int Exclusive[]{174,175,162,161,163,164};     // 0x2bca2..0x2bcc8, group 2
constexpr int Disabled[]{37,136,137,138,139,140,141,142,143,144,14,15,50,51,120,121,123,45,67,76,49,43,52,73};   // 0x2bccc..0x2bd70, group 0
constexpr int InventoryDisabled=22;                     // 0x2bd8a (page 1, cursor >= the first snack row)
constexpr int CooldownExclusive=175;constexpr int CooldownDisabled[]{43,52,73};   // 0x2bdf5..0x2be0d
static_assert(std::size(Disabled)==24,"24 always + 22 on the inventory page = the plan's 25");
constexpr int CtlDown=161,CtlUp=162,CtlLeft=163,CtlRight=164,CtlAccept=174,CtlCancel=175,CtlAlt=176;   // 0x2989d, 0x13e3e, 0x13949, 0x145a7
// header (0x24171 / 0x2a410 / 0x24c99 / 0x24a64)
constexpr float HeaderAlign=-0.0755f;                   // 0x2417d
constexpr float RowH=menudraw::RowH;
constexpr float BarX=0.188f,BarY=0.1f+0.034722f,BarW=0.225f,BarH=0.09f;   // static 5.88 (0x2a425..0x2a44c)
constexpr int HeaderOrder=4;                            // 0x24d21 / 0x24d92
constexpr int NameFont=4;constexpr float NameScaleX=0.5f,NameScaleY=0.525f,NameX=0.124f,NameY=0.103f;   // static 5.53 / 5.64
constexpr int LineFont=0;constexpr float LineScaleX=0.332f,LineScaleY=0.342f,LineX=0.124f,LineY=0.136f; // static 5.66 / 5.77
constexpr int HeaderJust=1,HeaderNextColour=1;          // 0x24a83 / 0x24aa5 (0x24a64's arguments from 0x24249 / 0x24285)
constexpr const char* HeaderDict="MPInteraction";constexpr const char* HeaderBar="menubar";
constexpr const char* CrewNone="CREW_NONE";             // 0x242c6: the offline second line (the disc's own label)
// the list (0x259b5 + 0x2a410)
constexpr float ListY=0.034722f*3.0f+0.05f;             // global 51.184 = 0.154166
constexpr int MaxVisible=7;                             // 0x259d8
constexpr int PageBlue[4]{0,71,133,255};                // 0x29290 (subtitle: hidden by HIDE)
constexpr int PageBlueA[4]{0,71,133,191};               // 0x29260 / 0x29230 / 0x29200: rows, arrows bar, description (ROUND(255*0.75))
inline constexpr menudraw::Style Look{ListY,MaxVisible,true,false,true,PageBlue,PageBlueA,PageBlueA,PageBlueA};        // main page: "HIDE"
inline constexpr menudraw::Style LookTitled{ListY,MaxVisible,false,false,true,PageBlue,PageBlueA,PageBlueA,PageBlueA};  // sub pages: a title row
// (LookTitled is the library's non-HIDE path, verified against hairdo_shop_sp's copy by the shop lane, with am_pi_menu's page colours;
// am_pi_menu's own sub pages were not run on the host: the accept handler 0x13e30 is gated by 0x20fc8 there. INFERRED.)
constexpr const char* SoundSet="HUD_FRONTEND_DEFAULT_SOUNDSET";
constexpr std::uint32_t FreemodeHash=0xC875557Du;      // joaat("freemode"): a real freemode runs the real menu; the replica stands down
constexpr int FreemodeCheckFrames=30;
// The game's pending text-command word (recompiled BEGIN_TEXT_COMMAND_* -> sub_82A98348 / 82A98528 / 82A987A8: lis -31841, -27740;
// 0 = idle, 5 display, 3 width, 8 scaleform string; END (sub_82AA3E08) writes 0). Non-zero at our tick = the hosting script is between
// its own BEGIN and END: our pair would overwrite its pending label, so nothing with text is drawn this frame.
constexpr std::uint32_t TextPendingWord=0x839E93A4u;

// ---- the model (rows, pages, cursor, repeat timers: lane upd1-imenu/model-rows) ----
struct Pad {                       // one frame of the menu's controls, read once per open frame (0x2989d / 0x13e30 / 0x138f6)
    bool down=false,up=false;      // IS_CONTROL_PRESSED(2,161 / 162): hold-to-repeat is the model's (250 ms, 0x29995)
    bool left=false,right=false;   // IS_CONTROL_PRESSED(2,163 / 164)
    bool leftJust=false,rightJust=false;   // IS_CONTROL_JUST_PRESSED(2,163 / 164)
    bool accept=false,cancel=false,alt=false;   // IS_CONTROL_JUST_PRESSED(2,174 / 175 / 176)
};
enum Event : unsigned {None=0,Moved=1u,Changed=2u,Selected=4u,Back=8u,Close=16u};
// Contract: closed() is called exactly once after each open(), and never without one (a menu closed while MPInteraction was still
// streaming never called open(), so it calls no closed() either).
struct Model {
    virtual ~Model()=default;
    virtual void open(std::uint32_t nowMs) noexcept=0;                          // a fresh menu (main page, first row)
    virtual unsigned input(const Pad& pad,std::uint32_t nowMs) noexcept=0;      // this frame's Event bits
    virtual const menudraw::Page& page() const noexcept=0;                      // what to draw (rows / selected / help / buttons)
    virtual bool inventoryControls() const noexcept {return false;}             // control 22 too (0x2bd74)
    virtual void closed(const char* why) noexcept {(void)why;}
};

using LogFn=void(*)(const char*);
using HasFn=bool(*)(std::uint32_t hash);

namespace detail {
inline bool call(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a,std::uint32_t* res=nullptr) noexcept {
    std::uint32_t args[12]{};unsigned k=0;for(auto v:a){if(k<12)args[k++]=v;}
    std::uint32_t out[3]{};const bool ok=nat.call(h,args,k,out);if(res){res[0]=out[0];res[1]=out[1];res[2]=out[2];}return ok;
}
inline std::uint32_t u(int v) noexcept {return std::uint32_t(v);}
inline bool query(Natives& nat,std::uint32_t h) noexcept {std::uint32_t r[3]{};return call(nat,h,{},r) && r[0];}
inline bool query(Natives& nat,std::uint32_t h,std::uint32_t a) noexcept {std::uint32_t r[3]{};return call(nat,h,{a},r) && r[0];}
inline bool pressed(Natives& nat,std::uint32_t h,int g,int c) noexcept {std::uint32_t r[3]{};return call(nat,h,{u(g),u(c)},r) && r[0];}
inline std::uint32_t id(Natives& nat) noexcept {std::uint32_t r[3]{};return call(nat,n::PlayerId,{},r)?r[0]:0u;}
inline void disable(Natives& nat,int g,int c) noexcept {call(nat,n::Disable,{u(g),u(c)});}
inline bool requestDictAt(Natives& nat,std::uint32_t s) noexcept {
    call(nat,n::RequestTxd,{s,0});std::uint32_t r[3]{};return call(nat,n::TxdLoaded,{s},r) && r[0];
}
}

// The drawing half of one open frame, usable by whichever object owns the menu's state (Menu below, or the model lane's
// imenu::Model through imenu_view_adapter.h): the assets, the frame's draw in am_pi_menu's order, the sounds, the controls, the
// release. It reads no input and decides nothing.
class Drawer {
public:
    void setLog(LogFn f) noexcept {draw_.setLog(f);}
    void setNativeCheck(HasFn f) noexcept {draw_.setNativeCheck(f);}
    const menudraw::Renderer& renderer() const noexcept {return draw_;}
    unsigned textBusy() const noexcept {return textBusy_;}
    void resetCounters() noexcept {textBusy_=0;}
    // 0x2a173, every open frame before the input: CommonMenu (every frame) + instructional_buttons. false = nothing drawn yet.
    bool load(Natives& nat) noexcept {return draw_.load(nat,nullptr,&Look);}
    // 0x2115e .. 0x21175: the text guard, HIDE_HELP_TEXT_THIS_FRAME, the header, the list behind the library's gate 0x240e9.
    // A page with a title row is drawn with LookTitled, else with the main page's "HIDE" Look. false = skipped by the guard.
    bool draw(Natives& nat,const menudraw::Page& page) noexcept {
        if(nat.load(TextPendingWord)!=0){++textBusy_;return false;}   // someone else's text command is open
        detail::call(nat,n::HideHelp,{});
        header(nat);
        if(listAllowed(nat)){menudraw::Page p=page;p.style=p.title && p.title[0]?&LookTitled:&Look;draw_.draw(nat,p);}
        return true;
    }
    static void hideHelp(Natives& nat) noexcept {detail::call(nat,n::HideHelp,{});}   // 0x1c0 (the body's own, after accept)
    static void sound(Natives& nat,const char* name) noexcept {
        const std::uint32_t a=nat.text(name),b=nat.text(SoundSet);if(a && b)detail::call(nat,n::Sound,{0xFFFFFFFFu,a,b});
    }
    static void openControls(Natives& nat,bool inventory) noexcept {          // 0x2bc95, every open frame
        for(int c:Exclusive)detail::call(nat,n::Exclusive,{2,detail::u(c)});
        for(int c:Disabled)detail::disable(nat,0,c);
        if(inventory)detail::disable(nat,0,InventoryDisabled);
    }
    static void cooldownControls(Natives& nat) noexcept {                    // 0x2bdf5, every frame of the 1 s after a close
        detail::call(nat,n::Exclusive,{2,detail::u(CooldownExclusive)});
        for(int c:CooldownDisabled)detail::disable(nat,0,c);
    }
    // 0x13709 (+ ours: MPInteraction). Idempotent on the renderer side (nothing loaded = nothing released but MPInteraction).
    void release(Natives& nat,const char* why) noexcept {
        draw_.close(nat,why);                                    // RESET_HUD_COMPONENT_VALUES(15), feed 0, CommonMenu, the movie
        const std::uint32_t d=nat.text(HeaderDict);if(d)detail::call(nat,n::ReleaseTxd,{d});   // (the disc keeps it until freemode ends)
    }
    static bool requestHeader(Natives& nat) noexcept {const std::uint32_t s=nat.text(HeaderDict);return s && detail::requestDictAt(nat,s);}
private:
    // 0x240e9 (named part): the list is drawn only when the screen is faded in and no pause menu, switch or warning is up
    static bool listAllowed(Natives& nat) noexcept {
        using detail::query;
        return query(nat,n::FadedIn) && !query(nat,n::PauseMenu) && !query(nat,n::Switching) && !query(nat,n::Warning);
    }
    // 0x246bc: the header's own gate (asked once per frame; the disc asks it per element)
    static bool headerAllowed(Natives& nat) noexcept {
        using detail::query;
        return !query(nat,n::FadedOut) && !query(nat,n::FadingOut) && !query(nat,n::FadingIn) && !query(nat,n::FrontendFading);
    }
    static void header(Natives& nat) noexcept {
        using detail::call;using detail::u;
        call(nat,n::Align,{u('L'),u('T')});call(nat,n::AlignParams,{bits(HeaderAlign),bits(HeaderAlign),bits(0.0f),bits(0.0f)});
        const std::uint32_t d=nat.text(HeaderDict);
        const bool barIn=d && detail::requestDictAt(nat,d);
        const bool allowed=headerAllowed(nat);
        if(barIn && allowed){
            const std::uint32_t t=nat.text(HeaderBar);
            call(nat,n::DrawOrder,{u(HeaderOrder)});
            if(d && t)call(nat,n::DrawSprite,{d,t,bits(BarX),bits(BarY),bits(BarW),bits(BarH),bits(0.0f),255,255,255,255});
            call(nat,n::DrawOrder,{u(HeaderOrder)});
        }
        if(allowed){
            std::uint32_t r[3]{};
            const std::uint32_t name=call(nat,n::PlayerName,{detail::id(nat)},r)?r[0]:0u;   // the game's own string: the profile name
            if(name)headerText(nat,name,NameFont,NameScaleX,NameScaleY,NameX,NameY);
            const std::uint32_t key=nat.text(CrewNone);
            const std::uint32_t line=key && call(nat,n::LabelText,{key},r)?r[0]:0u;  // the label's text, through STRING (0x242c7)
            if(line)headerText(nat,line,LineFont,LineScaleX,LineScaleY,LineX,LineY);
        }
        call(nat,n::AlignReset,{});
    }
    static void headerText(Natives& nat,std::uint32_t text,int font,float sx,float sy,float x,float y) noexcept {
        using detail::call;using detail::u;
        const std::uint32_t l=nat.text(menudraw::Literal);if(!l)return;
        call(nat,n::TextFont,{u(font)});call(nat,n::TextScale,{bits(sx),bits(sy)});call(nat,n::TextColour,{0,0,0,255});   // 0x248db
        call(nat,n::TextJust,{u(HeaderJust)});
        call(nat,n::BeginText,{l});call(nat,n::NextColour,{u(HeaderNextColour)});call(nat,n::AddLiteral,{text});
        call(nat,n::EndText,{bits(x),bits(y)});
    }
    menudraw::Renderer draw_{};unsigned textBusy_=0;
};

class Menu {
public:
    enum State : int {Closed=0,Opening=1,Open=2,Closing=3};
    Menu() noexcept=default;
    void setLog(LogFn f) noexcept {log_=f;d_.setLog(f);}
    void setNativeCheck(HasFn f) noexcept {has_=f;d_.setNativeCheck(f);}
    void setModel(Model* m) noexcept {model_=m;}
    // Script thread, every tick before tick(): another menu that shares the D-pad / accept / cancel controls is up (the story phone,
    // the property-doors chooser, the creator menu). Refuses an open like the can-open check does; an open menu is not affected.
    void setBlocked(bool b) noexcept {blocked_=b;}
    // Any thread (the app's touch gesture / the menu-style setting): open without the hold, or close. One atomic store each.
    // An open request is served only by a Closed frame; one that arrives while the menu is Opening / Open / Closing is dropped.
    void requestOpen() noexcept {request_.store(1,std::memory_order_relaxed);}
    void requestClose() noexcept {request_.store(2,std::memory_order_relaxed);}
    State state() const noexcept {return state_;}                 // (script thread)
    bool cooling() const noexcept {return cooling_;}              // (script thread)
    unsigned stateWord() const noexcept {return word_.load(std::memory_order_relaxed);}   // any thread: state | cool-down << 4
    unsigned textBusyFrames() const noexcept {return d_.textBusy();}
    const menudraw::Renderer& renderer() const noexcept {return d_.renderer();}

    // Every Sandbox game frame at the trainer safe point (the glue never calls it in Story mode).
    void tick(Natives& nat,std::uint32_t nowMs) noexcept {
        if(unavailable_ || !model_)return;
        if(!checked_){checked_=true;if(has_)for(const auto& u:Used)if(!has_(u.hash)){unavailable_=true;logf("RENDER_IMENU off reason=missing-native name=%s",u.name);return;}}
        switch(state_){
        case Closed:closedFrame(nat,nowMs);break;
        case Opening:openingFrame(nat,nowMs);break;
        case Open:openFrame(nat,nowMs);break;
        case Closing:closingFrame(nat,nowMs);break;
        }
        word_.store(unsigned(state_)|(cooling_?16u:0u),std::memory_order_relaxed);
    }
    // Every other exit (Sandbox ended, the library is being torn down): released now, idempotent; no cool-down is owed afterwards.
    void shutdown(Natives& nat,const char* why) noexcept {
        if(state_==Open || state_==Opening || state_==Closing){d_.release(nat,why);if(model_ && modelOpen_)model_->closed(why);logf("RENDER_IMENU closed reason=%s",why?why:"shutdown");}
        state_=Closed;cooling_=false;holding_=false;waitRelease_=false;modelOpen_=false;request_.store(0,std::memory_order_relaxed);
        word_.store(0,std::memory_order_relaxed);
    }

private:
    // ---- closed: one poll ----
    void closedFrame(Natives& nat,std::uint32_t now) noexcept {
        if(cooling_){if(now-closedAt_>=CooldownMs)cooling_=false;else Drawer::cooldownControls(nat);}
        const int req=request_.exchange(0,std::memory_order_relaxed);
        const bool back=pressed(nat,n::DisabledPressed,BackGroup,BackControl);   // THE closed-state native
        if(req==1){if(canOpen(nat))beginOpening(nat,now,"request");return;}
        if(!back){holding_=false;waitRelease_=false;return;}
        if(waitRelease_)return;                                  // (a close by Back, or a refused hold: release first)
        if(!holding_){holding_=true;holdAt_=now;return;}         // the timer starts on the first pressed frame
        if(now-holdAt_<HoldMs)return;
        if(canOpen(nat)){holding_=false;disable(nat,BackGroup,BackControl);beginOpening(nat,now,"hold");}   // fm 0xcb34d
        else disable(nat,BackGroup,BackControl);                 // fm 0xcb377: past 350 ms, control 0 stays disabled while held
    }
    void beginOpening(Natives&,std::uint32_t now,const char* how) noexcept {
        state_=Opening;openedAt_=now;logf("RENDER_IMENU opening via=%s",how);
    }
    // ---- state 0: stream MPInteraction ----
    void openingFrame(Natives& nat,std::uint32_t now) noexcept {
        if(request_.exchange(0,std::memory_order_relaxed)==2){finish(nat,now,"request",false);return;}   // (an open request: dropped)
        if(cooling_){if(now-closedAt_>=CooldownMs)cooling_=false;else Drawer::cooldownControls(nat);}   // 0x2bda5 runs in state 0 too
        if(Drawer::requestHeader(nat)){
            model_->open(now);modelOpen_=true;state_=Open;cooling_=false;backArmed_=false;frames_=0;d_.resetCounters();holdBackAt_=0;holdingBack_=false;
            logf("%s","RENDER_IMENU open");
        } else if(now-openedAt_>=StreamTimeoutMs){finish(nat,now,"stream-timeout",true);}   // OURS: bounded (the disc waits forever)
        else {                                                   // OURS: asked only on frames still streaming (none in the 1:1 run)
            const std::uint32_t pl=id(nat);
            if(!query(nat,n::Playing,pl))finish(nat,now,"not-playing",true);
            else if(!query(nat,n::ControlOn,pl))finish(nat,now,"control-off",true);
        }
        disable(nat,BackGroup,BackControl);                      // 0x1a2 (also on a decision frame: control 0 is ours until the release)
    }
    // ---- state 1 ----
    void openFrame(Natives& nat,std::uint32_t now) noexcept {
        if(request_.exchange(0,std::memory_order_relaxed)==2){finish(nat,now,"request",false);return;}   // (an open request: dropped)
        const bool back=pressed(nat,n::DisabledPressed,BackGroup,BackControl);
        if(!back)backArmed_=true;                                // 0x2c0e9: bit 17 cleared once Back is let go after the open
        Drawer::openControls(nat,model_->inventoryControls());   // 0x2bc95
        if(++frames_%FreemodeCheckFrames==0 && freemodeRuns(nat)){finish(nat,now,"freemode",false);return;}
        const bool ready=d_.load(nat);                           // 0x2a173
        unsigned ev=None;
        if(ready && !query(nat,n::Warning)) {                    // 0x210a8: no input while a warning screen is up
            ev=model_->input(readPad(nat),now);
            if(ev&Moved)Drawer::sound(nat,"NAV_UP_DOWN");        // 0x298c3 / 0x299c9
            if(ev&Changed)Drawer::sound(nat,"NAV_LEFT_RIGHT");   // 0x29ade / 0x29c4d
            d_.draw(nat,model_->page());                         // guard, 0x2115e, header 0x24171, list 0x21175
        }
        if(ev&Selected)Drawer::sound(nat,"SELECT");              // 0x13e4e
        Drawer::hideHelp(nat);                                   // 0x1c0
        // 0x138f6, in its order
        const std::uint32_t pl=id(nat);
        if(!query(nat,n::Playing,pl)){finish(nat,now,"not-playing",false);return;}
        if(!query(nat,n::ControlOn,pl)){finish(nat,now,"control-off",false);return;}
        if(ev&Back)Drawer::sound(nat,"BACK");                    // 0x13994..
        if(ev&Close){Drawer::sound(nat,"CANCEL");finish(nat,now,"cancel",false);return;}   // 0x1397a
        if(back && backArmed_) {                                 // 0x13a84: Back held again for > 350 ms
            if(!holdingBack_){holdingBack_=true;holdBackAt_=now;}   // (0x13c6b: started here, elapsed 0 < 350)
            else if(now-holdBackAt_>=HoldMs){Drawer::sound(nat,"CANCEL");finish(nat,now,"back-hold",true);return;}   // 0x13ac4
        } else holdingBack_=false;
    }
    // The decision frame ends here; the release and the cool-down start on the next frame, as state 2 does.
    void finish(Natives& nat,std::uint32_t now,const char* why,bool byBack) noexcept {
        (void)nat;(void)now;why_=why;state_=Closing;waitRelease_=byBack;holding_=false;
    }
    void closingFrame(Natives& nat,std::uint32_t now) noexcept {
        cooling_=true;closedAt_=now;Drawer::cooldownControls(nat);   // 0x2bda5: the timer restarts on this frame
        d_.release(nat,why_);                                    // 0x13709
        if(modelOpen_){model_->closed(why_);modelOpen_=false;}   // (Model contract: closed() only after open())
        logf("RENDER_IMENU closed reason=%s textBusy=%u",why_,d_.textBusy());
        state_=Closed;
        request_.store(0,std::memory_order_relaxed);            // a request sent before this release frame is stale (review P1)
        waitRelease_=waitRelease_ || pressed(nat,n::DisabledPressed,BackGroup,BackControl);   // a held Back never re-opens by itself
    }
    Pad readPad(Natives& nat) noexcept {
        Pad p;
        p.down=pressed(nat,n::Pressed,2,CtlDown);p.up=pressed(nat,n::Pressed,2,CtlUp);
        p.leftJust=pressed(nat,n::JustPressed,2,CtlLeft);p.left=p.leftJust || pressed(nat,n::Pressed,2,CtlLeft);
        p.rightJust=pressed(nat,n::JustPressed,2,CtlRight);p.right=p.rightJust || pressed(nat,n::Pressed,2,CtlRight);
        p.accept=pressed(nat,n::JustPressed,2,CtlAccept);p.cancel=pressed(nat,n::JustPressed,2,CtlCancel);p.alt=pressed(nat,n::JustPressed,2,CtlAlt);
        return p;
    }
    // fm 0xcb3ed (self): playing, not ragdolling, in control; plus no warning screen / pause menu and no real freemode (INFERRED subset;
    // the helpers not replicated are listed in NOTES 3). blocked_ stands in for those that guard other menus on the same controls
    // (0x9e84: likely the phone). The disc asks this every frame Back stays held past 350 ms (fm 0xcb339..0xcb345), and so do we.
    bool canOpen(Natives& nat) noexcept {
        if(blocked_)return false;                                // no native
        const std::uint32_t pl=id(nat);
        if(!query(nat,n::Playing,pl) || !query(nat,n::ControlOn,pl))return false;
        std::uint32_t r[3]{};const std::uint32_t ped=call(nat,n::PlayerPed,{},r)?r[0]:0u;
        if(!ped || query(nat,n::Ragdoll,ped))return false;
        if(query(nat,n::Warning) || query(nat,n::PauseMenu))return false;
        return !freemodeRuns(nat);
    }
    bool freemodeRuns(Natives& nat) noexcept {std::uint32_t r[3]{};return call(nat,n::Threads,{FreemodeHash},r) && r[0]!=0;}
    static void disable(Natives& nat,int g,int c) noexcept {detail::disable(nat,g,c);}
    static std::uint32_t id(Natives& nat) noexcept {return detail::id(nat);}
    static bool pressed(Natives& nat,std::uint32_t h,int g,int c) noexcept {return detail::pressed(nat,h,g,c);}
    static bool query(Natives& nat,std::uint32_t h) noexcept {return detail::query(nat,h);}
    static bool query(Natives& nat,std::uint32_t h,std::uint32_t a) noexcept {return detail::query(nat,h,a);}
    static bool call(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a,std::uint32_t* res=nullptr) noexcept {return detail::call(nat,h,a,res);}
    static std::uint32_t u(int v) noexcept {return std::uint32_t(v);}
    template<class... A> void logf(const char* f,A... a) const noexcept {if(!log_)return;char line[160];std::snprintf(line,sizeof line,f,a...);log_(line);}

    LogFn log_=nullptr;HasFn has_=nullptr;Model* model_=nullptr;
    Drawer d_{};
    std::atomic<int> request_{0};std::atomic<unsigned> word_{0};
    State state_=Closed;
    bool checked_=false,unavailable_=false,holding_=false,waitRelease_=false,cooling_=false,backArmed_=false,holdingBack_=false;
    bool modelOpen_=false,blocked_=false;
    std::uint32_t holdAt_=0,closedAt_=0,openedAt_=0,holdBackAt_=0,frames_=0;
    const char* why_="close";
};

constexpr bool noForbidden() noexcept {for(const auto& x:Used)if(menudraw::forbiddenName(x.name))return false;return true;}
static_assert(noForbidden(),"the interaction menu's drawer and gate call no network / stats / save / ped / player / entity setters");

} // namespace recomp::imenudraw
