#pragma once
// trainer (2026-10-25; shared library code for every port: Thor, Fold QoL, iOS, Duo. Owner 2026-10-25: "two finger swipe the bottom
// screen for a trainer menu ... and the Script mod support in the main options menu"). Single player only.
//
// What it is: a small queue of requests from the app (recomp_trainer_v1) that the library executes on the game's own script thread,
// by calling the game's RECOMPILED native handlers with the arguments its scripts pass. Nothing here writes game state directly.
//
// Where it runs (indirect_call_resolver.cpp, the glue): right after a script's PLAYER_PED_ID (fa92e226) returns through the script
// VM's native call (sub_83439990, return address 0x8343A208; the call context at r1+128: +0 result pointer, +4 argument count,
// +8 argument pointer, +12 = 0). That is the script thread, inside a script's own native call, so the natives see a current script
// thread exactly as for the game's own calls. At most once per game frame (the frame counter GET_FRAME_COUNT reads, u32 0x83C5D10C).
// The glue saves the whole register context and restores it afterwards; argument blocks live below the VM's stack pointer.
//
// Native ABI (from our recompiled handlers, e.g. GET_ENTITY_COORDS sub_82D773A0, CREATE_VEHICLE sub_82E06618): r3 = the context;
// arguments are 4-byte big-endian slots (int, float bits, BOOL, guest pointer); a Vector3 argument is 3 slots; a Vector3 result is
// written as 3 words at the result pointer. Hash = joaat of the lower-case name (the 360 build's rule, natives/registrations.json).
//
// Safety: OFF until files/settings/trainer.txt says "trainer=on" (read at the first API call and at every Sync). Requests and toggles
// act only in gameplay (player playing, no pause menu, no cutscene, no character switch, screen not faded out, player alive, no network
// game). Every request is logged (RENDER_TRAINER). A request that stays blocked for BlockedFramesMax game frames is dropped.
// This header is pure: no guest memory, no platform calls. The glue implements Natives; the tests implement it with mocks.
#include "online_leave_flag.h"   // v1085: state bit 26
#include "local_session_flag.h"   // v1013
#include "online_curtain_flag.h"  // v1056: the Online curtain is our own fade for the gate
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <iterator>
#include <mutex>
#if __has_include("phone_call.h")
#include "phone_call.h"   // Update 1 P7: the renderer's "phone up" bit (v852; pure header), a SwitchCharacter refusal
#define RECOMP_TRAINER_PHONE_UP 1
#endif

