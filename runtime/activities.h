#pragma once
// activities.h (Update 1, Online World lane "freeroam-activities", 2026-11-06). Integrated in attempt v957 (OW Level 2 step 1c,
// trainer action 19 Activity, list 8; ALLOCATION section 11): the lane's file plus the diag-activity-ungated path (Sandbox only), the
// FA-D1 thread-word diagnostic, the list labels and the trainer's ActivityActions (class Activities, bottom). Host-tested only.
// Pure: no guest memory, no platform calls. The glue supplies Natives (trainer.h) and a Starter (below).
//
// What it is: the Sandbox "Activities" catalog and its launcher. Each entry is one disc script with the launch facts the disc's own
// launcher uses for it (freemode's mission-type table 0x7663, stack rule 0x26559, 22-word mission block; evidence in NOTES.md 2).
//
// Two tiers (NOTES.md 3):
//  - Session: the multiplayer am_* activities. Every one of them ends itself unless NETWORK_IS_GAME_IN_PROGRESS is true (entry,
//    registration and script-status gates, NOTES.md 2.2), so the launcher REFUSES them ("fail needs-session") until a local session
//    (Level 2, ONLINE-WORLD-PLAN 4) reports a network game. Nothing here fakes that state.
//  - World: single-player scripts the disc's MP launchers hand over to (am_mp_carwash_launch -> carwash1/2). They need no session.
//
// Start path: NOT the START_NEW_SCRIPT natives (second registration table, not captured by the trainer). Their handler
// (sub_8343B9C8) also touches the CALLING thread (the trainer's host script): on a failed start it stores 1 into the caller's +12
// word; on success it stores 0 there and copies bytes 48..63 (4 words) of the caller into the new thread where the new thread's
// are zero. The glue calls the start-script implementation sub_82AAC528 (r3 name, r4 argument block, r5 argument BYTES, r6 stack;
// returns the thread id, 0 = failed) directly, the function both START_NEW_SCRIPT handlers reach through the pointer at 0x8386AA50
// (stored in the code at ppc_recomp.126.cpp: lis -32085 / addi -15064 = 0x82AAC528), so neither caller-thread action happens
// (NOTES.md 2.4, Q3). The Sandbox filter is sub_82A9B020, which sub_82AAC528 calls (v954 sandbox_launch.inc; the sub_82AAC528 hook
// only records its caller), so a held name would still be held; no catalog entry is held (tests, against sandbox_denylist.h).
//
// Frame-rate rule: idle = no native call at all. A started activity is polled every PollFrames (one native) until it ends.
//
// Preconditions (2026-11-06, local-analysis/update-1/activities-preconditions/NOTES.md; device evidence: v957 FA-D1, car-wash-1 started
// on foot in Michael's driveway ended itself on its first frame and was seen 1 s later as a silent "ended"). Each entry carries what
// its script, or the disc's own launcher that normally starts it, requires before the first frame, read from the disc's bytecode:
//  - carwash1 / carwash2 terminate on their first frame unless the player is in a vehicle (carwash1 0xe7..0xf4, carwash2 0xeb..0xf8),
//    set the mission flag, then warp the vehicle to their own wash entrance (state 0, SET_ENTITY_COORDS at 0x6b0 / 0x6b4);
//  - launcher_carwash (the world-point script that starts them, START_NEW_SCRIPT(name, 1024) at 0x7f5) offers the wash only to the
//    DRIVER of a CAR (IS_THIS_MODEL_A_CAR, no trailer, drivable, not on fire; also model width <= 3.4 and height <= 2.9) inside a 4.0 m
//    sphere around the trigger (startup 0x3dba7 / 0x3dbc4 -> global 19741[255] / [256]; locate 0xcbb = VDIST2 <= r*r), with no
//    mission flag and no wanted level.
// The launcher refuses an unmet precondition with a reason word on the request ("fail key=car-wash-1 needs-vehicle setup=spawn-vehicle")
// and checks again just before the start (the load takes frames). It never sets anything up itself and writes no game state: where an
// existing trainer action meets the precondition, the reason names it (setup=spawn-vehicle: action 5 seats the player as driver;
// setup=teleport-waypoint: action 7 keeps the vehicle and probes the ground). A precondition native that is not registered refuses
// ("precondition-unavailable"), never passes. An activity that ends within two polls of its start is logged "early".
// NOT replicated: the launcher's size limit. GET_MODEL_DIMENSIONS takes two vector REFERENCES: its handler (sub_82D67360) gets them
// through sub_834384D8, which keeps the script pointers at context+16+4k and the 16-byte temporaries at context+32+16k for the VM to
// copy back after the call. The trainer's GuestNatives puts the arguments at context+0x20 (the first temporary) and copies nothing
// back, so the second pointer would be read from an overwritten slot. A too-big car therefore passes here (NOTES.md, open question).
#include "trainer.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::activities {
using recomp::trainer::Natives;
using recomp::trainer::joaat;

namespace n {
// v1015 (the activity gates line)
constexpr std::uint32_t PlayerActive=joaat("NETWORK_IS_PLAYER_ACTIVE"),   // 1 -> 1
    PlayerPlaying=joaat("IS_PLAYER_PLAYING");   // 1 -> 1
// v1017 (the fmflag line): freemode's own calls pass (name, instance, 0, 0), e.g. freemode 0x1eb5..0x1ebb ("..", -1, 0, 0), b1 0x11
constexpr std::uint32_t ScriptActive=joaat("NETWORK_IS_SCRIPT_ACTIVE");   // 4 -> 1
// v1025 (the am_pi_menu input probe; counts from am_pi_menu's own NATIVE instructions: 0x13a84 b1 0x09, 0x2989d b1 0x09, 0x1390d b1 0x05)
constexpr std::uint32_t DisabledControlPressed=joaat("IS_DISABLED_CONTROL_PRESSED"),   // 2 -> 1
    ControlPressed=joaat("IS_CONTROL_PRESSED"),   // 2 -> 1
    PlayerControlOn=joaat("IS_PLAYER_CONTROL_ON"),   // 1 -> 1
// v1026 (the freemode open shim; counts from am_pi_menu's own NATIVE instructions, e.g. 0x1a4 b1 0x08)
    GameTimer=joaat("GET_GAME_TIMER"),   // 0 -> 1
    DisableControl=joaat("DISABLE_CONTROL_ACTION");   // 2 -> 0
// Argument / result counts: from the disc scripts' own NATIVE instructions (b1 = args<<2 | returns), checked by tests/check_natives.py
// over every decoded script that imports them; handlers recorded in natives/registrations.json (checked there too).
// v957 (FA open question 5, read from the handlers in our recompiled code, v957 ppc_recomp.197 / .186): DOES_SCRIPT_EXIST sub_82E04F98,
// REQUEST_SCRIPT sub_82E04F38, HAS_SCRIPT_LOADED sub_82E04F58, SET_SCRIPT_AS_NO_LONGER_NEEDED sub_82E04F48: one word each (lwz r11,8(r3);
// lwz r3,0(r11)), handed to a worker that hashes it as a string pointer (sub_823C0348(r3, 0) when r3 != 0): a char* name.
// GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH sub_82DFEEE0: one word, the hash (sub_8241FB48 looks it up) -> count.
// GET_NAME_OF_SCRIPT_WITH_THIS_ID sub_82DB3BF8: one word, the id (sub_83439478) -> a pointer to a 64-byte static copy of the name, or
// to a fixed default string when no thread has the id or its state word (+12) is 2. TERMINATE_THREAD sub_82DB3B88: one word, the id
// (tail call sub_83439408). GET_PLAYER_TEAM sub_82DB0C90: one word (player) -> team, or -1. PLAYER_ID and NETWORK_IS_GAME_IN_PROGRESS:
// no argument (trainer.h).
constexpr std::uint32_t DoesScriptExist=joaat("DOES_SCRIPT_EXIST"),        // 1 -> 1
    RequestScript=joaat("REQUEST_SCRIPT"),                                   // 1 -> 0
    HasScriptLoaded=joaat("HAS_SCRIPT_LOADED"),                              // 1 -> 1
    ScriptNoLongerNeeded=joaat("SET_SCRIPT_AS_NO_LONGER_NEEDED"),            // 1 -> 0
    ThreadsWithHash=joaat("GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH"), // 1 -> 1
    NameOfThread=joaat("GET_NAME_OF_SCRIPT_WITH_THIS_ID"),                   // 1 -> 1 (char*; as sandbox_mode.h)
    TerminateThread=joaat("TERMINATE_THREAD"),                               // 1 -> 0
    NetworkGame=joaat("NETWORK_IS_GAME_IN_PROGRESS"),                        // 0 -> 1
    PlayerId=joaat("PLAYER_ID"),                                             // 0 -> 1
    PlayerTeam=joaat("GET_PLAYER_TEAM");                                     // 1 -> 1
// Preconditions (counts from launcher_carwash's / carwash1's own NATIVE instructions, as the comments above; checked by
// local-analysis/update-1/activities-preconditions/tests/check_pre_natives.py against the disc scripts and registrations.json).
constexpr std::uint32_t MissionFlag=joaat("GET_MISSION_FLAG"),               // 0 -> 1
    WantedLevel=joaat("GET_PLAYER_WANTED_LEVEL"),                            // 1 -> 1 (player)
    PlayerPed=joaat("PLAYER_PED_ID"),                                        // 0 -> 1
    InAnyVehicle=joaat("IS_PED_IN_ANY_VEHICLE"),                             // 2 -> 1 (ped, 0)
    VehicleOfPed=joaat("GET_VEHICLE_PED_IS_IN"),                             // 2 -> 1 (ped, 0)
    PedInSeat=joaat("GET_PED_IN_VEHICLE_SEAT"),                              // 2 -> 1 (vehicle, -1 = driver)
    EntityModel=joaat("GET_ENTITY_MODEL"),                                   // 1 -> 1
    ModelIsCar=joaat("IS_THIS_MODEL_A_CAR"),                                 // 1 -> 1
    HasTrailer=joaat("IS_VEHICLE_ATTACHED_TO_TRAILER"),                      // 1 -> 1
    Driveable=joaat("IS_VEHICLE_DRIVEABLE"),                                 // 2 -> 1 (vehicle, 0)
    OnFire=joaat("IS_ENTITY_ON_FIRE"),                                       // 1 -> 1
    EntityCoords=joaat("GET_ENTITY_COORDS");                                 // 2 -> 3 (entity, 1)
}
inline constexpr std::uint32_t UsedNatives[]{n::DoesScriptExist,n::RequestScript,n::HasScriptLoaded,n::ScriptNoLongerNeeded,
    n::ThreadsWithHash,n::NameOfThread,n::TerminateThread,n::NetworkGame,n::PlayerId,n::PlayerTeam,n::PlayerActive,n::PlayerPlaying,   // v1015
    n::ScriptActive,   // v1017
    n::DisabledControlPressed,n::ControlPressed,n::PlayerControlOn,   // v1025
    n::GameTimer,n::DisableControl,   // v1026
    n::MissionFlag,n::WantedLevel,n::PlayerPed,n::InAnyVehicle,n::VehicleOfPed,n::PedInSeat,n::EntityModel,n::ModelIsCar,n::HasTrailer,
    n::Driveable,n::OnFire,n::EntityCoords};

