#pragma once
// touch_exports.h - Update 1 touch controls (ALLOCATION.md section 14; android-qol 2026-11-07): the library side of two exports the
// Android / iOS apps already probe for (dlsym):
//   int recomp_camera_turn_v1(float yaw_deg, float pitch_deg)  adds a turn to the gameplay camera; 0 accepted / 1 refused (not in gameplay)
//   int recomp_aim_assist_v1(int mode)                         0 the game's own targeting mode, 1 strong; -1 reads back; -2 bad mode;
//                                                              -3 the red-reticle reading (STATUS C4): bit 0 aiming with the reticle
//                                                              shown, bit 1 the game's reticle is red (hostile target); 0 otherwise
// The extern "C" wrappers and the tick call live in the glue (NOTE.md); this header holds the logic and is host-tested
// (tests/touch_exports_test.cpp). It depends only on trainer.h's Natives interface (any type with the same call()).
//
// Mechanism: the game's own natives, read from this disc's handlers (validated-ppc; registrations.json):
//   GET/SET_GAMEPLAY_CAM_RELATIVE_HEADING 82d59358 / 82d593a0: one float in degrees (the setter multiplies by the deg->rad constant at
//     0x82000B0C, wraps it in sub_82372D70, stores it on the gameplay camera at +1148 and sets its "apply" byte +1192);
//   GET/SET_GAMEPLAY_CAM_RELATIVE_PITCH  82d593f0 / 82d59438: the setter reads TWO floats (angle in degrees, scaling) and clamps the angle;
//   IS_GAMEPLAY_CAM_RENDERING 82d595b0, IS_PAUSE_MENU_ACTIVE 82d63610: the turn applies only while the gameplay camera renders, no pause;
//   SET_PLAYER_TARGETING_MODE 82db2590: sub_8257A408(2, mode) = a word store into the runtime settings table 0x83895A28[2];
//   GET_LOCAL_PLAYER_AIM_STATE 82dad078: reads that same word (0x83895A28 + 8). In-memory only: nothing is written to a save. Strong saves
//     the game's own value first and mode 0 puts it back.
// Cost while unused: the tick's first statement is one relaxed atomic load (wanted()); no native runs, nothing is allocated.
//
// Red reticle (mode -3; subagent red-reticle 2026-11-10, REPORT.md): the HUD reticle's own state, read from guest memory, not a
// re-implementation of the hostility rules. The HUD update sub_8248C268 (and sub_825EEA40) calls the reticle update sub_823010E0 with
// this = 0x83788D60 every frame. That update asks sub_82238370(_, player ped, weapon info) for the style of the ped in CPlayerInfo+536
// (the free-aim target, as GET_ENTITY_PLAYER_IS_FREE_AIMING_AT's second choice) and sends it to the reticle movie (HUD component 14)
// as SET_RETICLE_STYLE (string 0x82019950, call at 0x82302304), caching the value at +12. Styles: 0 no live ped, 1 hostile (the target
// is in combat with / targeting / hates or dislikes the player, or is a cop/swat/army ped while the player is wanted in a network game),
// 2 any other living ped, 3 dead, 4 a friendly network player. +40 is the last SET_RETICLE_VISIBLE (the engine itself tests == 1),
// +20 is -1 in the normal branch (3 = dimmed with style 0 sent; other values = a scripted flash colour, the style is not updated).
// Red = style 1: INFERRED from the chooser's control flow (the colour table lives in the movie, not in the executable); the device check
// t-reticle confirms it on screen before release.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

