#pragma once
// imenu_diag.h (Update 1, Interaction Menu replica, plan item 4: the E1-E5 diagnostic attempt; lane upd1-imenu/diag-run, 2026-11-07).
// DIAGNOSTIC ONLY. Nothing here runs unless the developer command file files/private-trace/diag-imenu has been written in Sandbox mode
// (trainer_glue.inc patch, Sandbox tick only; Story never constructs it). Pure: no guest memory except through Words, no files, no
// platform calls, no allocation (fixed arrays). Notes, log contract and evidence: ../NOTES.md.
//
// What it proves on the Thor (each with a log line the verdict tool reads, tools/imenu_verdict.py):
//   E0  every native it and menu_draw.h call is registered AND mapped on this build (one line at activation).
//   E1  drawing from the trainer tick (the VM's PLAYER_PED_ID return) every frame: tick coverage per 300 game frames, overall and while
//       the menu is open (a game frame with no tick = a frame the menu is missing = flicker), AND drawn coverage: of the open frames since
//       the renderer became ready, how many were really drawn (a guard skip or a missed tick is a frame without the menu).
//   E2  CommonMenu / MPInteraction / instructional_buttons: frames to load, drops after load, re-requests, the requesting thread's end.
//   E3  Back (control 0) press / release vs the follow-camera view mode, with the game timer; the 350 ms hold opener (freemode 0xcb17e
//       timer). Two control-0 rules: 'hold open' = OUR STRICTER DESIGN (disabled from the open through the release frame and the one
//       after it); 'hold open-orig' = the disc's rule (freemode disables control 0 once, on the open frame, DISABLE_CONTROL_ACTION(0,0)
//       at 0xcb260; am_pi_menu 0x2c0e9 then disables it only while IS_DISABLED_CONTROL_PRESSED(0,0), no release-frame tail). E3 measures
//       whether the original rule is enough from our hook. NOT copied from the disc opener: its not-shooting / not-sprinting / same-team
//       checks (the diagnostic opener has none of them).
//   E4  the five frontend sounds (and freemode's NAV_LEFT_RIGHT) play: HAS_SOUND_FINISHED stays false for >= 2 frames, with a
//       non-existent sound as the negative control.
//   E5  the action / mood animation dictionaries load in Sandbox, one gesture plays (IS_ENTITY_PLAYING_ANIM 0 -> 1), facial moods set.
//   G   the nested-text guard: the shared text-command state word and the shared style / GFX-align block (OBSERVED below), sampled at
//       every open tick; the forced-nest exercise g3a (guarded) / g3b (unguarded control) on our own outer command.
//   AB  frame cost: the menu forced open / closed by the presence of files/private-trace/diag-imenu-ab, one cost line per 60 frames.
//
// OBSERVED (generated code v971 ppc_recomp.124/126.cpp AND the title image, tools/iter/ppc_disasm.py; addresses are guest):
//   0x839E93A4  the text-command state word, ONE for all script threads. BEGIN_TEXT_COMMAND_DISPLAY_TEXT (sub_82A98348) stores 5 only
//               when it is 0 (else the BEGIN is ignored), GET_SCREEN_WIDTH 3 (sub_82A98528), DISPLAY_HELP 6 (sub_82A98758), THEFEED_POST
//               14 (sub_82A97DF0), SCALEFORM_STRING 8 (sub_82A987A8). END_TEXT_COMMAND_DISPLAY_TEXT (sub_82AA3E08) acts only when it is 5,
//               then stores 0. ADD_TEXT_COMPONENT_* (sub_82A98CA8) is ignored at 0 and when 10 components are already held (0x839E8B20).
//   0x839E93A0  the pending command's label pointer (stored by the BEGIN that won).
//   0x837F00E0  SET_SCRIPT_GFX_ALIGN (+0,+1 bytes) / _PARAMS (+4..+16 floats) / RESET (sub_82D4E588): 20 bytes, global.
//   0x837F00F4  the text style: SET_TEXT_COLOUR +0, SCALE +4/+8, WRAP +12/+16, FONT +20, JUSTIFICATION/CENTRE +28, LEADING +29,
//               DROPSHADOW +30 ...: 36 bytes, global; END_TEXT_COMMAND_DISPLAY_TEXT resets +0..+16 and several bytes.
// So a script that is between its own SET_TEXT_* / BEGIN and END when our tick runs inside its PLAYER_PED_ID call would get our text
// merged into its command (BEGIN ignored, our END consumes its pending command) and its style / alignment overwritten. The guard: no text
// at all while the state word is nonzero (the whole text-bearing draw is skipped that frame and counted), and the 14 words of
// 0x837F00E0..0x837F0117 are saved before our drawing and written back after it (only those words: storeAllowed()).
#include "trainer.h"
#include "menu_draw.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::imenudiag {
using trainer::Natives;
using trainer::joaat;
using trainer::bits;
using trainer::asFloat;
using LogFn=void(*)(const char*);
using ClockFn=std::uint64_t(*)();   // microseconds, monotonic (the glue: steady_clock)