namespace recomp::trainer {

// The capability marker the apps may look for (zero-terminated); the exports below are the API.
inline constexpr char Marker[]="recomp-trainer-v1";

constexpr std::uint32_t joaat(const char* s) noexcept {
    std::uint32_t h=0;
    for(;*s;++s){unsigned char c=static_cast<unsigned char>(*s);if(c>='A' && c<='Z')c=static_cast<unsigned char>(c+32);
        h+=c;h+=h<<10;h^=h>>6;}
    h+=h<<3;h^=h>>11;h+=h<<15;return h;
}

// The natives the trainer calls (hash = joaat of the name; each handler's argument use was read from its asm, see the README).
namespace n {
constexpr std::uint32_t PlayerId=joaat("PLAYER_ID"),PlayerPedId=joaat("PLAYER_PED_ID"),IsPlayerPlaying=joaat("IS_PLAYER_PLAYING"),
    IsPauseMenuActive=joaat("IS_PAUSE_MENU_ACTIVE"),IsCutsceneActive=joaat("IS_CUTSCENE_ACTIVE"),
    IsSwitchInProgress=joaat("IS_PLAYER_SWITCH_IN_PROGRESS"),IsScreenFadedOut=joaat("IS_SCREEN_FADED_OUT"),IsEntityDead=joaat("IS_ENTITY_DEAD"),
    NetworkGame=joaat("NETWORK_IS_GAME_IN_PROGRESS"),
    SetPlayerInvincible=joaat("SET_PLAYER_INVINCIBLE"),SetWantedLevel=joaat("SET_PLAYER_WANTED_LEVEL"),
    SetWantedLevelNow=joaat("SET_PLAYER_WANTED_LEVEL_NOW"),SetMaxWantedLevel=joaat("SET_MAX_WANTED_LEVEL"),
    ClearWantedLevel=joaat("CLEAR_PLAYER_WANTED_LEVEL"),GetWantedLevel=joaat("GET_PLAYER_WANTED_LEVEL"),
    SetEntityHealth=joaat("SET_ENTITY_HEALTH"),GetEntityMaxHealth=joaat("GET_ENTITY_MAX_HEALTH"),SetPedArmour=joaat("SET_PED_ARMOUR"),
    GetPlayerMaxArmour=joaat("GET_PLAYER_MAX_ARMOUR"),ClearPedBlood=joaat("CLEAR_PED_BLOOD_DAMAGE"),
    SetInfiniteAmmoClip=joaat("SET_PED_INFINITE_AMMO_CLIP"),GiveWeapon=joaat("GIVE_WEAPON_TO_PED"),IsWeaponValid=joaat("IS_WEAPON_VALID"),
    IsModelInCdimage=joaat("IS_MODEL_IN_CDIMAGE"),IsModelAVehicle=joaat("IS_MODEL_A_VEHICLE"),RequestModel=joaat("REQUEST_MODEL"),
    HasModelLoaded=joaat("HAS_MODEL_LOADED"),CreateVehicle=joaat("CREATE_VEHICLE"),SetPedIntoVehicle=joaat("SET_PED_INTO_VEHICLE"),
    VehicleNoLongerNeeded=joaat("SET_VEHICLE_AS_NO_LONGER_NEEDED"),ModelNoLongerNeeded=joaat("SET_MODEL_AS_NO_LONGER_NEEDED"),
    GetEntityCoords=joaat("GET_ENTITY_COORDS"),GetEntityHeading=joaat("GET_ENTITY_HEADING"),IsPedInAnyVehicle=joaat("IS_PED_IN_ANY_VEHICLE"),
    GetVehiclePedIsIn=joaat("GET_VEHICLE_PED_IS_IN"),SetVehicleFixed=joaat("SET_VEHICLE_FIXED"),
    SetVehicleDeformationFixed=joaat("SET_VEHICLE_DEFORMATION_FIXED"),SetVehicleDirt=joaat("SET_VEHICLE_DIRT_LEVEL"),
    SetVehicleOnGround=joaat("SET_VEHICLE_ON_GROUND_PROPERLY"),SetVehicleEngineOn=joaat("SET_VEHICLE_ENGINE_ON"),
    FirstBlipInfoId=joaat("GET_FIRST_BLIP_INFO_ID"),DoesBlipExist=joaat("DOES_BLIP_EXIST"),BlipInfoIdCoord=joaat("GET_BLIP_INFO_ID_COORD"),
    GroundZ=joaat("GET_GROUND_Z_FOR_3D_COORD"),SetPedCoordsKeepVehicle=joaat("SET_PED_COORDS_KEEP_VEHICLE"),
    RequestCollisionAtCoord=joaat("REQUEST_COLLISION_AT_COORD"),SetClockTime=joaat("SET_CLOCK_TIME"),PauseClock=joaat("PAUSE_CLOCK"),
    SetWeatherNowPersist=joaat("SET_WEATHER_TYPE_NOW_PERSIST"),ClearWeatherPersist=joaat("CLEAR_WEATHER_TYPE_PERSIST"),
    SuperJumpThisFrame=joaat("SET_SUPER_JUMP_THIS_FRAME"),RunSprintMultiplier=joaat("SET_RUN_SPRINT_MULTIPLIER_FOR_PLAYER"),
    SwimMultiplier=joaat("SET_SWIM_MULTIPLIER_FOR_PLAYER"),
    // v834 (thor-radio, the Quick Save button without the phone): SET_SAVE_MENU_ACTIVE (sub_82D7F9E8 -> sub_82D7E5E0: with arg 0 it does
    // nothing while the ped is in a vehicle; else the ped's velocities zeroed, then sub_825EBCE0(joaat("FE_MENU_VERSION_SAVEGAME") 809275cc,
    // 1, -1, 0) = the frontend's Save Game menu), GET_MISSION_FLAG, IS_AUTO_SAVE_IN_PROGRESS (refusals).
    SetSaveMenuActive=joaat("SET_SAVE_MENU_ACTIVE"),GetMissionFlag=joaat("GET_MISSION_FLAG"),IsAutoSaveInProgress=joaat("IS_AUTO_SAVE_IN_PROGRESS"),
    GetSelectedPedWeapon=joaat("GET_SELECTED_PED_WEAPON"),HasPedGotWeapon=joaat("HAS_PED_GOT_WEAPON"),SetCurrentPedWeapon=joaat("SET_CURRENT_PED_WEAPON"),   // v983 NextWeapon
    EarnFromRockstar=joaat("NETWORK_EARN_FROM_ROCKSTAR"),WalletBalance=joaat("NETWORK_GET_VC_WALLET_BALANCE"),   // v1153 AddCash (5a3733cc sub_82D69D90)
    BankBalance=joaat("NETWORK_GET_VC_BANK_BALANCE"),   // v1156: the earn credits the BANK (t-onlcash154: bank 100000, wallet 0)
    // v953 (Update 1 W1-5, OW E2 / SB Q5): IS_MODEL_VALID af8f8e9d -> sub_82DB5FC8 (1 argument: lwz r3,0(args) -> sub_82D9FC18 -> BOOL)
    IsModelValid=joaat("IS_MODEL_VALID"),
    // Update 1 P7 SwitchCharacter (Sandbox only). Argument use read from our recompiled handlers (lane switch-character NOTES.md):
    // CREATE_PED 0389ef71 -> sub_82DAE978: 8 words (ped type, model, x, y, z, heading, 2 BOOLs) -> ped handle;
    // CHANGE_PLAYER_PED be515485 -> sub_82DD7D20: 4 words (player, ped, 2 BOOLs) -> sub_82DD7490 (nothing when either is invalid);
    // SET_PLAYER_MODEL 774a4c54 -> sub_82DB0BD8: 2 words (player, model); SET_PED_DEFAULT_COMPONENT_VARIATION c866a984: 1 word (ped);
    // GET_ENTITY_MODEL dafcb3ec: 1 word -> hash; DOES_ENTITY_EXIST 3ac90869: 1 word; SET_ENTITY_AS_MISSION_ENTITY 5d1f9e0f: 3 words;
    // SET_PED_AS_NO_LONGER_NEEDED 9a388380 and DELETE_PED 13efb9a0: 1 word = a POINTER to the handle (lwz r3,0(r3) in the worker);
    // GET_RANDOM_EVENT_FLAG 794cc92c / IS_MINIGAME_IN_PROGRESS 53a95e13: none; IS_PED_RAGDOLL c833bbe1 / IS_PED_FALLING abf77334 /
    // IS_PED_IN_PARACHUTE_FREE_FALL cd71f11b: 1 word (ped) -> BOOL.
    GetEntityModel=joaat("GET_ENTITY_MODEL"),CreatePed=joaat("CREATE_PED"),ChangePlayerPed=joaat("CHANGE_PLAYER_PED"),
    SetPlayerModel=joaat("SET_PLAYER_MODEL"),SetPedDefaultVariation=joaat("SET_PED_DEFAULT_COMPONENT_VARIATION"),
    PedNoLongerNeeded=joaat("SET_PED_AS_NO_LONGER_NEEDED"),SetEntityAsMission=joaat("SET_ENTITY_AS_MISSION_ENTITY"),
    DeletePed=joaat("DELETE_PED"),DoesEntityExist=joaat("DOES_ENTITY_EXIST"),GetRandomEventFlag=joaat("GET_RANDOM_EVENT_FLAG"),
    IsMinigameInProgress=joaat("IS_MINIGAME_IN_PROGRESS"),IsPedRagdoll=joaat("IS_PED_RAGDOLL"),IsPedFalling=joaat("IS_PED_FALLING"),
    IsPedInParachuteFreeFall=joaat("IS_PED_IN_PARACHUTE_FREE_FALL"),
    // P7 review fixes (refusals and E5's log), argument use read from our recompiled handlers:
    // GET_INTERIOR_FROM_ENTITY 5c644614 -> sub_82D7D930: 1 word (entity) -> sub_822AC5F8, which answers 0 unless the entity is in an
    // interior (r30 = 0 on every path that does not find an interior location); IS_PED_CLIMBING bce03d35 -> sub_82DD1FF0 and
    // IS_PED_JUMPING 07e5bc0e -> sub_82DD1FB0: 1 word (ped) -> BOOL; IS_MOBILE_PHONE_CALL_ONGOING 4ed1400a -> sub_82D54080: none -> BOOL;
    // IS_PED_RINGTONE_PLAYING fe576ee4 -> sub_82D6CA70: 1 word (ped) -> BOOL (both as phone_call.h reads them);
    // IS_ENTITY_A_MISSION_ENTITY 2632e124 -> sub_82D77A50: 1 word (entity) -> BOOL (logged after a switch only).
    GetInteriorFromEntity=joaat("GET_INTERIOR_FROM_ENTITY"),IsPedClimbing=joaat("IS_PED_CLIMBING"),IsPedJumping=joaat("IS_PED_JUMPING"),
    IsMobilePhoneCallOngoing=joaat("IS_MOBILE_PHONE_CALL_ONGOING"),IsPedRingtonePlaying=joaat("IS_PED_RINGTONE_PLAYING"),
    IsEntityAMissionEntity=joaat("IS_ENTITY_A_MISSION_ENTITY"),
    // Update 1 OW Level 2 step 1b (Probe a0 = 1, the read-only net probe for D-NET1). Handlers (natives/registrations.json) and argument
    // use read from our recompiled code (v957 ppc_recomp.184.cpp):
    // NETWORK_IS_SIGNED_IN add0b40f -> sub_82DA5768: no argument read; result = (byte) sub_8253C548 = word 0x83B13BE4 != 0;
    // NETWORK_IS_SIGNED_ONLINE 6e5bdce2 -> sub_82DA57A0: no argument read (li r3,1 is ignored by sub_824A7CA0 = byte 0x83B13BA7 != 0);
    // NETWORK_HAVE_ONLINE_PRIVILEGES ef63bfdf -> sub_82DA5970: no argument read; sub_82267F78(-1, 1);
    // NETWORK_CAN_ACCESS_MULTIPLAYER a520b982 -> sub_82DA5A98: 1 word = a POINTER to the reason (lwz r10,8(r3); lwz r30,0(r10);
    //   sub_82543A90(word 0x83B13B04, &local); stw local,0(r30)) -> BOOL. sub_82543A90's whole body: reason = 1, return 0.
    NetworkIsSignedIn=joaat("NETWORK_IS_SIGNED_IN"),NetworkIsSignedOnline=joaat("NETWORK_IS_SIGNED_ONLINE"),
    NetworkHaveOnlinePrivileges=joaat("NETWORK_HAVE_ONLINE_PRIVILEGES"),NetworkCanAccessMultiplayer=joaat("NETWORK_CAN_ACCESS_MULTIPLAYER"),
    // OW L2 step 2 (D-NET2, v960), argument use read from v958 codegen: NETWORK_SESSION_HOST_SINGLE_PLAYER f3b1ca85 -> sub_82DA62F0
    // (ppc_recomp.184.cpp:2781): one word (lwz r3,0(args)) handed to sub_82F92910(arg, 1, 1), no result; NETWORK_SESSION_LEAVE_SINGLE_PLAYER
    // c692f86a -> sub_82D99B30 (ppc_recomp.182.cpp:9186): no argument, no result.
    NetworkSessionHostSinglePlayer=joaat("NETWORK_SESSION_HOST_SINGLE_PLAYER"),NetworkSessionLeaveSinglePlayer=joaat("NETWORK_SESSION_LEAVE_SINGLE_PLAYER");
}
// Every hash the trainer may call: the glue records their handlers when the game registers its natives.
inline constexpr std::uint32_t UsedNatives[]{
    n::PlayerId,n::PlayerPedId,n::IsPlayerPlaying,n::IsPauseMenuActive,n::IsCutsceneActive,n::IsSwitchInProgress,n::IsScreenFadedOut,
    n::IsEntityDead,n::NetworkGame,n::SetPlayerInvincible,n::SetWantedLevel,n::SetWantedLevelNow,n::SetMaxWantedLevel,n::ClearWantedLevel,
    n::GetWantedLevel,n::SetEntityHealth,n::GetEntityMaxHealth,n::SetPedArmour,n::GetPlayerMaxArmour,n::ClearPedBlood,
    n::SetInfiniteAmmoClip,n::GiveWeapon,n::IsWeaponValid,n::IsModelInCdimage,n::IsModelAVehicle,n::RequestModel,n::HasModelLoaded,
    n::CreateVehicle,n::SetPedIntoVehicle,n::VehicleNoLongerNeeded,n::ModelNoLongerNeeded,n::GetEntityCoords,n::GetEntityHeading,
    n::IsPedInAnyVehicle,n::GetVehiclePedIsIn,n::SetVehicleFixed,n::SetVehicleDeformationFixed,n::SetVehicleDirt,n::SetVehicleOnGround,
    n::SetVehicleEngineOn,n::FirstBlipInfoId,n::DoesBlipExist,n::BlipInfoIdCoord,n::GroundZ,n::SetPedCoordsKeepVehicle,
    n::RequestCollisionAtCoord,n::SetClockTime,n::PauseClock,n::SetWeatherNowPersist,n::ClearWeatherPersist,n::SuperJumpThisFrame,
    n::RunSprintMultiplier,n::SwimMultiplier,n::SetSaveMenuActive,n::GetMissionFlag,n::IsAutoSaveInProgress,n::IsModelValid,
    n::GetEntityModel,n::CreatePed,n::ChangePlayerPed,n::SetPlayerModel,n::SetPedDefaultVariation,n::PedNoLongerNeeded,n::SetEntityAsMission,
    n::DeletePed,n::DoesEntityExist,n::GetRandomEventFlag,n::IsMinigameInProgress,n::IsPedRagdoll,n::IsPedFalling,n::IsPedInParachuteFreeFall,
    n::GetInteriorFromEntity,n::IsPedClimbing,n::IsPedJumping,n::IsMobilePhoneCallOngoing,n::IsPedRingtonePlaying,n::IsEntityAMissionEntity,
    n::NetworkIsSignedIn,n::NetworkIsSignedOnline,n::NetworkHaveOnlinePrivileges,n::NetworkCanAccessMultiplayer,
    n::NetworkSessionHostSinglePlayer,n::NetworkSessionLeaveSinglePlayer,
    n::GetSelectedPedWeapon,n::HasPedGotWeapon,n::SetCurrentPedWeapon,   // v983 NextWeapon (registered: 82dfcb68 / 82dfc520 / 82e02ea0)
    n::EarnFromRockstar,n::WalletBalance,n::BankBalance};   // v1153 AddCash (+ v1156 bank)
inline bool used(std::uint32_t hash) noexcept {
    return std::find(std::begin(UsedNatives),std::end(UsedNatives),hash)!=std::end(UsedNatives);
}

// One call of a game native. Implemented by the glue (guest memory, the recompiled handler) and by the tests (mocks).
struct Natives {
    virtual ~Natives()=default;
    // n argument words (n <= 12); up to 3 result words in out (zeroed first). False: the native is not available (not registered).
    virtual bool call(std::uint32_t hash,const std::uint32_t* args,unsigned n,std::uint32_t* out)=0;
    // Guest scratch storage valid until the end of this tick: a zero-terminated copy of s / one word; 0 = no room.
    virtual std::uint32_t text(const char* s)=0;
    virtual std::uint32_t word(std::uint32_t value)=0;
    virtual std::uint32_t load(std::uint32_t address)=0;   // a scratch word back (big-endian guest word); OW L2 (v957): also a fixed
                                                           // game word the caller names and documents (net probe, activity thread words)
};
inline std::uint32_t bits(float f) noexcept {std::uint32_t w;std::memcpy(&w,&f,4);return w;}
inline float asFloat(std::uint32_t w) noexcept {float f;std::memcpy(&f,&w,4);return f;}

// Update 1 P8 (Online World Level 1): the Sandbox's online actions. online_world.h implements it; the glue hands it to the trainer only
// in Sandbox mode (null otherwise: the online actions then answer ErrArgs). Results: 0 running, 1 ok, 2 fail (detail written).
struct OnlineActions {
    virtual ~OnlineActions()=default;
    virtual bool characterActive() const noexcept=0;      // a freemode model is the player
    virtual bool creatorMenuOpen() const noexcept {return false;}   // v975 (G3): read from the app thread (an atomic behind it)
    virtual bool ownsScreenFade() const noexcept {return false;}   // v976: the place transition faded the screen (the gate ignores that fade)
    virtual bool validArgument(int action,int a0) const noexcept=0;
    virtual int begin(Natives& nat,int action,int a0,std::uint32_t player,std::uint32_t ped,std::uint32_t frame,char* detail,int cap) noexcept=0;
    virtual int step(Natives& nat,std::uint32_t player,std::uint32_t ped,std::uint32_t frame,char* detail,int cap) noexcept=0;
    virtual const char* abort(Natives& nat) noexcept=0;   // the gate closed: what was left ("not-applied", "applied", ...)
    // v957 (WAVE2-REPORT 2.1 item 3): the player ped a model change just made (0: none), taken once after each step. The trainer hands it
    // to the glue (notePedChange) so the hosting PLAYER_PED_ID result is rewritten, as after SwitchCharacter.
    virtual std::uint32_t takeNewPed() noexcept {return 0;}
};
// Update 1 OW Level 2 step 1c (action 19 Activity): the Sandbox's activity launcher. activities.h implements it; the glue hands it to
// the trainer only in Sandbox mode (null otherwise: action 19 then answers ErrArgs). request: 1 ok, 2 fail (detail written); the load
// and the start happen later on the Sandbox tick (activities.h Launcher::tick), logged as RENDER_TRAINER activity ... lines.
struct ActivityActions {
    virtual ~ActivityActions()=default;
    virtual bool validArgument(int a0,int a1) const noexcept=0;
    virtual int request(Natives& nat,int a0,int a1,char* detail,int cap) noexcept=0;
};

// PROPOSED (lane upd1-spoof/intro-cutscene, action 20 OnlineIntro): the Sandbox's intro cutscene player. online_intro.h implements it;
// the glue hands it to the trainer only in Sandbox mode with diag-online-local (null otherwise: action 20 then answers ErrArgs).
// begin / step: 0 running, 1 ok, 2 fail (detail written). abort: the gate closed for a reason the job does not own; what was left.
// pendingCleanup / cleanup: a cutscene request this job could not release yet (thread-bound REMOVE_CUTSCENE), retried every tick.
struct IntroActions {
    virtual ~IntroActions()=default;
    virtual bool validArgument(int a0,int a1) const noexcept=0;
    virtual int begin(Natives& nat,int a0,int a1,std::uint32_t player,std::uint32_t ped,bool onlineCharacter,std::uint32_t frame,char* detail,int cap) noexcept=0;
    virtual int step(Natives& nat,std::uint32_t player,std::uint32_t ped,std::uint32_t frame,char* detail,int cap) noexcept=0;
    virtual const char* abort(Natives& nat,std::uint32_t player,std::uint32_t ped,std::uint32_t frame) noexcept=0;
    virtual bool pendingCleanup() const noexcept=0;
    virtual void cleanup(Natives& nat,std::uint32_t frame) noexcept=0;
};

// PROPOSED (lane upd1-spoof/creator-route, action 21 DiscCreator): the disc's own character creator inside the Sandbox
// (creator_route.h Route). The glue hands it over only in Sandbox mode with files/private-trace/diag-online-local (null otherwise:
// action 21 then answers ErrArgs). request: 1 ok, 2 fail (detail written); the run itself happens on the Sandbox tick
// (RENDER_ONLINE creator ... lines).
struct DiscCreatorActions {
    static constexpr int CancelArg=1;   // a0 1: cancel, served at once by request() below, never queued (see cancel())
    virtual ~DiscCreatorActions()=default;
    virtual bool validArgument(int a0) const noexcept=0;
    virtual int request(Natives& nat,int a0,char* detail,int cap) noexcept=0;
    // Any thread; one atomic store, consumed by the route on the Sandbox tick. Not behind the trainer gate: the creator is a frontend
    // menu, so the gate reports pause-menu while it is up and would drop a queued cancel (blocked=pause-menu).
    virtual void cancel() noexcept=0;
};

// ---------------------------------------------------------------------------------------------------------------- the API's words
enum Action : int {
    Sync=0,            // re-read files/settings/trainer.txt; allowed while off
    Toggle=1,          // a0 = Toggles bit index, a1 = 0 off / 1 on
    Wanted=2,          // a0 = 0..5 stars, now
    Heal=3,            // full health, full armour, blood cleaned
    GiveWeapons=4,     // every weapon of WeaponList that the game knows, with ammo
    SpawnVehicle=5,    // a1 = 0: a0 = the model hash (joaat of the model name; any 32-bit pattern); a1 = 1: a0 = index into VehicleList
    FixVehicle=6,      // the current vehicle repaired, deformation fixed and cleaned
    TeleportWaypoint=7,// to the map's waypoint, onto the ground
    TeleportLandmark=8,// a0 = index into LandmarkList
    SetTime=9,         // a0 = hour 0..23, a1 = minute 0..59
    SetWeather=10,     // a0 = index into WeatherList (kept until ClearWeather)
    ClearWeather=11,   // back to the game's own weather cycle
    OpenSaveMenu=12,   // v834: the game's Save Game menu now (SET_SAVE_MENU_ACTIVE); allowed while the trainer is OFF (the Quick Save button);
                       // refused on a mission, with stars, during an autosave
    SwitchCharacter=13,// Update 1 P7: a0 = index into CharacterList (0 Michael, 1 Franklin, 2 Trevor); a1 = 0 the player-ped change (CREATE_PED +
                       // CHANGE_PLAYER_PED, the default), 1 SET_PLAYER_MODEL (experiment E5's comparison only; the apps send 0). SANDBOX ONLY:
                       // refused with ErrArgs in Story mode (the constructor's mode function; none = Story). Refused on a mission, in a random
                       // event or minigame, during an autosave, with the phone up / ringing / in a call, indoors, in a vehicle, falling /
                       // ragdoll / parachuting / climbing / jumping, while an online (freemode) character is the player (and by the gate:
                       // cutscene, switch, pause, fade).
    Probe=14,          // v953 (Update 1 W1-5, OW E2 + SB Q5): IS_MODEL_IN_CDIMAGE / IS_MODEL_VALID over ProbeList, one RENDER_TRAINER probe
                       // line each; read-only, so allowed while the trainer is OFF (like OpenSaveMenu). OW L2: a0 = 0 that model probe, a0 = 1
                       // the read-only net probe (ProbeNet: one RENDER_TRAINER probe net line); any other a0 = ErrArgs
    OnlineCharacter=15,// Update 1 P8 (OW E3): a0 = 0 male / 1 female freemode character, 2 = back to the story character; Sandbox mode only
                       // (ErrArgs otherwise); online_world.h Character
    OnlinePlace=16,    // Update 1 P8, ALLOCATION section 10 (accepted 2026-11-06): a0 = index into online_world.h PlaceList (the disc's
                       // multiplayer garages and apartments); Sandbox mode only
    CharacterLook=17,  // Update 1 OW Level 2 (character creator, ALLOCATION section 11): a0 = 0 apply the saved look / 1 save the edits /
                       // 2 discard the edits / 3 start editing; Sandbox mode only, a freemode model on (character_creator.h);
                       // PROPOSED a0 = 4: the in-game creator menu (creator_menu.h), one job that runs until the menu closes (save /
                       // exit / the gate closing: pause, cutscene, death, fade, switch); refused in a vehicle or on a mission
    CharacterEdit=18,  // ALLOCATION section 11: a0 = field << 16 | value, one field of the look changed and shown now (character_creator.h)
    Activity=19,       // ALLOCATION section 11 (freeroam activities): a0 = index into activities.h Catalog, a1 = 0 start / 1 stop; Sandbox
                       // mode only; Session-tier entries answer "needs-session" without a network game (activities.h)
    OnlineIntro=20,    // PROPOSED (lane upd1-spoof/intro-cutscene): a0 = 0 the Online intro (MP_INTRO_CONCAT) / 1 Lamar's drop-off
                       // (MP_INT_MCS_5_ALT1/2), a1 = 0 back to where the player stood / 1 stay where the cutscene leaves him; Sandbox
                       // only, an online (freemode) character the player, diag-online-local present (online_intro.h)
    DiscCreator=21,    // PROPOSED (lane upd1-spoof/creator-route): a0 = 0 start the disc's creator (maintransition UI 5, slot 0) / 1 cancel;
                       // Sandbox mode + diag-online-local only (creator_route.h); the created character is saved to the Sandbox
                       // generation and applied with 15 + 17 0
    NextWeapon=22,     // v983 (ALLOCATION section 13, approved): a0 = 0 the next owned weapon / 1 the previous one, in WEAPON_UNARMED +
                       // WeaponList order (the wheel's slot groups), wrapping; allowed while the trainer is OFF (a control, not a cheat);
                       // refused in a vehicle ("in-vehicle"); result "ok next-weapon weapon=<NAME>" / "fail next-weapon no-weapons"
    AddCash=23,        // v1153 (owner "we need money"; lead 21:4x; contract with android-qol 21:5x): a0 = dollars 1..1,000,000, a1 = 0. Only in our
                       // local session (NETWORK_IS_GAME_IN_PROGRESS + local_session::active() = the Online character), else "fail add-cash
                       // needs-session". The game's own earn path NETWORK_EARN_FROM_ROCKSTAR(a0) (sub_82D52360: its earn stats, the wallet add
                       // sub_82D51CC8, the cash event) - no memory writes. Result "ok add-cash <a0>"; the wallet before / after on its own line.
    ActionCount=24
};
enum ToggleBit : int {God=0,NeverWanted=1,InfiniteAmmo=2,SuperJump=3,FastRun=4,FastSwim=5,FreezeTime=6,ToggleCount=7};
inline const char* toggleName(int t) noexcept {
    static const char* const names[]={"god","never-wanted","infinite-ammo","super-jump","fast-run","fast-swim","freeze-time"};
    return t>=0 && t<ToggleCount?names[t]:"?";
}
inline const char* actionName(int a) noexcept {
    static const char* const names[]={"sync","toggle","wanted","heal","give-weapons","spawn-vehicle","fix-vehicle","teleport-waypoint",
        "teleport-landmark","set-time","set-weather","clear-weather","open-save-menu","switch-character","probe","online-character","online-place",
        "character-look","character-edit","activity","online-intro","disc-creator","next-weapon","add-cash"};
    return a>=0 && a<ActionCount?names[a]:"?";
}
// Return values of recomp_trainer_v1 (>= 0: the request's sequence number).
constexpr int ErrOff=-1,ErrArgs=-2,ErrFull=-3;
// Actions that run while the trainer is OFF (no cheat): the save menu (v834) and the read-only model probe (v953).
inline bool allowedWhileOff(int action) noexcept {return action==OpenSaveMenu || action==Probe || action==NextWeapon;}   // v983: + NextWeapon
// Probe's a0 (ALLOCATION section 11): 0 = the model probe (v953), 1 = the read-only net probe (D-NET1). 2 (the session probe) is
// allocated but NOT built: refused with ErrArgs until D-NET1 shows a non-null session object.
enum ProbeMode : int {ProbeModels=0,ProbeNet=1,ProbeSession=2,ProbeModeCount=3};
// D-NET2 (v960): the single-player session probe's timing (frames): host, poll GIP every SessionPollFrames for SessionHoldFrames, leave,
// then wait at most SessionLeaveFrames for GIP to fall back. The host argument is the plan's 2 (ONLINE-L2-PLAN.md section 4 row 10).
constexpr std::uint32_t SessionHostArg=2,SessionPollFrames=30,SessionHoldFrames=600,SessionLeaveFrames=300;
// The net probe's guest loads (no writes), each read from the recompiled worker that reads it (OW L2 plan section 3.3 b, FR NOTES 2.7):
//  SignedOnlineByte: sub_824A7CA0 (NETWORK_IS_SIGNED_ONLINE): lis -31823; lbz +15271;
//  RosIndexWord / RosByteBase: sub_82FA1248 (NETWORK_HAS_VALID_ROS_CREDENTIALS): index = word 0x8381CFE4 (lis -31870, -12316); index < 0
//    -> 0; else byte 0x83B12D10 + index (lis -31823, +11536; lbzx). The probe reads the byte only for index 0..3 (the four local users,
//    as sub_82267F78 loops); another index is logged with ros=-1 and no load;
//  NetObjectWord: the object word every session native passes (sub_82DA5A98 / sub_82DA5AF8: lis -31823; lwz +15108);
//  StatFailWord: sub_82AAF838 (STAT_CLOUD_SLOT_LOAD_FAILED): bit `slot` of the word at 0x83A31FB8 (lis -31837, +8064, +56).
namespace net {
constexpr std::uint32_t SignedOnlineByte=0x83B13BA7u,RosIndexWord=0x8381CFE4u,RosByteBase=0x83B12D10u,NetObjectWord=0x83B13B04u,
    StatFailWord=0x83A31FB8u;
constexpr std::int32_t RosUsers=4;
}
// v953 Probe: the models asked about (OW E2 / SB Q5), with a positive (adder, base game) and a negative (zentorno, a later download) control.
inline constexpr std::array<const char*,7> ProbeList{"mp_m_freemode_01","mp_f_freemode_01","player_zero","player_one","player_two",
    "adder","zentorno"};
// recomp_trainer_state_v1: toggles in bits 0..6, then these.
constexpr std::uint32_t StateEnabled=1u<<16,StateGameplay=1u<<17,StateBusy=1u<<18,StateWeatherLocked=1u<<19;
// v1086 (ALLOCATION state bits 27-28, android-qol's menu accent): the story character + 1 from the player ped's model, read by the
// glue every StoryCharFrames: 0 unknown / not a story model (freemode, any other model, no ped), 1 Michael, 2 Franklin, 3 Trevor.
constexpr std::uint32_t StoryCharShift=27,StoryCharMask=3u<<StoryCharShift,StoryCharFrames=30;
constexpr std::uint32_t storyCharacterOf(std::uint32_t model) noexcept {
    return model==joaat("player_zero")?1u:model==joaat("player_one")?2u:model==joaat("player_two")?3u:0u;}
// v1086: the glue's read (trainer_glue.inc, every StoryCharFrames on any frame tick: PLAYER_PED_ID, GET_ENTITY_MODEL) stores the value here;
// recomp_trainer_state_v1 serves it at bits 27-28 (Trainer::tick and state() are untouched: their native-call tests stay exact).
inline std::atomic<std::uint32_t> storyCharacter{0};
constexpr std::uint32_t storyBits(std::uint32_t value) noexcept {return (value&3u)<<StoryCharShift;}
// v1088 (lead after android-qol's t-qol19-v1086: plain Story kept bits 27-28 at 0 because the read sat behind the trainer's tickFrame, which
// is false when nothing is pending): the story read has its own cadence. Due on a frame that is a multiple of StoryCharFrames, once per
// frame (last = the frame of the previous read), whatever the trainer, the autopilot or the Sandbox want.
inline bool storyReadDue(std::uint32_t frame,std::atomic<std::uint32_t>& last) noexcept {
    return frame%StoryCharFrames==0u && last.exchange(frame,std::memory_order_relaxed)!=frame;}
constexpr std::uint32_t StateOnlineCharacter=1u<<20;   // Update 1 P8: a freemode model is the player (ALLOCATION: the next state bit)
constexpr std::uint32_t StateCreatorMenu=1u<<22;   // v975 (ALLOCATION section 12, G3): the in-game creator menu (17 4) is open
constexpr std::uint32_t StateSandbox=1u<<21;   // Update 1 P7 (PROPOSED allocation, lane switch-character NOTES section 4): this process
                                               // plays Sandbox, so SwitchCharacter may be offered

// ------------------------------------------------------------------------------------------------------------------ the lists
struct Vehicle {const char* label;const char* model;};
// Popular models of the base game (the 360 disc). A model the game does not have is refused at spawn time (IS_MODEL_IN_CDIMAGE).
inline constexpr std::array<Vehicle,24> VehicleList{{
    {"Adder","adder"},{"Entity XF","entityxf"},{"Infernus","infernus"},{"Cheetah","cheetah"},{"Banshee","banshee"},{"Comet","comet2"},
    {"Sultan","sultan"},{"Elegy RH8","elegy2"},{"Bati 801","bati"},{"Sanchez","sanchez"},{"Faggio","faggio2"},{"BMX","bmx"},
    {"Police Cruiser","police"},{"Ambulance","ambulance"},{"Fire Truck","firetruk"},{"Bus","bus"},{"Rhino Tank","rhino"},
    {"Buzzard","buzzard"},{"Maverick","maverick"},{"Cargobob","cargobob"},{"P-996 Lazer","lazer"},{"Duster","duster"},
    {"Jetmax","jetmax"},{"Seashark","seashark"}}};
struct Landmark {const char* label;float x,y,z;};
// v1020 TEST ONLY (lead: the shop tests cannot reach the shops; the autopilot will not walk 350 m and no landmark is a shop): the developer
// file files/private-trace/diag-trainer-online "tp X Y Z" arms this slot (trainer_glue.inc onlineSwitch) and queues TeleportLandmark with
// a0 = TestTeleportIndex, which then runs the ordinary teleport job (ground probe, Z as the height hint) to the armed point. Unarmed, the
// index is refused like any other out-of-range landmark; one arm = one trip. The app's landmark list is unchanged.
constexpr int TestTeleportIndex=1000;
inline std::atomic<bool> testTeleportArmed{false};
inline std::atomic<float> testTeleportX{0.f},testTeleportY{0.f},testTeleportZ{0.f};
// v1029 (t-doors7tp-20261107-1708: the ground probe put the ped on building 7's entrance canopy, z 48.0, above the door box 42.7..45.6):
// "tpz X Y Z" arms the slot as EXACT: the trip places the ped at X, Y, Z once (no ground probe) and requests collision there.
inline std::atomic<bool> testTeleportExact{false};
inline void armTestTeleport(float x,float y,float z,bool exact=false) noexcept {
    testTeleportExact.store(exact,std::memory_order_relaxed);
    testTeleportX.store(x,std::memory_order_relaxed);testTeleportY.store(y,std::memory_order_relaxed);testTeleportZ.store(z,std::memory_order_relaxed);
    testTeleportArmed.store(true,std::memory_order_release);
}
// Public map coordinates (approximate): the teleport probes the ground near z, so a few metres off is harmless.
inline constexpr std::array<Landmark,12> LandmarkList{{
    {"Michael's house",-852.4f,160.0f,65.6f},{"Franklin's house",7.4f,535.5f,176.0f},{"Trevor's trailer",1985.7f,3812.2f,32.2f},
    {"Maze Bank Tower roof",-75.0f,-818.2f,326.2f},{"Mount Chiliad",501.8f,5604.0f,797.9f},{"Los Santos Airport",-1102.3f,-2894.5f,13.9f},
    {"Sandy Shores airfield",1747.0f,3273.7f,41.1f},{"Fort Zancudo",-2047.4f,3132.1f,32.8f},{"Del Perro Pier",-1850.1f,-1231.8f,13.0f},
    {"Vinewood sign",711.4f,1198.1f,348.5f},{"Paleto Bay",-446.0f,6012.0f,31.7f},{"Los Santos Customs",-365.4f,-131.8f,37.9f}}};
inline constexpr std::array<const char*,13> WeatherList{"EXTRASUNNY","CLEAR","CLOUDS","SMOG","FOGGY","OVERCAST","RAIN","THUNDER",
    "CLEARING","NEUTRAL","SNOW","BLIZZARD","SNOWLIGHT"};
// Base-game weapons; IS_WEAPON_VALID filters any this build lacks.
inline constexpr std::array<const char*,36> WeaponList{"WEAPON_KNIFE","WEAPON_NIGHTSTICK","WEAPON_HAMMER","WEAPON_BAT","WEAPON_CROWBAR",
    "WEAPON_GOLFCLUB","WEAPON_PISTOL","WEAPON_COMBATPISTOL","WEAPON_APPISTOL","WEAPON_PISTOL50","WEAPON_MICROSMG","WEAPON_SMG",
    "WEAPON_ASSAULTSMG","WEAPON_ASSAULTRIFLE","WEAPON_CARBINERIFLE","WEAPON_ADVANCEDRIFLE","WEAPON_MG","WEAPON_COMBATMG",
    "WEAPON_PUMPSHOTGUN","WEAPON_SAWNOFFSHOTGUN","WEAPON_ASSAULTSHOTGUN","WEAPON_BULLPUPSHOTGUN","WEAPON_STUNGUN","WEAPON_SNIPERRIFLE",
    "WEAPON_HEAVYSNIPER","WEAPON_GRENADELAUNCHER","WEAPON_RPG","WEAPON_MINIGUN","WEAPON_GRENADE","WEAPON_STICKYBOMB",
    "WEAPON_SMOKEGRENADE","WEAPON_BZGAS","WEAPON_MOLOTOV","WEAPON_FIREEXTINGUISHER","WEAPON_PETROLCAN","WEAPON_FLARE"};
constexpr std::uint32_t WeaponAmmo=9999;
// P7: the three story characters. model = the public model name (IS_MODEL_IN_CDIMAGE 1 for all three on the disc: run A's Probe);
// pedType = the index of the name in the executable's ped-type name table (image 0x837B8AE0: PLAYER_0, PLAYER_1, NETWORK_PLAYER,
// PLAYER_2, CIVMALE, CIVFEMALE, COP, ...), so Trevor's PLAYER_2 is 3, not 2.
struct Character {const char* label;const char* model;std::uint32_t pedType;};
inline constexpr std::array<Character,3> CharacterList{{{"Michael","player_zero",0},{"Franklin","player_one",1},{"Trevor","player_two",3}}};
enum SwitchMethod : int {SwitchChangePed=0,SwitchSetModel=1,SwitchMethodCount=2};
// CHANGE_PLAYER_PED's two BOOLs (argument words 8 and 12, passed on to sub_829D87A0 as r5 / r6). Their meaning is not read yet: both
// FALSE until experiment E5 says otherwise (NOTES.md, open question 1).
constexpr std::uint32_t ChangePedFlag2=0,ChangePedFlag3=0;
// List 6 (Online places) and 7 (character limits) are allocated but have no list yet (-1). List 8 (Activities, activities.h Catalog)
// is answered by the glue (recomp_trainer_list_v1), which includes activities.h; this header cannot (activities.h includes it).
enum List : int {Vehicles=0,Landmarks=1,Weathers=2,Toggles=3,Actions=4,Characters=5,OnlinePlaces=6,CharacterLimits=7,Activities=8};
// The label of one list entry (for the app's menu): the list's size, writing the label when index is in range.
inline int listEntry(int list,int index,char* out,int cap) noexcept {
    const char* label=nullptr;int count=0;
    switch(list) {
    case Vehicles: count=int(VehicleList.size());if(index>=0 && index<count)label=VehicleList[std::size_t(index)].label;break;
    case Landmarks: count=int(LandmarkList.size());if(index>=0 && index<count)label=LandmarkList[std::size_t(index)].label;break;
    case Weathers: count=int(WeatherList.size());if(index>=0 && index<count)label=WeatherList[std::size_t(index)];break;
    case Toggles: count=ToggleCount;if(index>=0 && index<count)label=toggleName(index);break;
    case Actions: count=ActionCount;if(index>=0 && index<count)label=actionName(index);break;
    case Characters: count=int(CharacterList.size());if(index>=0 && index<count)label=CharacterList[std::size_t(index)].label;break;
    default: return -1;
    }
    if(label && out && cap>0)std::snprintf(out,std::size_t(cap),"%s",label);
    return count;
}

// files/settings/trainer.txt: words "key=value"; "trainer=on" (or 1 / true / yes) enables, anything else = off.
inline bool parseEnabled(const char* text,std::size_t n) noexcept {
    std::size_t i=0;bool on=false;
    while(i<n) {
        while(i<n && (text[i]==' ' || text[i]=='\n' || text[i]=='\r' || text[i]=='\t'))++i;
        std::size_t j=i;while(j<n && text[j]!=' ' && text[j]!='\n' && text[j]!='\r' && text[j]!='\t')++j;
        char word[48]{};const std::size_t len=std::min<std::size_t>(j-i,sizeof word-1);
        for(std::size_t k=0;k<len;++k){char c=text[i+k];if(c>='A' && c<='Z')c=char(c+32);word[k]=c;}
        if(!std::strncmp(word,"trainer=",8)) {
            const char* v=word+8;
            on=!std::strcmp(v,"on") || !std::strcmp(v,"1") || !std::strcmp(v,"true") || !std::strcmp(v,"yes");
        }
        i=j;
    }
    return on;
}

// ------------------------------------------------------------------------------------------------------------------ the engine
struct Request {int action=0,a0=0,a1=0,a2=0;int seq=0;std::uint32_t blockedFrames=0;};
constexpr std::size_t QueueMax=16;
constexpr std::uint32_t BlockedFramesMax=150;      // ~5 s at 30 fps: a request blocked this long is dropped
constexpr std::uint32_t ModelWaitFramesMax=300;    // a model that does not load in ~10 s: the spawn fails
constexpr std::uint32_t CollisionWaitFrames=3;     // frames between a teleport probe's move and its ground test
constexpr std::uint32_t WaypointSprite=8;          // the map waypoint's blip sprite
inline constexpr std::array<float,17> ProbeHeights{100,150,50,0,200,250,300,350,400,450,500,550,600,650,700,750,800};

class Trainer {
public:
    using LogFn=void(*)(const char* line);
    using ModeFn=bool(*)();   // P7: true = this process plays Sandbox (the glue: recomp::sandbox::active); none = Story
    explicit Trainer(LogFn log=nullptr,ModeFn sandbox=nullptr) noexcept:log_(log),sandbox_(sandbox) {}
    bool sandboxMode() const noexcept {return sandbox_ && sandbox_();}