namespace touchx {

constexpr std::uint32_t joaatLower(const char* s) noexcept {
    std::uint32_t h=0;
    for(;*s;++s){unsigned char c=static_cast<unsigned char>(*s);if(c>='A' && c<='Z')c=static_cast<unsigned char>(c+32);h+=c;h+=h<<10;h^=h>>6;}
    h+=h<<3;h^=h>>11;h+=h<<15;return h;
}
namespace n {
constexpr std::uint32_t GetRelHeading=joaatLower("GET_GAMEPLAY_CAM_RELATIVE_HEADING"),SetRelHeading=joaatLower("SET_GAMEPLAY_CAM_RELATIVE_HEADING"),
    GetRelPitch=joaatLower("GET_GAMEPLAY_CAM_RELATIVE_PITCH"),SetRelPitch=joaatLower("SET_GAMEPLAY_CAM_RELATIVE_PITCH"),
    CamRendering=joaatLower("IS_GAMEPLAY_CAM_RENDERING"),PauseMenu=joaatLower("IS_PAUSE_MENU_ACTIVE"),
    SetTargeting=joaatLower("SET_PLAYER_TARGETING_MODE"),GetAimState=joaatLower("GET_LOCAL_PLAYER_AIM_STATE"),
    FrameCount=joaatLower("GET_FRAME_COUNT");   // v1175: sub_82D666E8, no argument, the frame counter word
// red reticle gate (mode -3). Handlers on this disc (registrations.json): PLAYER_ID 82db1ea8, PLAYER_PED_ID 82db2118, IS_PLAYER_PLAYING
// 82db1198, IS_ENTITY_DEAD 82d77c78, IS_PED_IN_ANY_VEHICLE 82dd12f8 (2 words: ped, BOOL at-get-in), IS_CUTSCENE_ACTIVE 82d5af20,
// IS_PLAYER_SWITCH_IN_PROGRESS 82db6558, IS_PLAYER_FREE_AIMING 82db1770 (CPlayerInfo+2000 bit 0x02), IS_PLAYER_TARGETTING_ANYTHING
// 82db1980 (CPlayerInfo+384 != 0, the lock-on target).
constexpr std::uint32_t PlayerId=joaatLower("PLAYER_ID"),PlayerPedId=joaatLower("PLAYER_PED_ID"),PlayerPlaying=joaatLower("IS_PLAYER_PLAYING"),
    EntityDead=joaatLower("IS_ENTITY_DEAD"),PedInAnyVehicle=joaatLower("IS_PED_IN_ANY_VEHICLE"),CutsceneActive=joaatLower("IS_CUTSCENE_ACTIVE"),
    SwitchInProgress=joaatLower("IS_PLAYER_SWITCH_IN_PROGRESS"),FreeAiming=joaatLower("IS_PLAYER_FREE_AIMING"),
    TargettingAnything=joaatLower("IS_PLAYER_TARGETTING_ANYTHING");
static_assert(GetRelHeading==0xcaf839c2u && SetRelHeading==0x20c6217cu && GetRelPitch==0xfc5a4946u && SetRelPitch==0x6381b963u,
              "camera hashes as registered on this disc");
static_assert(CamRendering==0x0ef276dau && PauseMenu==0xd3600591u && SetTargeting==0x61cae253u && GetAimState==0x81802053u,
              "gate and targeting hashes as registered on this disc");
static_assert(PlayerId==0x8aea886cu && PlayerPedId==0xfa92e226u && PlayerPlaying==0xe15d777fu && EntityDead==0xb6f7cbacu &&
              PedInAnyVehicle==0x3b0171eeu && CutsceneActive==0xcce2fe9du && SwitchInProgress==0x56135accu && FreeAiming==0x1dec67b7u &&
              TargettingAnything==0x456db50du,"red-reticle gate hashes as registered on this disc");
}
// The natives this header calls (the glue lists them with its registered-native check, as creatormenu::UsedNatives).
inline constexpr std::uint32_t UsedNatives[]{n::GetRelHeading,n::SetRelHeading,n::GetRelPitch,n::SetRelPitch,n::CamRendering,n::PauseMenu,
                                              n::SetTargeting,n::GetAimState,n::FrameCount,
                                              n::PlayerId,n::PlayerPedId,n::PlayerPlaying,n::EntityDead,n::PedInAnyVehicle,n::CutsceneActive,
                                              n::SwitchInProgress,n::FreeAiming,n::TargettingAnything};

// The HUD reticle (see the header comment). Fixed guest words in .data: no pointer is followed.
namespace reticle {
constexpr std::uint32_t Object=0x83788D60u;    // lis -31879 / addi -29344 at 0x8248C430 and 0x825EEAC0 (both callers of sub_823010E0)
constexpr std::uint32_t Style=Object+12u;      // s32: last SET_RETICLE_STYLE; -1 unsent (reset sub_825D7208, homing lock-on branch)
constexpr std::uint32_t Flash=Object+20u;      // s32: -1 normal; 3 dimmed; other = scripted flash colour (style then not updated)
constexpr std::uint32_t Visible=Object+40u;    // s32: last SET_RETICLE_VISIBLE; 1 shown, 0 hidden, -1 reset (sub_825CB520)
constexpr std::int32_t Hostile=1;              // sub_82238370's hostile result (0x822385CC li r29,1 -> 0x822385DC returns 1)
constexpr int ReadTicks=15;                    // ticks kept reading after the app's last -3 call (~0.5 s at 30 fps)
constexpr std::uint64_t StaleMs=100;           // a reading older than this answers 0 (no tick: pause, loading, the VM not running)
// The published bits from one tick's reads. Pure (host-tested).
constexpr std::uint32_t bits(std::int32_t visible,std::int32_t flash,std::int32_t style,bool aiming,bool gateOk) noexcept {
    return (!gateOk || !aiming || visible!=1)?0u:(1u|((flash==-1 && style==Hostile)?2u:0u));
}
inline std::uint64_t nowMs() noexcept {   // +1: never 0 (0 = no reading yet)
    return std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())+1u;
}
}