// The natives this file calls that trainer.h / menu_draw.h do not list. Argument slots read from each handler (tools/handler_args.py on
// v971 src, tools/iter/ppc_disasm.py on the title image for the two facial handlers in chunk 191) and the script call shapes
// (freemode / shop scripts, the interaction-menu lane's calls.py): see NOTES.md section 3.
struct Named {const char* name;std::uint32_t hash;};
namespace n {
constexpr std::uint32_t DisabledPressed=joaat("IS_DISABLED_CONTROL_PRESSED"),DisableControl=joaat("DISABLE_CONTROL_ACTION"),
    Exclusive=joaat("SET_INPUT_EXCLUSIVE"),CamMode=joaat("GET_FOLLOW_PED_CAM_VIEW_MODE"),GameTimer=joaat("GET_GAME_TIMER"),
    SoundId=joaat("GET_SOUND_ID"),SoundDone=joaat("HAS_SOUND_FINISHED"),SoundRelease=joaat("RELEASE_SOUND_ID"),
    RequestAnim=joaat("REQUEST_ANIM_DICT"),AnimLoaded=joaat("HAS_ANIM_DICT_LOADED"),RemoveAnim=joaat("REMOVE_ANIM_DICT"),
    PlayAnim=joaat("TASK_PLAY_ANIM"),PlayingAnim=joaat("IS_ENTITY_PLAYING_ANIM"),StopAnim=joaat("STOP_ANIM_TASK"),
    FacialSet=joaat("SET_FACIAL_IDLE_ANIM_OVERRIDE"),FacialClear=joaat("CLEAR_FACIAL_IDLE_ANIM_OVERRIDE"),
    FeedBegin=joaat("BEGIN_TEXT_COMMAND_THEFEED_POST"),FeedEnd=joaat("END_TEXT_COMMAND_THEFEED_POST_TICKER"),
    HelpBegin=joaat("BEGIN_TEXT_COMMAND_DISPLAY_HELP"),HelpEnd=joaat("END_TEXT_COMMAND_DISPLAY_HELP"),
    HelpShown=joaat("IS_HELP_MESSAGE_BEING_DISPLAYED"),ThreadId=joaat("GET_ID_OF_THIS_THREAD"),
    ScriptHash=joaat("GET_HASH_OF_THIS_SCRIPT_NAME"),ThreadActive=joaat("IS_THREAD_ACTIVE"),
    ControlOn=joaat("IS_PLAYER_CONTROL_ON"),Warning=joaat("IS_WARNING_MESSAGE_ACTIVE");
}
inline constexpr Named Used[]{
    {"IS_DISABLED_CONTROL_PRESSED",n::DisabledPressed},{"DISABLE_CONTROL_ACTION",n::DisableControl},{"SET_INPUT_EXCLUSIVE",n::Exclusive},
    {"GET_FOLLOW_PED_CAM_VIEW_MODE",n::CamMode},{"GET_GAME_TIMER",n::GameTimer},{"GET_SOUND_ID",n::SoundId},
    {"HAS_SOUND_FINISHED",n::SoundDone},{"RELEASE_SOUND_ID",n::SoundRelease},{"REQUEST_ANIM_DICT",n::RequestAnim},
    {"HAS_ANIM_DICT_LOADED",n::AnimLoaded},{"REMOVE_ANIM_DICT",n::RemoveAnim},{"TASK_PLAY_ANIM",n::PlayAnim},
    {"IS_ENTITY_PLAYING_ANIM",n::PlayingAnim},{"STOP_ANIM_TASK",n::StopAnim},{"SET_FACIAL_IDLE_ANIM_OVERRIDE",n::FacialSet},
    {"CLEAR_FACIAL_IDLE_ANIM_OVERRIDE",n::FacialClear},{"BEGIN_TEXT_COMMAND_THEFEED_POST",n::FeedBegin},
    {"END_TEXT_COMMAND_THEFEED_POST_TICKER",n::FeedEnd},{"BEGIN_TEXT_COMMAND_DISPLAY_HELP",n::HelpBegin},
    {"END_TEXT_COMMAND_DISPLAY_HELP",n::HelpEnd},{"IS_HELP_MESSAGE_BEING_DISPLAYED",n::HelpShown},{"GET_ID_OF_THIS_THREAD",n::ThreadId},
    {"GET_HASH_OF_THIS_SCRIPT_NAME",n::ScriptHash},{"IS_THREAD_ACTIVE",n::ThreadActive},{"IS_PLAYER_CONTROL_ON",n::ControlOn},
    {"IS_WARNING_MESSAGE_ACTIVE",n::Warning}};
inline constexpr std::uint32_t UsedNatives[]{
    n::DisabledPressed,n::DisableControl,n::Exclusive,n::CamMode,n::GameTimer,n::SoundId,n::SoundDone,n::SoundRelease,n::RequestAnim,
    n::AnimLoaded,n::RemoveAnim,n::PlayAnim,n::PlayingAnim,n::StopAnim,n::FacialSet,n::FacialClear,n::FeedBegin,n::FeedEnd,n::HelpBegin,
    n::HelpEnd,n::HelpShown,n::ThreadId,n::ScriptHash,n::ThreadActive,n::ControlOn,n::Warning};
static_assert(std::size(Used)==std::size(UsedNatives));
constexpr bool forbiddenName(const char* s) noexcept {
    const auto starts=[](const char* a,const char* p){while(*p){if(*a!=*p)return false;++a;++p;}return true;};
    return starts(s,"NETWORK_") || starts(s,"STAT_") || starts(s,"NET_") || starts(s,"UGC_") || starts(s,"CLOUD_") || starts(s,"DATAFILE_")
        || starts(s,"SC_") || starts(s,"PLAYSTATS_") || starts(s,"SET_SAVE") || starts(s,"DO_AUTO_SAVE") || starts(s,"SET_PLAYER_")
        || starts(s,"SET_ENTITY_") || starts(s,"CREATE_") || starts(s,"DELETE_");
}
constexpr bool noForbidden() noexcept {for(const auto& x:Used)if(forbiddenName(x.name))return false;return true;}
static_assert(noForbidden(),"the diagnostic draws, plays sounds / anims and reads input only: no network, stats, saves, player or entity writes");

// ---- the shared text state (OBSERVED, header comment) ----
struct Words {
    virtual ~Words()=default;
    virtual std::uint32_t load(std::uint32_t address)=0;
    virtual bool store(std::uint32_t address,std::uint32_t value)=0;   // the glue refuses anything storeAllowed() refuses
};
constexpr std::uint32_t TextState=0x839E93A4u,TextLabel=0x839E93A0u,GfxBlock=0x837F00E0u;
constexpr unsigned GfxWords=5,StyleWords=9,BlockWords=GfxWords+StyleWords;   // 0x837F00E0..0x837F0117
constexpr unsigned StyleResetWords=5;   // style +0..+16 (colour, scale, wrap): what END_TEXT_COMMAND_DISPLAY_TEXT always rewrites
constexpr bool storeAllowed(std::uint32_t a) noexcept {return a>=GfxBlock && a<GfxBlock+4*BlockWords && !((a-GfxBlock)&3u);}
inline bool textIdle(Words& w) noexcept {return w.load(TextState)==0;}

