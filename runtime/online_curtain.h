#pragma once
// online_curtain.h (v1043, trainer-mods; owner 2026-11-07: "This should all be done behind a loading screen even if its fake"): the screen
// stays covered while the local GTA Online session comes up and while it goes down, the way GTA Online enters: dark screen + the game's own
// MP loading spinner (place_fade.h's decoded pair: DO_SCREEN_FADE_OUT / IN 500, BEGIN_TEXT_COMMAND_BUSYSPINNER_ON("MP_SPINLOADING") +
// END_TEXT_COMMAND_BUSYSPINNER_ON(1), BUSYSPINNER_OFF; the literal "Loading" when the MP label is not loaded). maintransition's own SP -> MP
// entry also fades and spins (DISCOVERIES 2026-11-07 19:10); its sky camera is not replicated here.
// Armed by Online mode (play_mode 3) or the developer switch files/private-trace/diag-online-curtain (Sandbox tests).
//  - Entry: from the first armed tick with no network game (gip 0): fade out (0 ms in Online mode: the story map is never shown; 500 ms
//    under the switch), the player's control off, the spinner once the screen is out. Revealed (spinner off, fade in 500, control back) when
//    gip has been 1 for SettleFrames, the car keep has nothing in flight and the screen has been out MinHoldFrames; or at once when an MP
//    cutscene runs (NETWORK_IS_IN_MP_CUTSCENE: fm_intro owns the screen and the control, so neither is touched); or after MaxCoverFrames
//    (never a stuck black screen; logged why=timeout).
//  - Leave: online_l0's L3a (the clean leave) first asks (leaveAsked) and waits up to LeaveWaitMs for leaveCovered (screen out, spinner up)
//    before it ends the session; revealed once gip has been 0 for SettleFrames and the car keep is idle (its leave recreate, v1042).
// Disarmed while covered (the mode or switch gone): revealed at once. Natives only; no allocation.
#include "trainer.h"
#include "place_fade.h"
#include "online_curtain_flag.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace recomp::online_curtain {
namespace n {
namespace pf=recomp::online::placefade::n;
using recomp::trainer::joaat;
constexpr std::uint32_t Gip=joaat("NETWORK_IS_GAME_IN_PROGRESS"),MpCut=joaat("NETWORK_IS_IN_MP_CUTSCENE"),PlayerId=joaat("PLAYER_ID");
constexpr std::uint32_t ControlOn=joaat("IS_PLAYER_CONTROL_ON");   // v1105 (E)
}
inline constexpr std::uint32_t UsedNatives[]{n::Gip,n::MpCut,n::PlayerId,n::pf::FadeOut,n::pf::FadeIn,n::pf::FadedOut,n::pf::PlayerControl,
    n::pf::SpinnerBegin,n::pf::SpinnerEnd,n::pf::SpinnerOff,n::pf::LabelExists,n::pf::AddLiteral,n::ControlOn};
constexpr std::uint32_t SettleFrames=60,MinHoldFrames=75,FadeWaitMax=60,MaxCoverFrames=4500,FadeMs=500;   // v1092: back to 4500 (v1091 had 9000 for a 3600-frame intro wait, withdrawn)
constexpr std::uint32_t LeaveWaitMs=3000;
// v1105 (FREEMODE-GATE-6 E): on the freemode route maintransition T30 / fm_intro own the reveal; the curtain only drops its spinner, falls back
// to a fade-in when the screen stays out FadeGraceMs after the card dropped (no fm_intro thread, no MP cutscene), and gives back the control
// its own entry took, once, ControlGraceMs after the card dropped and only while the control is off with no MP cutscene and no fm_intro.
constexpr std::uint64_t FadeGraceMs=3000,ControlGraceMs=10000;
// v1166 PROPOSAL (online-spinner REPORT.md, 2026-11-10): routeSwitch = Online mode with the freemode-route switch on (fm_route::routeOn(), known
// from the first tick, before maintransition's ENTER is adopted). Then the Entry curtain starts NO spinner of its own: maintransition's
// UI 3 / 27 handlers put the game's own HUD_JOINING busy spinner up behind the card (0x41f6d / 0x235e -> 0x1f17), and the engine has ONE
// busy-spinner slot (END_TEXT_COMMAND_BUSYSPINNER_ON -> sub_82584410(text,type,1); BUSYSPINNER_OFF -> sub_82231E90(1)), so our MP_SPINLOADING
// would overwrite it and our release-time BUSYSPINNER_OFF would clear the game's spinner (v1162 t-onlcash-A-20261110-0231: 05:08:37.48,
// ~3 s before maintransition's own clear when T28 ends). The Leave curtain is unchanged.
struct RouteIn {bool fmRoute=false,releaseRequested=false,released=false,gameIntro=false,routeSwitch=false;std::uint64_t nowMs=0;};
using LogFn=void(*)(const char*);