enum class Tier : std::uint8_t {World,Session};
// Field 18 of the mission block (the network-script instance; freemode stores it before each launch, NOTES.md 2.4).
enum class Instance : std::uint8_t {MinusOne,Player,Team};
constexpr unsigned MissionWords=22;   // every am_* activity takes 22 parameters (header +0x14), freemode passes 22 words

// What must hold before the start (the header). Bits are checked in this order; the first unmet one is the answer.
enum Need : std::uint8_t {NoMission=1,NoWanted=2,DrivingCar=4,AtTrigger=8};
struct Trigger {float x,y,z,radius;};   // the disc launcher's sphere (VDIST2 <= radius * radius)
struct Preconditions {
    std::uint8_t need=0;
    Trigger at{};
    const char* goTo="";   // the AtTrigger reason word ("go-to-the-car-wash")
};
// The car-wash rules (NOTES.md): launcher_carwash 0x197d -> 0x1ac3 (vehicle mode 1, price 15, radius 4.0), 0x10f4 (driver: 0x91f,
// locate: 0xcbb), 0x161b (car, trailer; its dimension test is not replicated: header), 0xd64 (on fire, drivable), 0x1be..0x1f2
// (mission flag, wanted level).
constexpr float CarWashRadius=4.0f;
constexpr std::uint8_t CarWashNeeds=NoMission|NoWanted|DrivingCar|AtTrigger;

// v1350 (update-1 session, lead 19:2x; update-1/activities-end/NOTES.md 3): the activity's own END area, read from the disc script and
// confirmed by a script trace on the Thor (v1137). Three Session entries ended within 30 frames because the player was outside the
// area the script itself requires (freemode starts them only there): the launcher refuses the start outside it with a reason word, as
// for the car wash; nothing is skipped, written or teleported. Checked only while Launcher::setEndCond(true)
// (default ON after t-actend-20261109-2048 / the soak; off switch files/private-trace/diag-activity-endcond-off).
//  - Sphere: the script ends when VDIST2(player, at[0]) >= size^2 (am_armybase 0x22c1, 1015 m of Fort Zancudo, solo host) -> needs
//    d^2 < size^2 (strict, as the script's >=).
//  - NearestBox: the script picks the nearest pick[i] (3D GET_DISTANCE_BETWEEN_COORDS, am_hold_up 0x1086e) and ends unless
//    IS_ENTITY_AT_COORD(player, at[i], size, size, size, 0, 1, 0) (0x28a: a 3D box) -> needs |dx|, |dy|, |dz| < size at the nearest.
struct EndSpot {float x,y,z;};
enum class EndKind : std::uint8_t {Sphere,NearestBox};
struct EndArea {EndKind kind;const EndSpot* at;const EndSpot* pick;unsigned count;float size;const char* goTo;};
inline constexpr EndSpot ArmyBaseSpot[1]{{-2076.37353515625f,3112.2939453125f,33.135841369628906f}};   // am_armybase 0x22db..0x22e5
inline constexpr EndArea ArmyBaseEnd{EndKind::Sphere,ArmyBaseSpot,ArmyBaseSpot,1,1015.0f,"go-to-the-army-base"};   // 1030225.0 = 1015^2
// am_hold_up 0x10231(i, &s129 = 1st vector, ..., &s126 = 4th vector): the 20 stores, float32 as the VM computes them (stores 8 and 10
// are VADD results of exact literals, evaluated in float32). update-1/activities-end/holdup_stores.txt; checked by tests/check_tables.py.
inline constexpr EndSpot HoldUpAt[20]{
    {1395.54785f,3600.5752f,33.9891396f},{-3038.65698f,589.517578f,6.90475082f},{-3240.03223f,1004.48175f,11.8307009f},
    {544.227844f,2672.50049f,41.1565018f},{2559.17627f,385.393555f,107.622971f},{2681.86401f,3282.60254f,54.2411385f},
    {1731.23669f,6411.55664f,34.0372314f},{1964.9989f,3740.79468f,31.34375f},{29.0335846f,-1348.90735f,28.4939175f},
    {376.63092f,323.553284f,102.566406f},{1166.49719f,2703.7561f,37.0630722f},{-2973.51172f,390.787323f,14.0432186f},
    {-1226.41748f,-902.673767f,11.334959f},{1141.3501f,-980.93219f,45.4159431f},{-1490.94873f,-383.477112f,39.1633186f},
    {1698.97961f,4929.09326f,41.0635719f},{-711.769958f,-916.468506f,18.2155685f},{-52.9045181f,-1756.47388f,28.4209709f},
    {1159.63306f,-326.508667f,68.2050705f},{-1822.37292f,788.285095f,137.187622f}};
inline constexpr EndSpot HoldUpPick[20]{
    {1394.16919f,3599.86011f,34.0121002f},{-3038.9082f,589.518677f,6.90479994f},{-3240.31689f,1004.43341f,11.8306999f},
    {544.280212f,2672.81128f,41.156601f},{2559.24707f,385.526611f,107.623001f},{2682.00342f,3282.54321f,54.2411003f},
    {1731.20984f,6411.40332f,34.0372009f},{1965.0542f,3740.55518f,31.3448009f},{29.0641003f,-1349.21277f,28.5035992f},
    {376.65329f,323.647095f,102.566399f},{1166.39197f,2703.50415f,37.157299f},{-2973.26172f,390.81839f,14.0432997f},
    {-1226.46436f,-902.586426f,11.2783003f},{1141.35962f,-980.880188f,45.4155006f},{-1491.05652f,-383.572815f,39.1706009f},
    {1698.80847f,4929.19775f,41.0783005f},{-711.721008f,-916.696472f,18.2145004f},{-53.1240005f,-1756.4054f,28.4209995f},
    {1159.54211f,-326.698608f,67.9229965f},{-1822.28662f,788.005981f,137.185898f}};
inline constexpr EndArea HoldUpEnd{EndKind::NearestBox,HoldUpAt,HoldUpPick,20,30.0f,"go-to-a-store"};