// Targeting mode for "strong" (aim snaps hardest). INFERRED from the natives' documented order (0 = assisted aim, full lock-on); the
// device check t-touch-exports reads GET_LOCAL_PLAYER_AIM_STATE for each pause-menu choice to confirm the mapping before release.
constexpr int StrongTargetingMode=0;
constexpr float MaxTurnDeg=90.f;   // a single call larger than this is clamped (a bad caller cannot spin the camera)
constexpr float PitchScale=1.f;    // SET_GAMEPLAY_CAM_RELATIVE_PITCH's second argument
// v1175 (lead 19:1x, Fold run-v1401-11101857: one ~14 deg up-swipe took the pitch from 4.7 to -70, yaw exact). The asm (this disc):
// SET_GAMEPLAY_CAM_RELATIVE_PITCH (sub_82D59438) does not turn the camera: it posts a TARGET (deg->rad, clamped to +-90) that the director
// (sub_824B9498) hands to the follow camera (sub_8262C5A0: cam+896 pitch, +904 rate, flags |0x29). The follow camera (sub_824B1038) blends
// toward +896 PLUS its attach-matrix pitch every frame and clears the request only within 1 deg. GET (sub_82D593F0 -> sub_8261A348) is the
// RENDERED pitch minus the ped's / vehicle's own pitch: another reference. Re-reading GET each tick while our target is still blending adds
// that difference again every tick of a swipe (the compounding). Units match (both natives convert degrees); one SET per tick (yaw, same
// path, exact). The game's own use: set a target once and let the camera blend. So: while our request is in flight a swipe's ticks add
// to OUR last target; GET is read only for a new swipe (more than PitchFlightTicks game frames, GET_FRAME_COUNT, after our last pitch SET).
// That read is also the last swipe's settled result (GET - target = the reference difference): kept for the logs / the Fold check. No
// extra tick and no native while unused (only a swipe's ticks read the frame count).
constexpr int PitchFlightTicks=15;   // game frames (0.5 s at 30 fps)
constexpr float MaxPitchDeg=89.f;    // the native clamps to +-90 deg
constexpr float MaxRefDiffDeg=20.f,MaxRefJumpDeg=6.f,MaxRefAgreeDeg=2.f;   // v1178: accepted / the largest change per reading / two first readings agree
// v1179 (lead 23:3x, Fold run-v1178-11102321: from swipe 3 6/6 within 15 %, but swipes 1-2 moved ~1.3 of 7-8.5 deg): the reference difference
// starts at the Fold's measured on-foot value (run-v1175-11102004 / run-v1178: diff = -7.2 on every settle) instead of 0, so swipe 1 is right
// on foot; the learned value (two agreeing settles) replaces it - another camera (a vehicle's) differs. Measuring it at runtime before the
// first swipe would need a probe SET, which the follow camera blends toward: a visible camera move.
constexpr float SeedRefDiffDeg=-7.2f;
constexpr int LookTicks=60;        // v990: looks after a refused turn (2 s at 30 fps), then quiet again until the next call

