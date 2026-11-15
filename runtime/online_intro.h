#pragma once
// online_intro.h (Update 1, lane upd1-spoof/intro-cutscene, 2026-11-06; PROPOSED trainer action 20 OnlineIntro, Sandbox only).
//
// Plays the disc's own Online intro cutscenes with the player's freemode character in them, then hands the player back to free roam.
// It does what the disc's intro scripts do (fm_intro / fm_intro_cut_dev, decoded from the player's own script.rpf; lane NOTES.md
// section 2), with the same natives, names, playback lists and flags, but without a network session: no online check is overridden,
// nothing is spoofed, no network traffic exists. Story mode never reaches it (the trainer refuses the action outside Sandbox, and the
// glue hands the object over only in Sandbox with files/private-trace/diag-online-local present).
//
// The two cutscenes (both on the disc: anim/cutscene/cuts_mp.rpf has mp_intro_concat.cut + 17 sections and mp_int_mcs_5_alt1/2):
//   Concat (a0 = 0): REQUEST_CUTSCENE_WITH_PLAYBACK_LIST("MP_INTRO_CONCAT", 31 male / 103 female, 8)   fm_intro_cut_dev 0x4a92
//                    REGISTER(player ped, "MP_Male_Character" | "MP_Female_Character", 0, 0, 0)          state 29, 0x10ae
//                    REGISTER(0, the other one, 3, its model, 0)  (= not in this cutscene)
//   Drop-off (a0 = 1): REQUEST_CUTSCENE("MP_INT_MCS_5_ALT1" male | "MP_INT_MCS_5_ALT2" female, 8)       state 50, 0x29b0
//                    REGISTER(player ped, "MP_1" male | "MP_Female_Character" female, 0, 0, 0)            state 51
//   Gender = GET_ENTITY_MODEL(player ped) == mp_f_freemode_01 (fm_intro_cut_dev 0x57d1). Before START, while
//   CAN_REQUEST_ASSETS_FOR_CUTSCENE_ENTITY: SET_CUTSCENE_PED_COMPONENT_VARIATION_FROM_PED(entity name, player ped, 0) (0x4a92).
//   Lamar, his car and the plane are not registered: the cutscene makes its own (the disc registers its script-made ones).
//
// The thread rule (OBSERVED in our recompiled code, NOTES section 3): REQUEST_CUTSCENE* stores the CALLING script thread's id at
// cutscene manager +3360 and ignores a request from any other thread while one is held (sub_82793568); START_CUTSCENE starts only
// when the caller's id equals it (sub_82785130); REMOVE_CUTSCENE releases only for it (sub_82788CE0). The trainer runs inside
// whichever script's PLAYER_PED_ID hosts the tick, so this machine binds to the thread that made the request and makes those three
// calls only on frames hosted by that thread (the current thread is word 0x83C5CA24, id at +4: sub_8228D5A8 -> sub_825453F8).
// Every other call (REGISTER, IS_/HAS_ queries, fades, coordinates) has no thread test. If the bound thread ends, the engine frees
// the request itself (sub_822324C8: requester no longer found -> released).
//
// Pure header: no guest memory, no platform calls; the trainer's Natives does the calls and the two documented guest loads.
#include "trainer.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::intro {
using recomp::trainer::Natives;
using recomp::trainer::joaat;
using recomp::trainer::bits;
using recomp::trainer::asFloat;