struct Entry {
    const char* key;      // stable id for logs / app (ascii, no spaces)
    const char* label;    // player-visible; FivePocket naming rule (no game or publisher names)
    const char* script;   // lower-case script name (joaat = the script header's name hash, L1 2.1)
    std::uint16_t stack;  // a stack size the disc itself uses for this script
    std::uint8_t type;    // freemode mission type (field 0 of the block); 0xFF = no block (World tier, no arguments)
    Instance inst;
    Tier tier;
    Preconditions pre{};  // none for the Session tier: their first-frame exit is the network game (needs-session, NOTES.md 3)
    bool fmFlag=false;    // v1017: hold freemode's per-player flag global(1338503 + player) at 1 for the run, local session only (Launcher)
                          // v1020: every Session catalog entry (each script reads it in one helper: global 1338499 field 3 array, ARRAY_U8_LOAD:
                          // am_joyrider 0x1cfb, am_armybase 0x2e4a, am_bru_box 0x184f, am_crate_drop 0x1c594, am_hold_up 0x1097b, am_imp_exp 0xbaf9,
                          // am_cr_securityvan 0x8bd, am_backup_heli 0xfcf, am_airstrike 0x1e23, am_ammo_drop 0x2ba, am_heli_taxi 0x18be, am_boat_taxi 0x290);
                          // v1023: the am_pi_menu probe too (0x741 / 0x138f6, at PiMenuProbe); it is still refused while ANOTHER entry holds it
};

// The first list (NOTES.md 3.2): single-player-capable activities with the fewest dependencies. Order = app order. The two Session
// entries whose start path does not reach the freemode-only flag (army-base, bru-box; NOTES.md 2.2) come first.
inline constexpr std::array<Entry,14> Catalog{{
    {"army-base",      "Restricted Base",     "am_armybase",       1820,42,Instance::Team,    Tier::Session,{},true},
    {"bru-box",        "Box Collectibles",    "am_bru_box",        1024,30,Instance::Player,  Tier::Session,{},true},
    {"crate-drop",     "Crate Drop",          "am_crate_drop",     3150,18,Instance::MinusOne,Tier::Session,{},true},
    {"hold-up",        "Store Hold-ups",      "am_hold_up",        3150, 1,Instance::MinusOne,Tier::Session,{},true},
    {"import-export",  "Vehicle Export List", "am_imp_exp",        1820,19,Instance::MinusOne,Tier::Session,{},true},
    {"joyriders",      "Joyriders",           "am_joyrider",       3150,31,Instance::MinusOne,Tier::Session,{},true},
    {"security-van",   "Security Van",        "am_cr_securityvan", 3150, 2,Instance::MinusOne,Tier::Session,{},true},
    {"backup-heli",    "Backup Helicopter",   "am_backup_heli",    1024,26,Instance::Player,  Tier::Session,{},true},
    {"airstrike",      "Airstrike",           "am_airstrike",      1024,27,Instance::Player,  Tier::Session,{},true},
    {"ammo-drop",      "Ammo Drop",           "am_ammo_drop",      1024,28,Instance::MinusOne,Tier::Session,{},true},
    {"heli-pickup",    "Helicopter Pickup",   "am_heli_taxi",      1820,33,Instance::MinusOne,Tier::Session,{},true},
    {"boat-pickup",    "Boat Pickup",         "am_boat_taxi",      1820,32,Instance::Player,  Tier::Session,{},true},
    // triggers: startup 0x3dba7 (index 255, Strawberry) and 0x3dbc4 (index 256, Little Seoul), the positions launcher_carwash reads
    {"car-wash-1",     "Car Wash 1",          "carwash1",          1024,0xFF,Instance::MinusOne,Tier::World,
        {CarWashNeeds,{50.8179f,-1392.02f,28.4059f,CarWashRadius},"go-to-the-car-wash"}},
    {"car-wash-2",     "Car Wash 2",          "carwash2",          1024,0xFF,Instance::MinusOne,Tier::World,
        {CarWashNeeds,{-699.7f,-922.9f,18.5f,CarWashRadius},"go-to-the-car-wash"}},
}};
// v1350: the end area of a catalog entry, by its script (a lookup beside the catalog, so the rows keep their shape for the checkers
// that parse them); null = no end area found for that script.
inline const EndArea* endAreaOf(const Entry& e) noexcept {
    if(std::strcmp(e.script,"am_armybase")==0)return &ArmyBaseEnd;
    if(std::strcmp(e.script,"am_hold_up")==0)return &HoldUpEnd;
    return nullptr;
}
// v1013 (lead 12:4x): the disc's interaction menu as a PROBE, not in the app's list (Catalog / listEntry unchanged): action 19 a0 = 100
// starts it only while the L1 local session is up (local_session_flag.h). freemode's own table: type 17 = AM_PI_MENU (0x7663), category 6,
// stack 3150 (the stack rule 0x26559; fa_mission_table.py work/mission_table.tsv), started by the generic launcher 0x264e0 with the 22-word
// block. The instance is the block initialiser's default -1 (the disc's own type-17 launch site is not decoded).
// v1023: the probe holds the flag too. am_pi_menu's main loop (func 0x148, 0x1c4) calls 0x138f6, which returns 1 ("close") when
// 0x741(PLAYER_ID, 1, 1) fails (the flag check is on); then static39.f1 = 2 and 0x13769, so with the flag 0 the menu can never open.
inline constexpr Entry PiMenuProbe{"pi-menu","Interaction Menu (disc probe)","am_pi_menu",3150,17,Instance::MinusOne,Tier::Session,{},true};
// v1035 (lead / owner: the online intro the way GTA Online runs it): the game's own fm_intro, started as freemode starts it. freemode's
// direct launcher 0x9317 takes an explicit stack, FM_intro with 3150 (freeroam-activities NOTES "Direct launcher"); the generic one
// passes the 22-word block with field 0 = the mission type, FM_INTRO = 20 (work/mission_table.tsv). Session tier, local session only
// (its gates: G1344076 == 0 && !GIP -> end, !NETWORK_IS_SIGNED_ONLINE -> end, the host broadcast wait); the freemode flag is held as for
// every Session entry. A probe (action 19 a0 = 101), not an app row.
inline constexpr Entry FmIntroProbe{"fm-intro","Online intro (disc probe)","fm_intro",3150,20,Instance::MinusOne,Tier::Session,{},true};
constexpr int ProbeIndex=100,InternalProbe=int(Catalog.size());   // the action argument / the launcher's internal index
constexpr int FmIntroIndex=101,InternalFmIntro=int(Catalog.size())+1;   // v1035
inline const Entry& entryAt(int i) noexcept {return i==InternalProbe?PiMenuProbe:i==InternalFmIntro?FmIntroProbe:Catalog[std::size_t(i)];}
inline bool probeAllowed() noexcept {return ::recomp::local_session::active();}
inline int internalIndex(int a0) noexcept {return a0==ProbeIndex?InternalProbe:a0==FmIntroIndex?InternalFmIntro:a0;}

// Stack sizes found as literals at the disc's own START_NEW_SCRIPT(_WITH_ARGS) sites or in freemode's stack rule (NOTES.md 2.3).
// A size outside this set is refused: script stacks come from fixed pools and an unknown size is a guess.
inline constexpr std::array<std::uint16_t,12> DiscStacks{128,512,1024,1820,1828,2024,2048,3150,4592,9016,13300,15000};

inline bool stackKnown(std::uint16_t s) noexcept {for(auto v:DiscStacks)if(v==s)return true;return false;}
// List 8 "Activities" (recomp_trainer_list_v1): the catalog labels; the list's size, writing the label when index is in range.
inline int listEntry(int index,char* out,int cap) noexcept {
    const int count=int(Catalog.size());
    if(index>=0 && index<count && out && cap>0)std::snprintf(out,std::size_t(cap),"%s",Catalog[std::size_t(index)].label);
    return count;
}
inline int find(const char* key) noexcept {
    for(std::size_t i=0;i<Catalog.size();++i)if(std::strcmp(Catalog[i].key,key)==0)return int(i);
    return -1;
}

// The 22-word mission block exactly as freemode initialises it before a launch (0x92c7 / 0x58bf6 / 0x58d10: fields 1, 2, 11, 18, 21
// = -1, the rest 0), then field 0 = type and field 18 = the instance.
inline std::array<std::uint32_t,MissionWords> missionBlock(std::uint8_t type,std::uint32_t instance) noexcept {
    std::array<std::uint32_t,MissionWords> b{};
    b[1]=b[2]=b[11]=b[21]=0xFFFFFFFFu;b[0]=type;b[18]=instance;
    return b;
}

