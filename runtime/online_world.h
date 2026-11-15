#pragma once
// online_world.h (Update 1 P8, Online World Level 1; design docs/fable/ONLINE-WORLD-PLAN.md §3, lane notes
// local-analysis/.agents/upd1/online-world-l1/NOTES.md). Pure: no guest memory, no platform calls; the trainer (trainer.h) and its
// glue drive it through trainer::Natives. Sandbox mode only: the glue allows it only while the play-mode latch says Sandbox.
//
// What the player's own disc says (E4, a names-only bytecode read of the disc's 667 scripts; evidence in NOTES.md):
//  - The launch disc has no multiplayer IPL group. Of the 74 multiplayer-family scripts only three IPL names are literal arguments:
//    TrevorsMP (one job step of fm_mission_controler, with TrevorsTrailer / TrevorsTrailerTrash removed) and GolfFlags (golf_mp, the
//    same as story golf). freemode's single REQUEST_IPL / REMOVE_IPL sit on a branch its own constant helper never takes.
//  - The one world-wide multiplayer call is SET_INSTANCE_PRIORITY_MODE(1) (freemode_init); story's startup and main_persistent call
//    SET_INSTANCE_PRIORITY_MODE(0). World below = exactly that pair. It is NOT map state: the mode word's only reader (sub_82243A58)
//    turns mode 1 into a streaming / priority setting (two globals set to 2). No multiplayer map content loads; developer-only switch.
//  - The multiplayer apartments and garages are interiors of the ordinary map. The disc's interior table (am_mp_property_int, the
//    same table compiled into freemode and fm_intro) gives each one's lookup point and archetype name; the scripts reach one with
//    GET_INTERIOR_AT_COORDS_WITH_TYPE, PIN_INTERIOR_IN_MEMORY and IS_INTERIOR_READY. Places below = those entries.
//  - freemode sets its character with: HAS_MODEL_LOADED -> SET_PLAYER_MODEL(PLAYER_ID, model) -> SET_MODEL_AS_NO_LONGER_NEEDED; the
//    menu ped (mp_menuped) blends heads with SET_PED_HEAD_BLEND_DATA(ped, h, h+1, 0, 0, 0, 0, mix, 0.0, 0.0, TRUE). Character below.
//
// Argument slots, read from our recompiled handlers (validated-ppc corpus; NOTES.md lists the instructions):
//  SET_PLAYER_MODEL sub_82DB0BD8: 2 (player, model). SET_PED_DEFAULT_COMPONENT_VARIATION sub_82DD3B10: 1 (ped).
//  SET_PED_HEAD_BLEND_DATA sub_82DD3BC0: 11 (ped, 6 ints at +4..+24, floats at +28/+32/+36, BOOL at +40 tested != 0).
//  HAS_PED_HEAD_BLEND_FINISHED sub_82DD3C50: 1 (ped) -> BOOL. GET_ENTITY_MODEL sub_82D8A300: 1 (entity) -> hash.
//  SET_INSTANCE_PRIORITY_MODE sub_82D67C40: 1 (int; its worker sub_829C04F0 stores it at u32 0x839E5314). GET_INTERIOR_AT_COORDS_WITH_TYPE
//  sub_82D647D8: 4 (x, y, z floats, char* type) -> id. PIN_INTERIOR_IN_MEMORY sub_82D646B8: 1 (id; ignored when <= 0). UNPIN_INTERIOR
//  sub_82D64710 / IS_INTERIOR_READY sub_82D64768: 1 (id). SET_ENTITY_HEADING sub_82D783F8: 2 (entity, float at +4).
//  IS_INTERIOR_DISABLED sub_82D64958 / IS_INTERIOR_CAPPED sub_82D649E0: 1 (id) -> the proxy's u16 +22 bit 0x2 / bit 0x1.
//  DISABLE_INTERIOR sub_82D64938: 2 (id, BOOL at +4 tested != 0) -> sub_82D64338 -> sub_829E3338 (writes bit 0x2 of +22).
//
// Why the places need DISABLE_INTERIOR(id, FALSE) (wave2 lane local-analysis/.agents/upd1-wave2/mp-interiors/NOTES.md): story's
// startup (0xe17) and building_controller (0x1a9f5) mark the multiplayer interiors 12-14, 25-44, 46 (and 47) "disable when far";
// building_controller's every-frame 0x1534c then calls DISABLE_INTERIOR(id, TRUE) for each marked one once the player is more than
// 250 m from it. A disabled interior never becomes ready (E4b on v955: 65794 / 63490 / 31234 not-ready, all > 1500 m from the
// start; 83970, 205 m from the start, ready). The disc's own multiplayer scripts enable before pinning: am_mp_property_int 0x7c / 0x247de
// (every frame: IS_INTERIOR_DISABLED -> DISABLE_INTERIOR(id, FALSE) -> PIN_INTERIOR_IN_MEMORY), freemode 0x2719d (garage 13),
// fm_intro 0x4a60 (garage 14). PlaceTrip does the same, at the start and on every waiting tick.
#include "trainer.h"
#include "character_creator.h"   // Update 1 OW character creator (lane upd1-online/character-creator): actions 17 / 18, PROPOSED
#include "place_fade.h"         // v976: online's fade + loading prompt around every single place trip (16 n, door enter / exit)
#include "property_doors.h"     // lane property-doors (PROPOSED): action 16's trip back out of a place (exitTarget)
#include "creator_menu.h"        // PROPOSED (lane upd1-creator-menu/creator-controller): 17 4 = the in-game creator menu (one trainer job)
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::online {
using trainer::Natives;
using trainer::joaat;
using trainer::bits;

struct Named {const char* name;std::uint32_t hash;};
namespace n {
constexpr std::uint32_t SetPlayerModel=joaat("SET_PLAYER_MODEL"),DefaultVariation=joaat("SET_PED_DEFAULT_COMPONENT_VARIATION"),
    HeadBlend=joaat("SET_PED_HEAD_BLEND_DATA"),HeadBlendFinished=joaat("HAS_PED_HEAD_BLEND_FINISHED"),
    GetEntityModel=joaat("GET_ENTITY_MODEL"),InstancePriorityMode=joaat("SET_INSTANCE_PRIORITY_MODE"),
    InteriorAtCoordsWithType=joaat("GET_INTERIOR_AT_COORDS_WITH_TYPE"),PinInterior=joaat("PIN_INTERIOR_IN_MEMORY"),
    UnpinInterior=joaat("UNPIN_INTERIOR"),IsInteriorReady=joaat("IS_INTERIOR_READY"),SetEntityHeading=joaat("SET_ENTITY_HEADING"),
    IsInteriorDisabled=joaat("IS_INTERIOR_DISABLED"),DisableInterior=joaat("DISABLE_INTERIOR"),IsInteriorCapped=joaat("IS_INTERIOR_CAPPED"),
    TaskLeaveVehicle=joaat("TASK_LEAVE_VEHICLE"),GetEntitySpeed=joaat("GET_ENTITY_SPEED"),IsEntityInAir=joaat("IS_ENTITY_IN_AIR"),
    IsEntityInWater=joaat("IS_ENTITY_IN_WATER");   // v969: action 16 auto-exit
}
// The natives this file adds (names kept for the host checks: registered on this disc, and no network / stats native).
inline constexpr Named Used[]{{"SET_PLAYER_MODEL",n::SetPlayerModel},{"SET_PED_DEFAULT_COMPONENT_VARIATION",n::DefaultVariation},
    {"SET_PED_HEAD_BLEND_DATA",n::HeadBlend},{"HAS_PED_HEAD_BLEND_FINISHED",n::HeadBlendFinished},{"GET_ENTITY_MODEL",n::GetEntityModel},
    {"SET_INSTANCE_PRIORITY_MODE",n::InstancePriorityMode},{"GET_INTERIOR_AT_COORDS_WITH_TYPE",n::InteriorAtCoordsWithType},
    {"PIN_INTERIOR_IN_MEMORY",n::PinInterior},{"UNPIN_INTERIOR",n::UnpinInterior},{"IS_INTERIOR_READY",n::IsInteriorReady},
    {"SET_ENTITY_HEADING",n::SetEntityHeading},{"IS_INTERIOR_DISABLED",n::IsInteriorDisabled},{"DISABLE_INTERIOR",n::DisableInterior},
    {"IS_INTERIOR_CAPPED",n::IsInteriorCapped},{"TASK_LEAVE_VEHICLE",n::TaskLeaveVehicle},{"GET_ENTITY_SPEED",n::GetEntitySpeed},
    {"IS_ENTITY_IN_AIR",n::IsEntityInAir},{"IS_ENTITY_IN_WATER",n::IsEntityInWater},
    // v976: the place transition (place_fade.h)
    {"DO_SCREEN_FADE_OUT",placefade::n::FadeOut},{"DO_SCREEN_FADE_IN",placefade::n::FadeIn},{"IS_SCREEN_FADED_OUT",placefade::n::FadedOut},
    {"SET_PLAYER_CONTROL",placefade::n::PlayerControl},{"BEGIN_TEXT_COMMAND_BUSYSPINNER_ON",placefade::n::SpinnerBegin},
    {"END_TEXT_COMMAND_BUSYSPINNER_ON",placefade::n::SpinnerEnd},{"BUSYSPINNER_OFF",placefade::n::SpinnerOff},
    {"DOES_TEXT_LABEL_EXIST",placefade::n::LabelExists},{"ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME",placefade::n::AddLiteral}};
inline constexpr std::uint32_t UsedNatives[]{n::SetPlayerModel,n::DefaultVariation,n::HeadBlend,n::HeadBlendFinished,n::GetEntityModel,
    n::InstancePriorityMode,n::InteriorAtCoordsWithType,n::PinInterior,n::UnpinInterior,n::IsInteriorReady,n::SetEntityHeading,
    n::IsInteriorDisabled,n::DisableInterior,n::IsInteriorCapped,n::TaskLeaveVehicle,n::GetEntitySpeed,n::IsEntityInAir,n::IsEntityInWater,
    placefade::n::FadeOut,placefade::n::FadeIn,placefade::n::FadedOut,placefade::n::PlayerControl,placefade::n::SpinnerBegin,
    placefade::n::SpinnerEnd,placefade::n::SpinnerOff,placefade::n::LabelExists,placefade::n::AddLiteral};   // v976
static_assert(std::size(Used)==std::size(UsedNatives));

constexpr std::uint32_t MaleModel=joaat("mp_m_freemode_01"),FemaleModel=joaat("mp_f_freemode_01"); // E2: cd=1 valid=1 on the Thor
constexpr bool isFreemode(std::uint32_t m) noexcept {return m==MaleModel || m==FemaleModel;}
// The instance priority mode as the game holds it: sub_829C04F0 (SET_INSTANCE_PRIORITY_MODE's worker) does stw r3 here (lis -31842,
// +21268); sub_82243A58 is its only reader. A guest load, not a native call.
constexpr std::uint32_t ModeWord=0x839E5314u;
constexpr std::uint32_t ModelWaitFramesMax=300,BlendWaitFramesMax=90,InteriorWaitFramesMax=300,SettleFrames=3;
// The developer sweep (16 PlaceSweep, device check only): every place in order, each with a shorter wait, then a short stay.
constexpr std::uint32_t SweepWaitFramesMax=150,SweepStayFrames=30;
constexpr int PlaceSweep=99;
using LogFn=void(*)(const char*);   // optional per-place line (the glue passes trainerLog); null = no line
// The default parents (the menu ped's pattern: second = first + 1, mix 0.5, skins 0, TRUE). The female pair is an inference (NOTES.md,
// NEEDS DEVICE CHECK in E3); the male pair is the first ids.
struct Blend {std::uint32_t first,second;float mix;};
inline constexpr Blend MaleBlend{0,1,0.5f},FemaleBlend{21,22,0.5f};
enum Which : int {Male=0,Female=1,Story=2};

// One step's result, as the trainer reports it.
struct Outcome {enum Kind:std::uint8_t {Running,Ok,Fail} kind=Running;char detail[64]{};};
inline Outcome running() noexcept {return Outcome{};}
template<class... A> inline Outcome finished(Outcome::Kind k,const char* f,A... a) noexcept {
    Outcome o;o.kind=k;
    if constexpr(sizeof...(a)==0)std::snprintf(o.detail,sizeof o.detail,"%s",f);else std::snprintf(o.detail,sizeof o.detail,f,a...);
    return o;
}

// ---------------------------------------------------------------------------------------------------- the freemode character
// Male / Female: the story model is remembered (GET_ENTITY_MODEL of the ped before the first change), the freemode model requested,
// and once loaded, in ONE tick: SET_PLAYER_MODEL, the NEW ped (PLAYER_PED_ID again) is checked to carry the model, gets the default
// components and the head blend, the model is released. Then the blend is awaited (bounded). Story: the remembered model back the same
// way, without default components or blend (the game's own model defaults apply; E3 checks the story outfit).
// Male / Female are refused until the Sandbox core reports its autosave hold (setAutosaveHold): no Sandbox-root save may store a
// freemode model. reconcile(): the game itself may replace the model (a switch, a respawn, a script); then current_ is cleared.
// IS_PED_IN_ANY_VEHICLE(ped, FALSE) (trainer.h's hash; 2 words). An unavailable native reads "not in one", as PlaceTrip always did.
inline bool pedInAnyVehicle(Natives& nat,std::uint32_t ped) noexcept {
    std::uint32_t out[3]{};const std::uint32_t a[2]{ped,0};return nat.call(trainer::n::IsPedInAnyVehicle,a,2,out) && out[0];
}
// v957 (WAVE2-REPORT 2.1 item 4): why the player model may not change now (nullptr: it may), asked at begin and again right before
// SET_PLAYER_MODEL. GET_MISSION_FLAG and IS_AUTO_SAVE_IN_PROGRESS: no argument -> BOOL, the trainer's own natives (OpenSaveMenu /
// switchRefusal use them). Like OpenSaveMenu and PlaceTrip, a native that is not registered reads "no" (on this disc all three are
// registered: trainer.h UsedNatives); a model change during an autosave or on a mission is what is refused.
inline const char* modelChangeRefusal(Natives& nat,std::uint32_t ped) noexcept {
    std::uint32_t out[3]{};
    if(nat.call(trainer::n::GetMissionFlag,nullptr,0,out) && out[0])return "on-a-mission";
    if(nat.call(trainer::n::IsAutoSaveInProgress,nullptr,0,out) && out[0])return "autosave";
    if(pedInAnyVehicle(nat,ped))return "in-vehicle";
    return nullptr;
}
class Character {
public:
    bool active() const noexcept {return current_.load(std::memory_order_relaxed)!=0;}   // a freemode model is the player now
    std::uint32_t current() const noexcept {return current_.load(std::memory_order_relaxed);}
    std::uint32_t storyModel() const noexcept {return story_;}
    bool busy() const noexcept {return step_!=Step::Idle;}
    void setAutosaveHold(bool on) noexcept {hold_.store(on,std::memory_order_relaxed);}
    bool autosaveHold() const noexcept {return hold_.load(std::memory_order_relaxed);}

    Outcome begin(Natives& nat,std::uint32_t ped,int which,std::uint32_t frame) noexcept {
        if(which==Story) {
            if(!active())return finished(Outcome::Fail,"already-story");
            if(!story_)return finished(Outcome::Fail,"no-story-model");
            target_=story_;
        } else {
            target_=which==Female?FemaleModel:MaleModel;
            if(current()==target_)return finished(Outcome::Ok,"model=%08x already",target_);
            if(!autosaveHold())return finished(Outcome::Fail,"no-autosave-hold");
        }
        // v957 (OW E3 on the Thor, v955, 2026-11-06 18:08, update-1/device-runs/ow-e3-20261106-1806): 15 1 while SEATED in a car made
        // the new ped on foot at the seat, inside the car body (telemetry veh=1 -> 0, pos unchanged; it never moved again; the next
        // autopilot "leave" answered already-on-foot). SET_PLAYER_MODEL does not carry the seat: on foot only, as PlaceTrip (16).
        if(const char* why=modelChangeRefusal(nat,ped))return finished(Outcome::Fail,why);   // (+ v957 item 4: mission, autosave)
        if(!call1(nat,trainer::n::IsModelInCdimage,{target_}) || !call1(nat,trainer::n::IsModelValid,{target_}))
            return finished(Outcome::Fail,"model=%08x not-in-cdimage",target_);
        if(which!=Story && !active()) {   // the story character to come back to
            const std::uint32_t m=call1(nat,n::GetEntityModel,{ped});
            if(!m)return finished(Outcome::Fail,"no-current-model");
            story_=m;
        }
        which_=which;call0(nat,trainer::n::RequestModel,{target_});
        step_=Step::Loading;start_=frame;
        return running();
    }
    Outcome step(Natives& nat,std::uint32_t player,std::uint32_t frame) noexcept {
        if(step_==Step::Loading) {
            if(!call1(nat,trainer::n::HasModelLoaded,{target_})) {
                if(frame-start_>=ModelWaitFramesMax)return stop(nat,"model=%08x model-timeout");
                call0(nat,trainer::n::RequestModel,{target_});
                return running();
            }
            // v957: the world may have changed while the model loaded (a vehicle, an autosave, a mission): nothing changed yet, the
            // request is released (as PlaceTrip re-checks)
            if(const char* why=modelChangeRefusal(nat,call1(nat,trainer::n::PlayerPedId,{}))){release(nat);step_=Step::Idle;return finished(Outcome::Fail,why);}
            std::uint32_t out[3]{};const std::uint32_t args[2]{player,target_};
            if(!nat.call(n::SetPlayerModel,args,2,out))return stop(nat,"model=%08x set-model-unavailable");   // nothing changed
            current_.store(which_==Story?0u:target_,std::memory_order_relaxed);   // the model is the player's from here on
            const std::uint32_t ped=call1(nat,trainer::n::PlayerPedId,{});   // SET_PLAYER_MODEL makes a new ped
            if(!ped)return stop(nat,"model=%08x no-ped-after-model");
            lastPed_=ped;newPed_=ped;   // v957 (WAVE2 item 3): handed to the trainer (notePedChange) by Actions::takeNewPed
            if(const std::uint32_t got=call1(nat,n::GetEntityModel,{ped});got && got!=target_) {   // (0: unreadable, best effort)
                current_.store(isFreemode(got)?got:0u,std::memory_order_relaxed);
                release(nat);step_=Step::Idle;
                return finished(Outcome::Fail,"model=%08x not-applied got=%08x",target_,got);
            }
            if(which_!=Story) {
                call0(nat,n::DefaultVariation,{ped});
                const Blend& b=which_==Female?FemaleBlend:MaleBlend;
                call0(nat,n::HeadBlend,{ped,b.first,b.second,0,0,0,0,bits(b.mix),bits(0.0f),bits(0.0f),1});
            }
            release(nat);
            if(which_==Story){step_=Step::Idle;return finished(Outcome::Ok,"model=%08x story",target_);}
            step_=Step::WaitBlend;start_=frame;
            return running();
        }
        if(step_==Step::WaitBlend) {
            const std::uint32_t ped=call1(nat,trainer::n::PlayerPedId,{});
            // v969 (saved look after a switch): the ped this switch made is handed to the creator once (Actions::reconcile)
            if(ped && call1(nat,n::HeadBlendFinished,{ped})){step_=Step::Idle;finishedPed_=ped;return finished(Outcome::Ok,"model=%08x blend=1",target_);}
            if(frame-start_>=BlendWaitFramesMax){step_=Step::Idle;finishedPed_=ped;return finished(Outcome::Ok,"model=%08x blend=timeout",target_);}
            return running();
        }
        return finished(Outcome::Fail,"idle");
    }
    // The gate closed (pause, cutscene, death, ...): before the model change nothing has changed (the request is released); after it
    // the new model stays (it was applied in one tick) and only the blend wait ends.
    // v957: the player ped SET_PLAYER_MODEL made (0: none since the last take). Taken once.
    std::uint32_t takeNewPed() noexcept {const std::uint32_t p=newPed_;newPed_=0;return p;}
    // v969: the ped of the last male/female switch that finished ok (0: none since the last take). Taken once, by Actions::reconcile.
    std::uint32_t takeFinishedPed() noexcept {const std::uint32_t p=finishedPed_;finishedPed_=0;return p;}
    const char* abort(Natives& nat) noexcept {
        const char* what=step_==Step::Loading?"not-applied":step_==Step::WaitBlend?"applied":"idle";
        if(step_==Step::Loading)release(nat);
        step_=Step::Idle;
        return what;
    }
    // The game replaced the player's model behind us. Runs only while a freemode model is on and no change of ours is running, and
    // costs one GET_ENTITY_MODEL per NEW ped (the glue passes the ped it reads). Same freemode model (a respawn): nothing changes here;
    // true is returned so the creator can put the saved look back on the new ped.
    template<class Log> bool reconcile(Natives& nat,std::uint32_t ped,Log log) noexcept {
        if(!active() || busy() || !ped || ped==lastPed_)return false;
        const std::uint32_t m=call1(nat,n::GetEntityModel,{ped});
        if(!m)return false;   // not readable yet: looked at again next tick
        lastPed_=ped;
        if(m==current())return true;
        current_.store(isFreemode(m)?m:0u,std::memory_order_relaxed);
        char line[112];
        std::snprintf(line,sizeof line,"RENDER_ONLINE character=%s reason=model-changed model=%08x ped=%08x",isFreemode(m)?"online":"story",m,ped);
        log(line);
        return false;
    }
    // v1105 (FREEMODE-GATE-6 B1): the game's own freemode player becomes the online character - on the freemode route maintransition T26
    // made it (SET_PLAYER_MODEL at 0x58fdf), our action 15 never ran, so current_ stayed 0 and the saved look was refused
    // "no-online-character" (t-onlgate102). Refused while busy, when a model of ours is on, without a ped, or when the ped is not a freemode
    // model. On success current_ = the model (active(): the Online save holds and guards key on it), lastPed_ = ped (reconcile does not
    // take the same ped for a respawn), story_ stays 0 (15 2 answers "no-story-model": the game owns the way back).
    template<class Log> bool adopt(Natives& nat,std::uint32_t ped,Log log) noexcept {
        if(busy() || current()!=0u || !ped)return false;
        const std::uint32_t m=call1(nat,n::GetEntityModel,{ped});
        if(!isFreemode(m))return false;
        current_.store(m,std::memory_order_relaxed);lastPed_=ped;
        char line[112];std::snprintf(line,sizeof line,"RENDER_ONLINE character=online reason=game-model model=%08x ped=%08x",m,ped);log(line);
        return true;
    }
private:
    enum class Step:std::uint8_t {Idle,Loading,WaitBlend};
    static std::uint32_t call1(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};return nat.call(h,a.begin(),unsigned(a.size()),out)?out[0]:0u;
    }
    static void call0(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};(void)nat.call(h,a.begin(),unsigned(a.size()),out);
    }
    void release(Natives& nat) noexcept {call0(nat,trainer::n::ModelNoLongerNeeded,{target_});}
    Outcome stop(Natives& nat,const char* f) noexcept {release(nat);step_=Step::Idle;return finished(Outcome::Fail,f,target_);}
    Step step_=Step::Idle;int which_=Male;std::uint32_t target_=0,start_=0,story_=0,lastPed_=0,newPed_=0,finishedPed_=0;   // v969: finishedPed_
    std::atomic<std::uint32_t> current_{0};std::atomic<bool> hold_{false};
};