namespace n {
// Hash -> handler (natives/registrations.json); argument use read from the handlers' asm (NOTES section 3) and the disc's calls.
constexpr std::uint32_t
    RequestCutscene=joaat("REQUEST_CUTSCENE"),                       // b5977853 sub_82D5AAF8: (name, flags)
    RequestCutsceneList=joaat("REQUEST_CUTSCENE_WITH_PLAYBACK_LIST"),// d98f656a sub_82D5AB48: (name, list -> mgr+3388, flags)
    CanRequestAssets=joaat("CAN_REQUEST_ASSETS_FOR_CUTSCENE_ENTITY"),// dd8878e9 sub_82D5AC88: () -> mgr+3415 ? mgr+3440 : 0
    SetVariationFromPed=joaat("SET_CUTSCENE_PED_COMPONENT_VARIATION_FROM_PED"), // 1e7da95e sub_82D6E5B8: (entity name, ped, model)
    HasCutsceneLoaded=joaat("HAS_CUTSCENE_LOADED"),                  // f9998582 sub_82D5ABA8: () -> byte mgr+3150
    RegisterEntity=joaat("REGISTER_ENTITY_FOR_CUTSCENE"),            // 7cbc3ec7 sub_82D6E598: (entity, name, type, model, flags); no thread test
    StartCutscene=joaat("START_CUTSCENE"),                           // 210106f6 sub_82D5AD58: (flags); thread-bound
    IsCutscenePlaying=joaat("IS_CUTSCENE_PLAYING"),                  // a3a78392 sub_82D5AF58: () -> byte mgr+3147
    HasCutsceneFinished=joaat("HAS_CUTSCENE_FINISHED"),              // 5ded14b4 sub_82D5AED8: () -> !byte mgr+3147
    IsCutsceneActive=joaat("IS_CUTSCENE_ACTIVE"),                    // cce2fe9d sub_82D5AF20: () -> byte mgr+3415
    CanSetExitState=joaat("CAN_SET_EXIT_STATE_FOR_REGISTERED_ENTITY"),// 8ff5d3c4 sub_82D5B1B0: (name, model) -> BOOL
    WasCutsceneSkipped=joaat("WAS_CUTSCENE_SKIPPED"),                // c9b6949d sub_82D5AEA0: () -> BOOL
    StopCutsceneImmediately=joaat("STOP_CUTSCENE_IMMEDIATELY"),      // f528a2ad sub_82D4CA98: () ; only while running; no thread test
    RemoveCutscene=joaat("REMOVE_CUTSCENE"),                         // 8052f533 sub_82D4CA60: () ; thread-bound; no-op when nothing held
    GetEntityModel=joaat("GET_ENTITY_MODEL"),GetEntityCoords=joaat("GET_ENTITY_COORDS"),GetEntityHeading=joaat("GET_ENTITY_HEADING"),
    IsPedInAnyVehicle=joaat("IS_PED_IN_ANY_VEHICLE"),GetMissionFlag=joaat("GET_MISSION_FLAG"),
    GetWantedLevel=joaat("GET_PLAYER_WANTED_LEVEL"),
    DoScreenFadeOut=joaat("DO_SCREEN_FADE_OUT"),DoScreenFadeIn=joaat("DO_SCREEN_FADE_IN"),IsScreenFadedOut=joaat("IS_SCREEN_FADED_OUT"),
    FreezeEntity=joaat("FREEZE_ENTITY_POSITION"),                    // (entity, BOOL)
    SetEntityCoords=joaat("SET_ENTITY_COORDS"),                      // 8 words as the disc calls it: (entity, x, y, z, 1, 0, 0, 1)
    SetEntityHeading=joaat("SET_ENTITY_HEADING"),
    RequestCollision=joaat("REQUEST_COLLISION_AT_COORD"),
    SetPlayerControl=joaat("SET_PLAYER_CONTROL"),                    // (player, BOOL, flags) as the disc calls it
    // v984 (T3 on v981: wanted 0 -> 3 at LSIA during the cut, then police killed him after back=start): fm_intro's own guard -
    // SET_MAX_WANTED_LEVEL(0) at 0x4b22 and SET_POLICE_IGNORE_PLAYER(player, 1) at 0x85a7 for the cut; afterwards (0x860f / 0x79c1)
    // police ignore off and max wanted 5 (here: the value read before), with maintransition's CLEAR_PLAYER_WANTED_LEVEL.
    SetMaxWanted=joaat("SET_MAX_WANTED_LEVEL"),GetMaxWanted=joaat("GET_MAX_WANTED_LEVEL"),ClearWanted=joaat("CLEAR_PLAYER_WANTED_LEVEL"),
    PoliceIgnore=joaat("SET_POLICE_IGNORE_PLAYER"),
    // v1006 (t-intro-fc-20261107-1157: female intro back=start with the player out of view): fm_intro's own camera exit, 0x7335..0x734c:
    // CAN_SET_EXIT_STATE_FOR_CAMERA(0) -> SET_GAMEPLAY_CAM_RELATIVE_HEADING(0) and SET_GAMEPLAY_CAM_RELATIVE_PITCH(0, 1.0). The visibility
    // read is diagnostic only (one line at the return).
    CanSetExitCamera=joaat("CAN_SET_EXIT_STATE_FOR_CAMERA"),       // edae6c02 sub_82D5B230: (BOOL) -> BOOL
    SetCamRelHeading=joaat("SET_GAMEPLAY_CAM_RELATIVE_HEADING"),   // 20c6217c sub_82D593A0: (float)
    SetCamRelPitch=joaat("SET_GAMEPLAY_CAM_RELATIVE_PITCH"),       // 6381b963 sub_82D59438: (float, float)
    IsEntityVisible=joaat("IS_ENTITY_VISIBLE"),                     // 120b4ed5 sub_82D77F50: (entity) -> BOOL
    // v1008 (t-intro-fc-20261107-1211: visible=1 camera_exit=1 and still no player in the after shot): a 10 s read-only watch after
    // the return, one line per second: ped position / heading, gameplay cam position / relative heading, which camera renders, on-screen.
    GetCamCoord=joaat("GET_GAMEPLAY_CAM_COORD"),                   // 9388cf79 sub_82D591D0: () -> vector
    GetCamRelHeading=joaat("GET_GAMEPLAY_CAM_RELATIVE_HEADING"),   // caf839c2 sub_82D59358: () -> float
    IsCamRendering=joaat("IS_GAMEPLAY_CAM_RENDERING"),             // 0ef276da sub_82D595B0: () -> BOOL
    GetRenderingCam=joaat("GET_RENDERING_CAM"),                     // 0fcf4df1 sub_82D586B8: () -> cam (-1 = gameplay)
    IsOnScreen=joaat("IS_ENTITY_ON_SCREEN"),                        // c1fec5ed sub_82D77E00: (entity) -> BOOL
    // v1014 (t-intro-fc-20261107-1244: the camera 4.3 m behind the ped, rel 0, visible=1, on screen, and still no ped drawn): a staged,
    // developer-only visibility experiment after the return (diag-intro-vis); GET_ENTITY_ALPHA is not registered in this build.
    SetEntityVisible=joaat("SET_ENTITY_VISIBLE"),                   // d043e8e1 sub_82D78610: (entity, BOOL) as fm_intro 0x4e81
    ResetEntityAlpha=joaat("RESET_ENTITY_ALPHA"),                   // 8a30761c sub_82D78690: (entity)
    SetLocalVisible=joaat("SET_LOCAL_PLAYER_VISIBLE_LOCALLY"),      // 324b56db sub_82DA91F8: (BOOL)
    HasModelLoaded=joaat("HAS_MODEL_LOADED"),                       // 62bfdb37 sub_82DB5E98: (model) -> BOOL
    IsVisibleToScript=joaat("IS_ENTITY_VISIBLE_TO_SCRIPT"),         // 5d240e9d sub_82D77F90: (entity) -> BOOL
    // v1028 (lead item 3: the female ped not drawn after the return although the GAMEPLAY camera renders 4 m behind her, visible=1,
    // on screen; not the idle camera): the watch adds what is drawn. Counts from freemode's own NATIVE instructions.
    PlayerPedNow=joaat("PLAYER_PED_ID"),                            // 0 -> 1
    DrawableVariation=joaat("GET_PED_DRAWABLE_VARIATION");          // 2 -> 1 (ped, component)
}
inline constexpr std::uint32_t UsedNatives[]{n::RequestCutscene,n::RequestCutsceneList,n::CanRequestAssets,n::SetVariationFromPed,
    n::HasCutsceneLoaded,n::RegisterEntity,n::StartCutscene,n::IsCutscenePlaying,n::HasCutsceneFinished,n::IsCutsceneActive,
    n::CanSetExitState,n::WasCutsceneSkipped,n::StopCutsceneImmediately,n::RemoveCutscene,n::GetEntityModel,n::GetEntityCoords,
    n::GetEntityHeading,n::IsPedInAnyVehicle,n::GetMissionFlag,n::GetWantedLevel,n::DoScreenFadeOut,n::DoScreenFadeIn,
    n::IsScreenFadedOut,n::FreezeEntity,n::SetEntityCoords,n::SetEntityHeading,n::RequestCollision,n::SetPlayerControl,
    n::SetMaxWanted,n::GetMaxWanted,n::ClearWanted,n::PoliceIgnore,   // v984: the wanted / police guard
    n::CanSetExitCamera,n::SetCamRelHeading,n::SetCamRelPitch,n::IsEntityVisible,   // v1006: fm_intro's camera exit + the visibility read
    n::GetCamCoord,n::GetCamRelHeading,n::IsCamRendering,n::GetRenderingCam,n::IsOnScreen,   // v1008: the after-return watch
    n::SetEntityVisible,n::ResetEntityAlpha,n::SetLocalVisible,n::HasModelLoaded,n::IsVisibleToScript,   // v1014: the visibility experiment
    n::DrawableVariation};   // v1028: the watch's draw fields (PLAYER_PED_ID is recorded through the trainer's own list; listing it here
                             // would count the trainer gate's calls as intro calls in the pause check)