// FA-D1's diagnostic (NOTES.md 2.4 / Q3): the START_NEW_SCRIPT handler copies the CALLING thread's bytes 48..63 into the new thread
// where those are zero; the direct start does not. After each start the launcher logs those 4 words of the new thread and of the
// current (host) thread, guest loads only. The thread of an id is found as sub_83439478 finds it: the table at 0x83C5CE3C (+0 the
// array of thread pointers, +4 the count as a halfword), the entry whose word +4 equals the id. The current thread is the word at
// 0x83C5CA24 (sandbox_diag.h CurrentThread).
constexpr std::uint32_t ThreadTable=0x83C5CE3Cu,CurrentThreadWord=0x83C5CA24u,ThreadWordsFirst=48,ThreadWordsCount=4,ThreadTableMax=1024;
inline bool plausible(std::uint32_t p) noexcept {return p>=0x10000u && p<0xFFFF0000u && !(p&3u);}
inline std::uint32_t threadOf(Natives& nat,std::uint32_t id) noexcept {
    if(!id)return 0;
    const std::uint32_t array=nat.load(ThreadTable),count=nat.load(ThreadTable+4)>>16;
    if(!plausible(array) || count>ThreadTableMax)return 0;
    for(std::uint32_t k=0;k<count;++k){const std::uint32_t th=nat.load(array+4*k);if(plausible(th) && nat.load(th+4)==id)return th;}
    return 0;
}

// The glue's start: sub_82AAC528(name, block, bytes, stack) -> thread id (0 = failed). Tests mock it.
struct Starter {
    virtual ~Starter()=default;
    virtual std::uint32_t start(std::uint32_t nameAddr,std::uint32_t blockAddr,std::uint32_t blockBytes,std::uint32_t stack)=0;
    // v1017: one guest word store, range-checked by the glue (as DiscCreatorGlue::storeWord); false = refused. Only the fmflag hold uses it.
    virtual bool storeWord(std::uint32_t,std::uint32_t) {return false;}
};

constexpr std::uint32_t LoadFrames=300,PollFrames=30;
// v1027 (lead routing decision: am_pi_menu runs for the whole local session beside the catalog activities): the freemode flag has more
// than one user. The first launcher that writes the 0 -> 1 owns the write; later users only count; the 0 goes back when the LAST user
// releases, and only if a launcher wrote it and the word is still 1. Script thread only (the launchers' own rule).
struct FmFlagShare {unsigned users=0;bool wrote=false;std::uint32_t global=0;};
inline FmFlagShare& fmFlagShare() noexcept {static FmFlagShare s;return s;}
using LogFn=void(*)(const char*);

// One activity at a time (the requests come from the trainer queue, one per tick): a start is refused with Busy while another start
// is pending or while the tracked activity's thread still runs it, so a running activity is never orphaned. Not thread-safe: the
// glue calls it on the script thread only, like the trainer's own jobs.
class Launcher {
public:
    enum class Result : std::int8_t {Ok=0,Started=1,Pending=2,Busy=-1,BadIndex=-2,NeedsSession=-3,NotOnDisc=-4,LoadTimeout=-5,
        StartRefused=-6,NoScratch=-7,BadStack=-8,NotRunning=-9,AlreadyRunning=-10,NotReady=-11};
    explicit Launcher(LogFn log) noexcept:log_(log) {}
    ~Launcher() {if(held_){FmFlagShare& sh=fmFlagShare();if(sh.users)--sh.users;if(!sh.users)sh.wrote=false;}}   // v1027 (no guest write here)
    Launcher(const Launcher&)=delete;Launcher& operator=(const Launcher&)=delete;

    // Start request: validated now; the load happens over the next ticks. Busy while another start is pending or another tracked
    // activity still runs (one native to check it: only on a request, never per frame).
    // ungated (diag-activity-ungated, FA-D2 only; the Activities class passes it only in Sandbox mode): a Session entry starts without
    // a network game, so the script can be seen ending itself. Nothing is faked: the script reads the game's own state.
    Result begin(Natives& nat,int index,bool ungated=false) noexcept {
        if(index<0 || (index>=int(Catalog.size()) && !((index==InternalProbe || index==InternalFmIntro) && probeAllowed()))){logf("RENDER_TRAINER activity fail index=%d bad-index",index);return Result::BadIndex;}
        const Entry& e=entryAt(index);
        if(pending_>=0){logf("RENDER_TRAINER activity fail key=%s busy=%s",e.key,entryAt(pending_).key);return Result::Busy;}
        // v1017: am_pi_menu reads the same flag (helper 0x741, 29 sites): never start it while the launcher holds the flag
        if(held_ && index==InternalProbe && running_!=InternalProbe){logf("RENDER_TRAINER activity fail key=%s fmflag-held",e.key);return Result::Busy;}
        if(running_>=0) {
            const Entry& r=entryAt(running_);
            if(alive(nat,r)) {
                if(running_==index){logf("RENDER_TRAINER activity ok key=%s already-running id=%u",e.key,runningId_);return Result::AlreadyRunning;}
                logf("RENDER_TRAINER activity fail key=%s busy=%s",e.key,r.key);return Result::Busy;
            }
            logf("RENDER_TRAINER activity ended key=%s id=%u",r.key,runningId_);clearRunning();
        }
        if(!stackKnown(e.stack)){logf("RENDER_TRAINER activity fail key=%s bad-stack=%u",e.key,unsigned(e.stack));return Result::BadStack;}
        if(e.tier==Tier::Session && call1(nat,n::NetworkGame,{})!=1u){
            if(!ungated){logf("RENDER_TRAINER activity fail key=%s needs-session",e.key);return Result::NeedsSession;}
            logf("RENDER_TRAINER activity ungated key=%s network_game=0",e.key);}
        if(e.tier==Tier::Session && ::recomp::local_session::active())logGates(nat,e);   // v1015: read only, one line per Session start in the L1 session
        if(call1(nat,n::ThreadsWithHash,{joaat(e.script)})>0){
            logf("RENDER_TRAINER activity ok key=%s already-running",e.key);return Result::AlreadyRunning;}
        if(!ready(nat,e)){logf("RENDER_TRAINER activity fail key=%s %s",e.key,reason_);return Result::NotReady;}
        const std::uint32_t name=nat.text(e.script);
        if(!name || !call1(nat,n::DoesScriptExist,{name})){logf("RENDER_TRAINER activity fail key=%s not-on-disc",e.key);return Result::NotOnDisc;}
        call0(nat,n::RequestScript,{name});
        pending_=index;waited_=0;
        logf("RENDER_TRAINER activity request key=%s script=%s stack=%u",e.key,e.script,unsigned(e.stack));
        return Result::Pending;
    }