// ---------------------------------------------------------------------------------------------------- the online places
// The multiplayer interiors of the disc's own interior table (am_mp_property_int; garages ids 12-14, high-end apartments 25-44,
// the low-end apartment 46). Each has the table's lookup point (for GET_INTERIOR_AT_COORDS_WITH_TYPE) and a standing point:
// apartments 1-17 stand on the table's position function point (0xeb1d) with its heading (the third float triple); their lookup point
// is that point + (0, 0, 1), as the table does. The others have no position function: they stand 1 m above the lookup point.
struct Place {const char* label;const char* type;float x,y,z;float sx,sy,sz,heading;bool turn;};
constexpr Place byLookup(const char* label,const char* type,float x,float y,float z) noexcept {return {label,type,x,y,z,x,y,z+1.0f,0.0f,false};}
constexpr Place byPosition(const char* label,float x,float y,float z,float h) noexcept {return {label,"v_apartment_high",x,y,z+1.0f,x,y,z,h,true};}
// v978: the interior lookup stays at the old (device-proven) point, the player stands on the disc's own entry spawn (am_mp_property_int,
// property-doors work/decoded.json apartments[p].entry_spawn): the old points were the apartment frame's origin, 16 m off and 6 m
// BELOW the floor, so the front door's exit box (on the floor) was out of reach ("you can't leave the apartment", owner 2026-11-07).
constexpr Place bySpawn(const char* label,float x,float y,float z,float sx,float sy,float sz,float h) noexcept {return {label,"v_apartment_high",x,y,z+1.0f,sx,sy,sz,h,true};}
inline constexpr std::array<Place,24> PlaceList{{
    byLookup("Garage, 2 cars","v_garages",173.118f,-1003.279f,-100.0f),byLookup("Garage, 6 cars","v_garagem",199.971f,-999.668f,-100.0f),
    byLookup("Garage, 10 cars","v_garagel",228.606f,-992.054f,-100.0f),
    bySpawn("Apartment 1",-794.918f,339.627f,200.413f,-779.73f,340.197f,206.621f,90.2f),bySpawn("Apartment 2",-761.098f,317.626f,169.596f,-776.286f,317.056f,175.804f,-89.8f),
    bySpawn("Apartment 3",-761.189f,317.629f,216.05f,-776.377f,317.06f,222.258f,-89.8f),bySpawn("Apartment 4",-795.386f,340.019f,152.794f,-780.197f,340.589f,159.001f,90.2f),
    bySpawn("Apartment 5",-258.181f,-950.685f,70.024f,-262.84f,-965.152f,76.231f,-19.8f),bySpawn("Apartment 6",-285.005f,-957.655f,85.303f,-280.346f,-943.188f,91.511f,160.2f),
    bySpawn("Apartment 7",-1471.882f,-530.748f,62.349f,-1459.768f,-521.57f,68.557f,125.2f),bySpawn("Apartment 8",-1471.882f,-530.748f,49.722f,-1459.768f,-521.57f,55.929f,125.2f),
    bySpawn("Apartment 9",-885.37f,-451.478f,119.327f,-898.572f,-459.009f,125.534f,-62.244f),bySpawn("Apartment 10",-913.039f,-438.428f,114.4f,-899.725f,-431.097f,120.607f,116.891f),
    bySpawn("Apartment 11",-892.55f,-430.479f,88.254f,-885.166f,-443.763f,94.461f,27.119f),bySpawn("Apartment 12",-35.046f,-576.317f,82.907f,-20.579f,-580.976f,89.115f,70.2f),
    bySpawn("Apartment 13",-10.379f,-590.743f,93.025f,-15.038f,-605.21f,99.233f,-19.8f),bySpawn("Apartment 14",-900.631f,-376.746f,78.273f,-913.915f,-384.132f,84.481f,-62.874f),
    bySpawn("Apartment 15",-929.483f,-374.51f,102.233f,-916.267f,-367.004f,108.44f,117.647f),bySpawn("Apartment 16",-617.165f,64.604f,100.82f,-601.977f,65.174f,107.027f,90.2f),
    bySpawn("Apartment 17",-584.201f,42.713f,86.419f,-599.39f,42.143f,92.626f,-89.8f),
    byLookup("Apartment 18","v_apartment_high",-470.375f,-698.521f,51.528f),byLookup("Apartment 19","v_apartment_high",-460.613f,-691.556f,69.907f),
    byLookup("Apartment 20","v_apartment_high",300.633f,-997.429f,-99.973f),   // under the map by the garages: E4 checks it (16 22)
    byLookup("Small apartment","v_apart_midspaz",341.1f,-1000.0f,-99.2f)}};