// v1014: files/private-trace/diag-intro-vis (developer switch; the trainer glue reads it with the online switches): the staged visibility
// experiment after the intro returns. Off: the return is unchanged.
inline std::atomic<bool> visibilityExperiment{false};
constexpr std::uint32_t MaleModel=joaat("mp_m_freemode_01"),FemaleModel=joaat("mp_f_freemode_01");  // 1885233650 / 2627665880
// Guest words (read only, through Natives::load):
//  CurrentThreadWord: sub_825453F8 `lis r11,-31802; lwz r3,-13788(r11)` = the running script thread; its id at +4 (sub_8228D5A8 users).
//  ManagerWord: sub_823EB510 `lis r11,-31855; lwz r3,-18004(r11)` = the cutscene manager; +3360 requester thread id, +3415 held byte.
constexpr std::uint32_t CurrentThreadWord=0x83C5CA24u,ThreadIdOffset=4,ManagerWord=0x8390B9ACu,RequesterOffset=3360,HeldByteOffset=3415;
constexpr std::uint32_t CutsceneFlags=8,StartFlags=0,AnimateExisting=0,DontAnimate=3,FadeMs=500;

enum Cut : int {Concat=0,DropOff=1,CutCount=2};
enum Exit : int {ExitReturn=0,ExitStay=1,ExitCount=2};
struct CutInfo {
    const char* label;const char* name[2];int list[2];const char* entity[2];bool hideOther;   // [0] male, [1] female; list -1 = none
};
inline constexpr CutInfo Cuts[CutCount]{
    {"concat",{"MP_INTRO_CONCAT","MP_INTRO_CONCAT"},{31,103},{"MP_Male_Character","MP_Female_Character"},true},
    {"drop-off",{"MP_INT_MCS_5_ALT1","MP_INT_MCS_5_ALT2"},{-1,-1},{"MP_1","MP_Female_Character"},false}};