    // v1015 (t-acts-20261107-1314: am_joyrider still ends at once with gip=1): the inputs of the activities' entry gate, read only. am_joyrider
    // pc 0x59: NETWORK_IS_GAME_IN_PROGRESS && 0x1cfb(PLAYER_ID, 0, 1) = NETWORK_IS_PLAYER_ACTIVE(p) && IS_PLAYER_PLAYING(p) &&
    // global(1338499+3)[p] (written only by freemode); every frame (0x11bd): global 1341588.field7 set -> end, global 1344076 == 0 && !GIP
    // -> end (freeroam-activities NOTES 2.2). Globals through the VM's block table (0x83C5CE48, sizes 0x83C5CF50); -1 = not readable.
    static int readGlobal(Natives& nat,std::uint32_t gl) noexcept {
        const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,base=nat.load(0x83C5CE48u+4u*b),size=nat.load(0x83C5CF50u+4u*b);
        return (base && !(base&3u) && w<size)?int(nat.load(base+4u*w)):-1;
    }
    void logGates(Natives& nat,const Entry& e) noexcept {
        const std::uint32_t player=call1(nat,n::PlayerId,{});
        logf("RENDER_TRAINER activity gates key=%s gip=%u player=%u active=%u playing=%u fm_array_count=%d fm_player=%d g1344076=%d g1341588_7=%d",
             e.key,call1(nat,n::NetworkGame,{}),player,call1(nat,n::PlayerActive,{player}),call1(nat,n::PlayerPlaying,{player}),
             readGlobal(nat,1338502u),player<32?readGlobal(nat,1338502u+1u+player):-1,readGlobal(nat,1344076u),readGlobal(nat,1341588u+7u));
    }
    // v1017 (lead am-activity-exit, t-acts-20261107-1335 fm_player=0): am_joyrider's entry gate 0x1cfb reads global(1338503 + p), and
    // freemode (not running in our session) is its only writer; 0xaca and 0x1192 read it again during the run. Joyriders only
    // (Entry::fmFlag), local L1 session only: before the start, p < global(1338502) (the array count) is required, the word is read
    // through the block table, and only a 0 is written to 1 (Starter::storeWord, the glue's range check); the launcher holds it for the
    // run and restores the 0 only if it wrote the 1 and the word is still 1 (ended, stop, a failed start, session leave). Never written:
    // 1335094.f0, any f166 bit. Every write is logged with the value read before and after.
    static constexpr std::uint32_t FmFlagCount=1338502u,FmFlagFirst=1338503u;
    static std::uint32_t globalAddr(Natives& nat,std::uint32_t gl) noexcept {
        const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,base=nat.load(0x83C5CE48u+4u*b),size=nat.load(0x83C5CF50u+4u*b);
        return (base && !(base&3u) && w<size)?base+4u*w:0u;
    }
    bool holdFmFlag(Natives& nat,Starter& st,const Entry& e) noexcept {
        const std::uint32_t player=call1(nat,n::PlayerId,{});
        const int count=readGlobal(nat,FmFlagCount);
        if(count<=0 || player>=std::uint32_t(count)){logf("RENDER_TRAINER activity fmflag refused key=%s player=%u count=%d",e.key,player,count);return false;}
        const std::uint32_t gl=FmFlagFirst+player,a=globalAddr(nat,gl);
        if(!a){logf("RENDER_TRAINER activity fmflag refused key=%s player=%u no-address",e.key,player);return false;}
        FmFlagShare& sh=fmFlagShare();
        if(sh.users>0 && sh.global==gl){   // v1027: another launcher holds it: count, no write
            ++sh.users;held_=true;heldGlobal_=gl;heldAddr_=a;
            logf("RENDER_TRAINER activity fmflag shared key=%s users=%u value=%u",e.key,sh.users,nat.load(a));return true;}
        const std::uint32_t before=nat.load(a);
        if(before!=0){logf("RENDER_TRAINER activity fmflag before=%u kept key=%s player=%u global=%u",before,e.key,player,gl);return true;}
        if(!st.storeWord(a,1u)){logf("RENDER_TRAINER activity fmflag refused key=%s player=%u addr=%08x store",e.key,player,a);return false;}
        const std::uint32_t after=nat.load(a);
        held_=true;heldGlobal_=gl;heldAddr_=a;sh.users=1;sh.wrote=true;sh.global=gl;
        logf("RENDER_TRAINER activity fmflag before=0 after=%u key=%s player=%u global=%u addr=%08x",after,e.key,player,gl,a);
        return after==1u;
    }
    void releaseFmFlag(Natives& nat,Starter& st,const char* why) noexcept {
        if(!held_)return;
        held_=false;
        FmFlagShare& sh=fmFlagShare();
        if(sh.users>1){--sh.users;logf("RENDER_TRAINER activity fmflag released why=%s still-held users=%u",why,sh.users);heldGlobal_=heldAddr_=0;return;}
        sh.users=0;
        if(!sh.wrote){logf("RENDER_TRAINER activity fmflag released why=%s (not written by a launcher)",why);heldGlobal_=heldAddr_=0;return;}
        sh.wrote=false;
        const std::uint32_t a=globalAddr(nat,heldGlobal_),now=a==heldAddr_?nat.load(a):0xFFFFFFFFu;
        const bool wrote=a==heldAddr_ && now==1u && st.storeWord(a,0u);
        logf("RENDER_TRAINER activity fmflag restore why=%s global=%u was=%d restored=%d after=%d",why,heldGlobal_,a==heldAddr_?int(now):-1,
             wrote?1:0,a==heldAddr_?int(nat.load(a)):-1);
        heldGlobal_=heldAddr_=0;
    }
    void logPoll(Natives& nat,const Entry& e) noexcept {
        const std::uint32_t player=call1(nat,n::PlayerId,{}),name=nat.text(e.script),other=nat.text(ProbeControl);
        std::uint32_t out[3]{};
        const auto active=[&](std::uint32_t nm,std::uint32_t inst)->int{
            if(!nm)return -2;
            const std::uint32_t a[4]{nm,inst,0u,0u};out[0]=0;
            return nat.call(n::ScriptActive,a,4,out)?int(out[0]):-1;};
        logf("RENDER_TRAINER activity poll key=%s fmflag=%d held=%d nisa=%d nisa_control=%d gip=%u",e.key,
             player<32?readGlobal(nat,FmFlagFirst+player):-1,held_?1:0,active(name,instance(nat,e.inst)),active(other,0xFFFFFFFFu),
             call1(nat,n::NetworkGame,{}));
    }
    // v1025 (lead after t-pim-20261107-1556: am_pi_menu ran the whole time, the 1 s synthetic Back hold opened nothing). am_pi_menu opens
    // on its own hold timer: 0x13a82..0x13abb, IS_DISABLED_CONTROL_PRESSED(0, 0) and static bit 17 clear -> the 350 ms timer kept in
    // freemode's block global(1338499).f504.f2 (= global 1339005, 0x13c6b / 0x13c5e). While the probe runs, every frame: control 0 on
    // pad 0 (disabled-pressed / pressed), IS_PLAYER_CONTROL_ON(PLAYER_ID) and the two timer words; one line per change, at most 200.
    static constexpr std::uint32_t PiTimerGlobal=1339005u;static constexpr unsigned PiInputLinesMax=200;
    void piMenuInput(Natives& nat,std::uint32_t frame) noexcept {
        if(piLines_>=PiInputLinesMax)return;
        const std::uint32_t pc[2]{0u,0u};std::uint32_t out[3]{};
        const int dcp=nat.call(n::DisabledControlPressed,pc,2,out)?int(out[0]):-1;out[0]=0;
        const int cp=nat.call(n::ControlPressed,pc,2,out)?int(out[0]):-1;
        const int ctl=int(call1(nat,n::PlayerControlOn,{call1(nat,n::PlayerId,{})}));
        const int t0=readGlobal(nat,PiTimerGlobal),t1=readGlobal(nat,PiTimerGlobal+1u);
        const int key[5]{dcp,cp,ctl,t0,t1};
        bool same=piSeen_;for(int k=0;k<5;++k)same=same && key[k]==piLast_[k];
        if(same)return;
        for(int k=0;k<5;++k)piLast_[k]=key[k];piSeen_=true;++piLines_;
        logf("RENDER_TRAINER pimenu input frame=%u disabled_pressed0=%d pressed0=%d control_on=%d timer=%d,%d",frame,dcp,cp,ctl,t0,t1);
    }
    // v1026 (lead pi-menu-open.md, verified on the disc): am_pi_menu has no open-on-hold logic. Closed (menu state static57.f1 = 0) its
    // body 0x148 tests only G(1338499).f504.f6 >= 0 (0x17c-0x186; f504.fN = word 1339003 + N), then streams MPInteraction, runs init
    // 0x2a2e1 (f5 = PLAYER_ID if f5 < 0, 0x2a2f3) and goes to state 1. The producer is freemode's self branch 0xcb2e5-0xcb375:
    // IS_DISABLED_CONTROL_PRESSED(0, 0) && f4 == 0 && f5 == -1 && f6 == -1 && f12 == 0, 350 ms (0xaa11) and the gate 0xcb3ed(PLAYER_ID)
    // -> DISABLE_CONTROL_ACTION(0, 0), f5 = PLAYER_ID, f6 = 0. freemode does not run in the Sandbox, so nothing writes f6. The shim, only
    // while the launcher's am_pi_menu probe runs in the local L1 session: the same hold on GET_GAME_TIMER with IS_PLAYER_CONTROL_ON and
    // the idle fields (f10 == 0 too: 0xcb3ed refuses on it), then DISABLE_CONTROL_ACTION(0, 0) and ONLY 1339009 = 0 (f5 left at -1, so
    // f5 = PLAYER_ID afterwards proves pi_menu's init ran). Not replicated: 0xcb3ed's other checks (freemode-internal helpers, header).
    // f5 / f6 are logged every 150 frames for 3600 frames after the request (an open near 120 s = the host broadcast wait S5; none = S3 or
    // S7). After the probe ends or the session leaves, the idle words f4 / f5 / f6 / f12 go back to the idle snapshot where they differ
    // (an outside TERMINATE skips pi_menu's own cleanup 0x1ea), each write logged.
    static constexpr std::uint32_t PiF4=1339007u,PiF5=1339008u,PiF6=1339009u,PiF10=1339013u,PiF12=1339015u;
    static constexpr std::uint32_t PiHoldMs=350,PiWatchFrames=3600,PiWatchEvery=150;
    static constexpr unsigned PiChangeMax=24;int piLastKey_[5]{-9,-9,-9,-9,-9};unsigned piChangeLines_=0;   // v1097
    void piOpenShim(Natives& nat,Starter& st,std::uint32_t frame) noexcept {
        const int f4=readGlobal(nat,PiF4),f5=readGlobal(nat,PiF5),f6=readGlobal(nat,PiF6),f10=readGlobal(nat,PiF10),f12=readGlobal(nat,PiF12);
        if(piArmed_) {
            if(frame-piArmedFrame_<=PiWatchFrames && (frame-piArmedFrame_)%PiWatchEvery==0)
                logf("RENDER_TRAINER pimenu request watch t=%u f4=%d f5=%d f6=%d f10=%d f12=%d",frame-piArmedFrame_,f4,f5,f6,f10,f12);
            {   // v1097 (lead 11:27: in Online mode am_pi_menu closed itself within 150 frames, never drawn): every change of the request words
                // while armed, with its frame, so init (f5 = PLAYER_ID) then the cleanup (f5 = f6 = -1) are timed exactly; at most PiChangeMax lines
                const int key[5]{f4,f5,f6,f10,f12};
                if(std::memcmp(key,piLastKey_,sizeof key)!=0 && piChangeLines_<PiChangeMax){std::memcpy(piLastKey_,key,sizeof key);++piChangeLines_;
                    logf("RENDER_TRAINER pimenu words changed t=%u f4=%d f5=%d f6=%d f10=%d f12=%d",frame-piArmedFrame_,f4,f5,f6,f10,f12);}
            }
            if(f6==-1 && f5==-1 && frame-piArmedFrame_>PiWatchEvery){   // pi_menu closed itself (cleanup 0x1ea: f6 = -1, f5 = -1)
                logf("RENDER_TRAINER pimenu closed by the script t=%u f12=%d",frame-piArmedFrame_,f12);piArmed_=false;piHoldFrom_=0;}
            return;
        }
        const std::uint32_t pc[2]{0u,0u};std::uint32_t out[3]{};
        const bool touch=piTouch_.exchange(false,std::memory_order_acq_rel);   // v1027: the app's side-menu row (recomp_imenu_v1(5))
        const bool held=nat.call(n::DisabledControlPressed,pc,2,out) && out[0]!=0;
        if(!held && !touch){piHoldFrom_=0;return;}
        const bool idle=f4==0 && f5==-1 && f6==-1 && f10==0 && f12==0;
        const bool ctl=call1(nat,n::PlayerControlOn,{call1(nat,n::PlayerId,{})})!=0;
        if(!idle || !ctl){if(piHoldFrom_){logf("RENDER_TRAINER pimenu hold refused f4=%d f5=%d f6=%d f10=%d f12=%d control=%d",f4,f5,f6,f10,f12,ctl?1:0);}
            piHoldFrom_=0;return;}
        const std::uint32_t now=call1(nat,n::GameTimer,{})|1u;   // 0 means "not holding"
        if(touch){piHoldFrom_=now-PiHoldMs;logf("%s","RENDER_TRAINER pimenu request via touch");}   // no hold for the touch row
        if(!piHoldFrom_){piHoldFrom_=now;return;}
        if(now-piHoldFrom_<PiHoldMs)return;
        const std::uint32_t a=globalAddr(nat,PiF6);
        if(!a){logf("%s","RENDER_TRAINER pimenu request refused no-address");piHoldFrom_=0;return;}
        call0(nat,n::DisableControl,{0u,0u});
        piSnap_[0]=f4;piSnap_[1]=f5;piSnap_[2]=f6;piSnap_[3]=f12;
        const bool ok=st.storeWord(a,0u);const std::uint32_t heldMs=now-piHoldFrom_;
        piArmed_=ok;piArmedFrame_=frame;piHoldFrom_=0;
        logf("RENDER_TRAINER pimenu request f6 before=%d after=%d ok=%d held_ms=%u f5=%d (freemode 0xcb2e5 self branch; f5 left for pi_menu's init)",
             f6,readGlobal(nat,PiF6),ok?1:0,heldMs,f5);
    }
    void piRestore(Natives& nat,Starter& st,const char* why) noexcept {
        piArmed_=false;piHoldFrom_=0;
        static constexpr std::uint32_t Words[4]{PiF4,PiF5,PiF6,PiF12};
        for(int k=0;k<4;++k){
            const int now=readGlobal(nat,Words[k]);const std::uint32_t a=globalAddr(nat,Words[k]);
            if(!a || now==piSnap_[k])continue;
            const bool ok=st.storeWord(a,std::uint32_t(piSnap_[k]));
            logf("RENDER_TRAINER pimenu restore why=%s global=%u was=%d back=%d ok=%d",why,Words[k],now,piSnap_[k],ok?1:0);
        }
    }
    void requestMenuOpen() noexcept {piTouch_.store(true,std::memory_order_release);}   // v1027: any thread; served on the next tick
    bool menuOpen() const noexcept {return piArmed_;}   // v1027: the request is out and pi_menu has not closed itself
    static constexpr const char* ProbeControl="am_armwrestling";   // the control call (expect 0): v1020 a disc script outside the catalog
                                                                    // (am_hold_up, v1017's control, is a catalog entry the runs now start)
    bool fmFlagHeld() const noexcept {return held_;}
    // Every game frame (Sandbox, trainer safe point). Idle: returns at once, no native.
    Result tick(Natives& nat,Starter& st,std::uint32_t frame) noexcept {
        if(running_==InternalProbe)piMenuInput(nat,frame);   // v1025: read only, logged on change
        if(running_==InternalProbe && ::recomp::local_session::active())piOpenShim(nat,st,frame);   // v1026
        else if(piArmed_)piRestore(nat,st,running_==InternalProbe?"session-left":"probe-ended");
        if(held_ && (running_<0 || !::recomp::local_session::active()))   // ended / stopped (next tick) or the session left
            releaseFmFlag(nat,st,running_<0?"ended":"session-left");
        if(pending_<0)return pollRunning(nat,frame);
        const Entry& e=entryAt(pending_);
        const std::uint32_t name=nat.text(e.script);
        if(!name){finish();logf("RENDER_TRAINER activity fail key=%s no-scratch",e.key);return Result::NoScratch;}
        if(!call1(nat,n::HasScriptLoaded,{name})) {
            if(++waited_<LoadFrames){call0(nat,n::RequestScript,{name});return Result::Pending;}
            call0(nat,n::ScriptNoLongerNeeded,{name});finish();
            logf("RENDER_TRAINER activity fail key=%s load-timeout frames=%u",e.key,unsigned(LoadFrames));return Result::LoadTimeout;
        }
        if(!ready(nat,e)){call0(nat,n::ScriptNoLongerNeeded,{name});finish();   // the world changed while the script loaded
            logf("RENDER_TRAINER activity fail key=%s %s",e.key,reason_);return Result::NotReady;}
        std::uint32_t block=0,bytes=0;
        if(e.type!=0xFF) {
            const std::uint32_t inst=instance(nat,e.inst);
            const auto b=missionBlock(e.type,inst);
            block=nat.word(b[0]);
            for(unsigned k=1;k<MissionWords && block;++k)if(nat.word(b[k])!=block+4*k)block=0;   // contiguous or nothing
            if(!block){call0(nat,n::ScriptNoLongerNeeded,{name});finish();
                logf("RENDER_TRAINER activity fail key=%s no-scratch",e.key);return Result::NoScratch;}
            bytes=4*MissionWords;
        }
        if(e.fmFlag && ::recomp::local_session::active() && !holdFmFlag(nat,st,e)){
            call0(nat,n::ScriptNoLongerNeeded,{name});finish();
            logf("RENDER_TRAINER activity fail key=%s fmflag-unavailable",e.key);return Result::NotReady;}
        const std::uint32_t id=st.start(name,block,bytes,e.stack);
        call0(nat,n::ScriptNoLongerNeeded,{name});
        const int index=pending_;finish();
        if(!id){releaseFmFlag(nat,st,"start-refused");logf("RENDER_TRAINER activity fail key=%s start-refused",e.key);return Result::StartRefused;}
        running_=index;runningId_=id;lastPoll_=frame;startFrame_=frame;
        logf("RENDER_TRAINER activity ok key=%s id=%u stack=%u args=%u",e.key,id,unsigned(e.stack),bytes/4);
        logThreadWords(nat,e,id);
        return Result::Started;
    }