inline std::uint32_t fbits(float f) noexcept {std::uint32_t w;std::memcpy(&w,&f,4);return w;}
inline float fval(std::uint32_t w) noexcept {float f;std::memcpy(&f,&w,4);return f;}

class TouchExports {
public:
    // ---- any thread (the app's JNI / UIKit thread)
    // recomp_camera_turn_v1: adds to the pending turn; refused (1) while the last tick saw no gameplay camera (menus, cutscene, pause).
    int turn(float yawDeg,float pitchDeg) noexcept {
        if(!(yawDeg==yawDeg) || !(pitchDeg==pitchDeg))return 1;   // NaN
        touchInput();   // v1171 (owner): a swipe is touch input - Strong (if chosen) may apply
        if(!gameplay_.load(std::memory_order_relaxed)){lookBudget_.store(LookTicks,std::memory_order_relaxed);needCheck_.store(true,std::memory_order_release);return 1;} // (ticks look again, up to LookTicks)
        add(yaw_,clamp(yawDeg));add(pitch_,clamp(pitchDeg));
        pending_.store(true,std::memory_order_release);
        return 0;
    }
    // recomp_aim_assist_v1: 0 game / 1 strong (applied at the next tick); -1 reads back the requested mode; anything else -2.
    int aim(int mode) noexcept {
        if(mode==-3)return reticleRead(reticle::nowMs());   // red reticle (C4): never -2 on a library that has it
        if(mode==-1)return wantStrong_.load(std::memory_order_relaxed)?1:0;
        if(mode==-5){controllerInput();return 0;}   // v1171: the app saw a controller input (QolTouch.merge: the controller won)
        if(mode==-6){touchInput();return 0;}        // v1171: the app saw touch input (re-arms Strong if chosen)
        if(mode!=0 && mode!=1)return -2;
        wantStrong_.store(mode==1,std::memory_order_relaxed);
        touchInput();   // the app's own aim choice comes from its touch menu
        aimDirty_.store(true,std::memory_order_release);
        return 0;
    }
    // v1171 (OWNER ORDER 2026-11-10: "STRONG AIM ASSIST IS FOR THE TOUCH CONTROLS", never for a controller): Strong is in effect only
    // while touch is the active input. touchInput() = a touch-originated call (camera_turn, the app's aim / -6); controllerInput() = the app's
    // aim(-5) on a controller input (the library cannot tell: the Fold's on-screen pad and a real controller share updateGamepad). The game's
    // own targeting mode comes back at the next tick; Strong is applied again only after the next touch input.
    void touchInput() noexcept {if(!touchActive_.exchange(true,std::memory_order_acq_rel))aimDirty_.store(true,std::memory_order_release);}
    void controllerInput() noexcept {if(touchActive_.exchange(false,std::memory_order_acq_rel))aimDirty_.store(true,std::memory_order_release);}
    bool touchActive() const noexcept {return touchActive_.load(std::memory_order_relaxed);}
    // settings/menu.txt as read at start (the app's words; "aim-assist=game" = the game's own mode, no word = strong).
    void loadSettings(const char* text) noexcept {
        bool strong=true;
        if(text){const char* p=std::strstr(text,"aim-assist=");
            while(p){const char* v=p+11;strong=!(std::strncmp(v,"game",4)==0 && (v[4]==0 || v[4]==' ' || v[4]=='\n' || v[4]=='\r' || v[4]=='\t'));p=std::strstr(v,"aim-assist=");}}
        // v1171: the preference only - no touch input yet, so a process (a Thor, a Fold with a pad) never starts in Strong
        wantStrong_.store(strong,std::memory_order_relaxed);aimDirty_.store(true,std::memory_order_release);
    }

    // mode -3: the last tick's bits if fresh, else 0; asks the ticks to keep reading for ReadTicks more. Wait-free (two relaxed atomics).
    int reticleRead(std::uint64_t now) noexcept {
        if(reticleBudget_.load(std::memory_order_relaxed)<reticle::ReadTicks)reticleBudget_.store(reticle::ReadTicks,std::memory_order_relaxed);
        const std::uint64_t v=reticle_.load(std::memory_order_relaxed),at=v>>8;
        return (at!=0 && now>=at && now-at<=reticle::StaleMs)?int(v&3u):0;
    }
    void reticleArm(int ticks) noexcept {reticleBudget_.store(ticks>0?ticks:0,std::memory_order_relaxed);}   // diag "reticle N"