inline bool validArgs(int cut,int exit) noexcept {return cut>=0 && cut<CutCount && exit>=0 && exit<ExitCount;}

// Budgets in game frames (~30 fps). Nothing here runs when idle: the trainer calls step() only while a job runs.
constexpr std::uint32_t RequestTakeMax=60,LoadMax=900,BoundWaitMax=300,StartMax=150,PlayMax=9000,ReleaseMax=120,FadeMax=90,
    SettleFrames=60,OrphanMax=1800;

enum class Result : int {Running=0,Ok=1,Fail=2};
enum class State : std::uint8_t {Idle,Requesting,Starting,WaitPlaying,Playing,Releasing,FadingOut,Settling,Orphan};
inline const char* stateName(State s) noexcept {
    static const char* const names[]={"idle","requesting","starting","wait-playing","playing","releasing","fading-out","settling","orphan"};
    return names[int(s)];
}

class Player {
public:
    using LogFn=void(*)(const char*);
    explicit Player(LogFn log=nullptr) noexcept:log_(log) {}
    State state() const noexcept {return st_;}
    bool busy() const noexcept {return st_!=State::Idle && st_!=State::Orphan;}
    bool pendingCleanup() const noexcept {return st_==State::Orphan || watchLeft_;}   // v1008: + the after-return watch (10 s)
    std::uint32_t boundThread() const noexcept {return bound_;}

    // The current script thread's id (0 = none / unreadable).
    static std::uint32_t hostThread(Natives& nat) noexcept {
        const std::uint32_t t=nat.load(CurrentThreadWord);
        return t?nat.load(t+ThreadIdOffset):0u;
    }

    // Start. Running = the request is out; Fail = refused, nothing was changed (detail says why).
    Result begin(Natives& nat,int cut,int exit,std::uint32_t player,std::uint32_t ped,bool onlineCharacter,std::uint32_t frame,
                 char* detail,int cap) noexcept {
        if(busy()){say(detail,cap,"busy");return Result::Fail;}
        if(st_==State::Orphan){say(detail,cap,"previous-release-pending");return Result::Fail;}
        if(!validArgs(cut,exit)){say(detail,cap,"bad-args");return Result::Fail;}
        if(!onlineCharacter){say(detail,cap,"needs-online-character");return Result::Fail;}
        const std::uint32_t model=call1(nat,n::GetEntityModel,{ped});
        if(model!=MaleModel && model!=FemaleModel){say(detail,cap,"needs-online-character model=%08x",model);return Result::Fail;}
        if(call1(nat,n::IsPedInAnyVehicle,{ped,0})){say(detail,cap,"in-vehicle");return Result::Fail;}
        if(call1(nat,n::GetMissionFlag,{})){say(detail,cap,"on-a-mission");return Result::Fail;}
        if(call1(nat,n::GetWantedLevel,{player})){say(detail,cap,"wanted");return Result::Fail;}
        if(call1(nat,n::IsCutsceneActive,{})){say(detail,cap,"cutscene-busy");return Result::Fail;}
        const std::uint32_t host=hostThread(nat);
        if(!host){say(detail,cap,"no-script-thread");return Result::Fail;}
        std::uint32_t here[3]{};const std::uint32_t ca[2]{ped,1};
        if(!nat.call(n::GetEntityCoords,ca,2,here)){say(detail,cap,"natives-missing");return Result::Fail;}
        x_=asFloat(here[0]);y_=asFloat(here[1]);z_=asFloat(here[2]);
        if(!std::isfinite(x_) || !std::isfinite(y_) || !std::isfinite(z_)){say(detail,cap,"bad-coords");return Result::Fail;}
        heading_=call1(nat,n::GetEntityHeading,{ped});
        cut_=cut;exit_=exit;female_=model==FemaleModel;bound_=host;begin_=phase_=frame;lastBound_=frame;
        requested_=frozen_=faded_=exitSeen_=skipped_=camExit_=false;requestCalls_=0;watchLeft_=false;
        st_=State::Requesting;
        logf("RENDER_ONLINE intro begin cut=%s gender=%s exit=%s thread=%08x",Cuts[cut_].label,female_?"female":"male",
             exit_==ExitStay?"stay":"return",bound_);
        requestStep(nat,frame);   // the first request now (this frame's host is the bound thread by definition)
        guardOn(nat,player);   // v984: as fm_intro (max wanted 0, police ignore) for the whole cut
        return Result::Running;
    }