    // Stop: only the thread this launcher started, and only while it still runs that script (a thread id can be reused). A stop for
    // any other entry changes nothing: the tracked activity stays tracked and stoppable.
    Result stop(Natives& nat,int index) noexcept {
        if(index<0 || (index>=int(Catalog.size()) && index!=InternalProbe && index!=InternalFmIntro))return Result::BadIndex;
        const Entry& e=entryAt(index);
        if(pending_==index){if(const std::uint32_t nm=nat.text(e.script))call0(nat,n::ScriptNoLongerNeeded,{nm});finish();
            logf("RENDER_TRAINER activity stop key=%s cancelled",e.key);return Result::Ok;}
        if(running_!=index){logf("RENDER_TRAINER activity stop key=%s not-running",e.key);return Result::NotRunning;}
        if(!alive(nat,e)){clearRunning();logf("RENDER_TRAINER activity stop key=%s not-running",e.key);return Result::NotRunning;}
        call0(nat,n::TerminateThread,{runningId_});
        logf("RENDER_TRAINER activity stop key=%s id=%u terminated",e.key,runningId_);
        clearRunning();return Result::Ok;
    }
    int pending() const noexcept {return pending_;}
    int running() const noexcept {return running_;}
    std::uint32_t runningId() const noexcept {return runningId_;}
    const char* reason() const noexcept {return reason_;}   // the last NotReady answer's words ("needs-vehicle setup=spawn-vehicle")

