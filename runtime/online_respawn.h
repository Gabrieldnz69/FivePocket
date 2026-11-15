#pragma once
// online_respawn.h (v1123, trainer-mods; owner via the lead 2026-11-09: "game freezes when character dies?" in Online mode on v1196).
// OBSERVED (lead, the owner's run pid 18621): after the death the controller and the env lines kept logging every second (ped alive in the
// table, vis=1) - the game ran, nothing was hung. OBSERVED (code): in GTA Online the respawn belongs to freemode (freemode 0xbaa9b..0xbab63:
// NETWORK_LEAVE_PED_BEHIND_BEFORE_WARP when dead, then NETWORK_RESURRECT_LOCAL_PLAYER(x, y, z, heading, 5000, 1) at its spawn point; the WASTED
// shard = MP_BIG_MESSAGE_FREEMODE's SHOW_WASTED_MP_MESSAGE with the title RESPAWN_W, freemode 0xb6d0a / 0xb6eb1 / 0xb7162). In Online mode with
// the freemode route off, freemode does not run, and in a network game the engine leaves the respawn to the scripts - so nobody resurrects the
// player: he stays dead. Nothing in our Online code handled death (grep of v1196: no respawn / resurrect / IS_PLAYER_DEAD use).
// v1123: in play_mode=online only, while a network game is in progress and NO freemode thread runs (on the route freemode answers it itself),
// the player's death is answered as GTA Online does: the WASTED shard (the game's own movie, method and text), after WastedMs a fade out, then
// NETWORK_RESURRECT_LOCAL_PLAYER at the nearest safe coordinate to the death spot (GET_SAFE_COORD_FOR_PED, else the death spot itself) with
// freemode's 5000 ms / 1 arguments, wanted level cleared, control on, a fade in. One attempt per death; a still-dead player after the fade in
// gets one retry, then (still dead) a new attempt after RetryCooldownMs. files/private-trace/diag-online-respawn-off disables it (read once by the glue). Pure: the state machine over Natives.
#include "trainer.h"
#include <cstdint>
#include <cstdio>
namespace recomp::online_respawn {
using trainer::Natives;using trainer::joaat;using trainer::bits;using trainer::asFloat;
namespace n {
constexpr std::uint32_t PlayerId=joaat("PLAYER_ID"),PlayerPed=joaat("PLAYER_PED_ID"),EntityDead=joaat("IS_ENTITY_DEAD"),PlayerDead=joaat("IS_PLAYER_DEAD"),
    Coords=joaat("GET_ENTITY_COORDS"),Heading=joaat("GET_ENTITY_HEADING"),SafeCoord=joaat("GET_SAFE_COORD_FOR_PED"),
    Resurrect=joaat("NETWORK_RESURRECT_LOCAL_PLAYER"),FadeOut=joaat("DO_SCREEN_FADE_OUT"),FadeIn=joaat("DO_SCREEN_FADE_IN"),
    FadedOut=joaat("IS_SCREEN_FADED_OUT"),Control=joaat("SET_PLAYER_CONTROL"),Threads=joaat("GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH"),
    Timer=joaat("GET_GAME_TIMER"),Gip=joaat("NETWORK_IS_GAME_IN_PROGRESS"),ClearWanted=joaat("CLEAR_PLAYER_WANTED_LEVEL"),
    RequestMovie=joaat("REQUEST_SCALEFORM_MOVIE"),MovieLoaded=joaat("HAS_SCALEFORM_MOVIE_LOADED"),BeginMethod=joaat("BEGIN_SCALEFORM_MOVIE_METHOD"),
    BeginSfText=joaat("BEGIN_TEXT_COMMAND_SCALEFORM_STRING"),EndSfText=joaat("END_TEXT_COMMAND_SCALEFORM_STRING"),EndMethod=joaat("END_SCALEFORM_MOVIE_METHOD"),
    DrawFull=joaat("DRAW_SCALEFORM_MOVIE_FULLSCREEN"),ReleaseMovie=joaat("SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED");
}
inline constexpr std::uint32_t UsedNatives[]{n::PlayerId,n::PlayerPed,n::EntityDead,n::PlayerDead,n::Coords,n::Heading,n::SafeCoord,n::Resurrect,n::FadeOut,
    n::FadeIn,n::FadedOut,n::Control,n::Threads,n::Timer,n::Gip,n::ClearWanted,n::RequestMovie,n::MovieLoaded,n::BeginMethod,n::BeginSfText,n::EndSfText,
    n::EndMethod,n::DrawFull,n::ReleaseMovie};
constexpr std::uint32_t Freemode=0xC875557Du;              // joaat("freemode")
constexpr std::uint32_t RetryCooldownMs=30000;   // after a FAILED answer: the next attempt (never stuck dead, no tight loop)
constexpr std::uint32_t WastedMs=4500,FadeOutMs=500,FadeInMs=1000,FadeWaitMaxMs=2500,AliveCheckMs=3000,CheckEveryFrames=10;
constexpr std::uint32_t InvincibleMs=5000,ResurrectFlag=1;  // freemode 0xbab3d / 0xbab40
constexpr char Movie[]="MP_BIG_MESSAGE_FREEMODE",Method[]="SHOW_WASTED_MP_MESSAGE",Title[]="RESPAWN_W";
enum class State : std::uint8_t {Idle,Wasted,FadingOut,FadingIn};
using LogFn=void(*)(const char*);
class Respawn {
public:
    explicit Respawn(LogFn log) noexcept:log_(log) {}
    State state() const noexcept {return st_;}
    unsigned respawns() const noexcept {return done_;}
    // Every Online-mode tick (play_mode=online, the glue's controller branch). off: the kill switch.
    void tick(Natives& nat,std::uint32_t frame,bool off) noexcept {
        if(off)return;
        switch(st_) {
        case State::Idle: {
            if(frame-lastCheck_<CheckEveryFrames && checked_)return;
            checked_=true;lastCheck_=frame;
            if(coolUntil_ && int(one(nat,n::Timer)-coolUntil_)<0)return;
            coolUntil_=0;
            if(one(nat,n::Gip)!=1u)return;                              // no network game: Story / Sandbox paths own death
            if(one1(nat,n::Threads,Freemode)!=0u)return;                // freemode runs (the route): it answers death itself
            const std::uint32_t pid=one(nat,n::PlayerId),ped=one(nat,n::PlayerPed);
            if(!(one1(nat,n::PlayerDead,pid)==1u || (ped && one1(nat,n::EntityDead,ped)==1u)))return;
            std::uint32_t c[3]{};const std::uint32_t a[2]{ped,1u};(void)nat.call(n::Coords,a,2,c);
            x_=asFloat(c[0]);y_=asFloat(c[1]);z_=asFloat(c[2]);h_=asFloat(one1(nat,n::Heading,ped));
            t0_=one(nat,n::Timer);movie_=0;shown_=false;retried_=false;
            const std::uint32_t m=nat.text(Movie);if(m){std::uint32_t o[3]{};if(nat.call(n::RequestMovie,&m,1,o))movie_=o[0];}
            char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_RESPAWN dead at %.1f,%.1f,%.1f heading %.0f (no freemode script): WASTED, respawn in %u ms (movie %u)",
                x_,y_,z_,h_,unsigned(WastedMs),unsigned(movie_));log_(l);
            st_=State::Wasted;return;
        }
        case State::Wasted: {
            shard(nat);
            if(one(nat,n::Timer)-t0_<WastedMs)return;
            const std::uint32_t ms=FadeOutMs;(void)go(nat,n::FadeOut,&ms,1);
            t1_=one(nat,n::Timer);st_=State::FadingOut;return;
        }
        case State::FadingOut: {
            shard(nat);
            if(one(nat,n::FadedOut)!=1u && one(nat,n::Timer)-t1_<FadeWaitMaxMs)return;
            resurrect(nat);return;
        }
        case State::FadingIn: {
            const std::uint32_t now=one(nat,n::Timer);
            if(!fadedIn_ && now-t1_>=300u){const std::uint32_t ms=FadeInMs;(void)go(nat,n::FadeIn,&ms,1);fadedIn_=true;}
            if(now-t1_<AliveCheckMs)return;
            const std::uint32_t pid=one(nat,n::PlayerId),ped=one(nat,n::PlayerPed);
            const bool dead=one1(nat,n::PlayerDead,pid)==1u || (ped && one1(nat,n::EntityDead,ped)==1u);
            if(dead && !retried_){retried_=true;log_("RENDER_ONLINE_RESPAWN still dead after the fade in: one retry");
                const std::uint32_t ms=FadeOutMs;(void)go(nat,n::FadeOut,&ms,1);t1_=now;st_=State::FadingOut;return;}
            char l[120];std::snprintf(l,sizeof l,"RENDER_ONLINE_RESPAWN %s (respawn #%u)",dead?"FAILED: the player is still dead":"done: the player is alive",done_);log_(l);
            if(dead)coolUntil_=now+RetryCooldownMs;
            st_=State::Idle;checked_=false;return;
        }
        }
    }
private:
    static bool go(Natives& nat,std::uint32_t h,const std::uint32_t* a,unsigned k) noexcept {std::uint32_t o[3]{};return nat.call(h,a,k,o);}
    static std::uint32_t one(Natives& nat,std::uint32_t h) noexcept {std::uint32_t o[3]{};return nat.call(h,nullptr,0,o)?o[0]:0u;}
    static std::uint32_t one1(Natives& nat,std::uint32_t h,std::uint32_t a) noexcept {std::uint32_t o[3]{};return nat.call(h,&a,1,o)?o[0]:0u;}
    void shard(Natives& nat) noexcept {   // the game's own WASTED message: shown once loaded, drawn every frame until the respawn
        if(!movie_)return;
        if(!shown_) {
            if(one1(nat,n::MovieLoaded,movie_)!=1u)return;
            const std::uint32_t meth=nat.text(Method);if(!meth)return;
            const std::uint32_t ma[2]{movie_,meth};std::uint32_t o[3]{};
            if(!nat.call(n::BeginMethod,ma,2,o))return;
            const std::uint32_t t=nat.text(Title);if(t){(void)go(nat,n::BeginSfText,&t,1);(void)go(nat,n::EndSfText,nullptr,0);}
            (void)go(nat,n::EndMethod,nullptr,0);
            shown_=true;log_("RENDER_ONLINE_RESPAWN WASTED shard shown (MP_BIG_MESSAGE_FREEMODE SHOW_WASTED_MP_MESSAGE RESPAWN_W)");
        }
        const std::uint32_t d[6]{movie_,255u,255u,255u,255u,0u};(void)go(nat,n::DrawFull,d,6);
    }
    void resurrect(Natives& nat) noexcept {
        float sx=x_,sy=y_,sz=z_;const std::uint32_t out=nat.word(0);(void)nat.word(0);(void)nat.word(0);
        bool safe=false;
        if(out){const std::uint32_t a[6]{bits(x_),bits(y_),bits(z_),1u,out,16u};std::uint32_t o[3]{};
            if(nat.call(n::SafeCoord,a,6,o) && o[0]==1u){sx=asFloat(nat.load(out));sy=asFloat(nat.load(out+4u));sz=asFloat(nat.load(out+8u));safe=true;}}
        const std::uint32_t r[6]{bits(sx),bits(sy),bits(sz),bits(h_),InvincibleMs,ResurrectFlag};const bool ok=go(nat,n::Resurrect,r,6);
        const std::uint32_t pid=one(nat,n::PlayerId);(void)go(nat,n::ClearWanted,&pid,1);
        const std::uint32_t ctl[3]{pid,1u,0u};(void)go(nat,n::Control,ctl,3);
        if(movie_){(void)go(nat,n::ReleaseMovie,&movie_,1);movie_=0;}
        ++done_;t1_=one(nat,n::Timer);fadedIn_=false;st_=State::FadingIn;
        char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_RESPAWN resurrect ok=%d at %.1f,%.1f,%.1f (%s) heading %.0f, %u ms protection",
            ok?1:0,sx,sy,sz,safe?"GET_SAFE_COORD_FOR_PED":"the death spot",h_,unsigned(InvincibleMs));log_(l);
    }
    LogFn log_;State st_=State::Idle;bool checked_=false,shown_=false,retried_=false,fadedIn_=false;
    std::uint32_t lastCheck_=0,t0_=0,t1_=0,movie_=0,coolUntil_=0;unsigned done_=0;float x_=0,y_=0,z_=0,h_=0;
};
}
