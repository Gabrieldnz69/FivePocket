#pragma once
// place_fade.h (v976, trainer-mods 2026-11-07; the owner: "add a loading screen so it transitions like gta online"): the screen
// transition around every single place trip (16 n, a door enter, a door exit), as online's property entry does it. Decoded from the disc:
//  - am_mp_property_ext 0x010c4: DO_SCREEN_FADE_OUT(500) (0 on one flagged path); am_mp_property_int 0x00c49 / 0x00cf1 / 0x00f97 /
//    0x020dc: DO_SCREEN_FADE_IN(500) once IS_SCREEN_FADED_OUT. No minimum hold seen (the fade-in follows the load / ready checks).
//  - the loading prompt: neither property script calls a spinner; freemode does: BEGIN_TEXT_COMMAND_BUSYSPINNER_ON("MP_SPINLOADING")
//    then END_TEXT_COMMAND_BUSYSPINNER_ON(1); BUSYSPINNER_OFF ends it (game-ui-assets NOTES §3: all three registered). The label is in an
//    MP text block, so it is used only when DOES_TEXT_LABEL_EXIST says so; else "STRING" + the literal "Loading" (the registered
//    literal route, ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME).
// Sequence: begin (the trip was accepted) = fade out + SET_PLAYER_CONTROL(player, 0, 0); readyToMove = faded out (or FadeWaitMax frames
// passed: never wait forever), and the spinner starts then; end (EVERY exit path: ok, fail, refusal after the start, abort / gate /
// death, Sandbox end) = BUSYSPINNER_OFF, DO_SCREEN_FADE_IN(500), SET_PLAYER_CONTROL(player, 1, 0); end is idempotent.
// A missing fade native (DO_SCREEN_FADE_OUT answers false) = no transition at all (the trip runs as before). Natives only, no allocation.
#include "trainer.h"
#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace recomp::online::placefade {
using trainer::Natives;using trainer::joaat;
struct Named {const char* name;std::uint32_t hash;};
namespace n {
constexpr std::uint32_t FadeOut=joaat("DO_SCREEN_FADE_OUT"),FadeIn=joaat("DO_SCREEN_FADE_IN"),FadedOut=joaat("IS_SCREEN_FADED_OUT"),
    PlayerControl=joaat("SET_PLAYER_CONTROL"),SpinnerBegin=joaat("BEGIN_TEXT_COMMAND_BUSYSPINNER_ON"),
    SpinnerEnd=joaat("END_TEXT_COMMAND_BUSYSPINNER_ON"),SpinnerOff=joaat("BUSYSPINNER_OFF"),LabelExists=joaat("DOES_TEXT_LABEL_EXIST"),
    AddLiteral=joaat("ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME");
}
inline constexpr Named Used[]{{"DO_SCREEN_FADE_OUT",n::FadeOut},{"DO_SCREEN_FADE_IN",n::FadeIn},{"IS_SCREEN_FADED_OUT",n::FadedOut},
    {"SET_PLAYER_CONTROL",n::PlayerControl},{"BEGIN_TEXT_COMMAND_BUSYSPINNER_ON",n::SpinnerBegin},
    {"END_TEXT_COMMAND_BUSYSPINNER_ON",n::SpinnerEnd},{"BUSYSPINNER_OFF",n::SpinnerOff},{"DOES_TEXT_LABEL_EXIST",n::LabelExists},
    {"ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME",n::AddLiteral}};
constexpr std::uint32_t FadeMs=500;            // online's DO_SCREEN_FADE_OUT / _IN duration (decoded above)
// v978 (owner: "the transition is too short"): online's property scripts fade 500 / 500 (decoded) but then wait on the network load, so
// its dark screen with the spinner lasts seconds; the scripts hold no fixed time (the fade-in follows the load / ready checks). Per the
// lead's rule for an unclear hold: the spinner stays at least MinHoldFrames (~2.5 s at the Thor's ~30 fps) after the screen is out, on
// a successful trip only (a failure comes back at once).
constexpr std::uint32_t MinHoldFrames=75;
constexpr std::uint32_t FadeWaitMax=60;        // frames: never wait longer for IS_SCREEN_FADED_OUT (the fade itself is ~15 at 30 fps)
constexpr std::uint32_t SpinnerStyle=1;        // freemode's END_TEXT_COMMAND_BUSYSPINNER_ON argument with MP_SPINLOADING
constexpr const char* SpinnerLabel="MP_SPINLOADING";
constexpr const char* LiteralLabel="STRING";constexpr const char* LiteralText="Loading";
using LogFn=void(*)(const char*);