    // The entry's preconditions, in the launcher's order (NOTES.md): true = all met. Otherwise reason() holds the first unmet one.
    // Natives only, no write, no guest load.
    bool ready(Natives& nat,const Entry& e) noexcept {
        reason_[0]=0;
        const Preconditions& p=e.pre;
        if(!p.need)return endReady(nat,e);   // v1350
        bool ok=true;   // false once a native was not available: refused, never treated as met
        const auto q=[&](std::uint32_t h,std::initializer_list<std::uint32_t> a,std::uint32_t* out3=nullptr)->std::uint32_t{
            std::uint32_t out[3]{};if(!nat.call(h,a.begin(),unsigned(a.size()),out))ok=false;
            if(out3){out3[0]=out[0];out3[1]=out[1];out3[2]=out[2];}
            return out[0];};
        const auto no=[&](const char* why)->bool{
            std::snprintf(reason_,sizeof reason_,"%s",ok?why:"precondition-unavailable");return false;};
        if(p.need&NoMission){const bool on=q(n::MissionFlag,{})!=0;if(!ok || on)return no("on-mission");}
        if(p.need&NoWanted){const std::uint32_t player=q(n::PlayerId,{});const bool w=q(n::WantedLevel,{player})!=0;if(!ok || w)return no("wanted");}
        const std::uint32_t ped=(p.need&(DrivingCar|AtTrigger))?q(n::PlayerPed,{}):0u;
        if(!ok)return no("");
        if(p.need&DrivingCar) {
            if(!q(n::InAnyVehicle,{ped,0}) || !ok)return no("needs-vehicle setup=spawn-vehicle");
            const std::uint32_t veh=q(n::VehicleOfPed,{ped,0});
            if(!veh || !ok)return no("needs-vehicle setup=spawn-vehicle");
            if(q(n::PedInSeat,{veh,0xFFFFFFFFu})!=ped || !ok)return no("needs-driver");
            const std::uint32_t model=q(n::EntityModel,{veh});
            if(!q(n::ModelIsCar,{model}) || !ok)return no("needs-car setup=spawn-vehicle");
            if(q(n::HasTrailer,{veh}) || !ok)return no("has-trailer");
            if(!q(n::Driveable,{veh,0}) || q(n::OnFire,{veh}) || !ok)return no("vehicle-damaged");
        }
        if(p.need&AtTrigger) {
            std::uint32_t c[3]{};(void)q(n::EntityCoords,{ped,1},c);
            if(!ok)return no("");
            const float dx=trainer::asFloat(c[0])-p.at.x,dy=trainer::asFloat(c[1])-p.at.y,dz=trainer::asFloat(c[2])-p.at.z;
            const float d2=dx*dx+dy*dy+dz*dz;
            if(!(d2<=p.at.radius*p.at.radius)) {
                std::snprintf(reason_,sizeof reason_,"%s dist=%.0f setup=teleport-waypoint",p.goTo,double(std::sqrt(d2)));return false;}
        }
        return endReady(nat,e);   // v1350
    }
    // v1350: the entry's END area (above). true = inside it, or no area, or the switch is off. Natives only (PLAYER_PED_ID,
    // GET_ENTITY_COORDS); an unavailable native refuses ("precondition-unavailable"), never passes.
    void setEndCond(bool on) noexcept {endCond_=on;}
    bool endCond() const noexcept {return endCond_;}
    bool endReady(Natives& nat,const Entry& e) noexcept {
        const EndArea* area=endCond_?endAreaOf(e):nullptr;
        if(!area)return true;
        const EndArea& a=*area;
        std::uint32_t out[3]{};
        if(!nat.call(n::PlayerPed,nullptr,0,out)){std::snprintf(reason_,sizeof reason_,"precondition-unavailable");return false;}
        const std::uint32_t ped[2]{out[0],0u};std::uint32_t c[3]{};
        if(!nat.call(n::EntityCoords,ped,2,c)){std::snprintf(reason_,sizeof reason_,"precondition-unavailable");return false;}
        const float x=trainer::asFloat(c[0]),y=trainer::asFloat(c[1]),z=trainer::asFloat(c[2]);
        const auto d2=[&](const EndSpot& s){const float dx=x-s.x,dy=y-s.y,dz=z-s.z;return dx*dx+dy*dy+dz*dz;};
        unsigned i=0;
        if(a.kind==EndKind::NearestBox){float best=d2(a.pick[0]);for(unsigned k=1;k<a.count;++k){const float v=d2(a.pick[k]);if(v<best){best=v;i=k;}}}
        const EndSpot& s=a.at[i];
        const bool in=a.kind==EndKind::Sphere?d2(s)<a.size*a.size
                     :(std::fabs(x-s.x)<a.size && std::fabs(y-s.y)<a.size && std::fabs(z-s.z)<a.size);
        if(in)return true;
        if(a.kind==EndKind::Sphere)std::snprintf(reason_,sizeof reason_,"%s dist=%.0f setup=teleport-waypoint",a.goTo,double(std::sqrt(d2(s))));
        else std::snprintf(reason_,sizeof reason_,"%s store=%u dist=%.0f setup=teleport-waypoint",a.goTo,i,double(std::sqrt(d2(s))));
        return false;
    }

private:
    static std::uint32_t call1(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};return nat.call(h,a.begin(),unsigned(a.size()),out)?out[0]:0u;
    }
    static void call0(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};(void)nat.call(h,a.begin(),unsigned(a.size()),out);
    }
    static std::uint32_t instance(Natives& nat,Instance i) noexcept {
        if(i==Instance::MinusOne)return 0xFFFFFFFFu;
        const std::uint32_t player=call1(nat,n::PlayerId,{});
        return i==Instance::Player?player:call1(nat,n::PlayerTeam,{player});
    }
    bool alive(Natives& nat,const Entry& e) noexcept {
        const std::uint32_t p=call1(nat,n::NameOfThread,{runningId_});
        return p && nameIs(nat,p,e.script);
    }
    Result pollRunning(Natives& nat,std::uint32_t frame) noexcept {
        if(running_<0 || frame-lastPoll_<PollFrames)return Result::Ok;
        lastPoll_=frame;
        const Entry& e=entryAt(running_);
        if(e.tier==Tier::Session && ::recomp::local_session::active())logPoll(nat,e);   // v1017: read only (the flag, two probe natives)
        if(call1(nat,n::ThreadsWithHash,{joaat(e.script)})==0) {
            // ended within two polls of its start: the script stopped itself early (a first-frame exit), never a silent "ended"
            if(frame-startFrame_<=2*PollFrames)logf("RENDER_TRAINER activity ended key=%s id=%u early frames=%u",e.key,runningId_,unsigned(frame-startFrame_));
            else logf("RENDER_TRAINER activity ended key=%s id=%u",e.key,runningId_);
            clearRunning();
        }
        return Result::Ok;
    }
    // The guest string at `at` equals `want` (lower case, as every catalog name is; tested) ignoring ASCII case: same length, no prefix
    // match. Reads at most 64 bytes; anything else (longer, a control byte, a different byte) is not a match.
    static bool nameIs(Natives& nat,std::uint32_t at,const char* want) noexcept {
        for(std::uint32_t i=0;i<64;++i){
            const std::uint32_t a=at+i,w=nat.load(a&~3u);char c=char((w>>(24-8*(a&3u)))&0xFFu);
            if(c>='A'&&c<='Z')c=char(c+32);
            if(c!=want[i])return false;
            if(!c)return true;
        }
        return false;
    }
    void logThreadWords(Natives& nat,const Entry& e,std::uint32_t id) noexcept {
        const std::uint32_t th=threadOf(nat,id),host=nat.load(CurrentThreadWord);
        std::uint32_t w[2][ThreadWordsCount]{};
        for(std::uint32_t k=0;k<ThreadWordsCount;++k) {
            if(th)w[0][k]=nat.load(th+ThreadWordsFirst+4*k);
            if(plausible(host))w[1][k]=nat.load(host+ThreadWordsFirst+4*k);
        }
        logf("RENDER_TRAINER activity thread key=%s id=%u thread=%08x host=%08x new48=%08x,%08x,%08x,%08x host48=%08x,%08x,%08x,%08x",e.key,id,th,
             plausible(host)?host:0u,w[0][0],w[0][1],w[0][2],w[0][3],w[1][0],w[1][1],w[1][2],w[1][3]);
    }
    void finish() noexcept {pending_=-1;waited_=0;}
    void clearRunning() noexcept {running_=-1;runningId_=0;}
    template<class... A> void logf(const char* f,A... a) const noexcept {if(!log_)return;char l[256];std::snprintf(l,sizeof l,f,a...);log_(l);}
    LogFn log_;int pending_=-1,running_=-1;std::uint32_t waited_=0,runningId_=0,lastPoll_=0,startFrame_=0;char reason_[80]{};
    bool held_=false;std::uint32_t heldGlobal_=0,heldAddr_=0;
    bool endCond_=false;   // v1350
    bool piSeen_=false;unsigned piLines_=0;int piLast_[5]{};   // v1025: the am_pi_menu input probe
    bool piArmed_=false;std::uint32_t piHoldFrom_=0,piArmedFrame_=0;int piSnap_[4]{0,-1,-1,0};   // v1026: the freemode open shim
    std::atomic<bool> piTouch_{false};   // v1027   // v1017: the fmflag this launcher wrote (holdFmFlag / releaseFmFlag)
};