    // ---- the app's side (any thread)
    void setEnabled(bool on) noexcept {
        const bool was=enabled_.exchange(on,std::memory_order_acq_rel);
        if(was!=on)logf("RENDER_TRAINER enabled=%d",on?1:0);
        if(!on) { // everything back off at the next safe point: toggles off, queue dropped
            std::lock_guard l(m_); // (v834: a pending save-menu request stays)
            for(auto it=queue_.begin();it!=queue_.end();)if(!allowedWhileOff(it->action))it=queue_.erase(it);else ++it;
            toggles_.store(0,std::memory_order_relaxed);
        }
        refreshWork();
    }
    bool enabled() const noexcept {return enabled_.load(std::memory_order_acquire);}
    // A request: >= 0 its sequence number, else ErrOff / ErrArgs / ErrFull.
    int request(int action,int a0,int a1,int a2) noexcept {
        if(action==Sync)return 0; // (handled by the export: it re-reads the settings file)
        if(!enabled() && !allowedWhileOff(action))return ErrOff; // (v834: the save menu is not a cheat; v953: nor is the read-only probe)
        if(!valid(action,a0,a1))return ErrArgs;
        if(action==SwitchCharacter && !sandboxMode()) { // P7: never in Story mode (the story's own character state stays the story's)
            logf("RENDER_TRAINER refused action=%s reason=story-mode",actionName(action));
            return ErrArgs;
        }
        if(online(action)) { // Update 1 P8: Sandbox mode only, and the argument checked by the online side
            OnlineActions* o=online_.load(std::memory_order_acquire);
            if(!o || !o->validArgument(action,a0))return ErrArgs;
        }
        if(action==OnlineIntro) { // PROPOSED: Sandbox mode only (the glue sets it there, with diag-online-local), arguments checked
            IntroActions* i=intro_.load(std::memory_order_acquire);
            if(!sandboxMode() || !i || !i->validArgument(a0,a1))return ErrArgs;
        }
        if(action==DiscCreator) { // PROPOSED: Sandbox mode + diag-online-local only (the glue sets it there), a0 checked by the route
            DiscCreatorActions* d=discCreator_.load(std::memory_order_acquire);
            if(!d || !sandboxMode() || !d->validArgument(a0))return ErrArgs;
            if(a0==DiscCreatorActions::CancelArg) { // served here, outside the gated queue (the creator's frontend closes the gate)
                const int seq=++seq_;
                logf("RENDER_TRAINER request seq=%d action=%s a0=%d a1=%d direct",seq,actionName(action),a0,a1);
                d->cancel();
                result(seq,"ok",actionName(action),"cancel-requested");
                return seq;
            }
        }
        if(action==Activity) { // OW L2 1c: Sandbox mode only (the glue sets it there), the arguments checked by the launcher
            ActivityActions* a=activities_.load(std::memory_order_acquire);
            if(!a || !a->validArgument(a0,a1))return ErrArgs;
        }
        if(action==Toggle) { // toggles are state, not queued work
            const std::uint32_t bit=1u<<a0;
            if(a1)toggles_.fetch_or(bit,std::memory_order_acq_rel);else toggles_.fetch_and(~bit,std::memory_order_acq_rel);
            const int seq=++seq_;
            logf("RENDER_TRAINER request seq=%d action=toggle %s=%d",seq,toggleName(a0),a1?1:0);
            refreshWork();
            return seq;
        }
        std::lock_guard l(m_);
        if(queue_.size()>=QueueMax)return ErrFull;
        Request r{action,a0,a1,a2,++seq_,0};
        queue_.push_back(r);
        logf("RENDER_TRAINER request seq=%d action=%s a0=%d a1=%d",r.seq,actionName(action),a0,a1);
        work_.store(true,std::memory_order_release);
        return r.seq;
    }
    std::uint32_t state() const noexcept {
        std::uint32_t s=toggles_.load(std::memory_order_relaxed)&((1u<<ToggleCount)-1);
        if(enabled())s|=StateEnabled;
        if(gameplay_.load(std::memory_order_relaxed))s|=StateGameplay;
        if(busy_.load(std::memory_order_relaxed))s|=StateBusy;
        if(weatherLocked_.load(std::memory_order_relaxed))s|=StateWeatherLocked;
        if(const OnlineActions* o=online_.load(std::memory_order_acquire);o && o->characterActive())s|=StateOnlineCharacter;
        if(sandboxMode())s|=StateSandbox;
        if(const OnlineActions* o=online_.load(std::memory_order_acquire);o && o->creatorMenuOpen())s|=StateCreatorMenu;   // v975 (G3)
        if(::recomp::online_leave::done.load(std::memory_order_acquire))s|=1u<<26;   // v1085: ALLOCATION bit 26 StateOnlineLeaveDone
        return s;
    }
    // The last result line ("seq=N ok|fail|blocked|dropped ...") and its sequence number.
    int status(char* out,int cap) const noexcept {
        std::lock_guard l(m_);
        if(out && cap>0)std::snprintf(out,std::size_t(cap),"%s",status_);
        return statusSeq_;
    }
    // P7: after a completed switch, the player ped before and after (once; script thread). The glue rewrites the VM's PLAYER_PED_ID
    // result from old to new, so the script whose call hosted the tick does not go on with the old ped.
    bool takePedChange(std::uint32_t& oldPed,std::uint32_t& newPed) noexcept {
        if(!pedChange_)return false;
        oldPed=pedChangeOld_;newPed=pedChangeNew_;pedChange_=false;return true;
    }
    // A save was committed (the save store): logged when toggles are on (the owner allows it; the log records it).
    void noteSave(const char* name) noexcept {
        const auto t=toggles_.load(std::memory_order_relaxed);
        if(enabled() && t)logf("RENDER_TRAINER save name=%s toggles=%#x (saved with trainer toggles on)",name?name:"?",t);
    }