class Fade {
public:
    bool active() const noexcept {return active_;}
    bool spinner() const noexcept {return spinner_;}
    // v978: a successful trip may end (fade in) only once the screen has been out MinHoldFrames (no transition / not out yet: yes).
    bool holdDone(std::uint32_t frame) const noexcept {return !active_ || !out_ || frame-outAt_>=MinHoldFrames;}
    // The trip was accepted (running): start the transition. Returns false (and does nothing more) when the fade native is missing.
    bool begin(Natives& nat,std::uint32_t player,std::uint32_t frame,LogFn log) noexcept {
        if(active_)return true;
        log_=log;player_=player;start_=last_=frame;spinner_=false;out_=false;
        if(!call(nat,n::FadeOut,{FadeMs})){say("RENDER_ONLINE place-fade off reason=missing-native");return false;}
        (void)call(nat,n::PlayerControl,{player_,0,0});
        active_=true;say("RENDER_ONLINE place-fade out ms=500");
        return true;
    }
    // Every trip step: may the player be moved now? (no transition: yes). Starts the spinner once the screen is out.
    bool readyToMove(Natives& nat,std::uint32_t frame) noexcept {
        if(!active_ || out_)return true;
        last_=frame;
        std::uint32_t r[3]{};
        const bool faded=nat.call(n::FadedOut,nullptr,0,r) && r[0];
        if(!faded && frame-start_<FadeWaitMax)return false;
        out_=true;outAt_=frame;spinnerOn(nat);
        char l[96];std::snprintf(l,sizeof l,"RENDER_ONLINE place-fade faded frames=%u%s",frame-start_,faded?"":" timeout=1");say(l);
        return true;
    }
    // Every exit path. Idempotent: a second call does nothing.
    void end(Natives& nat,const char* why,std::uint32_t frame=0) noexcept {   // frame 0: the last frame the transition saw (abort paths)
        if(!active_)return;
        if(frame)last_=frame;
        if(spinner_)(void)call(nat,n::SpinnerOff,{});
        (void)call(nat,n::FadeIn,{FadeMs});
        (void)call(nat,n::PlayerControl,{player_,1,0});
        active_=false;spinner_=false;out_=false;
        char l[112];std::snprintf(l,sizeof l,"RENDER_ONLINE place-fade in why=%s frames=%u",why?why:"-",last_-start_);say(l);
    }
private:
    void spinnerOn(Natives& nat) noexcept {
        const std::uint32_t label=nat.text(SpinnerLabel);std::uint32_t r[3]{};
        const bool mp=label && nat.call(n::LabelExists,&label,1,r) && r[0];
        if(mp){if(!call(nat,n::SpinnerBegin,{label}))return;}
        else {
            const std::uint32_t lit=nat.text(LiteralLabel),txt=nat.text(LiteralText);
            if(!lit || !call(nat,n::SpinnerBegin,{lit}))return;
            if(txt)(void)call(nat,n::AddLiteral,{txt});
        }
        (void)call(nat,n::SpinnerEnd,{SpinnerStyle});
        spinner_=true;say(mp?"RENDER_ONLINE place-fade spinner label=MP_SPINLOADING":"RENDER_ONLINE place-fade spinner label=literal");
    }
    static bool call(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};return nat.call(h,a.begin(),unsigned(a.size()),out);
    }
    void say(const char* l) const noexcept {if(log_)log_(l);}
    bool active_=false,spinner_=false,out_=false;std::uint32_t player_=0,start_=0,last_=0,outAt_=0;LogFn log_=nullptr;
};
} // namespace recomp::online::placefade