// On foot only (a vehicle would be moved into the interior with the ped). Enable the interior when story code has disabled it (the
// multiplayer scripts' order: DISABLE_INTERIOR(id, FALSE), then PIN_INTERIOR_IN_MEMORY), wait until it is ready (bounded; on any
// waiting tick it reads disabled again, enabled and pinned again, as am_mp_property_int 0x247de does every frame), put the player on
// the standing point (and heading), request collision there, give collision SettleFrames, unpin. The interior stays enabled; story's
// building_controller disables it again by itself only while it is still marked (never after it disabled it once).
class PlaceTrip {
public:
    bool busy() const noexcept {return step_!=Step::Idle;}
    bool wasDisabled() const noexcept {return wasDisabled_;}
    void setMoveAllowed(bool b) noexcept {moveAllowed_=b;}   // v976: the transition holds every teleport until the screen is out
    void setLog(LogFn f) noexcept {log_=f;}
    // v969 (action 16 auto-exit): autoExit = a single trip (the 16 99 sweep passes false and keeps the refusal).
    static constexpr float MaxLeaveSpeed=3.0f;          // m/s: faster = "in-vehicle moving" (leaving would throw the player out)
    static constexpr std::uint32_t LeaveTaskFrames=60,WarpGraceFrames=15;   // ~2 s for the task, then the warp-out, then give up
    static constexpr std::uint32_t LeaveWarpFlag=16;    // TASK_LEAVE_VEHICLE flag 16: out at once (no door animation)
    Outcome begin(Natives& nat,std::uint32_t ped,int index,std::uint32_t frame,std::uint32_t waitMax=InteriorWaitFramesMax,bool autoExit=false) noexcept {
        const Place& p=PlaceList[std::size_t(index)];place_=&p;index_=index;waitMax_=waitMax;
        id_=0;wasDisabled_=capped_=false;reenabled_=0;enableCalls_=0;vehicle_=0;warped_=false;
        if(inVehicle(nat,ped)) {
            if(!autoExit)return finished(Outcome::Fail,"in-vehicle");
            if(const char* why=leaveRefusal(nat,ped))return finished(Outcome::Fail,"in-vehicle %s",why);
            call0(nat,n::TaskLeaveVehicle,{ped,vehicle_,0});
            if(log_){char l[96];std::snprintf(l,sizeof l,"RENDER_ONLINE place=%d leave-vehicle vehicle=%08x",index_,vehicle_);log_(l);}
            step_=Step::Leaving;start_=frame;
            return running();
        }
        return enterInterior(nat,frame);
    }
    // The interior part of begin (v969: also reached from Leaving once the player is on foot).
    Outcome enterInterior(Natives& nat,std::uint32_t frame) noexcept {
        const Place& p=*place_;
        const std::uint32_t type=nat.text(p.type);
        if(!type)return finished(Outcome::Fail,"no-scratch");
        std::uint32_t out[3]{};const std::uint32_t args[4]{bits(p.x),bits(p.y),bits(p.z),type};
        id_=nat.call(n::InteriorAtCoordsWithType,args,4,out)?out[0]:0u;
        if(!id_ || int(id_)<0){line("no-interior",0);return finished(Outcome::Fail,"no-interior type=%s",p.type);}
        capped_=ask(nat,n::IsInteriorCapped,id_);   // logged only: no story script caps one of these 24 (NOTES.md)
        wasDisabled_=ask(nat,n::IsInteriorDisabled,id_);
        if(wasDisabled_){call0(nat,n::DisableInterior,{id_,0});++enableCalls_;}
        call0(nat,n::PinInterior,{id_});
        step_=Step::Pinned;start_=frame;
        return running();
    }
    Outcome step(Natives& nat,std::uint32_t ped,std::uint32_t frame) noexcept {
        if(step_==Step::Leaving) { // v969: wait for the player to be on foot (task, then warp-out, then give up)
            if(!inVehicle(nat,ped)){step_=Step::Idle;return enterInterior(nat,frame);}
            if(!warped_ && frame-start_>=LeaveTaskFrames) {
                call0(nat,n::TaskLeaveVehicle,{ped,vehicle_,LeaveWarpFlag});warped_=true;start_=frame;
                if(log_){char l[80];std::snprintf(l,sizeof l,"RENDER_ONLINE place=%d leave-vehicle warp-out",index_);log_(l);}
                return running();
            }
            if(warped_ && frame-start_>=WarpGraceFrames){step_=Step::Idle;return finished(Outcome::Fail,"leave-timeout");}
            return running();
        }
        if(step_==Step::Pinned) {
            if(ask(nat,n::IsInteriorDisabled,id_)){call0(nat,n::DisableInterior,{id_,0});call0(nat,n::PinInterior,{id_});++reenabled_;++enableCalls_;}
            if(!ask(nat,n::IsInteriorReady,id_)) {
                if(frame-start_>=waitMax_) {
                    unpin(nat);line("not-ready",frame-start_);
                    return finished(Outcome::Fail,"interior=%u not-ready dis=%d",id_,wasDisabled_?1:0);
                }
                return running();
            }
            if(inVehicle(nat,ped)){unpin(nat);return finished(Outcome::Fail,"in-vehicle");}   // got into one while waiting
            if(!moveAllowed_)return running();   // v976: the screen is still fading out (bounded by placefade::FadeWaitMax)
            const Place& p=*place_;
            call0(nat,trainer::n::SetPedCoordsKeepVehicle,{ped,bits(p.sx),bits(p.sy),bits(p.sz)});
            if(p.turn)call0(nat,n::SetEntityHeading,{ped,bits(p.heading)});
            call0(nat,trainer::n::RequestCollisionAtCoord,{bits(p.sx),bits(p.sy),bits(p.sz)});
            readyFrames_=frame-start_;step_=Step::Settling;start_=frame;
            return running();
        }
        if(step_==Step::Outside) {   // v976 (lane property-doors' trip back out, moved here from beginOutside)
            if(!moveAllowed_)return running();
            call0(nat,trainer::n::SetPedCoordsKeepVehicle,{ped,bits(ox_),bits(oy_),bits(oz_)});
            call0(nat,n::SetEntityHeading,{ped,bits(oh_)});
            call0(nat,trainer::n::RequestCollisionAtCoord,{bits(ox_),bits(oy_),bits(oz_)});
            readyFrames_=0;step_=Step::Settling;start_=frame;
            return running();
        }
        if(step_==Step::Settling) {
            if(frame-start_<SettleFrames)return running();
            if(!place_){step_=Step::Idle;line("outside",0);   // lane property-doors: the trip back out (no interior pinned)
                return finished(Outcome::Ok,"outside x=%.1f y=%.1f z=%.1f",double(ox_),double(oy_),double(oz_));}
            unpin(nat);line("ready",readyFrames_);
            return finished(Outcome::Ok,"interior=%u x=%.1f y=%.1f z=%.1f dis=%d",id_,double(place_->sx),double(place_->sy),
                            double(place_->sz),wasDisabled_?1:0);
        }
        return finished(Outcome::Fail,"idle");
    }
    // lane property-doors (PROPOSED): 16 with a0 = propdoors::exitArg(building, entrance), the trip back out of a place to that
    // building's leave point (the disc's +120[0]+8 / +893+24 with its heading). On foot only; no interior: move, heading, collision,
    // SettleFrames, done (the same Settling step as a place trip).
    Outcome beginOutside(Natives& nat,std::uint32_t ped,int a0,std::uint32_t frame) noexcept {
        place_=nullptr;index_=a0;id_=0;wasDisabled_=capped_=false;reenabled_=0;enableCalls_=0;
        if(!propdoors::exitTarget(a0,ox_,oy_,oz_,oh_))return finished(Outcome::Fail,"bad-exit a0=%d",a0);
        if(inVehicle(nat,ped))return finished(Outcome::Fail,"in-vehicle");
        step_=Step::Outside;start_=frame;   // v976: the move happens in step() once the transition allows it
        return running();
    }
    const char* abort(Natives& nat) noexcept {
        const char* what=step_==Step::Settling?"moved":step_==Step::Pinned?"not-moved":step_==Step::Leaving?"leaving":step_==Step::Outside?"not-moved":"idle";
        if(step_==Step::Leaving || step_==Step::Outside){step_=Step::Idle;return what;}   // v969: nothing pinned yet (v976: nor outside)
        if(step_!=Step::Idle)unpin(nat);
        return what;
    }
private:
    enum class Step:std::uint8_t {Idle,Leaving,Pinned,Settling,Outside};   // v969: Leaving (auto-exit); v976: Outside (deferred move)
    // v969: why the player may not be taken out of the vehicle now (nullptr: he may); vehicle_ is set. Unregistered natives read 0.
    const char* leaveRefusal(Natives& nat,std::uint32_t ped) noexcept {
        std::uint32_t out[3]{};const std::uint32_t a[2]{ped,0};
        vehicle_=nat.call(trainer::n::GetVehiclePedIsIn,a,2,out)?out[0]:0u;
        if(!vehicle_)return "no-vehicle";
        const std::uint32_t v[1]{vehicle_};float speed=0;
        if(nat.call(n::GetEntitySpeed,v,1,out)){std::memcpy(&speed,&out[0],4);}
        if(!(speed<=MaxLeaveSpeed))return "moving";   // (also NaN)
        if(ask(nat,n::IsEntityInAir,vehicle_))return "airborne";
        if(ask(nat,n::IsEntityInWater,vehicle_))return "in-water";
        return nullptr;
    }
    static void call0(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};(void)nat.call(h,a.begin(),unsigned(a.size()),out);
    }
    static bool ask(Natives& nat,std::uint32_t h,std::uint32_t id) noexcept {
        std::uint32_t out[3]{};const std::uint32_t a[1]{id};return nat.call(h,a,1,out) && out[0];
    }
    static bool inVehicle(Natives& nat,std::uint32_t ped) noexcept {return pedInAnyVehicle(nat,ped);}   // (v957: shared with Character)
    void line(const char* what,std::uint32_t frames) const noexcept {
        if(!log_)return;
        char l[200];   // (v958: longer line)
        std::snprintf(l,sizeof l,"RENDER_ONLINE place=%d interior=%u %s frames=%u disabled_before=%d enable_calls=%u reenabled_while_waiting=%u capped=%d",
                      index_,id_,what,frames,wasDisabled_?1:0,enableCalls_,reenabled_,capped_?1:0);   // v958: enable_calls (lead 2026-11-06)
        log_(l);
    }
    void unpin(Natives& nat) noexcept {if(id_)call0(nat,n::UnpinInterior,{id_});step_=Step::Idle;}   // (property-doors: none pinned outside)
    Step step_=Step::Idle;const Place* place_=nullptr;std::uint32_t id_=0,start_=0,waitMax_=InteriorWaitFramesMax,readyFrames_=0;
    float ox_=0,oy_=0,oz_=0,oh_=0;   // lane property-doors: the leave point of a trip back out
    int index_=0;bool wasDisabled_=false,capped_=false;unsigned reenabled_=0,enableCalls_=0;LogFn log_=nullptr;
    std::uint32_t vehicle_=0;bool warped_=false;   // v969 auto-exit
    bool moveAllowed_=true;   // v976
};