class TextGuard {
public:
    void enter(Words& w) noexcept {
        state_=w.load(TextState);
        for(unsigned k=0;k<BlockWords;++k)saved_[k]=w.load(GfxBlock+4*k);
        in_=true;used_=false;++ticks_;
        if(state_){++busy_;last_=state_;}
        if(canon_){
            bool a=false,s=false;
            for(unsigned k=0;k<GfxWords;++k)a|=saved_[k]!=canon_w_[k];
            for(unsigned k=GfxWords;k<GfxWords+StyleResetWords;++k)s|=saved_[k]!=canon_w_[k];
            alignPending_+=a;stylePending_+=s;
        }
    }
    bool idle() const noexcept {return in_ && state_==0;}
    void noteText() noexcept {used_=true;}   // our drawing ended with END_TEXT_COMMAND_DISPLAY_TEXT then RESET_SCRIPT_GFX_ALIGN
    void leave(Words& w) noexcept {
        if(!in_)return;
        if(!canon_ && used_ && state_==0){for(unsigned k=0;k<BlockWords;++k)canon_w_[k]=w.load(GfxBlock+4*k);canon_=true;}
        for(unsigned k=0;k<BlockWords;++k){
            const std::uint32_t a=GfxBlock+4*k;
            if(w.load(a)!=saved_[k]){if(storeAllowed(a) && w.store(a,saved_[k]))++restored_;else ++storeFails_;}
        }
        in_=false;
    }
    unsigned ticks() const noexcept {return ticks_;}
    unsigned busy() const noexcept {return busy_;}
    unsigned lastBusy() const noexcept {return last_;}
    unsigned stylePending() const noexcept {return stylePending_;}
    unsigned alignPending() const noexcept {return alignPending_;}
    unsigned restored() const noexcept {return restored_;}
    unsigned storeFails() const noexcept {return storeFails_;}
    bool canonical() const noexcept {return canon_;}
    void resetCounts() noexcept {ticks_=busy_=stylePending_=alignPending_=restored_=storeFails_=0;}
private:
    std::uint32_t saved_[BlockWords]{},canon_w_[BlockWords]{},state_=0,last_=0;
    bool in_=false,used_=false,canon_=false;
    unsigned ticks_=0,busy_=0,stylePending_=0,alignPending_=0,restored_=0,storeFails_=0;
};

// Counts every native call (the cost lines) and the calls the glue refused (not registered / not mapped: must stay 0).
class Counting final : public Natives {
public:
    explicit Counting(Natives& in) noexcept:in_(in) {}
    bool call(std::uint32_t h,const std::uint32_t* a,unsigned k,std::uint32_t* out) override {++calls;const bool ok=in_.call(h,a,k,out);refused+=!ok;return ok;}
    std::uint32_t text(const char* s) override {return in_.text(s);}
    std::uint32_t word(std::uint32_t v) override {return in_.word(v);}
    std::uint32_t load(std::uint32_t a) override {return in_.load(a);}
    unsigned calls=0,refused=0;
private:
    Natives& in_;
};

// E0: one line naming every native that is not usable (has = registered AND its handler maps to recompiled code).
template<class Has> int reportNatives(Has has,LogFn log) noexcept {
    char miss[400]{};std::size_t at=0;int total=0,bad=0;
    const auto one=[&](const char* name,std::uint32_t h){
        ++total;if(has(h))return;++bad;
        const int k=std::snprintf(miss+at,sizeof miss-at,"%s%s",at?",":"",name);if(k>0 && at+std::size_t(k)<sizeof miss)at+=std::size_t(k);
    };
    for(const auto& u:Used)one(u.name,u.hash);
    for(const auto& u:menudraw::Used)one(u.name,u.hash);
    if(log){char l[512];std::snprintf(l,sizeof l,"RENDER_IMENU e0 natives=%d usable=%d missing=%s",total,total-bad,bad?miss:"none");log(l);}
    return bad;
}

// The controls the disc menu owns while open (am_pi_menu 0x2bc95, interaction-menu NOTES 2): exclusive on pad 2, disabled on pad 0
// (24 always; 22 only on the inventory page, which the diagnostic page is not).
constexpr int ExclusiveControls[]{174,175,161,162,163,164};
constexpr int DisabledControls[]{14,15,37,43,45,49,50,51,52,67,73,76,120,121,123,136,137,138,139,140,141,142,143,144};
static_assert(std::size(DisabledControls)==24);
constexpr std::uint32_t HoldOpenMs=350;   // freemode 0xcb17e: timer helper 0xaa11 with 350

enum class Hold {Off,Observe,Open,OpenOrig};

class Diag {
public:
    static constexpr unsigned CoverageWindow=300,CostWindow=60,G3Frames=90,LoadTimeout=300,SoundTimeout=120;
    explicit Diag(LogFn log) noexcept:log_(log) {renderer_.setLog(log);}

    bool active() const noexcept {return active_;}
    bool open() const noexcept {return open_;}
    const TextGuard& guard() const noexcept {return guard_;}
    unsigned guardSkips() const noexcept {return guardSkips_;}

    // One line of files/private-trace/diag-imenu. Unknown words are refused (logged, nothing changes).
    bool command(const char* line,std::uint32_t frame) noexcept {
        char w[16]{},a[24]{};const int k=std::sscanf(line?line:"","%15s %23s",w,a);
        bool ok=k>=1;
        if(!ok){}
        else if(!std::strcmp(w,"open"))want_=Want::Open;
        else if(!std::strcmp(w,"close"))want_=Want::Close;
        else if(!std::strcmp(w,"hold") && k==2)ok=setHold(a);
        else if(!std::strcmp(w,"cam") && k==2){camProbe_=!std::strcmp(a,"on");ok=camProbe_ || !std::strcmp(a,"off");}
        else if(!std::strcmp(w,"e4")){sound_=0;soundStep_=SoundStep::Start;}
        else if(!std::strcmp(w,"e5")){anim_=0;animStep_=AnimStep::Start;}
        else if(!std::strcmp(w,"mood")){moodFrames_=0;moodOn_=true;}
        else if(!std::strcmp(w,"feed"))feedWait_=30;
        else if(!std::strcmp(w,"help")){helpWait_=30;helpPoll_=0;}
        else if(!std::strcmp(w,"g3a") || !std::strcmp(w,"g3b")){g3_=G3{};g3_.mode=w[2]=='a'?1:2;}
        else if(!std::strcmp(w,"sublen"))sublen_=0;
        else if(!std::strcmp(w,"mark") && k==2){}
        // stop: what needs a native (sound id, facial override, animation task, our dictionary) is undone on the next tick (stopCleanup);
        // a hold that already opened keeps its control-0 rule until its release (holdTick), so the release never reaches the camera.
        else if(!std::strcmp(w,"stop")){want_=Want::Close;hold_=Hold::Off;camProbe_=false;cleanup_=true;g3_.mode=0;sublen_=-1;
            feedWait_=helpWait_=helpPoll_=0;}
        else ok=false;
        logf("RENDER_IMENU cmd=%s%s%s frame=%u ok=%d",w,k==2?" ":"",k==2?a:"",frame,ok?1:0);
        // the A/B period's ends, with the state at that moment (ab_toggle's first block is closed and is never logged as a change)
        if(ok && !std::strcmp(w,"mark") && (!std::strcmp(a,"ab-start") || !std::strcmp(a,"ab-end")))
            logf("RENDER_IMENU ab open=%d frame=%u mark=%s",ab_?1:0,frame,a);
        if(ok)active_=true;
        return ok;
    }
    // The A/B switch (presence of diag-imenu-ab), from the glue's poll.
    void setAb(bool on,std::uint32_t frame) noexcept {
        if(on==ab_)return;
        ab_=on;active_=true;logf("RENDER_IMENU ab open=%d frame=%u",on?1:0,frame);
    }