    // ---- the script thread (the trainer's safe point; the glue calls it each tick)
    bool reticleWanted() const noexcept {return reticleBudget_.load(std::memory_order_relaxed)>0;}
    // gateOk: the trainer's gate and the phone (glue). Three guest loads; the natives run only while the reticle is shown. Returns true
    // when the published bits changed (the glue logs the transition).
    template<class Nat> bool reticleTick(Nat& nat,bool gateOk,std::uint64_t now) noexcept {
        if(!reticleWanted())return false;
        reticleBudget_.fetch_sub(1,std::memory_order_relaxed);
        lastVisible_=std::int32_t(nat.load(reticle::Visible));lastFlash_=std::int32_t(nat.load(reticle::Flash));
        lastStyle_=std::int32_t(nat.load(reticle::Style));
        bool aiming=false;
        if(gateOk && lastVisible_==1) {
            std::uint32_t o[3]{};
            const std::uint32_t pid=nat.call(n::PlayerId,nullptr,0,o)?o[0]:0xFFFFFFFFu;
            const std::uint32_t ped=nat.call(n::PlayerPedId,nullptr,0,o)?o[0]:0u;
            if(pid!=0xFFFFFFFFu && ped!=0 && ped!=0xFFFFFFFFu && !one(nat,n::PauseMenu) && !one(nat,n::CutsceneActive) && !one(nat,n::SwitchInProgress)) {
                const std::uint32_t ap[1]{pid},ae[1]{ped},av[2]{ped,1u};
                if(oneA(nat,n::PlayerPlaying,ap,1) && !oneA(nat,n::EntityDead,ae,1) && !oneA(nat,n::PedInAnyVehicle,av,2))
                    aiming=oneA(nat,n::FreeAiming,ap,1) || oneA(nat,n::TargettingAnything,ap,1);
            }
        }
        const std::uint32_t b=reticle::bits(lastVisible_,lastFlash_,lastStyle_,aiming,gateOk);
        const std::uint64_t old=reticle_.load(std::memory_order_relaxed);
        reticle_.store((now<<8)|b,std::memory_order_relaxed);
        return old==0 || std::uint32_t(old&3u)!=b;
    }
    bool wanted() const noexcept {return pending_.load(std::memory_order_relaxed) || aimDirty_.load(std::memory_order_relaxed) || needCheck_.load(std::memory_order_relaxed)
        || strongOn_ || (touchActive_.load(std::memory_order_relaxed) && wantStrong_.load(std::memory_order_relaxed));}   // v1171 (strongOn_: script thread, as wanted()'s caller)
    // gateOk: the trainer's gate (player playing, no pause / cutscene / switch / fade / death). Natives as trainer.h's.
    template<class Nat> void tick(Nat& nat,bool gateOk) noexcept {
        if(!wanted()){return;}
        needCheck_.store(false,std::memory_order_relaxed);
        const bool game=gateOk && one(nat,n::CamRendering) && !one(nat,n::PauseMenu);
        gameplay_.store(game,std::memory_order_relaxed);
        // v990 (trainer-mods, t-touch v989): after a refusal, keep looking for up to LookTicks ticks while the gameplay camera comes back
        // (the glue ends the idle camera each touch tick); one look was not enough: the camera returns a frame or more later, and a
        // single pitch turn 1 s after the refused primer was refused again.
        if(game)lookBudget_.store(0,std::memory_order_relaxed);
        else {
            // v992 (t-touch v989/v990: a turn accepted just as the idle camera started was dropped): a pending turn is kept, and looked at
            // again, for one budget; only a turn still waiting when the budget runs out is dropped
            if(pending_.load(std::memory_order_relaxed) && !held_ && lookBudget_.load(std::memory_order_relaxed)==0){held_=true;lookBudget_.store(LookTicks,std::memory_order_relaxed);}
            if(lookBudget_.load(std::memory_order_relaxed)>0){lookBudget_.fetch_sub(1,std::memory_order_relaxed);needCheck_.store(true,std::memory_order_relaxed);}
        }
        {const bool dirty=aimDirty_.exchange(false,std::memory_order_acquire);   // v1171: re-checked every tick while Strong is wanted or applied
         if(dirty || strongOn_ || (touchActive_.load(std::memory_order_relaxed) && wantStrong_.load(std::memory_order_relaxed)))applyAim(nat,game);}
        if(!game) {
            havePitch_=false;   // v1175: no camera to blend (menu, cutscene): the next swipe reads the camera again
            if(pending_.load(std::memory_order_relaxed) && lookBudget_.load(std::memory_order_relaxed)>0)return;   // v992: kept for the camera
            if(pending_.exchange(false,std::memory_order_acquire)){(void)take(yaw_);(void)take(pitch_);}   // the budget ran out: dropped
            held_=false;return;
        }
        held_=false;
        if(!pending_.exchange(false,std::memory_order_acquire))return;
        const float dy=take(yaw_),dp=take(pitch_);
        if(dy!=0.f){std::uint32_t o[3]{};float h=0.f;if(call(nat,n::GetRelHeading,nullptr,0,o))h=fval(o[0]);
            const std::uint32_t a[1]{fbits(h+dy)};call(nat,n::SetRelHeading,a,1,o);++applied_;}
        if(dp!=0.f){std::uint32_t o[3]{};
            const std::uint32_t frame=call(nat,n::FrameCount,nullptr,0,o)?o[0]:0u;   // v1175: the game's own frame count (0 = unknown: read the camera)
            float base=pitchTarget_;   // v1175: our request is still in flight (a swipe's next turn): add to it, the camera has not reached it yet
            if(!(havePitch_ && frame!=0u && frame-pitchFrame_<=std::uint32_t(PitchFlightTicks))) {   // a new swipe: from the camera's own pitch
                base=0.f;if(call(nat,n::GetRelPitch,nullptr,0,o))base=fval(o[0]);
                if(havePitch_){settledPitch_=base;settledTarget_=pitchTarget_;++settles_;   // the last swipe's result (logs, the Fold check)
                    // v1178 (Fold run-v1175-11102004: diff = -7.2 on both swipes -> up 0.56x, down 1.44x): the reference difference D is
                    // the follow camera's own (GET = rendered pitch - the entity's; SET's target = +896 + the attach-matrix pitch): learned
                    // from the settles: applied once two readings agree within MaxRefAgreeDeg (one reading alone may hold the player's own camera
                    // move between swipes), then halved toward each new reading; a reading more than MaxRefJumpDeg off is such a move: ignored.
                    const float d=base-pitchTarget_;
                    const auto near=[](float a,float b,float lim){return a-b<=lim && b-a<=lim;};
                    if(d>=-MaxRefDiffDeg && d<=MaxRefDiffDeg) {
                        if(haveRef_){if(near(d,refDiff_,MaxRefJumpDeg))refDiff_=0.5f*(refDiff_+d);}
                        else if(haveCand_ && near(d,refCand_,MaxRefAgreeDeg)){refDiff_=0.5f*(refCand_+d);haveRef_=true;}
                        else {refCand_=d;haveCand_=true;}
                    }}
                base-=refDiff_;   // v1178: in SET's own reference (v1179: the seed until two settles agree)
            }
            float target=base+dp;target=target>MaxPitchDeg?MaxPitchDeg:target<-MaxPitchDeg?-MaxPitchDeg:target;
            const std::uint32_t a[2]{fbits(target),fbits(PitchScale)};call(nat,n::SetRelPitch,a,2,o);++applied_;
            pitchTarget_=target;pitchFrame_=frame;havePitch_=true;}
    }
    // (No periodic refresh: a refused turn asks the next tick to look again, so an unused export costs one relaxed load per tick and no
    //  native. The first swipe after a menu closes may be refused once while the tick looks; the next one is taken.)