// The developer sweep (device check only; the apps send 0..23): every place in order with SweepWaitFramesMax each, a stay of
// SweepStayFrames after each ready one, then one result with three 24-bit masks (bit i = PlaceList[i]): ready, was disabled before
// the trip, no interior found. In a vehicle (or no scratch) it stops at that place.
class PlaceSweepRun {
public:
    explicit PlaceSweepRun(PlaceTrip& t) noexcept : trip_(t) {}
    bool active() const noexcept {return active_;}
    Outcome begin(Natives& nat,std::uint32_t ped,std::uint32_t frame) noexcept {
        active_=true;staying_=false;at_=0;ready_=dis_=none_=0;return next(nat,ped,frame);
    }
    Outcome step(Natives& nat,std::uint32_t ped,std::uint32_t frame) noexcept {
        if(staying_) {
            if(frame-stayStart_<SweepStayFrames)return running();
            staying_=false;++at_;return next(nat,ped,frame);
        }
        return settle(trip_.step(nat,ped,frame),nat,ped,frame);
    }
    const char* abort(Natives& nat) noexcept {active_=false;staying_=false;return trip_.abort(nat);}
private:
    Outcome next(Natives& nat,std::uint32_t ped,std::uint32_t frame) noexcept {
        while(at_<int(PlaceList.size())) {
            const Outcome o=trip_.begin(nat,ped,at_,frame,SweepWaitFramesMax);
            if(o.kind==Outcome::Running)return o;
            if(stopper(o)){active_=false;return finished(Outcome::Fail,"sweep %s at=%d",o.detail,at_);}
            record(o);++at_;
        }
        active_=false;
        return finished(Outcome::Ok,"sweep ready=%06x dis=%06x none=%06x",ready_,dis_,none_);
    }
    Outcome settle(const Outcome& o,Natives& nat,std::uint32_t ped,std::uint32_t frame) noexcept {
        if(o.kind==Outcome::Running)return o;
        if(stopper(o)){active_=false;return finished(Outcome::Fail,"sweep %s at=%d",o.detail,at_);}
        record(o);
        if(o.kind==Outcome::Ok){staying_=true;stayStart_=frame;return running();}
        ++at_;return next(nat,ped,frame);
    }
    static bool stopper(const Outcome& o) noexcept {return !std::strcmp(o.detail,"in-vehicle") || !std::strcmp(o.detail,"no-scratch");}
    void record(const Outcome& o) noexcept {
        const std::uint32_t bit=1u<<unsigned(at_);
        if(o.kind==Outcome::Ok)ready_|=bit;
        if(trip_.wasDisabled())dis_|=bit;
        if(!std::strncmp(o.detail,"no-interior",11))none_|=bit;
    }
    PlaceTrip& trip_;bool active_=false,staying_=false;int at_=0;std::uint32_t stayStart_=0,ready_=0,dis_=0,none_=0;
};