    // One frame of the running job (the trainer: gate open, or closed only by this job's own cutscene / fade).
    Result step(Natives& nat,std::uint32_t player,std::uint32_t ped,std::uint32_t frame,char* detail,int cap) noexcept {
        const bool onBound=hostThread(nat)==bound_;
        if(onBound)lastBound_=frame;
        switch(st_) {
        case State::Requesting: {
            if(onBound) {
                const int r=requestStep(nat,frame);
                if(r<0)return fail(nat,player,ped,frame,detail,cap,owner_?"cutscene-busy":"request-refused",false);
            } else if(frame-lastBound_>BoundWaitMax)return fail(nat,player,ped,frame,detail,cap,"host-thread-lost",requested_);
            if(requested_ && call1(nat,n::CanRequestAssets,{}))variationFromPed(nat,ped);
            if(requested_ && call1(nat,n::HasCutsceneLoaded,{})){st_=State::Starting;phase_=frame;}
            else if(frame-begin_>LoadMax)return fail(nat,player,ped,frame,detail,cap,"load-timeout",requested_);
            return Result::Running;}
        case State::Starting: {
            if(!onBound) {
                if(frame-lastBound_>BoundWaitMax)return fail(nat,player,ped,frame,detail,cap,"host-thread-lost",true);
                return Result::Running;
            }
            const CutInfo& c=Cuts[cut_];
            const std::uint32_t me=nat.text(c.entity[female_]);
            if(!me)return fail(nat,player,ped,frame,detail,cap,"no-scratch",true);
            call0(nat,n::RegisterEntity,{ped,me,AnimateExisting,0,0});
            if(c.hideOther) {   // the other gender's character is not in this cutscene (the disc's own type-3 registration)
                const std::uint32_t other=nat.text(c.entity[!female_]);
                if(other)call0(nat,n::RegisterEntity,{0,other,DontAnimate,female_?MaleModel:FemaleModel,0});
            }
            call0(nat,n::StartCutscene,{StartFlags});
            st_=State::WaitPlaying;phase_=frame;
            return Result::Running;}
        case State::WaitPlaying:
            if(call1(nat,n::IsCutscenePlaying,{})) {
                st_=State::Playing;phase_=frame;
                logf("RENDER_ONLINE intro playing cut=%s after=%u",Cuts[cut_].label,frame-begin_);
            } else if(frame-phase_>StartMax)return fail(nat,player,ped,frame,detail,cap,"start-refused",true);
            return Result::Running;
        case State::Playing: {
            if(!exitSeen_) {
                const std::uint32_t me=nat.text(Cuts[cut_].entity[female_]);
                if(me && call1(nat,n::CanSetExitState,{me,0})){exitSeen_=true;logf("RENDER_ONLINE intro exit-state at=%u",frame-phase_);}
            }
            if(!camExit_ && call1(nat,n::CanSetExitCamera,{0})) {   // v1006: fm_intro 0x7335 (once)
                camExit_=true;call0(nat,n::SetCamRelHeading,{bits(0.0f)});call0(nat,n::SetCamRelPitch,{bits(0.0f),bits(1.0f)});
                logf("RENDER_ONLINE intro camera-exit at=%u heading=0 pitch=0",frame-phase_);
            }
            if(call1(nat,n::HasCutsceneFinished,{})) {
                skipped_=call1(nat,n::WasCutsceneSkipped,{})!=0;played_=frame-phase_;
                st_=State::Releasing;phase_=frame;
                return Result::Running;
            }
            if(frame-phase_>PlayMax){call0(nat,n::StopCutsceneImmediately,{});return fail(nat,player,ped,frame,detail,cap,"play-timeout",true);}
            return Result::Running;}
        case State::Releasing:
            if(!call1(nat,n::IsCutsceneActive,{})) {
                if(exit_==ExitStay)return done(nat,player,ped,frame,detail,cap,"stay");
                call0(nat,n::DoScreenFadeOut,{FadeMs});faded_=true;
                st_=State::FadingOut;phase_=frame;
                return Result::Running;
            }
            if(onBound)call0(nat,n::RemoveCutscene,{});
            if(frame-phase_>ReleaseMax)return fail(nat,player,ped,frame,detail,cap,"release-timeout",true);
            return Result::Running;
        case State::FadingOut:
            if(call1(nat,n::IsScreenFadedOut,{}) || frame-phase_>FadeMax) {
                call0(nat,n::FreezeEntity,{ped,1});frozen_=true;
                call0(nat,n::SetEntityCoords,{ped,bits(x_),bits(y_),bits(z_),1,0,0,1});
                st_=State::Settling;phase_=frame;
            }
            return Result::Running;
        case State::Settling:
            call0(nat,n::RequestCollision,{bits(x_),bits(y_),bits(z_)});
            if(frame-phase_>=SettleFrames) {
                call0(nat,n::FreezeEntity,{ped,0});frozen_=false;
                call0(nat,n::SetEntityHeading,{ped,heading_});
                // v1011 (t-intro-fc-20261107-1224 watch: rel=-117.2, the gameplay cam 4.3 m front-left of the ped, the garden wall between
                // them): fm_intro resets the camera at the cut's camera exit and keeps the player where the cut left him; our return turns
                // the ped back to its start heading afterwards, so the same reset (relative heading 0, pitch 0) is repeated for that heading.
                call0(nat,n::SetCamRelHeading,{bits(0.0f)});call0(nat,n::SetCamRelPitch,{bits(0.0f),bits(1.0f)});
                logf("%s","RENDER_ONLINE intro camera-behind after return (relative heading 0, pitch 0)");
                return done(nat,player,ped,frame,detail,cap,"start");
            }
            return Result::Running;
        default:
            say(detail,cap,"not-running");return Result::Fail;
        }
    }