    // Once per game frame on the trainer tick (Sandbox only). frame = the GET_FRAME_COUNT word.
    void tick(Natives& raw,Words& w,std::uint32_t frame,ClockFn clock) noexcept {
        if(!active_)return;                         // never activated: nothing at all
        const std::uint64_t t0=clock?clock():0;
        Counting nat(raw);
        coverage(frame);
        holdTick(nat,frame);
        if(cleanup_)stopCleanup(nat,frame);
        if(want_==Want::Close)holdOpened_=false;
        const bool shouldOpen=ab_ || want_==Want::Open || holdOpened_;
        if(shouldOpen && !open_)doOpen(nat,ab_?"ab":holdOpened_?"hold":"cmd",frame);
        else if(!shouldOpen && open_)doClose(nat,want_==Want::Close?"cmd":"ab");
        if(want_==Want::Close)want_=Want::None;
        const bool needText=open_ || feedWait_ || helpWait_ || g3_.mode || sublen_>=0;
        if(needText)guard_.enter(w);
        if(open_)menuFrame(nat,w,frame);
        if(feedWait_)feedFrame(nat,w);
        if(helpWait_ || helpPoll_)helpFrame(nat,w,frame);
        if(g3_.mode)g3Frame(nat,w);
        if(sublen_>=0)sublenFrame(nat,w);
        if(needText)guard_.leave(w);
        soundFrame(nat,frame);
        animFrame(nat,frame);
        moodFrame(nat);
        const std::uint64_t us=clock?clock()-t0:0;
        costWindow(frame,nat.calls,nat.refused,us);
    }

private:
    enum class Want {None,Open,Close};
    enum class SoundStep {Idle,Start,Playing,Gap};
    enum class AnimStep {Idle,Start,Loading,Playing,Stop};
    struct G3 {int mode=0;unsigned frames=0,busy=0,outer5=0,labelOuter=0,innerSkipped=0,innerIgnored=0,afterInner0=0,afterOuter0=0;};
    struct SoundCase {const char* name;const char* set;bool control;};
    struct AnimCase {const char* dict;const char* clip;bool play;};
    static constexpr SoundCase Sounds[]{
        {"NAV_UP_DOWN","HUD_FRONTEND_DEFAULT_SOUNDSET",false},{"NAV_LEFT_RIGHT","HUD_FRONTEND_DEFAULT_SOUNDSET",false},
        {"SELECT","HUD_FRONTEND_DEFAULT_SOUNDSET",false},{"BACK","HUD_FRONTEND_DEFAULT_SOUNDSET",false},
        {"CANCEL","HUD_FRONTEND_DEFAULT_SOUNDSET",false},{"NAV_LEFT_RIGHT","HUD_FREEMODE_SOUNDSET",false},
        {"FP_IMENU_NO_SUCH_SOUND","HUD_FRONTEND_DEFAULT_SOUNDSET",true}};
    // freemode's action table strings 0x0d925.. (dictionary, then its clips) and 0x0dafa / 0x0db5a; the in-car archive (plan E5: not found
    // on the host) is a finding either way, never a failure.
    static constexpr AnimCase Anims[]{
        {"mp_player_int_upperrock","mp_player_int_rock",true},{"mp_player_intdrink",nullptr,false},
        {"mp_player_inteat@burger",nullptr,false},{"amb@code_human_in_car_mp_actions@rock@std@ds@base",nullptr,false}};
    static constexpr int SubLengths[]{8,16,24,32,48,63,64,65,96,127,128,160,200,255};
    static constexpr const char* MpInteraction="MPInteraction";
    static constexpr const char* MenuBar="menubar";

    // ---- helpers ----
    static bool call(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a,std::uint32_t* res=nullptr) noexcept {
        std::uint32_t args[12]{};unsigned k=0;for(auto v:a)if(k<12)args[k++]=v;
        std::uint32_t out[3]{};const bool ok=nat.call(h,args,k,out);if(res){res[0]=out[0];res[1]=out[1];res[2]=out[2];}return ok;
    }
    static std::uint32_t ask(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a,std::uint32_t fallback=0) noexcept {
        std::uint32_t r[3]{};return call(nat,h,a,r)?r[0]:fallback;
    }
    template<class... A> void logf(const char* f,A... a) const noexcept {
        if(!log_)return;
        if constexpr(sizeof...(A)==0)log_(f);else {char line[240];std::snprintf(line,sizeof line,f,a...);log_(line);}
    }
    bool setHold(const char* a) noexcept {
        if(!std::strcmp(a,"off"))hold_=Hold::Off;else if(!std::strcmp(a,"observe"))hold_=Hold::Observe;else if(!std::strcmp(a,"open"))hold_=Hold::Open;
        else if(!std::strcmp(a,"open-orig"))hold_=Hold::OpenOrig;
        else return false;
        return true;
    }
    void literal(Natives& nat,std::uint32_t beginHash,const char* s) noexcept {   // STRING ("~a~") + SUBSTRING_PLAYER_NAME, our own text
        const std::uint32_t l=nat.text(menudraw::Literal),t=nat.text(s);if(!l || !t)return;
        call(nat,beginHash,{l});call(nat,menudraw::n::AddLiteral,{t});
    }
    static void style(Natives& nat,float scale) noexcept {
        call(nat,menudraw::n::TextFont,{0});call(nat,menudraw::n::TextScale,{bits(0.0f),bits(scale)});
        call(nat,menudraw::n::TextColour,{255,255,255,255});call(nat,menudraw::n::TextWrap,{bits(0.0f),bits(1.0f)});
        call(nat,menudraw::n::TextCentre,{0});
    }