    // ---- read back (tests, logs)
    unsigned applied() const noexcept {return applied_;}
    unsigned pitchSettles() const noexcept {return settles_;}   // v1175 (script thread; logs): the previous swipe's result, read at the next swipe
    float settledPitch() const noexcept {return settledPitch_;}float settledTarget() const noexcept {return settledTarget_;}
    float pitchRefDiff() const noexcept {return refDiff_;}   // v1178 (logs)
    bool strongApplied() const noexcept {return strongOn_;}
    int savedGameMode() const noexcept {return saved_;}
    int reticleBits() const noexcept {return int(reticle_.load(std::memory_order_relaxed)&3u);}   // (no re-arm, no staleness: logs)
    std::int32_t reticleStyle() const noexcept {return lastStyle_;}     // the raw words of the last reticle tick (logs, device check)
    std::int32_t reticleFlash() const noexcept {return lastFlash_;}
    std::int32_t reticleVisible() const noexcept {return lastVisible_;}

private:
    template<class Nat> static bool call(Nat& nat,std::uint32_t h,const std::uint32_t* a,unsigned k,std::uint32_t* o){return nat.call(h,a,k,o);}
    template<class Nat> static bool one(Nat& nat,std::uint32_t h){std::uint32_t o[3]{};return nat.call(h,nullptr,0,o) && o[0]!=0;}
    template<class Nat> static bool oneA(Nat& nat,std::uint32_t h,const std::uint32_t* a,unsigned k){std::uint32_t o[3]{};return nat.call(h,a,k,o) && o[0]!=0;}
    template<class Nat> void applyAim(Nat& nat,bool game){
        // v1171: Strong only with touch active and in play (the gate + the gameplay camera + no pause): a pause, death, switch or cutscene
        // puts the player's own mode back until play resumes
        const bool want=wantStrong_.load(std::memory_order_relaxed) && touchActive_.load(std::memory_order_relaxed) && game;
        std::uint32_t o[3]{};
        if(want && !strongOn_){
            if(nat.call(n::GetAimState,nullptr,0,o))saved_=int(o[0]);   // the game's own mode, put back by mode 0
            const std::uint32_t a[1]{std::uint32_t(StrongTargetingMode)};
            if(nat.call(n::SetTargeting,a,1,o))strongOn_=true;
        } else if(!want && strongOn_){
            const std::uint32_t a[1]{std::uint32_t(saved_)};
            nat.call(n::SetTargeting,a,1,o);strongOn_=false;
        }
    }
    static float clamp(float v) noexcept {return v>MaxTurnDeg?MaxTurnDeg:v<-MaxTurnDeg?-MaxTurnDeg:v;}
    static void add(std::atomic<float>& a,float v) noexcept {float c=a.load(std::memory_order_relaxed);while(!a.compare_exchange_weak(c,c+v,std::memory_order_relaxed)){}}
    static float take(std::atomic<float>& a) noexcept {return a.exchange(0.f,std::memory_order_relaxed);}