    // The trainer's gate closed for a reason this job does not own (death, a switch, a network game, trainer off, ...). Undoes what
    // this job holds; a held request it cannot release on this frame becomes Orphan (cleanup() releases it on a bound frame).
    const char* abort(Natives& nat,std::uint32_t player,std::uint32_t ped,std::uint32_t frame) noexcept {
        if(!busy())return "nothing";
        const State was=st_;
        const bool held=was!=State::Requesting || requested_;
        undo(nat,player,ped);
        logf("RENDER_ONLINE intro abort state=%s held=%d",stateName(was),held?1:0);
        if(held && was<State::FadingOut){toOrphan(frame);return "release-pending";}
        st_=State::Idle;return "released";
    }

    // Orphan: REMOVE_CUTSCENE on a frame hosted by the bound thread, until the manager no longer holds our request; given up after
    // OrphanMax (the engine then frees it when the bound thread ends, sub_822324C8). Called by the trainer every tick, gate or not.
    void cleanup(Natives& nat,std::uint32_t frame) noexcept {
        if(st_!=State::Orphan){watch(nat,frame);return;}   // v1008
        if(!heldByUs(nat)){st_=State::Idle;logf("RENDER_ONLINE intro released after=%u",frame-phase_);return;}
        if(hostThread(nat)==bound_)call0(nat,n::RemoveCutscene,{});
        if(!heldByUs(nat)){st_=State::Idle;logf("RENDER_ONLINE intro released after=%u",frame-phase_);return;}
        if(frame-phase_>OrphanMax){st_=State::Idle;logf("RENDER_ONLINE intro release-given-up thread=%08x",bound_);}
    }

private:
    template<class... A> void logf(const char* f,A... a) const noexcept {if(!log_)return;char l[320];   // v1008: 320 (the watch line)
                                                                  std::snprintf(l,sizeof l,f,a...);log_(l);}
    template<class... A> static void say(char* d,int cap,const char* f,A... a) noexcept {if(d && cap>0)std::snprintf(d,std::size_t(cap),f,a...);}
    static void say(char* d,int cap,const char* s) noexcept {if(d && cap>0)std::snprintf(d,std::size_t(cap),"%s",s);}
    static std::uint32_t call1(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};return nat.call(h,a.begin(),unsigned(a.size()),out)?out[0]:0u;
    }
    static void call0(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};(void)nat.call(h,a.begin(),unsigned(a.size()),out);
    }
    static int byteAt(Natives& nat,std::uint32_t a) noexcept {return int((nat.load(a&~3u)>>(24-8*(a&3u)))&0xFFu);}
    // The manager holds a request made by the bound thread.
    bool heldByUs(Natives& nat) noexcept {
        const std::uint32_t m=nat.load(ManagerWord);
        if(!m)return false;
        return byteAt(nat,m+HeldByteOffset)!=0 && nat.load(m+RequesterOffset)==bound_;
    }
    // One request call (bound frames only): 1 held by us, 0 not yet, -1 refused (held by another thread, or never taken).
    int requestStep(Natives& nat,std::uint32_t frame) noexcept {
        const CutInfo& c=Cuts[cut_];
        const std::uint32_t name=nat.text(c.name[female_]);
        if(!name)return -1;
        if(c.list[female_]>=0)call0(nat,n::RequestCutsceneList,{name,std::uint32_t(c.list[female_]),CutsceneFlags});
        else call0(nat,n::RequestCutscene,{name,CutsceneFlags});
        ++requestCalls_;
        const std::uint32_t m=nat.load(ManagerWord);
        const bool held=m && byteAt(nat,m+HeldByteOffset)!=0;
        const std::uint32_t who=m?nat.load(m+RequesterOffset):0u;
        if(held && who==bound_){if(!requested_)logf("RENDER_ONLINE intro requested name=%s list=%d",c.name[female_],c.list[female_]);
            requested_=true;owner_=0;return 1;}
        if(held && who!=bound_){owner_=who;return -1;}   // another script's cutscene: never touched
        if(frame-begin_>RequestTakeMax){owner_=0;return -1;}
        return 0;
    }
    void variationFromPed(Natives& nat,std::uint32_t ped) noexcept {
        const std::uint32_t me=nat.text(Cuts[cut_].entity[female_]);
        if(me)call0(nat,n::SetVariationFromPed,{me,ped,0});
    }
    // v984: fm_intro's wanted / police guard around the cut; guardOff on every exit (done, fail, abort) - idempotent.
    void guardOn(Natives& nat,std::uint32_t player) noexcept {
        if(guarded_)return;
        prevMaxWanted_=call1(nat,n::GetMaxWanted,{});
        call0(nat,n::ClearWanted,{player});call0(nat,n::SetMaxWanted,{0});call0(nat,n::PoliceIgnore,{player,1});guarded_=true;
        logf("RENDER_ONLINE intro guard on max_wanted_before=%u",prevMaxWanted_);
    }
    void guardOff(Natives& nat,std::uint32_t player) noexcept {
        if(!guarded_)return;
        const std::uint32_t restore=prevMaxWanted_>0 && prevMaxWanted_<=6?prevMaxWanted_:5u;   // fm_intro restores 5
        call0(nat,n::ClearWanted,{player});call0(nat,n::PoliceIgnore,{player,0});call0(nat,n::SetMaxWanted,{restore});guarded_=false;
        logf("RENDER_ONLINE intro guard off max_wanted=%u",restore);
    }
    void undo(Natives& nat,std::uint32_t player,std::uint32_t ped) noexcept {
        guardOff(nat,player);   // v984
        if(call1(nat,n::IsCutscenePlaying,{}))call0(nat,n::StopCutsceneImmediately,{});
        if(frozen_){call0(nat,n::FreezeEntity,{ped,0});frozen_=false;}
        if(faded_ || call1(nat,n::IsScreenFadedOut,{})){call0(nat,n::DoScreenFadeIn,{FadeMs});faded_=false;}
        call0(nat,n::SetPlayerControl,{player,1,0});
    }
    void toOrphan(std::uint32_t frame) noexcept {st_=State::Orphan;phase_=frame;}
    Result fail(Natives& nat,std::uint32_t player,std::uint32_t ped,std::uint32_t frame,char* detail,int cap,const char* why,bool held) noexcept {
        const State was=st_;
        undo(nat,player,ped);
        if(held){toOrphan(frame);if(hostThread(nat)==bound_)cleanup(nat,frame);}
        else st_=State::Idle;
        say(detail,cap,"cut=%s %s state=%s%s",Cuts[cut_].label,why,stateName(was),st_==State::Orphan?" release=pending":"");
        logf("RENDER_ONLINE intro fail cut=%s why=%s state=%s release=%s",Cuts[cut_].label,why,stateName(was),
             st_==State::Orphan?"pending":"none");
        return Result::Fail;
    }
    Result done(Natives& nat,std::uint32_t player,std::uint32_t ped,std::uint32_t frame,char* detail,int cap,const char* back) noexcept {
        guardOff(nat,player);   // v984: wanted cleared, police back, max wanted restored before control returns
        call0(nat,n::SetPlayerControl,{player,1,0});
        if(faded_){call0(nat,n::DoScreenFadeIn,{FadeMs});faded_=false;}
        st_=State::Idle;
        say(detail,cap,"cut=%s gender=%s played=%u skipped=%d exit_state=%d back=%s",Cuts[cut_].label,female_?"female":"male",played_,
            skipped_?1:0,exitSeen_?1:0,back);
        logf("RENDER_ONLINE intro done cut=%s played=%u skipped=%d back=%s total=%u",Cuts[cut_].label,played_,skipped_?1:0,back,frame-begin_);
        logf("RENDER_ONLINE intro after visible=%u camera_exit=%d",call1(nat,n::IsEntityVisible,{ped}),camExit_?1:0);   // v1006: read only
        watchLeft_=true;watchPed_=ped;watchStart_=frame;watchLast_=frame-WatchEvery;   // v1008: the after-return watch (read only)
        visDone_=0;   // v1014
        return Result::Ok;
    }

    LogFn log_;
    State st_=State::Idle;
    int cut_=0,exit_=0;bool female_=false,requested_=false,frozen_=false,faded_=false,exitSeen_=false,skipped_=false,camExit_=false;
    bool watchLeft_=false;std::uint32_t watchPed_=0,watchStart_=0,watchLast_=0;   // v1008
    static constexpr std::uint32_t WatchFrames=300,WatchEvery=30;
    void watch(Natives& nat,std::uint32_t frame) noexcept {
        if(!watchLeft_)return;
        if(frame-watchStart_>WatchFrames){watchLeft_=false;return;}
        if(visibilityExperiment.load(std::memory_order_relaxed))visStep(nat,frame-watchStart_);   // v1014 (developer switch)
        if(frame-watchLast_<WatchEvery)return;
        watchLast_=frame;
        std::uint32_t pc[3]{},cc[3]{};call3(nat,n::GetEntityCoords,{watchPed_,1},pc);call3(nat,n::GetCamCoord,{},cc);
        logf("RENDER_ONLINE intro watch t=%u ped=%.2f,%.2f,%.2f h=%.1f cam=%.2f,%.2f,%.2f rel=%.1f gameplay_cam=%u rendering_cam=%d onscreen=%u visible=%u",
             frame-watchStart_,double(asF(pc[0])),double(asF(pc[1])),double(asF(pc[2])),double(asF(call1(nat,n::GetEntityHeading,{watchPed_}))),
             double(asF(cc[0])),double(asF(cc[1])),double(asF(cc[2])),double(asF(call1(nat,n::GetCamRelHeading,{}))),call1(nat,n::IsCamRendering,{}),
             int(call1(nat,n::GetRenderingCam,{})),call1(nat,n::IsOnScreen,{watchPed_}),call1(nat,n::IsEntityVisible,{watchPed_}));
        // v1028: the model, whether the watched ped is still the player's ped, and the drawables of head / hair / torso / legs / top
        const std::uint32_t now=call1(nat,n::PlayerPedNow,{});
        logf("RENDER_ONLINE intro draw t=%u model=%08x player_ped=%u watched=%u d0=%d d2=%d d3=%d d4=%d d11=%d",frame-watchStart_,
             call1(nat,n::GetEntityModel,{watchPed_}),now,watchPed_,int(call1(nat,n::DrawableVariation,{watchPed_,0})),
             int(call1(nat,n::DrawableVariation,{watchPed_,2})),int(call1(nat,n::DrawableVariation,{watchPed_,3})),
             int(call1(nat,n::DrawableVariation,{watchPed_,4})),int(call1(nat,n::DrawableVariation,{watchPed_,11})));
    }
    // v1014: one step per stage, each logged once; the device script's screenshots after each stage show which one brings the ped back.
    unsigned visDone_=0;
    void visStep(Natives& nat,std::uint32_t t) noexcept {
        const std::uint32_t ped=watchPed_;
        if(t>=30 && !(visDone_&1)){visDone_|=1;const std::uint32_t model=call1(nat,n::GetEntityModel,{ped});
            logf("RENDER_ONLINE intro vis t=%u step=0 read model=%08x model_loaded=%u visible=%u visible_to_script=%u",t,model,
                 call1(nat,n::HasModelLoaded,{model}),call1(nat,n::IsEntityVisible,{ped}),call1(nat,n::IsVisibleToScript,{ped}));}
        if(t>=90 && !(visDone_&2)){visDone_|=2;call0(nat,n::SetEntityVisible,{ped,0});call0(nat,n::SetEntityVisible,{ped,1});
            logf("RENDER_ONLINE intro vis t=%u step=1 SET_ENTITY_VISIBLE(ped,0) then (ped,1)",t);}
        if(t>=150 && !(visDone_&4)){visDone_|=4;call0(nat,n::ResetEntityAlpha,{ped});logf("RENDER_ONLINE intro vis t=%u step=2 RESET_ENTITY_ALPHA(ped)",t);}
        if(t>=210 && t<270){call0(nat,n::SetLocalVisible,{1});
            if(!(visDone_&8)){visDone_|=8;logf("RENDER_ONLINE intro vis t=%u step=3 SET_LOCAL_PLAYER_VISIBLE_LOCALLY(1) every frame to t=270",t);}}
    }
    static float asF(std::uint32_t b) noexcept {float f;std::memcpy(&f,&b,4);return f;}
    static void call3(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a,std::uint32_t* out) noexcept {nat.call(h,a.begin(),unsigned(a.size()),out);}
    bool guarded_=false;std::uint32_t prevMaxWanted_=5;   // v984: the wanted / police guard
    std::uint32_t bound_=0,owner_=0,begin_=0,phase_=0,lastBound_=0,played_=0,requestCalls_=0,heading_=0;
    float x_=0,y_=0,z_=0;
};