class Curtain {
public:
    explicit Curtain(LogFn log) noexcept:log_(log) {}
    enum class State : std::uint8_t {Idle,Entry,Leave};
    // Every Sandbox tick. armed false and nothing covered: one store, no native.
    // v1056: revealAllowed false (Online mode before the online character is confirmed, online_mode::pedConfirmed) keeps every entry reveal
    // (session-ready, mp-cutscene, timeout) closed: no Story character is ever uncovered; the timeout then only logs once.
    void tick(recomp::trainer::Natives& nat,std::uint32_t frame,bool on,bool onlineMode,bool carBusy,bool revealAllowed=true,RouteIn rt={}) noexcept {
        armed.store(on,std::memory_order_release);covering.store(st_!=State::Idle,std::memory_order_release);
        if(!on){if(st_!=State::Idle)reveal(nat,frame,"disarmed",true);leaveAsked.store(false);leaveCovered.store(false);return;}
        const std::uint32_t gip=one(nat,n::Gip,{});
        switch(st_) {
        case State::Idle:
            if(!entryDone_ && gip==0u){cover(nat,frame,onlineMode?0u:FadeMs,"entry");st_=State::Entry;gipAt_=0;return;}
            entryDone_=true;
            if(leaveAsked.load(std::memory_order_acquire) && gip==1u){cover(nat,frame,FadeMs,"leave");st_=State::Leave;gipAt_=0;}
            return;
        case State::Entry: {
            out(nat,frame,!rt.routeSwitch);   // v1166 PROPOSAL: no own spinner on the freemode route (the game's HUD_JOINING owns the slot)
            if(gip==1u && !gipAt_)gipAt_=frame;
            if(rt.fmRoute){route(nat,frame,rt);return;}   // v1105 (E): the game reveals on the freemode route
            if(!revealAllowed) {   // v1056: still a Story character: covered, whatever else is ready
                if(frame-start_>=MaxCoverFrames && !heldLogged_){heldLogged_=true;say("RENDER_ONLINE_LOCAL curtain stays (not the online character yet) frames=%u",frame-start_);}
                return;
            }
            // v1091 (lead 09:40, t-onlcre74-20261108-0337: the MP-cutscene flag rose 22 frames after the intro start, this revealed in the same tick
            // and fm_intro's scripted hill shot showed ~60 s before MP_INTRO_CONCAT's first frame): the MP-cutscene reveal waits for the arrival too
            // (carBusy = the car keep or arrival.holding(), which stays true until the arrival releases: v1092 the flag with the cutscene's time advanced
            // or the gameplay camera off, i.e. fm_intro's tour camera has taken over).
            if(gipAt_ && !carBusy && one(nat,n::MpCut,{})==1u){reveal(nat,frame,"mp-cutscene",false);return;}   // fm_intro owns the screen and control
            if(gipAt_ && frame-gipAt_>=SettleFrames && !carBusy && out_ && frame-outAt_>=MinHoldFrames){reveal(nat,frame,"session-ready",true);return;}
            if(frame-start_>=MaxCoverFrames)reveal(nat,frame,"timeout",true);
            return;
        }
        case State::Leave:
            out(nat,frame);
            if(out_)leaveCovered.store(true,std::memory_order_release);
            if(gip==0u && !gipAt_)gipAt_=frame;
            if(gipAt_ && frame-gipAt_>=SettleFrames && !carBusy){reveal(nat,frame,"left",true);return;}
            if(frame-start_>=MaxCoverFrames)reveal(nat,frame,"timeout",true);
            return;
        }
    }
    State state() const noexcept {return st_;}
    bool spinner() const noexcept {return spinner_;}
private:
    // v1105 (FREEMODE-GATE-6 E): Entry on the freemode route. No FadeIn reveal and no control change of the normal kind.
    void route(recomp::trainer::Natives& nat,std::uint32_t frame,const RouteIn& rt) noexcept {
        if(rt.releaseRequested && spinner_){(void)call(nat,n::pf::SpinnerOff,{});spinner_=false;say("RENDER_ONLINE_LOCAL curtain fm-route spinner off (release requested) frames=%u",frame-start_);}
        if(rt.released && !releasedAt_)releasedAt_=rt.nowMs?rt.nowMs:1u;
        const bool cut=one(nat,n::MpCut,{})==1u;
        if(releasedAt_ && !fadeDone_) {
            if(rt.nowMs-releasedAt_>=FadeGraceMs) {
                fadeDone_=true;
                if(!rt.gameIntro && !cut && one(nat,n::pf::FadedOut,{})==1u){(void)call(nat,n::pf::FadeIn,{FadeMs});say("RENDER_ONLINE_LOCAL curtain fm-route fallback fade-in (still out %llu ms after the card dropped)",(unsigned long long)(rt.nowMs-releasedAt_));}
            }
        }
        if(releasedAt_ && !controlDone_ && rt.nowMs-releasedAt_>=ControlGraceMs) {
            const std::uint32_t pl=one(nat,n::PlayerId,{});const bool ctl=one(nat,n::ControlOn,{pl})==1u;
            if(ctl){controlDone_=true;say("RENDER_ONLINE_LOCAL curtain fm-route control already on (the game gave it back)");}
            else if(entryControlOff_ && !cut && !rt.gameIntro){controlDone_=true;(void)call(nat,n::pf::PlayerControl,{pl,1u,0u});
                say("RENDER_ONLINE_LOCAL curtain control restored (curtain's own entry hold; the game did not give it back in %llu ms)",(unsigned long long)(rt.nowMs-releasedAt_));}
        }
        if(releasedAt_ && !spinner_ && fadeDone_ && controlDone_){say("RENDER_ONLINE_LOCAL curtain fm-route entry done frames=%u",frame-start_);entryDone_=true;st_=State::Idle;out_=false;return;}
        if(frame-start_>=MaxCoverFrames && !routeTimeoutLogged_) {
            routeTimeoutLogged_=true;if(spinner_){(void)call(nat,n::pf::SpinnerOff,{});spinner_=false;}
            say("RENDER_ONLINE_LOCAL curtain fm-route timeout frames=%u: spinner off%s",frame-start_,rt.gameIntro?" (fm_intro runs: no fade-in)":"");
        }
    }
    void cover(recomp::trainer::Natives& nat,std::uint32_t frame,std::uint32_t ms,const char* why) noexcept {
        start_=frame;out_=false;spinner_=false;fadeAt_=frame;
        const bool f=call(nat,n::pf::FadeOut,{ms});
        player_=one(nat,n::PlayerId,{});(void)call(nat,n::pf::PlayerControl,{player_,0u,0u});if(st_==State::Idle && !entryDone_)entryControlOff_=true;   // v1105
        say("RENDER_ONLINE_LOCAL curtain cover why=%s fade_ms=%u ok=%d frame=%u",why,ms,f?1:0,frame);
    }
    void out(recomp::trainer::Natives& nat,std::uint32_t frame,bool ownSpinner=true) noexcept {   // the spinner once the screen is out (bounded wait)
        if(out_)return;
        const bool faded=one(nat,n::pf::FadedOut,{})==1u;
        if(!faded && frame-fadeAt_<FadeWaitMax)return;
        out_=true;outAt_=frame;if(ownSpinner)spinnerOn(nat);   // v1166 PROPOSAL: false on the freemode route (log: spinner=0 label=-)
        say("RENDER_ONLINE_LOCAL curtain out frames=%u spinner=%d label=%s%s",frame-start_,spinner_?1:0,spinner_?(spinnerMp_?"MP_SPINLOADING":"literal"):"-",faded?"":" timeout=1");
    }
    void reveal(recomp::trainer::Natives& nat,std::uint32_t frame,const char* why,bool control) noexcept {
        if(spinner_)(void)call(nat,n::pf::SpinnerOff,{});
        (void)call(nat,n::pf::FadeIn,{FadeMs});
        if(control)(void)call(nat,n::pf::PlayerControl,{player_,1u,0u});
        say("RENDER_ONLINE_LOCAL curtain reveal why=%s state=%s frames=%u control=%d",why,st_==State::Leave?"leave":"entry",frame-start_,control?1:0);
        if(st_==State::Entry)entryDone_=true;
        if(st_==State::Leave){leaveAsked.store(false,std::memory_order_release);leaveCovered.store(false,std::memory_order_release);}
        st_=State::Idle;spinner_=false;out_=false;
    }
    void spinnerOn(recomp::trainer::Natives& nat) noexcept {
        namespace pf=recomp::online::placefade;
        const std::uint32_t label=nat.text(pf::SpinnerLabel);
        const bool mp=label && one(nat,n::pf::LabelExists,{label})==1u;
        if(mp){if(!call(nat,n::pf::SpinnerBegin,{label}))return;}
        else {
            const std::uint32_t lit=nat.text(pf::LiteralLabel),txt=nat.text(pf::LiteralText);
            if(!lit || !call(nat,n::pf::SpinnerBegin,{lit}))return;
            if(txt)(void)call(nat,n::pf::AddLiteral,{txt});
        }
        (void)call(nat,n::pf::SpinnerEnd,{pf::SpinnerStyle});spinner_=true;spinnerMp_=mp;
    }
    static std::uint32_t one(recomp::trainer::Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};return nat.call(h,a.begin(),unsigned(a.size()),out)?out[0]:0xFFFFFFFFu;}
    static bool call(recomp::trainer::Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};return nat.call(h,a.begin(),unsigned(a.size()),out);}
    void say(const char* l) const noexcept {if(log_)log_(l);}   // v1105: a fixed line (no format)
    template<class A0,class... A> void say(const char* fmt,A0 a0,A... a) const noexcept {if(!log_)return;char l[200];std::snprintf(l,sizeof l,fmt,a0,a...);log_(l);}
    LogFn log_;State st_=State::Idle;bool entryDone_=false,out_=false,spinner_=false,spinnerMp_=false,heldLogged_=false;
    std::uint32_t start_=0,fadeAt_=0,outAt_=0,gipAt_=0,player_=0;
    std::uint64_t releasedAt_=0;bool fadeDone_=false,controlDone_=false,entryControlOff_=false,routeTimeoutLogged_=false;   // v1105
};
}