    // ---- E1: tick coverage ----
    void coverage(std::uint32_t frame) noexcept {
        if(haveFrame_){
            const std::uint32_t d=frame-lastFrame_;
            if(d>1 && d<100000){missed_+=d-1;if(open_ && lastOpen_){openMissed_+=d-1;readyFrames_+=readySeen_?d-1:0;}}
        } else winStart_=frame;
        haveFrame_=true;lastFrame_=frame;lastOpen_=open_;++ticks_;if(open_)++openTicks_;
        if(frame-winStart_>=CoverageWindow){
            logf("RENDER_IMENU e1 frames=%u ticks=%u missed=%u open_ticks=%u open_missed=%u busy=%u busy_state=%u style_pending=%u "
                 "align_pending=%u restored=%u store_fails=%u guard_skips=%u hosts=%u ready_frames=%u drawn=%u",
                 frame-winStart_,ticks_,missed_,openTicks_,openMissed_,guard_.busy(),guard_.lastBusy(),guard_.stylePending(),
                 guard_.alignPending(),guard_.restored(),guard_.storeFails(),guardSkips_,hosts_,readyFrames_,drawn_);
            winStart_=frame;ticks_=missed_=openTicks_=openMissed_=guardSkips_=hosts_=readyFrames_=drawn_=0;guard_.resetCounts();
        }
    }
    // ---- AB / cost ----
    void costWindow(std::uint32_t frame,unsigned calls,unsigned refused,std::uint64_t us) noexcept {
        if(!costFrames_){costStart_=frame;costOpen_=open_?1:0;}
        else if(costOpen_!=(open_?1:0))costOpen_=-1;
        ++costFrames_;costCalls_+=calls;costRefused_+=refused;costUs_+=us;
        if(costFrames_>=CostWindow){
            logf("RENDER_IMENU cost open=%d frames=%u span=%u natives=%u refused=%u us=%llu",costOpen_,costFrames_,frame-costStart_+1,
                 costCalls_,costRefused_,static_cast<unsigned long long>(costUs_));
            costFrames_=costCalls_=costRefused_=0;costUs_=0;
        }
    }
    // ---- E3: Back vs the camera; the 350 ms hold opener ----
    void holdTick(Natives& nat,std::uint32_t frame) noexcept {
        if(hold_==Hold::Off && !camProbe_ && !openedThisPress_)return;   // (an opened hold finishes its control-0 rule even after 'stop')
        const bool pressed=ask(nat,n::DisabledPressed,{0,0})!=0;            // the one closed-state call (freemode reads the same)
        if(pressed!=wasPressed_){
            const std::uint32_t t=ask(nat,n::GameTimer,{});
            if(pressed){pressT_=t;openedThisPress_=false;}
            logf("RENDER_IMENU e3 back=%d t=%u frame=%u",pressed?1:0,t,frame);
        }
        if(camProbe_){
            const int cam=int(ask(nat,n::CamMode,{},0xFFFFFFFFu));
            if(cam!=lastCam_){logf("RENDER_IMENU e3 cam=%d t=%u frame=%u",cam,ask(nat,n::GameTimer,{}),frame);lastCam_=cam;}
        }
        if(hold_==Hold::Open || hold_==Hold::OpenOrig){
            if(pressed && !openedThisPress_ && !open_){
                const std::uint32_t t=ask(nat,n::GameTimer,{});
                if(t-pressT_>=HoldOpenMs){
                    openedThisPress_=true;holdOpened_=true;origRule_=hold_==Hold::OpenOrig;
                    logf("RENDER_IMENU open-request via=hold hold_ms=%u control_on=%u warning=%u rule=%s frame=%u",t-pressT_,
                         ask(nat,n::ControlOn,{ask(nat,trainer::n::PlayerId,{})}),ask(nat,n::Warning,{}),origRule_?"orig":"strict",frame);
                }
            }
        }
        if(openedThisPress_ && origRule_){
            // the disc's rule: freemode 0xcb260 disables control 0 on the open frame; am_pi_menu 0x2c0e9 only while it is still pressed
            if(pressed){call(nat,n::DisableControl,{0,0});++disabled0_;}
            else {openedThisPress_=false;logf("RENDER_IMENU e3 control0_disabled_frames=%u rule=orig",disabled0_);disabled0_=0;}
        } else if(openedThisPress_){
            // OUR STRICTER DESIGN: control 0 stays disabled from the open through the release frame and the one after it
            // (IS_CONTROL_JUST_RELEASED's frame); not the disc's rule (header, E3)
            if(pressed || wasPressed_ || releaseTail_){call(nat,n::DisableControl,{0,0});++disabled0_;}
            if(!pressed && wasPressed_)releaseTail_=true;
            else if(releaseTail_ && !pressed){
                releaseTail_=false;openedThisPress_=false;
                logf("RENDER_IMENU e3 control0_disabled_frames=%u rule=strict",disabled0_);disabled0_=0;
            }
        }
        wasPressed_=pressed;
    }
    // ---- the menu (stand-in page drawn with menu_draw.h; draw-glue's am_pi_menu frame replaces drawPage) ----
    void doOpen(Natives& nat,const char* via,std::uint32_t frame) noexcept {
        open_=true;openFrame_=frame;readySeen_=false;mpiLoaded_=false;mpiFrames_=0;owner_=0;ownerEnded_=false;afterOwner_=0;host_=0;
        cmWasLoaded_=movieWasLoaded_=cmEver_=false;drops_=rerequests_=noAssetDraws_=0;
        logf("RENDER_IMENU open via=%s frame=%u",via,frame);(void)nat;
    }
    void doClose(Natives& nat,const char* why) noexcept {
        renderer_.close(nat,why);
        if(mpiRequested_){const std::uint32_t d=nat.text(MpInteraction);if(d)call(nat,menudraw::n::ReleaseTxd,{d});mpiRequested_=false;}
        logf("RENDER_IMENU close reason=%s drops=%u rerequests=%u no_asset_draws=%u guard_skips_total=%u",why,drops_,rerequests_,noAssetDraws_,guardSkipsTotal_);
        open_=false;holdOpened_=false;
    }
    void menuFrame(Natives& nat,Words& w,std::uint32_t frame) noexcept {
        for(int c:ExclusiveControls)call(nat,n::Exclusive,{2,std::uint32_t(c)});
        for(int c:DisabledControls)call(nat,n::DisableControl,{0,std::uint32_t(c)});
        streamWatch(nat,frame);
        // E1 drawn coverage: from the first ready frame of this open, every open frame counts (ready_frames) and only the frames that
        // really drew count as drawn; a guard skip, a missed tick (coverage()) or an asset drop after ready is a frame without the menu.
        if(!guard_.idle()){++guardSkips_;++guardSkipsTotal_;readyFrames_+=readySeen_;return;}   // a host command is pending: no text (G)
        const bool ready=renderer_.load(nat,menudraw::HeaderBarber);
        readySeen_|=ready;readyFrames_+=readySeen_;
        if(!ready)return;
        if(!cmLoaded_ || !movieLoaded_)++noAssetDraws_;                 // E2: a frame drawn with an asset not resident
        static constexpr menudraw::Row Rows[]{{"FivePocket diag 1","1 / 3",true},{"FivePocket diag 2"},{"FivePocket diag 3"},
            {"FivePocket diag 4"},{"FivePocket diag 5"},{"FivePocket diag 6"},{"FivePocket diag 7"},{"FivePocket diag 8",nullptr,false,false},
            {"FivePocket diag 9"},{"FivePocket diag 10"},{"FivePocket diag 11"}};
        static constexpr menudraw::Button Buttons[]{{30,"ITEM_SELECT"},{31,"ITEM_BACK"}};
        menudraw::Page p;p.title="FivePocket diagnostic";p.rows=Rows;p.rowCount=int(std::size(Rows));p.selected=int((frame/30)%std::size(Rows));
        p.help="FivePocket diagnostic page: draw, stream, sound";p.buttons=Buttons;p.buttonCount=2;
        renderer_.draw(nat,p);++drawn_;
        if(mpiLoaded_){   // E2 visual witness: the am_pi_menu header art, right of the stand-in menu (0.225 x 0.09, white)
            const std::uint32_t d=nat.text(MpInteraction),t=nat.text(MenuBar);
            if(d && t)call(nat,menudraw::n::DrawSprite,{d,t,bits(0.05f+0.225f+0.01f+0.1125f),bits(0.05f+0.045f),bits(0.225f),bits(0.09f),bits(0.0f),255,255,255,255});
        }
        guard_.noteText();(void)w;
    }
    // ---- E2 ----
    void streamWatch(Natives& nat,std::uint32_t frame) noexcept {
        const std::uint32_t cm=nat.text(menudraw::CommonMenu),mpi=nat.text(MpInteraction);
        if(!cm || !mpi)return;
        const std::uint32_t tid=ask(nat,n::ThreadId,{});
        if(tid!=host_){host_=tid;++hosts_;logf("RENDER_IMENU e2 host thread=%u script=%08x frame=%u",tid,ask(nat,n::ScriptHash,{}),frame);}
        cmLoaded_=ask(nat,menudraw::n::TxdLoaded,{cm})!=0;
        const bool mpiNow=ask(nat,menudraw::n::TxdLoaded,{mpi})!=0;
        const std::uint32_t mv=renderer_.movieHandle();
        movieLoaded_=mv && ask(nat,menudraw::n::MovieLoaded,{mv})!=0;
        if(!mpiNow){   // requested every frame until resident; after a drop this is the plan's fallback (counted)
            call(nat,menudraw::n::RequestTxd,{mpi,0});
            if(!mpiRequested_){mpiRequested_=true;owner_=tid;logf("RENDER_IMENU e2 request dict=MPInteraction thread=%u frame=%u",tid,frame);}
            else if(mpiFrames_)++rerequests_;
        }
        if(!cmLoaded_ && cmEver_){call(nat,menudraw::n::RequestTxd,{cm,0});++rerequests_;}   // the renderer stops asking once ready
        if(mpiNow && !mpiLoaded_)
            logf("RENDER_IMENU e2 %s dict=MPInteraction frames=%u",mpiFrames_?"reloaded":"loaded",frame-openFrame_);
        if(mpiLoaded_ && !mpiNow){++drops_;logf("RENDER_IMENU e2 drop dict=MPInteraction frame=%u owner_active=%u",frame,owner_?ask(nat,n::ThreadActive,{owner_}):9u);}
        if(mpiNow)mpiFrames_=1;
        mpiLoaded_=mpiNow;
        if(cmLoaded_ && !cmWasLoaded_)logf("RENDER_IMENU e2 %s dict=CommonMenu frames=%u",cmEver_?"reloaded":"loaded",frame-openFrame_);
        cmEver_|=cmLoaded_;
        if(cmWasLoaded_ && !cmLoaded_){++drops_;logf("RENDER_IMENU e2 drop dict=CommonMenu frame=%u",frame);}
        if(movieLoaded_ && !movieWasLoaded_)logf("RENDER_IMENU e2 loaded movie=instructional_buttons frames=%u",frame-openFrame_);
        if(movieWasLoaded_ && !movieLoaded_){++drops_;logf("RENDER_IMENU e2 drop movie=instructional_buttons frame=%u",frame);}
        cmWasLoaded_=cmLoaded_;movieWasLoaded_=movieLoaded_;
        if(owner_ && !ownerEnded_ && !ask(nat,n::ThreadActive,{owner_},1)){
            ownerEnded_=true;afterOwner_=0;afterLoaded_=0;
            logf("RENDER_IMENU e2 owner-ended thread=%u frame=%u mpi=%d cm=%d movie=%d",owner_,frame,mpiNow?1:0,cmLoaded_?1:0,movieLoaded_?1:0);
        }
        if(ownerEnded_ && afterOwner_<60){
            ++afterOwner_;afterLoaded_+=mpiNow;
            if(afterOwner_==60)logf("RENDER_IMENU e2 after-owner frames=60 mpi_loaded_frames=%u",afterLoaded_);
        }
    }
    // ---- feed / help (plan: added to E1) ----
    void feedFrame(Natives& nat,Words& w) noexcept {
        if(!guard_.idle() || !textIdle(w)){if(--feedWait_==0)logf("RENDER_IMENU feed refused reason=text-busy");return;}
        literal(nat,n::FeedBegin,"FivePocket diag: feed");
        const std::uint32_t id=ask(nat,n::FeedEnd,{0,1},0xFFFFFFFFu);
        logf("RENDER_IMENU feed id=%d state_after=%u",int(id),w.load(TextState));
        feedWait_=0;
    }
    void helpFrame(Natives& nat,Words& w,std::uint32_t frame) noexcept {
        if(helpWait_){
            if(!guard_.idle() || !textIdle(w)){if(--helpWait_==0)logf("RENDER_IMENU help refused reason=text-busy");return;}
            literal(nat,n::HelpBegin,"FivePocket diag: help");
            call(nat,n::HelpEnd,{0,0,1,0xFFFFFFFFu});   // freemode (0, 0, 1, ?); the shops pass -1 last
            helpWait_=0;helpPoll_=1;helpFrame0_=frame;
            logf("RENDER_IMENU help posted state_after=%u",w.load(TextState));
            return;
        }
        if(ask(nat,n::HelpShown,{})){logf("RENDER_IMENU help shown=1 frames=%u",frame-helpFrame0_);helpPoll_=0;}
        else if(++helpPoll_>30){logf("RENDER_IMENU help shown=0 frames=30");helpPoll_=0;}
    }
    // ---- G3: the forced nest on our own outer command ----
    void g3Frame(Natives& nat,Words& w) noexcept {
        if(!guard_.idle()){++g3_.busy;return;}   // a host command is pending: never nest into someone else's
        const std::uint32_t outerL=nat.text(menudraw::Literal),innerL=nat.text(menudraw::Literal);   // two copies: two addresses
        const std::uint32_t outerT=nat.text("FP G3 OUTER"),innerT=nat.text("FP G3 INNER");
        if(!outerL || !innerL || !outerT || !innerT)return;
        style(nat,0.5f);
        call(nat,menudraw::n::BeginText,{outerL});
        g3_.outer5+=w.load(TextState)==5;g3_.labelOuter+=w.load(TextLabel)==outerL;
        call(nat,menudraw::n::AddLiteral,{outerT});
        if(g3_.mode==1){
            if(!textIdle(w))++g3_.innerSkipped;   // the guard's predicate refuses the inner command
        } else {
            call(nat,menudraw::n::BeginText,{innerL});
            g3_.innerIgnored+=w.load(TextLabel)==outerL;   // the inner BEGIN did not replace the pending label
            call(nat,menudraw::n::AddLiteral,{innerT});
            call(nat,menudraw::n::EndText,{bits(0.5f),bits(0.62f)});
            g3_.afterInner0+=w.load(TextState)==0;          // the inner END consumed the OUTER command
        }
        call(nat,menudraw::n::EndText,{bits(0.5f),bits(0.55f)});
        g3_.afterOuter0+=w.load(TextState)==0;
        guard_.noteText();
        if(++g3_.frames>=G3Frames){
            logf("RENDER_IMENU g3 mode=%s frames=%u host_busy=%u outer_begin_state5=%u label_outer=%u inner_skipped=%u inner_begin_ignored=%u "
                 "state0_after_inner_end=%u state0_after_outer_end=%u",g3_.mode==1?"guarded":"unguarded",g3_.frames,g3_.busy,g3_.outer5,
                 g3_.labelOuter,g3_.innerSkipped,g3_.innerIgnored,g3_.afterInner0,g3_.afterOuter0);
            g3_.mode=0;
        }
    }
    // ---- the literal-substring length limit (plan: unmeasured) ----
    void sublenFrame(Natives& nat,Words& w) noexcept {
        if(!guard_.idle() || !textIdle(w))return;
        const int len=SubLengths[sublen_];
        for(int k=0;k<len;++k)buf_[k]='W';
        buf_[len]=0;
        style(nat,0.35f);
        const std::uint32_t l=nat.text(menudraw::Literal),t=nat.text(buf_);
        if(!l || !t){logf("RENDER_IMENU sublen n=%d width=-1 reason=scratch",len);}
        else {
            std::uint32_t r[3]{};
            call(nat,menudraw::n::BeginWidth,{l});call(nat,menudraw::n::AddLiteral,{t});
            const bool ok=call(nat,menudraw::n::EndWidth,{1},r);
            logf("RENDER_IMENU sublen n=%d width=%.5f",len,ok?double(asFloat(r[0])):-1.0);
        }
        if(++sublen_>=int(std::size(SubLengths))){sublen_=-1;logf("RENDER_IMENU sublen done");}
    }
    // ---- E4 ----
    void soundFrame(Natives& nat,std::uint32_t frame) noexcept {
        if(soundStep_==SoundStep::Idle)return;
        const SoundCase& s=Sounds[sound_];
        if(soundStep_==SoundStep::Start){
            soundIdNow_=ask(nat,n::SoundId,{},0xFFFFFFFFu);
            const std::uint32_t nm=nat.text(s.name),st=nat.text(s.set);
            if(soundIdNow_==0xFFFFFFFFu || !nm || !st){logf("RENDER_IMENU e4 sound=%s set=%s id=-1 playing_frames=-1",s.name,s.set);soundStep_=SoundStep::Gap;soundGap_=0;return;}
            call(nat,menudraw::n::Sound,{soundIdNow_,nm,st});
            soundFrames_=0;(void)frame;soundStep_=SoundStep::Playing;return;
        }
        if(soundStep_==SoundStep::Playing){
            const bool done=ask(nat,n::SoundDone,{soundIdNow_},1)!=0;
            if(!done && ++soundFrames_<SoundTimeout)return;
            logf("RENDER_IMENU e4 sound=%s set=%s id=%u playing_frames=%u control=%d",s.name,s.set,soundIdNow_,soundFrames_,s.control?1:0);
            call(nat,n::SoundRelease,{soundIdNow_});
            soundStep_=SoundStep::Gap;soundGap_=0;return;
        }
        if(++soundGap_<15)return;
        if(++sound_>=int(std::size(Sounds))){soundStep_=SoundStep::Idle;logf("RENDER_IMENU e4 done");return;}
        soundStep_=SoundStep::Start;
    }
    // ---- E5 ----
    void animFrame(Natives& nat,std::uint32_t frame) noexcept {
        if(animStep_==AnimStep::Idle)return;
        const AnimCase& a=Anims[anim_];
        const std::uint32_t d=nat.text(a.dict);if(!d)return;
        if(animStep_==AnimStep::Start){
            animBefore_=ask(nat,n::AnimLoaded,{d})!=0;
            call(nat,n::RequestAnim,{d});animT0_=frame;animStep_=AnimStep::Loading;
            animThread_=ask(nat,n::ThreadId,{});return;
        }
        if(animStep_==AnimStep::Loading){
            const bool loaded=ask(nat,n::AnimLoaded,{d})!=0;
            if(!loaded && frame-animT0_<LoadTimeout)return;
            logf("RENDER_IMENU e5 dict=%s before=%d loaded=%d frames=%u thread=%u",a.dict,animBefore_?1:0,loaded?1:0,frame-animT0_,animThread_);
            animLoaded_=loaded;
            if(loaded && a.play && a.clip){
                const std::uint32_t ped=ask(nat,trainer::n::PlayerPedId,{});
                const std::uint32_t c=nat.text(a.clip);
                if(!ped || !c || ask(nat,trainer::n::IsPedInAnyVehicle,{ped,0}) || ask(nat,trainer::n::IsEntityDead,{ped})){
                    logf("RENDER_IMENU e5 play dict=%s refused=not-on-foot",a.dict);animStep_=AnimStep::Stop;return;
                }
                playBefore_=ask(nat,n::PlayingAnim,{ped,d,c,3})!=0;
                // freemode 0x6fcdb shape: (ped, dict, clip, 8.0, -8.0, -1, flag 0, 0, 0, 0, 0)
                call(nat,n::PlayAnim,{ped,d,c,bits(8.0f),bits(-8.0f),0xFFFFFFFFu,0,0,0,0,0});
                animPed_=ped;animT0_=frame;animStep_=AnimStep::Playing;return;
            }
            animStep_=AnimStep::Stop;return;
        }
        if(animStep_==AnimStep::Playing){
            if(frame-animT0_==15){
                const std::uint32_t c=nat.text(a.clip);
                logf("RENDER_IMENU e5 play dict=%s clip=%s playing_before=%d playing_after=%d",a.dict,a.clip,playBefore_?1:0,
                     c && ask(nat,n::PlayingAnim,{animPed_,d,c,3})?1:0);
            }
            if(frame-animT0_<60)return;
            const std::uint32_t c=nat.text(a.clip);
            if(c)call(nat,n::StopAnim,{animPed_,d,c,bits(-8.0f)});   // freemode 0x6d266 shape: -8.0
            animStep_=AnimStep::Stop;return;
        }
        // Stop: our request is released (only a dictionary we requested and that was not resident before)
        if(animLoaded_ && !animBefore_){call(nat,n::RemoveAnim,{d});logf("RENDER_IMENU e5 removed dict=%s thread=%u",a.dict,ask(nat,n::ThreadId,{}));}
        if(++anim_>=int(std::size(Anims))){animStep_=AnimStep::Idle;logf("RENDER_IMENU e5 done");return;}
        animStep_=AnimStep::Start;
    }
    // ---- stop: undo what this diagnostic started (the tick after 'stop') ----
    void stopCleanup(Natives& nat,std::uint32_t frame) noexcept {
        cleanup_=false;
        if(soundStep_==SoundStep::Playing)call(nat,n::SoundRelease,{soundIdNow_});   // the id is never leaked
        soundStep_=SoundStep::Idle;
        if(moodOn_){
            if(moodFrames_>0){const std::uint32_t ped=ask(nat,trainer::n::PlayerPedId,{});if(ped)call(nat,n::FacialClear,{ped});}
            logf("RENDER_IMENU e5 mood cleared reason=stop");moodOn_=false;
        }
        if(animStep_==AnimStep::Loading || animStep_==AnimStep::Playing || animStep_==AnimStep::Stop){   // the current case was requested
            const AnimCase& a=Anims[anim_];const std::uint32_t d=nat.text(a.dict);
            if(d){
                if(animStep_==AnimStep::Playing && a.clip){const std::uint32_t c=nat.text(a.clip);if(c)call(nat,n::StopAnim,{animPed_,d,c,bits(-8.0f)});}
                if(!animBefore_){call(nat,n::RemoveAnim,{d});logf("RENDER_IMENU e5 removed dict=%s reason=stop",a.dict);}
            }
        }
        animStep_=AnimStep::Idle;
        logf("RENDER_IMENU stop cleaned frame=%u",frame);
    }
    void moodFrame(Natives& nat) noexcept {
        if(!moodOn_)return;
        const std::uint32_t ped=moodFrames_==0 || moodFrames_==120 || moodFrames_==240?ask(nat,trainer::n::PlayerPedId,{}):0;
        if(moodFrames_==0 || moodFrames_==120){
            const char* clip=moodFrames_==0?"mood_Happy_1":"mood_smug_1";   // freemode strings 0x00464 / 0x0049e; null dictionary (PUSH_0)
            const std::uint32_t c=nat.text(clip);
            if(ped && c){call(nat,n::FacialSet,{ped,c,0});logf("RENDER_IMENU e5 mood clip=%s ped=%08x",clip,ped);}
        }
        if(moodFrames_==240){if(ped)call(nat,n::FacialClear,{ped});logf("RENDER_IMENU e5 mood cleared");moodOn_=false;}
        ++moodFrames_;
    }