    // Update 1 P8: the Sandbox's online actions (the glue, in Sandbox mode only; nullptr = none). The object outlives the trainer.
    void setOnline(OnlineActions* o) noexcept {online_.store(o,std::memory_order_release);}
    static constexpr bool online(int action) noexcept {return action==OnlineCharacter || action==OnlinePlace || action==CharacterLook || action==CharacterEdit;}
    // OW L2 1c: the Sandbox's activity launcher (the glue, in Sandbox mode only; nullptr = none). The object outlives the trainer.
    void setActivities(ActivityActions* a) noexcept {activities_.store(a,std::memory_order_release);}
    // PROPOSED (action 20): the Sandbox's intro player (the glue, Sandbox mode + diag-online-local only; nullptr = none).
    void setIntro(IntroActions* i) noexcept {intro_.store(i,std::memory_order_release);refreshWork();}
    // PROPOSED (action 21): the disc creator route (the glue, Sandbox mode + diag-online-local only; nullptr = none).
    void setDiscCreator(DiscCreatorActions* d) noexcept {discCreator_.store(d,std::memory_order_release);}

    // ---- the script thread's side
    // Anything to do? (one relaxed load: the glue's test before it builds a tick)
    bool wanted() const noexcept {return work_.load(std::memory_order_relaxed);}
    // One tick at the safe point (at most once per game frame; the glue checks the frame counter).
    void tick(Natives& nat,std::uint32_t frame) noexcept {
        lastFrame_=frame;
        if(!enabled()) { // turned off: a running job ends, owed off actions run (in gameplay)
            if(job_.kind!=Job::None)finishJob(nat,false,"trainer-off",true);
            if(applied_)offAll(nat);
            bool pending=false;{std::lock_guard l(m_);pending=!queue_.empty();}
            if(pending) { // v834: a save-menu request (the only kind queued while off)
                const Gate g=gate(nat);
                if(!g.ok)ageQueue(g.reason);
                else {Request r{};bool have=false;
                    {std::lock_guard l(m_);if(!queue_.empty()){r=queue_.front();queue_.pop_front();have=true;}}
                    if(have && allowedWhileOff(r.action))start(nat,g,r,frame);}
            }
            publishScriptState();return;
        }
        const Gate g=gate(nat);
        gameplay_.store(g.ok,std::memory_order_relaxed);
        if(IntroActions* i=intro_.load(std::memory_order_acquire);i && job_.kind!=Job::Intro && i->pendingCleanup())
            i->cleanup(nat,frame);   // PROPOSED: a held cutscene request released on a frame its own thread hosts, gate or not
        if(!g.ok && job_.kind==Job::Intro && introOwns(g.reason)) { // PROPOSED: its own cutscene / fade; a pause only holds it
            // the gate stops at its first reason, so the two checks after "cutscene" / "faded-out" are made here
            const char* fatal=call1(nat,n::IsSwitchInProgress,{})?"character-switch":call1(nat,n::IsEntityDead,{job_.ped})?"player-dead":nullptr;
            if(fatal){finishJob(nat,false,fatal,true);ageQueue(fatal);publishScriptState();return;}
            if(std::strcmp(g.reason,"pause-menu")!=0)runJob(nat,g,frame);
            publishScriptState();return;
        }
        if(!g.ok && job_.kind==Job::Session && std::strcmp(g.reason,"network-game")==0) { // D-NET2: its own session (GIP = 1)
            runJob(nat,g,frame);publishScriptState();return;
        }
        if(!g.ok) {
            if(job_.kind!=Job::None)finishJob(nat,false,g.reason,true);
            ageQueue(g.reason);
            publishScriptState();
            return;
        }
        applyToggles(nat,g);
        if(job_.kind!=Job::None)runJob(nat,g,frame);
        else {
            Request r{};bool have=false;
            {std::lock_guard l(m_);if(!queue_.empty()){r=queue_.front();queue_.pop_front();have=true;}}
            if(have)start(nat,g,r,frame);
        }
        publishScriptState();
    }

private:
    struct Gate {bool ok=false;const char* reason="";std::uint32_t player=0,ped=0;};
    struct Job {
        enum Kind:std::uint8_t {None,Spawn,Teleport,Online,Switch,Session,Intro} kind=None;
        std::uint32_t player=0,ped=0;   // Intro (PROPOSED): the player and ped at the start (the gate may report none later)
        int seq=0;std::uint32_t model=0,startFrame=0,stepFrame=0;unsigned step=0;
        float x=0,y=0,zHint=0;bool hasHint=false,strictHint=false;float fromX=0,fromY=0,fromZ=0;const char* what="";   // v1030: strictHint = the test slot
        int character=0,method=0;   // Switch
        bool hosted=false,left=false,leaveTried=false;int gipBefore=-1,gipEnd=-1;std::uint32_t upFrame=0,leaveFrame=0,lastPoll=0;   // Session (D-NET2)
    };