// ---------------------------------------------------------------------------------------------------- the world toggle
// "Instance priority mode" (developer-only: diag-sandbox-online-world / sandbox_flags bit 0; no app row until E4 shows an effect).
// On: SET_INSTANCE_PRIORITY_MODE(1) as freemode_init calls it; off: 0, as story's startup and main_persistent call it. Story code may
// set 0 again at any time (main_persistent's 0x142 path, re-armed through 0x1bde5) without any ped change, so while it is wanted the
// game's own mode word is read every tick (one guest load, no native call) and the mode applied again when it differs
// ("reason=reset"). If our own write does not show in the word, the word checks stop (no per-frame fight) and the line says so.
class World {
public:
    void setWanted(bool on) noexcept {wanted_.store(on,std::memory_order_relaxed);}
    bool wanted() const noexcept {return wanted_.load(std::memory_order_relaxed);}
    bool applied() const noexcept {return applied_.load(std::memory_order_relaxed);}
    unsigned applications() const noexcept {return count_;}
    unsigned resets() const noexcept {return resets_;}
    template<class Log> void tick(Natives& nat,Log log) noexcept {
        const bool w=wanted();
        bool reset=false;
        if(w==applied()) {
            if(!w || !verified_)return;
            if(nat.load(ModeWord)==1u)return;
            reset=true;   // the game set the mode back behind us
        }
        std::uint32_t out[3]{};const std::uint32_t mode=w?1u:0u;
        if(!nat.call(n::InstancePriorityMode,&mode,1,out))return;   // not registered yet: try again next tick
        applied_.store(w,std::memory_order_relaxed);++count_;if(reset)++resets_;
        const std::uint32_t word=nat.load(ModeWord);
        verified_=word==mode;
        if(reset && verified_ && resets_>3 && (resets_&(resets_-1u)))return;   // logged: the first three resets, then powers of two
        char line[128];
        std::snprintf(line,sizeof line,"RENDER_ONLINE world=%d instance_priority_mode=%u word=%u%s%s",w?1:0,mode,word,
                      reset?" reason=reset":"",verified_?"":" unverified word-checks=off");
        log(line);
    }
private:
    std::atomic<bool> wanted_{false},applied_{false};bool verified_=false;unsigned count_=0,resets_=0;
};