    LogFn log_=nullptr;
    menudraw::Renderer renderer_;
    TextGuard guard_;
    bool active_=false,open_=false,ab_=false,holdOpened_=false,camProbe_=false,wasPressed_=false,openedThisPress_=false,releaseTail_=false,
        origRule_=false,cleanup_=false;
    Want want_=Want::None;Hold hold_=Hold::Off;
    std::uint32_t pressT_=0,disabled0_=0;int lastCam_=-2;
    // E1
    bool haveFrame_=false,lastOpen_=false;std::uint32_t lastFrame_=0,winStart_=0;
    unsigned ticks_=0,missed_=0,openTicks_=0,openMissed_=0,guardSkips_=0,guardSkipsTotal_=0,hosts_=0,readyFrames_=0,drawn_=0;
    bool readySeen_=false;
    // cost
    unsigned costFrames_=0,costCalls_=0,costRefused_=0;std::uint32_t costStart_=0;int costOpen_=0;std::uint64_t costUs_=0;
    // E2
    std::uint32_t openFrame_=0,owner_=0,host_=0;unsigned drops_=0,rerequests_=0,noAssetDraws_=0,afterOwner_=0,afterLoaded_=0,mpiFrames_=0;
    bool mpiRequested_=false,mpiLoaded_=false,cmEver_=false,cmLoaded_=false,movieLoaded_=false,cmWasLoaded_=false,movieWasLoaded_=false,ownerEnded_=false;
    // feed / help / G3 / sublen
    unsigned feedWait_=0,helpWait_=0,helpPoll_=0;std::uint32_t helpFrame0_=0;
    G3 g3_{};int sublen_=-1;char buf_[256]{};
    // E4 / E5
    SoundStep soundStep_=SoundStep::Idle;int sound_=0;unsigned soundFrames_=0,soundGap_=0;std::uint32_t soundIdNow_=0;
    AnimStep animStep_=AnimStep::Idle;int anim_=0;bool animBefore_=false,animLoaded_=false,playBefore_=false;
    std::uint32_t animT0_=0,animThread_=0,animPed_=0;
    bool moodOn_=false;unsigned moodFrames_=0;
};

} // namespace recomp::imenudiag