    static bool valid(int action,int a0,int a1) noexcept {
        switch(action) {
        case Toggle: return a0>=0 && a0<ToggleCount && (a1==0 || a1==1);
        case Wanted: return a0>=0 && a0<=5;
        case SpawnVehicle: return a1==0 || (a1==1 && a0>=0 && a0<int(VehicleList.size()));
        case TeleportLandmark: return (a0>=0 && a0<int(LandmarkList.size())) || (a0==TestTeleportIndex && testTeleportArmed.load(std::memory_order_acquire));   // v1020 test slot
        case SetTime: return a0>=0 && a0<=23 && a1>=0 && a1<=59;
        case SetWeather: return a0>=0 && a0<int(WeatherList.size());
        case SwitchCharacter: return a0>=0 && a0<int(CharacterList.size()) && a1>=0 && a1<SwitchMethodCount;
        case Heal: case GiveWeapons: case FixVehicle: case TeleportWaypoint: case ClearWeather: case OpenSaveMenu: return true;
        case NextWeapon: return (a0==0 || a0==1) && a1==0;   // v983
        case AddCash: return a0>=1 && a0<=1000000 && a1==0;   // v1153
        case Probe: return a0>=0 && a0<ProbeModeCount;   // (OW L2: 0 models, 1 net, 2 the session probe: v960, Sandbox only)
        case OnlineCharacter: case OnlinePlace: case CharacterLook: case CharacterEdit: return true;   // (their argument: OnlineActions::validArgument)
        case Activity: return true;   // (its arguments: ActivityActions::validArgument)
        case OnlineIntro: return true;   // (its arguments: IntroActions::validArgument)
        case DiscCreator: return true;   // PROPOSED (its argument: DiscCreatorActions::validArgument, 0 start / 1 cancel)
        default: return false;
        }
    }
    // Called by both threads: reads atomics only. The script thread publishes busy_ / owed_ first (publishScriptState).
    void refreshWork() noexcept {
        bool w=busy_.load(std::memory_order_relaxed) || owed_.load(std::memory_order_relaxed); // a job, or off actions still owed
        if(!w && enabled())w=toggles_.load(std::memory_order_relaxed)!=0;
        if(!w)if(const IntroActions* i=intro_.load(std::memory_order_acquire))w=i->pendingCleanup();   // PROPOSED: a release still owed
        if(!w){std::lock_guard l(m_);w=!queue_.empty();} // (v834: off, the queue holds save-menu requests only)
        work_.store(w,std::memory_order_release);
    }
    void publishScriptState() noexcept {
        busy_.store(job_.kind!=Job::None,std::memory_order_relaxed);owed_.store(applied_!=0,std::memory_order_relaxed);
        refreshWork();
    }
    template<class... A> void logf(const char* f,A... a) const noexcept {
        if(!log_)return;
        char line[240];std::snprintf(line,sizeof line,f,a...);log_(line);
    }
    void result(int seq,const char* kind,const char* what,const char* detail) noexcept {
        {
            std::lock_guard l(m_);
            std::snprintf(status_,sizeof status_,"seq=%d %s %s%s%s",seq,kind,what,detail && *detail?" ":"",detail?detail:"");
            statusSeq_=seq;
        }
        logf("RENDER_TRAINER result seq=%d %s %s%s%s",seq,kind,what,detail && *detail?" ":"",detail?detail:"");
    }

    // A native returning one word (0 when it is unavailable: the callers treat 0 as "no").
    static std::uint32_t call1(Natives& nat,std::uint32_t hash,std::initializer_list<std::uint32_t> a,bool* ok=nullptr) noexcept {
        std::uint32_t out[3]{};const bool r=nat.call(hash,a.begin(),unsigned(a.size()),out);
        if(ok)*ok=r;
        return r?out[0]:0u;
    }
    static void call0(Natives& nat,std::uint32_t hash,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};(void)nat.call(hash,a.begin(),unsigned(a.size()),out);
    }

    // v978: a plain-text help box (freemode's pattern: BEGIN_TEXT_COMMAND_DISPLAY_HELP("STRING") + the literal + END(0, 0, 1, 5000)).
    // The three natives are registered through the property doors' list (the glue's nativeHandler searches every list).
    static void tellPlayer(Natives& nat,const char* text) noexcept {
        const std::uint32_t label=nat.text("STRING"),body=nat.text(text);if(!label || !body)return;
        std::uint32_t out[3]{};const std::uint32_t a1[1]{label},a2[1]{body},a3[4]{0,0,1,5000};
        if(!nat.call(joaat("BEGIN_TEXT_COMMAND_DISPLAY_HELP"),a1,1,out))return;
        (void)nat.call(joaat("ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME"),a2,1,out);
        (void)nat.call(joaat("END_TEXT_COMMAND_DISPLAY_HELP"),a3,4,out);
    }
    bool ownFade() const noexcept {const OnlineActions* o=online_.load(std::memory_order_acquire);return (o && o->ownsScreenFade()) || ::recomp::online_curtain::covering.load(std::memory_order_acquire);}   // v976; v1056 the Online curtain
    Gate gate(Natives& nat) noexcept {
        Gate g;bool ok=false;
        g.player=call1(nat,n::PlayerId,{},&ok);
        if(!ok){g.reason="natives-unavailable";return g;}
        g.ped=call1(nat,n::PlayerPedId,{});
        if(call1(nat,n::NetworkGame,{}) && !::recomp::local_session::active()){g.reason="network-game";return g;}   // v1013: except the L1 local session
        if(!call1(nat,n::IsPlayerPlaying,{g.player})){g.reason="no-player";return g;}
        if(!g.ped){g.reason="no-ped";return g;}
        if(call1(nat,n::IsPauseMenuActive,{})){g.reason="pause-menu";return g;}
        if(call1(nat,n::IsCutsceneActive,{})){g.reason="cutscene";return g;}
        if(call1(nat,n::IsSwitchInProgress,{})){g.reason="character-switch";return g;}
        if(call1(nat,n::IsScreenFadedOut,{}) && !ownFade()){g.reason="faded-out";return g;}   // v976: not our own place transition's fade
        if(call1(nat,n::IsEntityDead,{g.ped})){g.reason="player-dead";return g;}
        g.ok=true;return g;
    }

    void applyToggles(Natives& nat,const Gate& g) noexcept {
        const std::uint32_t t=toggles_.load(std::memory_order_acquire);
        const auto on=[&](int b){return (t>>b)&1u;};
        // turned off since they were applied: their off action, once
        const std::uint32_t off=applied_&~t;
        if(off)offToggles(nat,g,off);
        if(on(God))call0(nat,n::SetPlayerInvincible,{g.player,1});
        if(on(NeverWanted)) {
            if(call1(nat,n::GetWantedLevel,{g.player}))call0(nat,n::ClearWantedLevel,{g.player});
            call0(nat,n::SetMaxWantedLevel,{0});
        }
        if(on(InfiniteAmmo))call0(nat,n::SetInfiniteAmmoClip,{g.ped,1});
        if(on(SuperJump))call0(nat,n::SuperJumpThisFrame,{g.player});
        if(on(FastRun))call0(nat,n::RunSprintMultiplier,{g.player,bits(1.49f)});
        if(on(FastSwim))call0(nat,n::SwimMultiplier,{g.player,bits(1.49f)});
        if(on(FreezeTime))call0(nat,n::PauseClock,{1});
        const std::uint32_t newly=t&~applied_;
        for(int b=0;b<ToggleCount;++b)if((newly>>b)&1u)logf("RENDER_TRAINER toggle %s=on applied",toggleName(b));
        applied_=t;
    }
    void offToggles(Natives& nat,const Gate& g,std::uint32_t off) noexcept {
        const auto has=[&](int b){return (off>>b)&1u;};
        if(has(God))call0(nat,n::SetPlayerInvincible,{g.player,0});
        if(has(NeverWanted))call0(nat,n::SetMaxWantedLevel,{5});
        if(has(InfiniteAmmo))call0(nat,n::SetInfiniteAmmoClip,{g.ped,0});
        if(has(FastRun))call0(nat,n::RunSprintMultiplier,{g.player,bits(1.0f)});
        if(has(FastSwim))call0(nat,n::SwimMultiplier,{g.player,bits(1.0f)});
        if(has(FreezeTime))call0(nat,n::PauseClock,{0});
        for(int b=0;b<ToggleCount;++b)if((off>>b)&1u)logf("RENDER_TRAINER toggle %s=off applied",toggleName(b));
        applied_&=~off;
    }
    // The trainer was turned off: every applied toggle's off action (only in gameplay, like every action), the weather released.
    void offAll(Natives& nat) noexcept {
        const Gate g=gate(nat);
        if(g.ok)offToggles(nat,g,applied_);
    }

    void ageQueue(const char* reason) noexcept {
        std::lock_guard l(m_);
        for(auto it=queue_.begin();it!=queue_.end();) {
            if(++it->blockedFrames>=BlockedFramesMax) {
                const int seq=it->seq;const int action=it->action;it=queue_.erase(it);
                std::snprintf(status_,sizeof status_,"seq=%d dropped %s blocked=%s",seq,actionName(action),reason);statusSeq_=seq;
                logf("RENDER_TRAINER result seq=%d dropped %s blocked=%s",seq,actionName(action),reason);
            } else ++it;
        }
    }