    std::atomic<float> yaw_{0.f},pitch_{0.f};
    std::atomic<bool> pending_{false},aimDirty_{false},wantStrong_{false},gameplay_{false},needCheck_{false};
    std::atomic<bool> touchActive_{false};   // v1171: a touch input seen since the last controller input (never at start)
    std::atomic<int> lookBudget_{0};   // v990: looks left after a refused turn
    bool strongOn_=false;                       // script thread only
    bool held_=false;                           // v992 (script thread): a pending turn already got its budget
    int saved_=0;unsigned applied_=0;           // script thread only
    float pitchTarget_=0.f,settledPitch_=0.f,settledTarget_=0.f;unsigned settles_=0;std::uint32_t pitchFrame_=0;bool havePitch_=false;   // v1175: script thread only
    float refDiff_=SeedRefDiffDeg,refCand_=0.f;bool haveRef_=false,haveCand_=false;   // v1178: script thread only (v1179: seeded)
    std::atomic<std::uint64_t> reticle_{0};     // red reticle: (tick time ms << 8) | bits; 0 = never read
    std::atomic<int> reticleBudget_{0};         // ticks left to read (the app's -3 calls refill it)
    std::int32_t lastStyle_=-1,lastFlash_=-1,lastVisible_=-1;   // script thread only (logs)
};

}  // namespace touchx