// ---------------------------------------------------------------------------------------------------- the trainer's view
// Trainer action 15 OnlineCharacter (ALLOCATION.md) and, if the owner accepts the lane's proposed allocation, 16 OnlinePlace.
constexpr int OnlinePlaceAction=16;   // PROPOSED (NOTES.md "Proposed allocations"); the trainer routes it only with patch 0002
class Actions final : public trainer::OnlineActions {
public:
    bool characterActive() const noexcept override {return character_.active();}
    bool creatorMenuOpen() const noexcept override {return menu_.openForApp();}   // v975 (G3)
    bool ownsScreenFade() const noexcept override {return fade_.active();}   // v976
    std::uint32_t takeNewPed() noexcept override {return character_.takeNewPed();}   // v957 (WAVE2 item 3)
    bool validArgument(int action,int a0) const noexcept override {
        if(action==trainer::OnlineCharacter)return a0>=Male && a0<=Story;
        if(action==OnlinePlaceAction)return (a0>=0 && a0<int(PlaceList.size())) || a0==PlaceSweep   // (wave2 mp-interiors: 99 = the sweep)
                                            || propdoors::validExitArg(a0);   // lane property-doors (PROPOSED): 1000 + building * 2 + door
        if(action==creator::CharacterLookAction && a0==creatormenu::OpenMenuOp)return true;   // PROPOSED: the in-game creator menu
        if(action==creator::CharacterLookAction || action==creator::CharacterEditAction)return creator::Creator::validArgument(action,a0);
        return false;
    }
    int begin(Natives& nat,int action,int a0,std::uint32_t player,std::uint32_t ped,std::uint32_t frame,char* detail,int cap) noexcept override {
        kind_=action;menuJob_=false;
        if(action==creator::CharacterLookAction && a0==creatormenu::OpenMenuOp) { // PROPOSED: the menu runs as this job until it closes
            // (v959's face camera is released by MenuHost::beforeOpen once every refusal passed; a live 17 3 session refuses 17 4)
            const int k=menu_.begin(nat,menuHost_,player,ped,frame,detail,cap);
            menuJob_=k==0;
            return k;
        }
        if(isCreator(action)) { // only with a freemode model on and no character change running
            // v959: the face camera ends with the edit session (finalize / apply-saved / cancel) before the look's own apply
            if(action==creator::CharacterLookAction && a0!=creator::StartEditing)
                camera_.release(nat,a0==creator::SaveEdits?"finalize":a0==creator::DiscardEdits?"cancel":"apply-saved");
            if(character_.busy())return report(finished(Outcome::Fail,"character-busy"),detail,cap);   // (a 15 job: its begin released the camera)
            const int k=reportLook(creator_.begin(nat,action,a0,ped,character_.current(),frame),detail,cap);
            lookResult(nat,action,k,ped);   // v959
            return k;
        }
        if(action==trainer::OnlineCharacter)camera_.release(nat,a0==Story?"online-character-ended":"character-change");   // v959
        if(action==trainer::OnlineCharacter)return report(character_.begin(nat,ped,a0,frame),detail,cap);
        if(a0==PlaceSweep){place_.setMoveAllowed(true);return report(sweep_.begin(nat,ped,frame),detail,cap);}   // (no transition: device tool)
        place_.setMoveAllowed(false);   // v976: nothing moves before the transition says so
        heldOk_=false;   // v978
        const Outcome o=propdoors::validExitArg(a0)?place_.beginOutside(nat,ped,a0,frame)
                        :place_.begin(nat,ped,a0,frame,InteriorWaitFramesMax,true);   // v969 auto-exit + v972 property doors (the trip back out)
        if(o.kind==Outcome::Running){if(!fade_.begin(nat,player,frame,fadeLog_))place_.setMoveAllowed(true);}   // v976: refusals never fade
        else place_.setMoveAllowed(true);
        return report(o,detail,cap);
    }
    int step(Natives& nat,std::uint32_t player,std::uint32_t ped,std::uint32_t frame,char* detail,int cap) noexcept override {
        if(menuJob_){const int k=menu_.step(nat,menuHost_,player,ped,frame,detail,cap);if(k!=0)menuJob_=false;return k;}   // PROPOSED
        if(isCreator(kind_)){const int k=reportLook(creator_.step(nat,frame),detail,cap);lookResult(nat,kind_,k,ped);
            if(k==1 && fadeLog_) {   // v997: the hair as set (drawable / texture / palette after the game-count clamp) and the game's counts
                const auto& ap=creator_.applier();const auto& h=ap.applied().part[2];
                char l[128];std::snprintf(l,sizeof l,"RENDER_ONLINE look hair=%d/%d/%d of=%d/%d",h.drawable,h.texture,h.palette,ap.hairCount(),ap.hairTextures());fadeLog_(l);
            }
            return k;}   // (v959)
        if(kind_==trainer::OnlineCharacter)return report(character_.step(nat,player,frame),detail,cap);
        if(sweep_.active()){place_.setMoveAllowed(true);return report(sweep_.step(nat,ped,frame),detail,cap);}
        if(heldOk_) {   // v978: the trip is done; the spinner holds until online's minimum dark time passed
            if(!fade_.holdDone(frame))return 0;
            heldOk_=false;fade_.end(nat,"ok",frame);place_.setMoveAllowed(true);return report(held_,detail,cap);
        }
        place_.setMoveAllowed(fade_.readyToMove(nat,frame));   // v976: no transition = always allowed
        const Outcome o=place_.step(nat,ped,frame);
        if(o.kind==Outcome::Ok && !fade_.holdDone(frame)){held_=o;heldOk_=true;return 0;}   // v978
        if(o.kind!=Outcome::Running){fade_.end(nat,o.kind==Outcome::Ok?"ok":"fail",frame);place_.setMoveAllowed(true);}   // v976
        return report(o,detail,cap);
    }
    const char* abort(Natives& nat) noexcept override {
        if(menuJob_){menuJob_=false;return menu_.abort(nat,menuHost_,"blocked");}   // PROPOSED: the gate closed while the menu was open
        if(isCreator(kind_)){camera_.release(nat,"blocked");return creator_.abort();}   // v959: the gate closed during a creator job
        if(kind_==trainer::OnlineCharacter)return character_.abort(nat);
        if(sweep_.active())return sweep_.abort(nat);
        if(heldOk_){heldOk_=false;fade_.end(nat,"abort");place_.setMoveAllowed(true);return "moved";}   // v978: aborted during the hold
        const char* what=place_.abort(nat);fade_.end(nat,"abort");place_.setMoveAllowed(true);   // v976: the gate closed (death, pause, ...)
        return what;
    }
    // Optional: one RENDER_ONLINE place line per trip (the glue passes trainerLog). Null keeps the trip silent (the result line stays).
    void setPlaceLog(LogFn f) noexcept {place_.setLog(f);fadeLog_=f;}   // v976: the transition logs with the place lines
    // v976: Sandbox ended (the glue) or any other teardown: the screen comes back, the spinner stops, control returns. Idempotent.
    void endPlaceFade(Natives& nat,const char* why) noexcept {fade_.end(nat,why);place_.setMoveAllowed(true);}   // (a held result stays held; the job then ends ok once the trainer steps it)
    bool placeFadeActive() const noexcept {return fade_.active();}
    // The creator's record lives in the Sandbox generation (character_store.h FileStore); the glue hands it over in Sandbox mode.
    void setCreatorStore(creator::Store* s) noexcept {creator_.setStore(s);}
    void setLookRefresher(creator::Refresher* r) noexcept {creator_.setRefresher(r);}   // v1509 (look_refresh.h; the glue sets it in Online mode)
    const creator::Creator& creator() const noexcept {return creator_;}
    const Character& character() const noexcept {return character_;}
    // The Sandbox core says its autosave hold on state bit 20 is in place (recomp_online_set_autosave_hold); until then 15 0 / 15 1
    // answer "fail no-autosave-hold".
    void setAutosaveHold(bool on) noexcept {character_.setAutosaveHold(on);}
    // Every Sandbox tick while a freemode model is on and no character change runs (the glue's onlineTick). A respawn with the same
    // freemode model has the creator put the look back in the background (bounded job, no trainer request).
    template<class Log> void reconcile(Natives& nat,std::uint32_t ped,Log log) noexcept {
        if(character_.reconcile(nat,ped,log)){(void)character_.takeFinishedPed();creator_.noteRespawn(ped);}   // (a respawn supersedes a switch's ped)
        else if(const std::uint32_t fp=character_.takeFinishedPed())creator_.noteRespawn(fp);   // v969: the saved look after 15 0 / 15 1
        camera_.tick(nat,ped,character_.active());   // v959: off = no call; a new ped / model ends the face camera
        // PROPOSED (creator menu): no background full apply (it sets the head blend) while the menu is open: its camera may be
        // rendering (the C2 rule). A respawn noted meanwhile waits; the menu's close notes the current ped again.
        const std::uint32_t tick=++ticks_;
        if(!menu_.isOpen())creator_.tickIdle(nat,ped,character_.current(),tick,log);
    }
    // v1105 (FREEMODE-GATE-6 B2): adopt the game's freemode player (Character::adopt). 1 adopted, 2 refused (detail: why). Refused while the
    // creator menu is open, the look applier is busy, or a character job (15) runs.
    template<class Log> int adoptGameCharacter(Natives& nat,std::uint32_t ped,char* detail,int cap,Log log) noexcept {
        const auto say=[&](int r,const char* w){if(detail && cap>0)std::snprintf(detail,std::size_t(cap),"%s",w);return r;};
        if(menu_.isOpen())return say(2,"menu-open");
        if(creator_.busy())return say(2,"look-busy");
        if(character_.busy())return say(2,"character-busy");
        if(character_.active())return say(2,"already-online");
        return character_.adopt(nat,ped,log)?say(1,"adopted"):say(2,"not-a-freemode-model");
    }
    // v959: the face camera's log line (RENDER_ONLINE look camera=...) and the glue's registered-native check; the camera itself.
    void setCameraLog(facecam::LogFn f) noexcept {camera_.setLog(f);}
    void setCameraNativeCheck(facecam::HasFn f) noexcept {camera_.setNativeCheck(f);}
    const facecam::FaceCamera& camera() const noexcept {return camera_;}
    // v959: Sandbox ended while the camera is on (the play-mode latch never changes in a process, so the glue's call is a guard).
    bool endCamera(Natives& nat,const char* reason) noexcept {return camera_.release(nat,reason);}
    // PROPOSED (creator menu): its log line (RENDER_ONLINE menu ...), the glue's registered-native check (the menu refuses to open
    // unless every native it calls is registered), the menu itself, and the Sandbox-off guard (its job then ends with "menu-closed").
    void setMenuLog(creatormenu::CreatorMenu::LogFn f) noexcept {menu_.setLog(f);}
    void setMenuNativeCheck(creatormenu::CreatorMenu::HasFn f) noexcept {menu_.setNativeCheck(f);}
    const creatormenu::CreatorMenu& menu() const noexcept {return menu_;}
    const char* endMenu(Natives& nat,const char* reason) noexcept {return menu_.abort(nat,menuHost_,reason);}
private:
    static constexpr bool isCreator(int action) noexcept {return action==creator::CharacterLookAction || action==creator::CharacterEditAction;}
    static int report(const Outcome& o,char* detail,int cap) noexcept {
        if(detail && cap>0)std::snprintf(detail,std::size_t(cap),"%s",o.detail);
        return o.kind==Outcome::Running?0:o.kind==Outcome::Ok?1:2;
    }
    // v959: a creator result (0 running, 1 ok, 2 fail). An ok edit, or a finished StartEditing, starts the face camera while editing;
    // any refused / failed creator request ends it.
    void lookResult(Natives& nat,int action,int k,std::uint32_t ped) noexcept {
        if(k==2){camera_.release(nat,"refused");return;}
        if(k==1 && creator_.editing())
            if(action==creator::CharacterEditAction || action==creator::CharacterLookAction)camera_.begin(nat,ped,action==creator::CharacterEditAction?"edit":"start-editing");
    }
    static int reportLook(const creator::Outcome& o,char* detail,int cap) noexcept {
        if(detail && cap>0)std::snprintf(detail,std::size_t(cap),"%s",o.detail);
        return o.kind==creator::Outcome::Running?0:o.kind==creator::Outcome::Ok?1:2;
    }
    placefade::Fade fade_;placefade::LogFn fadeLog_=nullptr;   // v976
    Outcome held_{};bool heldOk_=false;   // v978: a finished trip waiting out the minimum hold
    Character character_;PlaceTrip place_;PlaceSweepRun sweep_{place_};creator::Creator creator_;std::uint32_t ticks_=0;int kind_=trainer::OnlineCharacter;
    facecam::FaceCamera camera_;   // v959
    // PROPOSED (creator menu): what the menu may use of this object - the creator and action 15's model change - and nothing else.
    struct MenuHost final : creatormenu::Host {
        explicit MenuHost(Actions& a) noexcept:a_(a) {}
        creator::Creator& looks() noexcept override {return a_.creator_;}
        std::uint32_t onlineModel() const noexcept override {return a_.character_.current();}
        bool modelBusy() const noexcept override {return a_.character_.busy();}
        int changeModel(Natives& nat,std::uint32_t ped,bool female,std::uint32_t frame,char* d,int cap) noexcept override {
            return report(a_.character_.begin(nat,ped,female?Female:Male,frame),d,cap);
        }
        int stepModel(Natives& nat,std::uint32_t player,std::uint32_t frame,char* d,int cap) noexcept override {
            return report(a_.character_.step(nat,player,frame),d,cap);
        }
        void abortModel(Natives& nat) noexcept override {(void)a_.character_.abort(nat);}
        bool editSessionLive() const noexcept override {return a_.creator_.editing() && a_.camera_.on();}   // v959: on = in a 17 3 session
        bool doorChooserOpen() const noexcept override {return propdoors::chooserOpen.load(std::memory_order_acquire);}   // v975 (G2)
        void beforeOpen(Natives& nat) noexcept override {a_.camera_.release(nat,"menu");}   // never shares the screen with the menu's cameras
        Actions& a_;
    };
    creatormenu::CreatorMenu menu_;MenuHost menuHost_{*this};bool menuJob_=false;
};

// Level 1 never calls a network or stats native (review §4 OW host check).
constexpr bool forbiddenName(const char* s) noexcept {
    const auto starts=[](const char* a,const char* p){while(*p){if(*a!=*p)return false;++a;++p;}return true;};
    return starts(s,"NETWORK_") || starts(s,"STAT_") || starts(s,"NET_") || starts(s,"UGC_") || starts(s,"CLOUD_");
}
constexpr bool noForbidden() noexcept {for(const auto& u:Used)if(forbiddenName(u.name))return false;return true;}
static_assert(noForbidden(),"Online World Level 1 must not call network / stats natives");

} // namespace recomp::online