// The trainer's action 19 (trainer.h ActivityActions). Sandbox mode only: the glue hands it to the trainer only there, and request()
// asks the mode function again (none = Story: refused). The ungated switch is honoured only while the mode function says Sandbox.
class Activities final : public recomp::trainer::ActivityActions {
public:
    using ModeFn=bool(*)();
    enum Op : int {Start=0,Stop=1};
    Activities(LogFn log,ModeFn sandbox) noexcept:launcher_(log),menu_(log),log_(log),sandbox_(sandbox) {}
    bool validArgument(int a0,int a1) const noexcept override {return ((a0>=0 && a0<int(Catalog.size())) || ((a0==ProbeIndex || a0==FmIntroIndex) && probeAllowed())) && (a1==Start || a1==Stop);}
    int request(Natives& nat,int a0,int a1,char* detail,int cap) noexcept override {
        const auto say=[&](const char* f,const char* key,const char* w){if(detail && cap>0)std::snprintf(detail,std::size_t(cap),f,key,w);};
        if(!sandbox_ || !sandbox_()){say("key=%s %s",validArgument(a0,a1)?entryAt(internalIndex(a0)).key:"?","sandbox-only");return 2;}
        if(!validArgument(a0,a1)){say("key=%s %s","?","bad-argument");return 2;}
        const Entry& e=entryAt(internalIndex(a0));
        Launcher& L=a0==ProbeIndex?menu_:launcher_;   // v1027: am_pi_menu has its own launcher (it runs for the whole session)
        const Launcher::Result r=a1==Stop?L.stop(nat,internalIndex(a0)):L.begin(nat,internalIndex(a0),a0>=ProbeIndex?false:ungated());
        say("key=%s %s",e.key,r==Launcher::Result::NotReady?L.reason():resultName(r));   // the precondition's own words
        return int(r)>=0 || r==Launcher::Result::AlreadyRunning?1:2;
    }
    // Every Sandbox tick (the glue, after the Sandbox core): the pending load / start and the 30-frame poll; idle = no native call.
    Launcher::Result tick(Natives& nat,Starter& st,std::uint32_t frame) noexcept {
        const Launcher::Result r=launcher_.tick(nat,st,frame);
        menuTick(nat,st,frame);
        return r;
    }
    // v1027 (lead routing decision, owner-delegated): in the local online session (local_session::active() and gip 1) am_pi_menu is started
    // once the game reports the network game and kept for the session (it ends itself at the leave; its launcher then releases the
    // flag); a Select hold (350 ms) or the app's side-menu row opens it through the v1026 shim. Outside the session: nothing runs here (one
    // atomic load; the menu launcher's idle tick makes no native call). The gip read is once per MenuRetryFrames, at most MenuMaxAttempts
    // starts per session; realMenuAvailable() / realMenuOpen() feed state bits 24 / 25 (the app yields its hold-Select on 24) and the
    // replica's opener / Back gate yield on 24 (glue).
    static constexpr std::uint32_t MenuRetryFrames=150;static constexpr unsigned MenuMaxAttempts=5;
    void menuTick(Natives& nat,Starter& st,std::uint32_t frame) noexcept {
        menu_.tick(nat,st,frame);
        const bool session=::recomp::local_session::active();
        // v1028 (t-route-20261107-1700: the leave ended am_pi_menu and the auto-start retried in the same tick, refused fmflag-held while the
        // old hold was still out): after the menu's thread ends, the next attempt waits a full MenuRetryFrames (by then the leave has
        // cleared gip, so no start during the leave).
        const bool menuUp=menu_.running()==InternalProbe;
        if(menuWasUp_ && !menuUp){menuTried_=true;menuLastTry_=frame;}
        menuWasUp_=menuUp;
        avail_.store(session && menu_.running()==InternalProbe,std::memory_order_release);
        open_.store(session && menu_.running()==InternalProbe && menu_.menuOpen(),std::memory_order_release);
        if(!session){menuAttempts_=0;menuTried_=false;return;}
        if(menu_.pending()>=0 || menu_.running()>=0)return;
        if(menuTried_ && frame-menuLastTry_<MenuRetryFrames)return;
        menuTried_=true;menuLastTry_=frame;
        if(menuAttempts_>=MenuMaxAttempts)return;
        std::uint32_t out[3]{};
        if(!nat.call(n::NetworkGame,nullptr,0,out) || out[0]!=1u)return;   // the session is up only once gip is 1
        ++menuAttempts_;
        const Launcher::Result r=menu_.begin(nat,InternalProbe);
        if(log_){char l[120];std::snprintf(l,sizeof l,"RENDER_TRAINER pimenu auto-start attempt=%u result=%s",menuAttempts_,resultName(r));log_(l);}
    }
    bool realMenuAvailable() const noexcept {return avail_.load(std::memory_order_acquire);}   // any thread
    bool realMenuOpen() const noexcept {return open_.load(std::memory_order_acquire);}
    bool requestRealMenuOpen() noexcept {if(!realMenuAvailable())return false;menu_.requestMenuOpen();return true;}
    const Launcher& menuLauncher() const noexcept {return menu_;}
    // files/private-trace/diag-activity-ungated (the glue reads it in Sandbox mode only): one log line per change.
    void setUngatedSwitch(bool on) noexcept {
        if(on==switch_)return;
        switch_=on;
        if(log_){char l[96];std::snprintf(l,sizeof l,"RENDER_TRAINER activity ungated-switch=%d sandbox=%d",on?1:0,(sandbox_ && sandbox_())?1:0);log_(l);}
    }
    bool ungated() const noexcept {return switch_ && sandbox_ && sandbox_();}
    // v1350: the end-area preconditions, on unless files/private-trace/diag-activity-endcond-off (the glue reads it in Sandbox mode only); one log line per change.
    void setEndCondSwitch(bool on) noexcept {
        if(on==launcher_.endCond())return;
        launcher_.setEndCond(on);
        if(log_){char l[80];std::snprintf(l,sizeof l,"RENDER_TRAINER activity endcond-switch=%d",on?1:0);log_(l);}
    }
    const Launcher& launcher() const noexcept {return launcher_;}
    static const char* resultName(Launcher::Result r) noexcept {
        switch(r) {
        case Launcher::Result::Ok: return "stopped";case Launcher::Result::Started: return "started";case Launcher::Result::Pending: return "pending";
        case Launcher::Result::Busy: return "busy";case Launcher::Result::BadIndex: return "bad-index";
        case Launcher::Result::NeedsSession: return "needs-session";case Launcher::Result::NotOnDisc: return "not-on-disc";
        case Launcher::Result::LoadTimeout: return "load-timeout";case Launcher::Result::StartRefused: return "start-refused";
        case Launcher::Result::NoScratch: return "no-scratch";case Launcher::Result::BadStack: return "bad-stack";
        case Launcher::Result::NotRunning: return "not-running";case Launcher::Result::AlreadyRunning: return "already-running";
        case Launcher::Result::NotReady: return "not-ready";
        }
        return "?";
    }
private:
    Launcher launcher_,menu_;LogFn log_;ModeFn sandbox_;bool switch_=false;
    std::atomic<bool> avail_{false},open_{false};unsigned menuAttempts_=0;bool menuTried_=false;std::uint32_t menuLastTry_=0;   // v1027
    bool menuWasUp_=false;   // v1028
};

} // namespace recomp::activities