    void start(Natives& nat,const Gate& g,const Request& r,std::uint32_t frame) noexcept {
        const char* what=actionName(r.action);
        switch(r.action) {
        case Wanted:
            if(r.a0>0 && (toggles_.load(std::memory_order_relaxed)>>NeverWanted)&1u){result(r.seq,"fail",what,"never-wanted-is-on");return;}
            if(r.a0==0)call0(nat,n::ClearWantedLevel,{g.player});
            else {call0(nat,n::SetWantedLevel,{g.player,std::uint32_t(r.a0),0});call0(nat,n::SetWantedLevelNow,{g.player,0});}
            {char d[16];std::snprintf(d,sizeof d,"stars=%d",r.a0);result(r.seq,"ok",what,d);}
            return;
        case Heal: {
            const std::uint32_t maxHealth=call1(nat,n::GetEntityMaxHealth,{g.ped});
            if(maxHealth)call0(nat,n::SetEntityHealth,{g.ped,maxHealth});
            const std::uint32_t maxArmour=call1(nat,n::GetPlayerMaxArmour,{g.player});
            call0(nat,n::SetPedArmour,{g.ped,maxArmour});
            call0(nat,n::ClearPedBlood,{g.ped});
            char d[40];std::snprintf(d,sizeof d,"health=%u armour=%u",maxHealth,maxArmour);result(r.seq,"ok",what,d);
            return;}
        case GiveWeapons: {
            const unsigned given=giveWeapons(nat,g.ped);
            char d[24];std::snprintf(d,sizeof d,"weapons=%u",given);result(r.seq,given?"ok":"fail",what,d);
            return;}
        case FixVehicle: {
            if(!call1(nat,n::IsPedInAnyVehicle,{g.ped,0})){result(r.seq,"fail",what,"not-in-a-vehicle");return;}
            const std::uint32_t veh=call1(nat,n::GetVehiclePedIsIn,{g.ped,0});
            if(!veh){result(r.seq,"fail",what,"no-vehicle");return;}
            call0(nat,n::SetVehicleFixed,{veh});call0(nat,n::SetVehicleDeformationFixed,{veh});call0(nat,n::SetVehicleDirt,{veh,bits(0.0f)});
            result(r.seq,"ok",what,"");
            return;}
        case SetTime:
            call0(nat,n::SetClockTime,{std::uint32_t(r.a0),std::uint32_t(r.a1),0});
            {char d[24];std::snprintf(d,sizeof d,"time=%02d:%02d",r.a0,r.a1);result(r.seq,"ok",what,d);}
            return;
        case SetWeather: {
            const char* name=WeatherList[std::size_t(r.a0)];
            const std::uint32_t s=nat.text(name);
            if(!s){result(r.seq,"fail",what,"no-scratch");return;}
            call0(nat,n::SetWeatherNowPersist,{s});weatherLocked_.store(true,std::memory_order_relaxed);
            result(r.seq,"ok",what,name);
            return;}
        case ClearWeather:
            call0(nat,n::ClearWeatherPersist,{});weatherLocked_.store(false,std::memory_order_relaxed);
            result(r.seq,"ok",what,"");
            return;
        case Probe: { // v953: read-only model presence (OW E2: the freemode peds; SB Q5: the story characters; adder / zentorno = controls)
            if(r.a0==ProbeNet){netProbe(nat,r.seq,what);return;}   // OW L2 1b (D-NET1)
            if(r.a0==ProbeSession) { // OW L2 step 2 (D-NET2, v960): the game's own natives only; no global write, no stub
                if(!sandboxMode()){result(r.seq,"fail",what,"story-mode");return;}
                bool gipOk=false;const std::uint32_t gip=call1(nat,n::NetworkGame,{},&gipOk);
                std::uint32_t out[3]{};const std::uint32_t arg[1]{SessionHostArg};
                if(!nat.call(n::NetworkSessionHostSinglePlayer,arg,1,out)){result(r.seq,"fail",what,"natives-missing");return;}
                job_=Job{};job_.kind=Job::Session;job_.seq=r.seq;job_.startFrame=frame;job_.lastPoll=frame;job_.what=what;job_.hosted=true;
                job_.gipBefore=gipOk?int(gip!=0):-1;
                logf("RENDER_TRAINER probe session host seq=%d arg=%u gip_before=%d",r.seq,SessionHostArg,job_.gipBefore);
                { // v982 (diagnostic, read-only). CORRECTION (v989): host_ret is always 0 and says nothing: GuestNatives::call zeroes the
                  // result area and sub_82DA62F0 tail-calls sub_82F92910 -> sub_83061D70 without storing to [ctx+0] (that worker returns 0
                  // on every path anyway; see DISCOVERIES 2026-11-07 "the disc executable has no session host/join code").
                  // state = [netobj+0x10BB0] (gate 1: < 6); record = bytes +20..+23 of the account object 0x8381E258 (gate 4/5: +20 ready,
                  // +21 blocked, +22 name[0]); creds = the NETWORK_HAS_VALID_ROS_CREDENTIALS index word and its byte (index 0..3 only)
                    const std::uint32_t netobj=nat.load(net::NetObjectWord),state=netobj?nat.load(netobj+0x10BB0u):0xFFFFFFFFu;
                    const std::uint32_t rec=nat.load(0x8381E258u+20u);const std::int32_t idx=std::int32_t(nat.load(net::RosIndexWord));
                    const std::int32_t cred=(idx>=0 && idx<net::RosUsers)?std::int32_t(nat.load(net::RosByteBase+std::uint32_t(idx)&~3u)>>(8*(3-((net::RosByteBase+std::uint32_t(idx))&3u)))&0xFF):-1;
                    // v983: + the byte the worker tests last (sub_83061D70 after sub_8326C8C0: [netobj+0x10EBC] & 0x80, else return 0)
                    const std::uint32_t task=netobj?nat.load(netobj+0x10EBCu):0xFFFFFFFFu;
                    logf("RENDER_TRAINER probe session hostdiag seq=%d host_ret=%u netobj=%08x state=%u record=%08x creds_index=%d creds=%d task_word=%08x",
                         r.seq,out[0],netobj,state,rec,idx,cred,task);
                    // v989 E1 (lead-0607 session.md, read-only): what an offline host would need. Session / status pointers read at run
                    // time (sub_8306B0A0 swaps them); the network player manager 0x8381CF1C: +128 local net player (its +140 word and
                    // +41 byte), +280 pending, +292 connected, +728 active mask; the gamer index 0x8381CFE4 (0..3 for HostOffline);
                    // the byte 0x83C00F61. Pointers are dereferenced only when non-null, aligned and >= 0x40000000.
                    const auto ok=[](std::uint32_t a){return a>=0x40000000u && !(a&3u);};
                    const auto byteAt=[&](std::uint32_t a)->int{return int((nat.load(a&~3u)>>(8*(3-(a&3u))))&0xFFu);};
                    const std::uint32_t sns=ok(netobj)?nat.load(netobj+13644u):0u,stp=ok(netobj)?nat.load(netobj+13652u):0u;
                    const std::uint32_t st=ok(stp)?nat.load(stp):0xFFFFFFFFu,pm=nat.load(0x8381CF1Cu);
                    const std::uint32_t lp=ok(pm)?nat.load(pm+128u):0u;
                    logf("RENDER_TRAINER probe session e1 gamer=%d sns=%08x status_ptr=%08x status=%d pm=%08x local=%08x local140=%08x local41=%d pending=%d connected=%d active=%08x f61=%d",
                         int(nat.load(net::RosIndexWord)),sns,stp,int(st),pm,lp,ok(lp)?nat.load(lp+140u):0u,ok(lp)?byteAt(lp+41u):-1,
                         ok(pm)?int(nat.load(pm+280u)):-1,ok(pm)?int(nat.load(pm+292u)):-1,ok(pm)?nat.load(pm+728u):0u,byteAt(0x83C00F61u));
                }
                return;
            }
            unsigned cdCount=0,validCount=0;
            for(const auto& m:ProbeList) {
                const std::uint32_t h=joaat(m);
                bool cdOk=false,validOk=false;
                const std::uint32_t cd=call1(nat,n::IsModelInCdimage,{h},&cdOk),ok=call1(nat,n::IsModelValid,{h},&validOk);
                logf("RENDER_TRAINER probe seq=%d name=%s hash=%08x cd=%d valid=%d",r.seq,m,h,cdOk?int(cd!=0):-1,validOk?int(ok!=0):-1);
                cdCount+=cdOk && cd;validCount+=validOk && ok;
            }
            char d[48];std::snprintf(d,sizeof d,"models=%zu cd=%u valid=%u",ProbeList.size(),cdCount,validCount);result(r.seq,"ok",what,d);
            return;}
        case SwitchCharacter: { // P7
            if(!sandboxMode()){result(r.seq,"fail",what,"story-mode");return;}
            if(const char* why=switchRefusal(nat,g)){result(r.seq,"fail",what,why);return;}
            const Character& c=CharacterList[std::size_t(r.a0)];
            const std::uint32_t model=joaat(c.model);
            if(call1(nat,n::GetEntityModel,{g.ped})==model){result(r.seq,"fail",what,"already-that-character");return;}
            if(!call1(nat,n::IsModelInCdimage,{model})){char d[64];std::snprintf(d,sizeof d,"character=%s game-refused why=model-missing",c.label);result(r.seq,"fail",what,d);return;}
            call0(nat,n::RequestModel,{model});
            job_=Job{};job_.kind=Job::Switch;job_.seq=r.seq;job_.model=model;job_.startFrame=frame;job_.what=what;
            job_.character=r.a0;job_.method=r.a1;
            logf("RENDER_TRAINER switch seq=%d character=%s method=%s model=%08x requested",r.seq,c.label,
                 r.a1==SwitchSetModel?"set-model":"change-ped",model);
            return;}
        case NextWeapon: { // v983 (ALLOCATION section 13): the next / previous owned weapon in the wheel's group order, wrapping
            if(call1(nat,n::IsPedInAnyVehicle,{g.ped,0})){result(r.seq,"fail",what,"in-vehicle");return;}
            constexpr std::size_t N=WeaponList.size()+1;   // index 0 = WEAPON_UNARMED (always owned)
            const auto hashAt=[](std::size_t i){return i==0?joaat("WEAPON_UNARMED"):joaat(WeaponList[i-1]);};
            const std::uint32_t cur=call1(nat,n::GetSelectedPedWeapon,{g.ped});
            std::size_t at=0;bool found=false;for(std::size_t i=0;i<N;++i)if(hashAt(i)==cur){at=i;found=true;break;}
            for(std::size_t k=1;k<=N;++k) {
                const std::size_t i=r.a0==1?(found?(at+N-k)%N:(N-k)%N):(found?(at+k)%N:(k-1)%N);   // not in the list: next starts at 0, previous at the end
                const std::uint32_t h=hashAt(i);
                if(h==cur && found)continue;
                if(i==0 || call1(nat,n::HasPedGotWeapon,{g.ped,h,0})) {
                    call0(nat,n::SetCurrentPedWeapon,{g.ped,h,1});
                    char d[48];std::snprintf(d,sizeof d,"weapon=%s",i==0?"WEAPON_UNARMED":WeaponList[i-1]);result(r.seq,"ok",what,d);return;
                }
            }
            result(r.seq,"fail",what,"no-weapons");return;}
        case AddCash: { // v1153: the game's own earn path, only for the Online character in our local session
            bool gipOk=false;const std::uint32_t gip=call1(nat,n::NetworkGame,{},&gipOk);
            if(!gipOk || !gip || !::recomp::local_session::active()){result(r.seq,"fail",what,"needs-session");return;}
            bool wOk=false;const std::uint32_t before=call1(nat,n::WalletBalance,{0xFFFFFFFFu},&wOk);
            bool bOk=false;const std::uint32_t bankBefore=call1(nat,n::BankBalance,{},&bOk);
            call0(nat,n::EarnFromRockstar,{std::uint32_t(r.a0)});
            bool aOk=false;const std::uint32_t after=call1(nat,n::WalletBalance,{0xFFFFFFFFu},&aOk);
            bool cOk=false;const std::uint32_t bankAfter=call1(nat,n::BankBalance,{},&cOk);
            logf("RENDER_TRAINER add-cash seq=%d amount=%d wallet %d -> %d bank %d -> %d (NETWORK_EARN_FROM_ROCKSTAR)",r.seq,r.a0,wOk?int(before):-1,aOk?int(after):-1,bOk?int(bankBefore):-1,cOk?int(bankAfter):-1);
            char d[16];std::snprintf(d,sizeof d,"%d",r.a0);result(r.seq,"ok",what,d);return;}
        case OpenSaveMenu: // v834: refused where the game's own phone offers no save (a mission, stars, an autosave running)
            // v978 (owner: the Quick Save button "does nothing"): every refusal now tells the player why, in the game's own help box
            if(const OnlineActions* o=online_.load(std::memory_order_acquire);o && o->characterActive()){ // P8: never save a freemode model
                tellPlayer(nat,"Saving isn't available while playing your online character.");result(r.seq,"fail",what,"online-character");return;}
            if(call1(nat,n::GetMissionFlag,{})){tellPlayer(nat,"You can't save during a mission.");result(r.seq,"fail",what,"on-a-mission");return;}
            if(call1(nat,n::GetWantedLevel,{g.player})){tellPlayer(nat,"You can't save while you are wanted.");result(r.seq,"fail",what,"wanted");return;}
            if(call1(nat,n::IsAutoSaveInProgress,{})){tellPlayer(nat,"The game is already saving.");result(r.seq,"fail",what,"autosave");return;}
            call0(nat,n::SetSaveMenuActive,{1}); // (1: also in a vehicle - with 0 the native does nothing there)
            result(r.seq,"ok",what,"");
            return;
        case SpawnVehicle: {
            const std::uint32_t model=r.a1==1?joaat(VehicleList[std::size_t(r.a0)].model):std::uint32_t(r.a0);
            if(!call1(nat,n::IsModelInCdimage,{model}) || !call1(nat,n::IsModelAVehicle,{model})){
                char d[32];std::snprintf(d,sizeof d,"model=%08x not-a-vehicle",model);result(r.seq,"fail",what,d);return;}
            call0(nat,n::RequestModel,{model});
            job_=Job{};job_.kind=Job::Spawn;job_.seq=r.seq;job_.model=model;job_.startFrame=frame;job_.what=what;
            logf("RENDER_TRAINER spawn seq=%d model=%08x requested",r.seq,model);
            return;}
        case TeleportWaypoint: case TeleportLandmark: {
            float x=0,y=0,z=0;bool hint=false,strict=false;
            if(r.action==TeleportWaypoint) {
                const std::uint32_t blip=call1(nat,n::FirstBlipInfoId,{WaypointSprite});
                if(!blip || !call1(nat,n::DoesBlipExist,{blip})){result(r.seq,"fail",what,"no-waypoint");return;}
                std::uint32_t out[3]{};
                if(!nat.call(n::BlipInfoIdCoord,&blip,1,out)){result(r.seq,"fail",what,"no-coords");return;}
                x=asFloat(out[0]);y=asFloat(out[1]);
            } else if(r.a0==TestTeleportIndex) {   // v1020 test slot: consumed by this trip
                strict=true;   // v1030 (t-doors7tp 1708: probing from z + 5 landed on building 7's canopy at z 48; the shop runs ~3 m above their floor)
                if(!testTeleportArmed.exchange(false,std::memory_order_acq_rel)){result(r.seq,"fail",what,"test-slot-unarmed");return;}
                x=testTeleportX.load(std::memory_order_relaxed);y=testTeleportY.load(std::memory_order_relaxed);z=testTeleportZ.load(std::memory_order_relaxed);hint=true;
                if(testTeleportExact.exchange(false,std::memory_order_relaxed)) {   // v1029: exact, no probe
                    if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || std::fabs(x)>20000.f || std::fabs(y)>20000.f || std::fabs(z)>3000.f){
                        result(r.seq,"fail",what,"bad-coords");return;}
                    call0(nat,n::SetPedCoordsKeepVehicle,{g.ped,bits(x),bits(y),bits(z)});call0(nat,n::RequestCollisionAtCoord,{bits(x),bits(y),bits(z)});
                    char d[64];std::snprintf(d,sizeof d,"exact x=%.1f y=%.1f z=%.1f",double(x),double(y),double(z));result(r.seq,"ok",what,d);return;
                }
            } else {const auto& l=LandmarkList[std::size_t(r.a0)];x=l.x;y=l.y;z=l.z;hint=true;}
            if(!std::isfinite(x) || !std::isfinite(y) || std::fabs(x)>20000.f || std::fabs(y)>20000.f){result(r.seq,"fail",what,"bad-coords");return;}
            std::uint32_t here[3]{};const std::uint32_t args[2]{g.ped,1};
            (void)nat.call(n::GetEntityCoords,args,2,here);
            job_=Job{};job_.kind=Job::Teleport;job_.seq=r.seq;job_.x=x;job_.y=y;job_.zHint=z;job_.hasHint=hint;job_.strictHint=strict;job_.startFrame=frame;
            job_.fromX=asFloat(here[0]);job_.fromY=asFloat(here[1]);job_.fromZ=asFloat(here[2]);job_.what=what;job_.step=0;
            moveProbe(nat,g,frame);
            return;}
        case Activity: { // OW L2 1c: the request only; the load and the start run on the Sandbox tick (activities.h)
            ActivityActions* a=activities_.load(std::memory_order_acquire);
            if(!a){result(r.seq,"fail",what,"sandbox-only");return;}
            char d[96]{};const int k=a->request(nat,r.a0,r.a1,d,sizeof d);
            result(r.seq,k==1?"ok":"fail",what,d);
            return;}
        case DiscCreator: { // PROPOSED: the request only; the run happens on the Sandbox tick (creator_route.h Route::tick)
            DiscCreatorActions* d=discCreator_.load(std::memory_order_acquire);
            if(!d){result(r.seq,"fail",what,"sandbox-only");return;}
            char dd[96]{};const int k=d->request(nat,r.a0,dd,sizeof dd);
            result(r.seq,k==1?"ok":"fail",what,dd);
            return;}
        case OnlineIntro: { // PROPOSED (lane upd1-spoof/intro-cutscene)
            IntroActions* i=intro_.load(std::memory_order_acquire);
            if(!i || !sandboxMode()){result(r.seq,"fail",what,"sandbox-only");return;}
            const OnlineActions* o=online_.load(std::memory_order_acquire);
            char d[96]{};const int k=i->begin(nat,r.a0,r.a1,g.player,g.ped,o && o->characterActive(),frame,d,sizeof d);
            if(k==0){job_=Job{};job_.kind=Job::Intro;job_.seq=r.seq;job_.startFrame=frame;job_.what=what;job_.player=g.player;job_.ped=g.ped;
                logf("RENDER_TRAINER online-intro seq=%d a0=%d a1=%d started",r.seq,r.a0,r.a1);return;}
            result(r.seq,k==1?"ok":"fail",what,d);
            return;}
        case OnlineCharacter: case OnlinePlace: case CharacterLook: case CharacterEdit: { // Update 1 P8 (+ OW L2 creator 17 / 18)
            OnlineActions* o=online_.load(std::memory_order_acquire);
            if(!o){result(r.seq,"fail",what,"sandbox-only");return;}
            char d[64]{};const int k=o->begin(nat,r.action,r.a0,g.player,g.ped,frame,d,sizeof d);
            if(k==0){job_=Job{};job_.kind=Job::Online;job_.seq=r.seq;job_.startFrame=frame;job_.what=what;
                logf("RENDER_TRAINER online seq=%d action=%s a0=%d started",r.seq,what,r.a0);return;}
            result(r.seq,k==1?"ok":"fail",what,d);
            return;}
        default: result(r.seq,"fail",what,"unknown");return;
        }
    }

    // OW L2 1b, D-NET1: four gate natives through the handlers and five guest loads; nothing is written in the game (the reason pointer
    // is a scratch word of this tick). A native that is not registered prints -1. One line, then the result.
    static int byteAt(Natives& nat,std::uint32_t a) noexcept {return int((nat.load(a&~3u)>>(24-8*(a&3u)))&0xFFu);}
    void netProbe(Natives& nat,int seq,const char* what) noexcept {
        bool inOk=false,onOk=false,privOk=false,accOk=false;
        const std::uint32_t in=call1(nat,n::NetworkIsSignedIn,{},&inOk),on=call1(nat,n::NetworkIsSignedOnline,{},&onOk),
            priv=call1(nat,n::NetworkHaveOnlinePrivileges,{},&privOk);
        const std::uint32_t reasonAt=nat.word(0xFFFFFFFFu);   // the out slot, pre-set to -1 (the worker writes 1 when it runs)
        std::uint32_t access=0;int reason=-1;
        if(reasonAt){access=call1(nat,n::NetworkCanAccessMultiplayer,{reasonAt},&accOk);if(accOk)reason=int(nat.load(reasonAt));}
        const int onlineByte=byteAt(nat,net::SignedOnlineByte);
        const std::int32_t user=std::int32_t(nat.load(net::RosIndexWord));
        const int ros=user<0?0:user<net::RosUsers?int(byteAt(nat,net::RosByteBase+std::uint32_t(user))!=0):-1;
        const std::uint32_t netobj=nat.load(net::NetObjectWord),statFail=nat.load(net::StatFailWord);
        const auto b=[](bool ok,std::uint32_t v){return ok?int(v!=0):-1;};
        logf("RENDER_TRAINER probe net signed_in=%d signed_online=%d online_priv=%d access=%d reason=%d ros=%d netobj=%08x stat_fail=%08x"
             " seq=%d signed_online_byte=%d ros_user=%d",b(inOk,in),b(onOk,on),b(privOk,priv),reasonAt?b(accOk,access):-1,reason,ros,netobj,
             statFail,seq,onlineByte,int(user));
        char d[64];std::snprintf(d,sizeof d,"net natives=%d",int(inOk)+int(onOk)+int(privOk)+int(accOk));
        result(seq,"ok",what,d);
    }
    static unsigned giveWeapons(Natives& nat,std::uint32_t ped) noexcept {
        unsigned given=0;
        for(const char* w:WeaponList) {
            const std::uint32_t h=joaat(w);
            if(!call1(nat,n::IsWeaponValid,{h}))continue;
            call0(nat,n::GiveWeapon,{ped,h,WeaponAmmo,0,0});++given;
        }
        return given;
    }
    // P7: why the player may not switch now (nullptr: may). Fail-closed: a refusal native that is not registered refuses.
    const char* switchRefusal(Natives& nat,const Gate& g) const noexcept {
        // Update 1 P8 + P7: never while an online (freemode) character is the player; the online side owns that ped and would not
        // know it was replaced (its way back is OnlineCharacter a0 = 2)
        if(const OnlineActions* o=online_.load(std::memory_order_acquire);o && o->characterActive())return "online-character";
        struct Q {std::uint32_t hash;bool ped;const char* why;};
        // (the player-facing words have no '='; "not-available why=..." keeps the detail in the log, the apps hide key=value words)
        static constexpr Q qs[]{{n::GetMissionFlag,false,"on-a-mission"},{n::GetRandomEventFlag,false,"random-event"},
            {n::IsMinigameInProgress,false,"minigame"},{n::IsAutoSaveInProgress,false,"autosave"},
            {n::IsMobilePhoneCallOngoing,false,"phone-in-use"},{n::IsPedRingtonePlaying,true,"phone-in-use"},
            {n::GetInteriorFromEntity,true,"indoors"},   // (v1: CREATE_PED's interior placement is unproven; E5 is outdoors only)
            {n::IsPedRagdoll,true,"ragdoll"},{n::IsPedFalling,true,"falling"},
            {n::IsPedInParachuteFreeFall,true,"parachute"},{n::IsPedClimbing,true,"climbing"},{n::IsPedJumping,true,"jumping"}};
        for(const auto& q:qs) {
            bool ok=false;std::uint32_t v=0;
            if(q.ped)v=call1(nat,q.hash,{g.ped},&ok);else v=call1(nat,q.hash,{},&ok);
            if(!ok)return "not-available why=natives-unavailable";
            if(v)return q.why;
        }
#if defined(RECOMP_TRAINER_PHONE_UP)
        // the phone up without a call (the renderer's tracker, phone_call.h bit 0): the old ped holds the phone prop and the cellphone
        // scripts expect it. Fail-open where no tracker sets the bit (it then reads 0); the call / ringtone natives above are the game's.
        if(recomp::phonecall::state()&recomp::phonecall::Up)return "phone-in-use";
#endif
        bool ok=false;
        const std::uint32_t inVehicle=call1(nat,n::IsPedInAnyVehicle,{g.ped,0},&ok);
        if(!ok)return "not-available why=natives-unavailable";
        if(inVehicle)return "in-a-vehicle";   // (v1: the old ped would keep the seat; NOTES.md open question 3)
        return nullptr;
    }
    // P7, on the frame the model is loaded and every refusal re-checked. True: the player ped is now the character.
    bool switchNow(Natives& nat,const Gate& g,char* d,std::size_t cap) noexcept {
        // Internal failures read "game-refused why=<detail>": the apps show "game refused" and hide the key=value detail (log only).
        const Character& c=CharacterList[std::size_t(job_.character)];
        if(job_.method==SwitchSetModel) { // E5's comparison: the same ped (or the game's replacement) gets the model
            call0(nat,n::SetPlayerModel,{g.player,job_.model});
            const std::uint32_t now=call1(nat,n::PlayerPedId,{});
            if(!now || call1(nat,n::GetEntityModel,{now})!=job_.model){std::snprintf(d,cap,"character=%s method=set-model game-refused why=model-not-set",c.label);return false;}
            call0(nat,n::SetPedDefaultVariation,{now});
            unsigned weapons=0;
            if(now!=g.ped)notePedChange(g.ped,now);   // a new ped: the Sandbox tick gives the weapons (as after a ped change)
            else weapons=giveWeapons(nat,now);        // the same ped: no tick would notice, so they are given here
            std::snprintf(d,cap,"character=%s method=set-model ped=%08x old=%08x weapons=%u",c.label,now,g.ped,weapons);
            return true;
        }
        // Scratch words: this path takes at most 4 (old slot, new slot, then one of: the refused ped's delete slot + release slot, or
        // the old ped's release slot), 16 bytes of the tick's 0x580-byte heap (GuestNatives Heap..HeapEnd), and it is the only job of
        // its tick that takes any. The failure branches below are kept fail-safe but cannot be reached with the glue's GuestNatives.
        const std::uint32_t oldSlot=nat.word(g.ped);  // DELETE_PED / SET_PED_AS_NO_LONGER_NEEDED take a pointer to the handle
        if(!oldSlot){std::snprintf(d,cap,"character=%s game-refused why=no-scratch",c.label);return false;}
        std::uint32_t at[3]{};const std::uint32_t args[2]{g.ped,1};
        if(!nat.call(n::GetEntityCoords,args,2,at)){std::snprintf(d,cap,"character=%s game-refused why=no-coords",c.label);return false;}
        const std::uint32_t heading=call1(nat,n::GetEntityHeading,{g.ped});
        const std::uint32_t ped=call1(nat,n::CreatePed,{c.pedType,job_.model,at[0],at[1],at[2],heading,0,0});
        if(!ped){std::snprintf(d,cap,"character=%s game-refused why=create-failed",c.label);return false;}
        const std::uint32_t newSlot=nat.word(ped);
        if(!newSlot){ // (unreachable, see above) no pointer for DELETE_PED / the release: at least it is not left as a mission entity
            std::snprintf(d,cap,"character=%s game-refused why=no-scratch ped=%08x",c.label,ped);return false;}
        call0(nat,n::SetPedDefaultVariation,{ped});
        // Released while it is still an ordinary ped, so no script (the one whose call hosts this tick) owns the player ped afterwards.
        // Nothing runs between this and the change (one native sequence on the script thread). NOTE: the release's worker
        // (sub_82D750A8, loc_82D75190 / loc_82D751AC: li r11,0; stw r11,0(r30)) writes 0 into *newSlot, so newSlot is spent here.
        call0(nat,n::PedNoLongerNeeded,{newSlot});
        call0(nat,n::ChangePlayerPed,{g.player,ped,ChangePedFlag2,ChangePedFlag3});
        if(call1(nat,n::PlayerPedId,{})!=ped) { // the game refused the change: our ped goes again, the player is untouched
            // A FRESH slot for the delete (newSlot holds 0 now); if the ped survives the delete, it is released, never left as a
            // mission entity of the script that hosted the tick.
            call0(nat,n::SetEntityAsMission,{ped,1,1});
            if(const std::uint32_t slot=nat.word(ped))call0(nat,n::DeletePed,{slot});
            const bool left=call1(nat,n::DoesEntityExist,{ped})!=0;
            if(left){if(const std::uint32_t again=nat.word(ped))call0(nat,n::PedNoLongerNeeded,{again});}
            std::snprintf(d,cap,"character=%s game-refused why=change-refused ped=%s",c.label,left?"released":"deleted");return false;
        }
        // The old ped: deleted (no double of the character left behind). DELETE_PED only removes a mission entity (sub_82DC1478 needs
        // the entity's script extension), hence SET_ENTITY_AS_MISSION_ENTITY first; if it is still there, it is released instead.
        call0(nat,n::SetEntityAsMission,{g.ped,1,1});call0(nat,n::DeletePed,{oldSlot});
        const bool kept=call1(nat,n::DoesEntityExist,{g.ped})!=0;
        if(kept){const std::uint32_t again=nat.word(g.ped);if(again)call0(nat,n::PedNoLongerNeeded,{again});}
        // E5's evidence (review): is the new player ped a mission entity after the change (-1: the native is not available)?
        bool mok=false;const std::uint32_t mission=call1(nat,n::IsEntityAMissionEntity,{ped},&mok);
        notePedChange(g.ped,ped);
        std::snprintf(d,cap,"character=%s method=change-ped ped=%08x old=%08x %s mission=%d",c.label,ped,g.ped,kept?"old=released":"old=deleted",
                      mok?int(mission!=0):-1);
        return true;
    }
    void notePedChange(std::uint32_t oldPed,std::uint32_t newPed) noexcept {pedChangeOld_=oldPed;pedChangeNew_=newPed;pedChange_=true;}

    float probeHeight(unsigned step) const noexcept {
        if(job_.hasHint){if(step==0)return job_.zHint+(job_.strictHint?1.0f:5.0f);--step;}   // v1030: the test slot probes from z + 1
        return step<ProbeHeights.size()?ProbeHeights[step]:-1.0f;
    }
    unsigned probeCount() const noexcept {return unsigned(ProbeHeights.size())+(job_.hasHint?1u:0u);}
    void moveProbe(Natives& nat,const Gate& g,std::uint32_t frame) noexcept {
        const float h=probeHeight(job_.step);
        call0(nat,n::SetPedCoordsKeepVehicle,{g.ped,bits(job_.x),bits(job_.y),bits(h)});
        call0(nat,n::RequestCollisionAtCoord,{bits(job_.x),bits(job_.y),bits(h)});
        job_.stepFrame=frame;
    }

    // D-NET2 (v960): leave the session this job hosted (once). Every exit path of the job comes here (finished, aborted, blocked).
    void sessionLeave(Natives& nat,std::uint32_t frame) noexcept {
        if(!job_.hosted || job_.leaveTried)return;
        job_.leaveTried=true;
        std::uint32_t out[3]{};job_.left=nat.call(n::NetworkSessionLeaveSinglePlayer,nullptr,0,out);job_.leaveFrame=frame;
        logf("RENDER_TRAINER probe session leave seq=%d called=%d at=%u",job_.seq,job_.left?1:0,frame-job_.startFrame);
    }
    void runSession(Natives& nat,std::uint32_t frame) noexcept {
        if(frame-job_.lastPoll<SessionPollFrames)return;
        job_.lastPoll=frame;
        bool ok=false;const std::uint32_t gip=call1(nat,n::NetworkGame,{},&ok);const int now=ok?int(gip!=0):-1;
        if(now==1 && !job_.upFrame)job_.upFrame=frame;
        logf("RENDER_TRAINER probe session seq=%d gip=%d at=%u",job_.seq,now,frame-job_.startFrame);
        if(!job_.leaveTried) {if(frame-job_.startFrame>=SessionHoldFrames)sessionLeave(nat,frame);return;}
        if(now==0 || !job_.left || frame-job_.leaveFrame>=SessionLeaveFrames) {
            job_.gipEnd=now;
            char d[96];std::snprintf(d,sizeof d,"session gip_after=%d after=%u left=%d gip_end=%d",job_.upFrame?1:0,
                                     job_.upFrame?job_.upFrame-job_.startFrame:0u,job_.left?1:0,now);
            logf("RENDER_TRAINER probe %s",d);
            finishJob(nat,true,d,false);
        }
    }
    void runJob(Natives& nat,const Gate& g,std::uint32_t frame) noexcept {
        if(job_.kind==Job::Session){runSession(nat,frame);return;}   // D-NET2
        if(job_.kind==Job::Intro) { // PROPOSED: the intro player's frame (its own player / ped: the gate may say none mid-cutscene)
            IntroActions* i=intro_.load(std::memory_order_acquire);
            if(!i){finishJob(nat,false,"sandbox-only",false);return;}
            char d[128]{};const int k=i->step(nat,job_.player,job_.ped,frame,d,sizeof d);
            if(k!=0)finishJob(nat,k==1,d,false);
            return;
        }
        if(job_.kind==Job::Online) { // Update 1 P8
            OnlineActions* o=online_.load(std::memory_order_acquire);
            if(!o){finishJob(nat,false,"sandbox-only",false);return;}
            char d[64]{};const int k=o->step(nat,g.player,g.ped,frame,d,sizeof d);
            if(const std::uint32_t np=o->takeNewPed();np && np!=g.ped)notePedChange(g.ped,np);   // v957 (WAVE2 item 3): as switchNow does
            if(k!=0)finishJob(nat,k==1,d,false);
            return;
        }
        if(job_.kind==Job::Spawn) {
            if(!call1(nat,n::HasModelLoaded,{job_.model})) {
                if(frame-job_.startFrame>=ModelWaitFramesMax)finishJob(nat,false,"model-timeout",false);
                else call0(nat,n::RequestModel,{job_.model});
                return;
            }
            std::uint32_t at[3]{};const std::uint32_t args[2]{g.ped,1};
            (void)nat.call(n::GetEntityCoords,args,2,at);
            const float heading=asFloat(call1(nat,n::GetEntityHeading,{g.ped}));
            float x=asFloat(at[0]),y=asFloat(at[1]);const float z=asFloat(at[2]);
            if(call1(nat,n::IsPedInAnyVehicle,{g.ped,0})) { // in a vehicle: the new one 8 m ahead (the old one stays behind)
                const float rad=heading*3.14159265f/180.0f;x+=-std::sin(rad)*8.0f;y+=std::cos(rad)*8.0f;
            }
            const std::uint32_t veh=call1(nat,n::CreateVehicle,{job_.model,bits(x),bits(y),bits(z),bits(heading),0,0});
            if(!veh){finishJob(nat,false,"create-failed",false);return;}
            call0(nat,n::SetVehicleOnGround,{veh});
            call0(nat,n::SetPedIntoVehicle,{g.ped,veh,std::uint32_t(-1)});
            call0(nat,n::SetVehicleEngineOn,{veh,1,1});
            const std::uint32_t handle=nat.word(veh);
            if(handle)call0(nat,n::VehicleNoLongerNeeded,{handle}); // the world keeps it while used; the game cleans it up later
            char d[40];std::snprintf(d,sizeof d,"model=%08x vehicle=%08x",job_.model,veh);
            finishJob(nat,true,d,false);
            return;
        }
        if(job_.kind==Job::Switch) { // P7
            if(!call1(nat,n::HasModelLoaded,{job_.model})) {
                if(frame-job_.startFrame>=ModelWaitFramesMax)finishJob(nat,false,"game-refused why=model-timeout",false);
                else call0(nat,n::RequestModel,{job_.model});
                return;
            }
            // the world may have changed while the model loaded: every refusal again, on the frame of the change
            if(!sandboxMode()){finishJob(nat,false,"story-mode",false);return;}
            if(const char* why=switchRefusal(nat,g)){finishJob(nat,false,why,false);return;}
            if(call1(nat,n::GetEntityModel,{g.ped})==job_.model){finishJob(nat,false,"already-that-character",false);return;}
            char d[128];
            const bool ok=switchNow(nat,g,d,sizeof d);
            finishJob(nat,ok,d,false);
            return;
        }
        if(job_.kind==Job::Teleport) {
            if(frame-job_.stepFrame<CollisionWaitFrames)return;
            const float h=probeHeight(job_.step);
            const std::uint32_t outZ=nat.word(0);
            if(outZ && call1(nat,n::GroundZ,{bits(job_.x),bits(job_.y),bits(h),outZ})) {
                const float gz=asFloat(nat.load(outZ));
                if(std::isfinite(gz) && gz>-200.f && gz<2000.f) {
                    call0(nat,n::SetPedCoordsKeepVehicle,{g.ped,bits(job_.x),bits(job_.y),bits(gz+1.0f)});
                    char d[64];std::snprintf(d,sizeof d,"x=%.1f y=%.1f z=%.1f probes=%u",double(job_.x),double(job_.y),double(gz+1.0f),job_.step+1);
                    finishJob(nat,true,d,false);
                    return;
                }
            }
            if(job_.strictHint) {   // v1030: never the high probes for an explicit height (they take the topmost surface: roofs, canopies)
                call0(nat,n::SetPedCoordsKeepVehicle,{g.ped,bits(job_.x),bits(job_.y),bits(job_.zHint+1.0f)});
                finishJob(nat,true,"no-ground below z+1 (given height used)",false);
                return;
            }
            if(++job_.step>=probeCount()) { // no ground found anywhere: back to where the player was
                if(job_.hasHint)call0(nat,n::SetPedCoordsKeepVehicle,{g.ped,bits(job_.x),bits(job_.y),bits(job_.zHint+1.0f)});
                else call0(nat,n::SetPedCoordsKeepVehicle,{g.ped,bits(job_.fromX),bits(job_.fromY),bits(job_.fromZ)});
                finishJob(nat,job_.hasHint,job_.hasHint?"no-ground (landmark height used)":"no-ground (returned)",false);
                return;
            }
            moveProbe(nat,g,frame);
        }
    }
    void finishJob(Natives& nat,bool ok,const char* detail,bool blocked) noexcept {
        if(job_.kind==Job::Spawn || job_.kind==Job::Switch)call0(nat,n::ModelNoLongerNeeded,{job_.model});
        if(job_.kind==Job::Session)sessionLeave(nat,job_.leaveFrame?job_.leaveFrame:job_.startFrame);   // D-NET2: never leave a session open
        if(blocked && job_.kind==Job::Online) // P8: the online side releases what it holds and says what was left
            if(OnlineActions* o=online_.load(std::memory_order_acquire)){result(job_.seq,"blocked",job_.what,o->abort(nat));job_=Job{};return;}
        if(blocked && job_.kind==Job::Intro) // PROPOSED: the intro player undoes what it holds (a request may stay owed: cleanup)
            if(IntroActions* i=intro_.load(std::memory_order_acquire)){
                char d[96];std::snprintf(d,sizeof d,"%s left=%s",detail?detail:"",i->abort(nat,job_.player,job_.ped,lastFrame_));
                result(job_.seq,"blocked",job_.what,d);job_=Job{};return;}
        if(blocked && job_.kind==Job::Teleport) {/* left where the last probe put it: the gate closed (pause, cutscene, death) */}
        result(job_.seq,blocked?"blocked":ok?"ok":"fail",job_.what,detail);
        job_=Job{};
    }

    LogFn log_;
    ModeFn sandbox_;            // P7 (read on both threads; set once at construction)
    std::uint32_t pedChangeOld_=0,pedChangeNew_=0;bool pedChange_=false;   // P7, script thread only
    std::atomic<bool> enabled_{false},work_{false},gameplay_{false},busy_{false},owed_{false},weatherLocked_{false};
    std::atomic<std::uint32_t> toggles_{0};
    std::atomic<OnlineActions*> online_{nullptr};   // Update 1 P8 (Sandbox mode only)
    std::atomic<ActivityActions*> activities_{nullptr};   // OW L2 1c (Sandbox mode only)
    std::atomic<IntroActions*> intro_{nullptr};   // PROPOSED action 20 (Sandbox mode + diag-online-local only)
    std::atomic<DiscCreatorActions*> discCreator_{nullptr};   // PROPOSED action 21 (Sandbox mode + diag-online-local only)
    std::uint32_t lastFrame_=0;   // PROPOSED: the frame of the current tick (the intro abort's clock)
    // PROPOSED: gate reasons the intro job's own cutscene and fades cause (it keeps running), and a pause (it holds).
    static bool introOwns(const char* reason) noexcept {
        return !std::strcmp(reason,"cutscene") || !std::strcmp(reason,"faded-out") || !std::strcmp(reason,"pause-menu");
    }
    std::uint32_t applied_=0;   // script thread only: toggles whose per-frame action ran (their off action is still owed)
    Job job_;                   // script thread only
    mutable std::mutex m_;      // queue_ and status_
    std::deque<Request> queue_;
    char status_[128]="none";int statusSeq_=0;
    std::atomic<int> seq_{0};
};

} // namespace recomp::trainer