// The trainer's view (trainer.h IntroActions, patch 0001).
class Actions final : public recomp::trainer::IntroActions {
public:
    explicit Actions(Player::LogFn log=nullptr) noexcept:p_(log) {}
    bool validArgument(int a0,int a1) const noexcept override {return validArgs(a0,a1);}
    int begin(Natives& nat,int a0,int a1,std::uint32_t player,std::uint32_t ped,bool onlineCharacter,std::uint32_t frame,char* d,int cap) noexcept override {
        return int(p_.begin(nat,a0,a1,player,ped,onlineCharacter,frame,d,cap));
    }
    int step(Natives& nat,std::uint32_t player,std::uint32_t ped,std::uint32_t frame,char* d,int cap) noexcept override {
        return int(p_.step(nat,player,ped,frame,d,cap));
    }
    const char* abort(Natives& nat,std::uint32_t player,std::uint32_t ped,std::uint32_t frame) noexcept override {return p_.abort(nat,player,ped,frame);}
    bool pendingCleanup() const noexcept override {return p_.pendingCleanup();}
    void cleanup(Natives& nat,std::uint32_t frame) noexcept override {p_.cleanup(nat,frame);}
    const Player& player() const noexcept {return p_;}
private:
    Player p_;
};
} // namespace recomp::intro
