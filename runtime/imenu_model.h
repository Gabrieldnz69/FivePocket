#pragma once
// imenu_model.h (Update 1, the Interaction Menu replica for the offline Sandbox mode; lane upd1-imenu/model-rows, 2026-11-07).
// Route B of local-analysis/.agents/upd1-menus/MENUS-PLAN.md: our own copy of the disc's am_pi_menu (the 2013 launch menu), drawn
// with the game's own art, labels, fonts and sounds by the ONE shared renderer (upd1-creator-menu/shop-menu-look menu_draw.h, extended
// by the sibling lane upd1-imenu/draw-glue). This header is the MODEL only: the menu tree, navigation, the open / close rules, the
// per-row actions and the row visibility rules. It draws nothing itself; it hands a View (rows, header, counter, buttons, sounds) to
// the renderer each frame while open.
//
// Pure: no guest memory, no platform calls, no allocation (fixed arrays only). Natives through trainer::Natives (the glue's
// GuestNatives on the script thread, the tests' mock). Sandbox only: Story mode never opens it (Context::sandbox false = nothing).
//
// Frame rate: CLOSED and idle, one tick = exactly one native call (IS_DISABLED_CONTROL_PRESSED(0, 0), the disc's own hold-Back read)
// and nothing else. The game timer is read only while Back is held, during the 1 s cool-down and while open. While open: 6
// SET_INPUT_EXCLUSIVE + 24 (25 on the inventory page) DISABLE_CONTROL_ACTION, 7 control reads, the gate, then the renderer's draws.
//
// Evidence tags: OBSERVED = read from the disc's am_pi_menu / freemode with the lane tools (offset named), or from our own code.
// INFERRED = our reading, our Sandbox reinterpretation, or public knowledge; each is listed in ../NOTES.md section 5.
#include "trainer.h"
#include "imenu_back_gate.h"   // v989 (E3): Back as the player holds it, from the library's pad read
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::imenu {
using trainer::Natives;
using trainer::joaat;
using trainer::bits;
using trainer::asFloat;

// ------------------------------------------------------------------------------------------------------------- disc constants
constexpr std::uint32_t HoldOpenMs=350;      // freemode 0xcb335: IS_DISABLED_CONTROL_PRESSED(0,0) held 350 ms (timer 0xaa11)
constexpr std::uint32_t HoldCloseMs=350;     // am_pi_menu 0x13aa3: Back held again 350 ms while open closes (CANCEL)
constexpr std::uint32_t CooldownMs=1000;     // am_pi_menu 0x2bde3: 1000 ms after close (175 exclusive, 43/52/73 disabled)
constexpr std::int32_t RepeatMs=250;         // am_pi_menu 0x29995/0x29a7c/0x29beb/0x29d63: one shared repeat delay, -1 when released
constexpr std::uint32_t SnackCooldownMs=30000;  // am_pi_menu 0x13c6b caller (NOTES 3.2): 30,000 ms between snacks
constexpr std::uint32_t KillControlFlags=900;   // am_pi_menu 0x1ed86: SET_PLAYER_CONTROL(player, FALSE, 900); 900 = the flags word
                                                // (third argument), not a time. Role of its bits: not decoded.
constexpr std::uint32_t KillDeathWaitFrames=90; // ours: frames to see IS_ENTITY_DEAD after health 0, else control is given back
constexpr int MaxVisible=7;                  // am_pi_menu 0x259d8: menu field 3990 = 7 (the "n / m" counter above 7)
constexpr int GroupGame=0,GroupFrontend=2;
constexpr int CtlBack=0;                     // group 0 control 0 (freemode 0xcb2e7, am_pi_menu 0x13a84)
constexpr int CtlNext=161,CtlPrev=162;       // am_pi_menu 0x298a3: 161 -> row + 1 (wrap to 0); 0x299a9: 162 -> row - 1 (wrap to last)
constexpr int CtlLeft=163,CtlRight=164;      // 0x29a90: 163 -> value - 1 (wrap); 0x29bff: 164 -> value + 1
constexpr int CtlAccept=174,CtlCancel=175;   // 0x13e30 accept; 0x13947 cancel (IS_CONTROL_JUST_PRESSED(2,175))
inline constexpr int ExclusiveControls[6]{174,175,162,161,163,164};                    // 0x2bca2..0x2bcd0, group 2
inline constexpr int DisabledControls[24]{37,136,137,138,139,140,141,142,143,144,14,15,50,51,120,121,123,45,67,76,49,43,52,73}; // 0x2bcd4.., group 0
constexpr int InventoryDisabledControl=22;   // 0x2bd70: only on the inventory page (25 distinct with it)
constexpr int CooldownExclusive=175;
inline constexpr int CooldownDisabled[3]{43,52,73};
constexpr std::uint32_t WeaponUnarmed=0xA2719263u;      // am_pi_menu 0x20dc5 (PUSH -1569615261 = joaat("weapon_unarmed"))
constexpr std::uint32_t WeaponParachute=0xFBAB5776u;    // am_pi_menu inventory (PUSH -72657034 = joaat("gadget_parachute"))
constexpr int ParachuteComponent=5,ParachuteDrawable=8; // am_pi_menu (SET_PED_COMPONENT_VARIATION(ped,5,8,0,0) / (ped,5,0,0,0))
constexpr int MaskComponent=1;                          // MENUS-PLAN verifier: masks are component 1, not a prop
constexpr int HatAnchor=0,GlassesAnchor=1;              // INFERRED (prop anchors 0 head, 1 eyes; PIM_THELM / PIM_TGLAS rows)
// Kill Yourself's death step is freemode's (0x691ff..0x694ac, run while am_pi_menu's flag 20.84.194+5 is set): an instant death in a
// vehicle, in water or in the air; else the MP_SUICIDE clip "PISTOL" (pistol with ammo and no suppressor) or "PILL", the "Fire" anim
// event (the pistol shot), and at the clip's phase 0.365 / 0.536 a ragdoll and health 0.
constexpr std::uint32_t WeaponPistol=0x1B06D571u;       // freemode 0x692ba (PUSH 453432689 = joaat("weapon_pistol"))
constexpr std::uint32_t PistolSuppressor=0xC304849Au;   // freemode 0x692e5 (PUSH -1023114086 = joaat("component_at_pi_supp"))
constexpr const char* SuicideDict="MP_SUICIDE";         // freemode 0x69384
constexpr const char* SuicidePistol="PISTOL";constexpr const char* SuicidePill="PILL";   // 0x6932b / 0x69365
constexpr float SuicidePistolPhase=0.365f,SuicidePillPhase=0.536f;                       // 0x69343 / 0x6936f
constexpr std::uint32_t SuicideAnimFlags=0x202000u;     // 0x69397 PUSH_U24 32 32 0 (big-endian operand)
constexpr std::uint32_t FireEvent=joaat("Fire");        // 0x693e5 GET_HASH_KEY("Fire"): joaat, computed here (no native call)
constexpr int RagdollTimeMs=250;                        // 0x69446 SET_PED_TO_RAGDOLL(ped, 0, 250, 0, 0, 0, 0)

// --------------------------------------------------------------------------------------------------------------- natives
// Every native the model calls: name, hash (joaat of the lower-case name; unnamed = the hash itself), argument words and result
// words as the disc's scripts call it (OBSERVED: am_pi_menu, freemode or clothes_shop_mp; tools/check_natives.py re-checks each
// against natives/registrations.json, the v972 library's symbols and those script call sites). The tests reject any call whose
// argument count differs from this table.
struct NativeUse {const char* name;std::uint32_t hash;std::uint8_t args,results;};
namespace n {
constexpr std::uint32_t
    PlayerId=joaat("PLAYER_ID"),PlayerPedId=joaat("PLAYER_PED_ID"),IsPlayerPlaying=joaat("IS_PLAYER_PLAYING"),
    NetworkGame=joaat("NETWORK_IS_GAME_IN_PROGRESS"),IsPauseMenuActive=joaat("IS_PAUSE_MENU_ACTIVE"),IsCutsceneActive=joaat("IS_CUTSCENE_ACTIVE"),
    IsSwitchInProgress=joaat("IS_PLAYER_SWITCH_IN_PROGRESS"),IsScreenFadedOut=joaat("IS_SCREEN_FADED_OUT"),IsEntityDead=joaat("IS_ENTITY_DEAD"),
    IsPedRagdoll=joaat("IS_PED_RAGDOLL"),IsPlayerControlOn=joaat("IS_PLAYER_CONTROL_ON"),IsWarningActive=joaat("IS_WARNING_MESSAGE_ACTIVE"),
    IsPedInAnyVehicle=joaat("IS_PED_IN_ANY_VEHICLE"),GetPlayerName=joaat("GET_PLAYER_NAME"),GetGameTimer=joaat("GET_GAME_TIMER"),
    IsDisabledPressed=joaat("IS_DISABLED_CONTROL_PRESSED"),IsPressed=joaat("IS_CONTROL_PRESSED"),
    DisableControl=joaat("DISABLE_CONTROL_ACTION"),SetInputExclusive=joaat("SET_INPUT_EXCLUSIVE"),
    GetEntityCoords=joaat("GET_ENTITY_COORDS"),SetNewWaypoint=joaat("SET_NEW_WAYPOINT"),
    WaypointOff=0x62babf2cu,   // unnamed: am_pi_menu 0x1f005 (Quick GPS "None") and freemode; 0 args; INFERRED "remove the waypoint"
    GetEntityHealth=joaat("GET_ENTITY_HEALTH"),GetEntityMaxHealth=joaat("GET_ENTITY_MAX_HEALTH"),SetEntityHealth=joaat("SET_ENTITY_HEALTH"),
    ClearPedTasks=joaat("CLEAR_PED_TASKS"),SetPlayerControl=joaat("SET_PLAYER_CONTROL"),SetPedStealth=joaat("SET_PED_STEALTH_MOVEMENT"),
    SetCurrentPedWeapon=joaat("SET_CURRENT_PED_WEAPON"),SetPedCanBeDraggedOut=joaat("SET_PED_CAN_BE_DRAGGED_OUT"),
    SetEveryoneIgnorePlayer=joaat("SET_EVERYONE_IGNORE_PLAYER"),SetPoliceIgnorePlayer=joaat("SET_POLICE_IGNORE_PLAYER"),
    SetFacialIdle=joaat("SET_FACIAL_IDLE_ANIM_OVERRIDE"),
    RequestAnimDict=joaat("REQUEST_ANIM_DICT"),HasAnimDictLoaded=joaat("HAS_ANIM_DICT_LOADED"),RemoveAnimDict=joaat("REMOVE_ANIM_DICT"),
    TaskPlayAnim=joaat("TASK_PLAY_ANIM"),IsEntityPlayingAnim=joaat("IS_ENTITY_PLAYING_ANIM"),
    PropCount=joaat("GET_NUMBER_OF_PED_PROP_DRAWABLE_VARIATIONS"),GetPropIndex=joaat("GET_PED_PROP_INDEX"),
    SetPropIndex=joaat("SET_PED_PROP_INDEX"),ClearPedProp=joaat("CLEAR_PED_PROP"),
    DrawableCount=joaat("GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS"),GetDrawable=joaat("GET_PED_DRAWABLE_VARIATION"),
    SetComponent=joaat("SET_PED_COMPONENT_VARIATION"),HasPedGotWeapon=joaat("HAS_PED_GOT_WEAPON"),
    SmokeTrailColour=joaat("SET_PLAYER_PARACHUTE_SMOKE_TRAIL_COLOR"),SmokeTrailOn=joaat("SET_PLAYER_CAN_LEAVE_PARACHUTE_SMOKE_TRAIL"),
    IsEntityInWater=joaat("IS_ENTITY_IN_WATER"),IsEntityInAir=joaat("IS_ENTITY_IN_AIR"),GetAmmoInPedWeapon=joaat("GET_AMMO_IN_PED_WEAPON"),
    HasWeaponComponent=joaat("HAS_PED_GOT_WEAPON_COMPONENT"),HasAnimEventFired=joaat("HAS_ANIM_EVENT_FIRED"),
    ShootsAtCoord=joaat("SET_PED_SHOOTS_AT_COORD"),AnimCurrentTime=joaat("GET_ENTITY_ANIM_CURRENT_TIME"),SetPedToRagdoll=joaat("SET_PED_TO_RAGDOLL");
}
inline constexpr NativeUse Used[]{
    {"PLAYER_ID",n::PlayerId,0,1},{"PLAYER_PED_ID",n::PlayerPedId,0,1},{"IS_PLAYER_PLAYING",n::IsPlayerPlaying,1,1},
    {"NETWORK_IS_GAME_IN_PROGRESS",n::NetworkGame,0,1},{"IS_PAUSE_MENU_ACTIVE",n::IsPauseMenuActive,0,1},
    {"IS_CUTSCENE_ACTIVE",n::IsCutsceneActive,0,1},{"IS_PLAYER_SWITCH_IN_PROGRESS",n::IsSwitchInProgress,0,1},
    {"IS_SCREEN_FADED_OUT",n::IsScreenFadedOut,0,1},{"IS_ENTITY_DEAD",n::IsEntityDead,1,1},{"IS_PED_RAGDOLL",n::IsPedRagdoll,1,1},
    {"IS_PLAYER_CONTROL_ON",n::IsPlayerControlOn,1,1},{"IS_WARNING_MESSAGE_ACTIVE",n::IsWarningActive,0,1},
    {"IS_PED_IN_ANY_VEHICLE",n::IsPedInAnyVehicle,2,1},{"GET_PLAYER_NAME",n::GetPlayerName,1,1},{"GET_GAME_TIMER",n::GetGameTimer,0,1},
    {"IS_DISABLED_CONTROL_PRESSED",n::IsDisabledPressed,2,1},{"IS_CONTROL_PRESSED",n::IsPressed,2,1},
    {"DISABLE_CONTROL_ACTION",n::DisableControl,2,0},{"SET_INPUT_EXCLUSIVE",n::SetInputExclusive,2,0},
    {"GET_ENTITY_COORDS",n::GetEntityCoords,2,3},{"SET_NEW_WAYPOINT",n::SetNewWaypoint,2,0},{"0x62babf2c",n::WaypointOff,0,0},
    {"GET_ENTITY_HEALTH",n::GetEntityHealth,1,1},{"GET_ENTITY_MAX_HEALTH",n::GetEntityMaxHealth,1,1},{"SET_ENTITY_HEALTH",n::SetEntityHealth,2,0},
    {"CLEAR_PED_TASKS",n::ClearPedTasks,1,0},{"SET_PLAYER_CONTROL",n::SetPlayerControl,3,0},{"SET_PED_STEALTH_MOVEMENT",n::SetPedStealth,3,0},
    {"SET_CURRENT_PED_WEAPON",n::SetCurrentPedWeapon,3,0},{"SET_PED_CAN_BE_DRAGGED_OUT",n::SetPedCanBeDraggedOut,2,0},
    {"SET_EVERYONE_IGNORE_PLAYER",n::SetEveryoneIgnorePlayer,2,0},{"SET_POLICE_IGNORE_PLAYER",n::SetPoliceIgnorePlayer,2,0},
    {"SET_FACIAL_IDLE_ANIM_OVERRIDE",n::SetFacialIdle,3,0},{"REQUEST_ANIM_DICT",n::RequestAnimDict,1,0},
    {"HAS_ANIM_DICT_LOADED",n::HasAnimDictLoaded,1,1},{"REMOVE_ANIM_DICT",n::RemoveAnimDict,1,0},{"TASK_PLAY_ANIM",n::TaskPlayAnim,11,0},
    {"IS_ENTITY_PLAYING_ANIM",n::IsEntityPlayingAnim,4,1},
    {"GET_NUMBER_OF_PED_PROP_DRAWABLE_VARIATIONS",n::PropCount,2,1},{"GET_PED_PROP_INDEX",n::GetPropIndex,2,1},
    {"SET_PED_PROP_INDEX",n::SetPropIndex,5,0},{"CLEAR_PED_PROP",n::ClearPedProp,2,0},
    {"GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS",n::DrawableCount,2,1},{"GET_PED_DRAWABLE_VARIATION",n::GetDrawable,2,1},
    {"SET_PED_COMPONENT_VARIATION",n::SetComponent,5,0},{"HAS_PED_GOT_WEAPON",n::HasPedGotWeapon,3,1},
    {"SET_PLAYER_PARACHUTE_SMOKE_TRAIL_COLOR",n::SmokeTrailColour,4,0},{"SET_PLAYER_CAN_LEAVE_PARACHUTE_SMOKE_TRAIL",n::SmokeTrailOn,2,0},
    {"IS_ENTITY_IN_WATER",n::IsEntityInWater,1,1},{"IS_ENTITY_IN_AIR",n::IsEntityInAir,1,1},{"GET_AMMO_IN_PED_WEAPON",n::GetAmmoInPedWeapon,2,1},
    {"HAS_PED_GOT_WEAPON_COMPONENT",n::HasWeaponComponent,3,1},{"HAS_ANIM_EVENT_FIRED",n::HasAnimEventFired,2,1},
    {"SET_PED_SHOOTS_AT_COORD",n::ShootsAtCoord,5,0},{"GET_ENTITY_ANIM_CURRENT_TIME",n::AnimCurrentTime,3,1},{"SET_PED_TO_RAGDOLL",n::SetPedToRagdoll,7,1}};
inline constexpr std::uint32_t UsedNatives[]{
    n::PlayerId,n::PlayerPedId,n::IsPlayerPlaying,n::NetworkGame,n::IsPauseMenuActive,n::IsCutsceneActive,n::IsSwitchInProgress,
    n::IsScreenFadedOut,n::IsEntityDead,n::IsPedRagdoll,n::IsPlayerControlOn,n::IsWarningActive,n::IsPedInAnyVehicle,n::GetPlayerName,
    n::GetGameTimer,n::IsDisabledPressed,n::IsPressed,n::DisableControl,n::SetInputExclusive,n::GetEntityCoords,n::SetNewWaypoint,
    n::WaypointOff,n::GetEntityHealth,n::GetEntityMaxHealth,n::SetEntityHealth,n::ClearPedTasks,n::SetPlayerControl,n::SetPedStealth,
    n::SetCurrentPedWeapon,n::SetPedCanBeDraggedOut,n::SetEveryoneIgnorePlayer,n::SetPoliceIgnorePlayer,n::SetFacialIdle,
    n::RequestAnimDict,n::HasAnimDictLoaded,n::RemoveAnimDict,n::TaskPlayAnim,n::IsEntityPlayingAnim,n::PropCount,n::GetPropIndex,
    n::SetPropIndex,n::ClearPedProp,n::DrawableCount,n::GetDrawable,n::SetComponent,n::HasPedGotWeapon,n::SmokeTrailColour,n::SmokeTrailOn,
    n::IsEntityInWater,n::IsEntityInAir,n::GetAmmoInPedWeapon,n::HasWeaponComponent,n::HasAnimEventFired,n::ShootsAtCoord,n::AnimCurrentTime,
    n::SetPedToRagdoll};
static_assert(std::size(Used)==std::size(UsedNatives));
constexpr bool usedListsAgree() noexcept {for(std::size_t i=0;i<std::size(Used);++i)if(Used[i].hash!=UsedNatives[i])return false;return true;}
static_assert(usedListsAgree(),"Used[] and UsedNatives[] list the same hashes in the same order");
// The trainer's gate natives keep their trainer.h hashes (one glue handler table each; the same hash may sit in both lists).
static_assert(n::PlayerPedId==trainer::n::PlayerPedId && n::IsPedRagdoll==trainer::n::IsPedRagdoll && n::GetEntityCoords==trainer::n::GetEntityCoords);
// No money, stat, save or network-write native: the disc's three money natives (NETWORK_GET_VC_WALLET_BALANCE,
// NETWORK_SPENT_BUY_PASSIVE_MODE, NETWORK_BUY_HEALTHCARE) are unregistered and Sandbox is free; MP stats are never touched.
constexpr bool forbiddenName(const char* s) noexcept {
    const auto starts=[](const char* a,const char* p){while(*p){if(*a!=*p)return false;++a;++p;}return true;};
    return starts(s,"STAT_") || starts(s,"NETWORK_SPENT") || starts(s,"NETWORK_BUY") || starts(s,"NETWORK_GIVE") || starts(s,"NETWORK_SET")
        || starts(s,"NETWORK_GET_VC") || starts(s,"SET_SAVE") || starts(s,"DO_AUTO_SAVE") || starts(s,"PLAYSTATS_") || starts(s,"TRIGGER_SCRIPT_EVENT");
}
constexpr bool noForbidden() noexcept {for(const auto& u:Used)if(forbiddenName(u.name))return false;return true;}
static_assert(noForbidden(),"the menu model never calls money, stat, save or network-write natives");
inline const NativeUse* findUse(std::uint32_t hash) noexcept {for(const auto& u:Used)if(u.hash==hash)return &u;return nullptr;}

// ------------------------------------------------------------------------------------------------------------- text and view
// Text the renderer shows. Label = a game text key (localised by the game: the disc's global.gxt2 has every key used here);
// LabelInt = a key with one ADD_TEXT_COMPONENT_INTEGER ("NUMBER" = "~1~"); Literal = our own text through the "STRING" label +
// ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME (plan item 12; never a game sentence); Guest = a guest char* the game gave (the player
// name from GET_PLAYER_NAME, handed to SUBSTRING_PLAYER_NAME as is, exactly as am_pi_menu's header does).
struct Text {
    enum Kind : std::uint8_t {None,Label,LabelInt,Literal,Guest};
    Kind kind=None;const char* s=nullptr;int n=0;std::uint32_t guest=0;
    static constexpr Text label(const char* k) noexcept {return {Label,k,0,0};}
    static constexpr Text labelInt(const char* k,int v) noexcept {return {LabelInt,k,v,0};}
    static constexpr Text literal(const char* t) noexcept {return {Literal,t,0,0};}
    static constexpr Text guestString(std::uint32_t a) noexcept {return {Guest,nullptr,0,a};}
    bool empty() const noexcept {return kind==None;}
};
enum class Sound : std::uint8_t {UpDown,LeftRight,Select,Back,Cancel,Error};   // HUD_FRONTEND_DEFAULT_SOUNDSET (am_pi_menu)
constexpr const char* soundName(Sound s) noexcept {
    switch(s){case Sound::UpDown:return "NAV_UP_DOWN";case Sound::LeftRight:return "NAV_LEFT_RIGHT";case Sound::Select:return "SELECT";
              case Sound::Back:return "BACK";case Sound::Cancel:return "CANCEL";case Sound::Error:return "ERROR";}
    return "SELECT";
}
constexpr const char* SoundSet="HUD_FRONTEND_DEFAULT_SOUNDSET";

enum class Page : std::uint8_t {Main,Inventory,Character,Places,Activities,Vehicles,World};
// Row ids: 1..9 are the disc's main-page ids (builder 0x27b80, first argument of the add-row helper 0x26591). The rest are ours.
enum RowId : int {
    QuickGps=1,InventoryRow=2,ActionRow=3,MoodRow=4,PassiveRow=5,HighlightPlayer=6,ImpromptuRace=7,EmptyVehicle=8,KillYourself=9,
    SbCharacter=20,SbCreator=21,SbPlaces=22,SbActivities=23,SbVehicles=24,SbWorld=25,
    InvCash=40,InvGlasses=41,InvHats=42,InvMasks=43,InvParachute=44,InvSmokeTrail=45,InvSnack=46 /*46..50*/,InvSmokes=51,InvDunceCap=52,
    CharMale=60,CharFemale=61,CharStory=62,
    PlaceBase=100 /*100..*/,ActivityBase=200 /*200..*/,VehSpawn=300,VehFix=301,WorldWeather=310,WorldTime=311
};
struct RowView {int id=0;Text label,value;bool enabled=true,cycles=false;Text help;};
struct Button {int control;const char* label;};   // control in group 2; label = a game key (PIM_C*)
struct View {
    Page page=Page::Main;
    Text name,crew;               // header: player name (Guest) and "No Crew Membership" (CREW_NONE); no rank (Route B: hidden)
    Text title;                   // subtitle bar; None on the main page (the disc's "HIDE" keyword, 0x27b80)
    static constexpr int MaxRows=40;
    RowView rows[MaxRows]{};int count=0,selected=0,top=0;
    bool counter=false;int counterCurrent=0,counterTotal=0;   // "CM_ITEM_COUNT" (n / m) above MaxVisible rows
    Text help;                    // the description line under the list (the selected row's, or a refusal reason)
    Button buttons[3]{};int buttonCount=0;
};

// --------------------------------------------------------------------------------------------------------- data the rows use
// Player Mood: label PM_MOOD_<i> (OBSERVED keys 0..7 in global.gxt2), clip mood_*_1 (freemode strings 0x449..0x4aa, applied by
// freemode 0xab53 with SET_FACIAL_IDLE_ANIM_OVERRIDE(ped, clip, 0)). Smug and sulk exist only for the male online model
// (MENUS-PLAN verifier: facials@gen_male@base), so other models get the first 6.
inline constexpr const char* MoodLabels[8]{"PM_MOOD_0","PM_MOOD_1","PM_MOOD_2","PM_MOOD_3","PM_MOOD_4","PM_MOOD_5","PM_MOOD_6","PM_MOOD_7"};
inline constexpr const char* MoodClips[8]{"mood_Aiming_1","mood_Angry_1","mood_Happy_1","mood_Injured_1","mood_Normal_1",
    "mood_stressed_1","mood_smug_1","mood_sulk_1"};
constexpr int MoodNormal=4,MoodsAll=8,MoodsStory=6;
// On-foot actions (am_pi_menu table 0x28328 category 2, 0x287cc): label, dictionary, enter / idle / exit clips. The in-car list
// (category 0) is not offered: its amb@code_human_in_car_mp_actions@* archive has not been found (MENUS-PLAN E5). Crew list omitted.
struct ActionClip {const char* label;const char* dict;const char* enter;const char* idle;const char* exit;};
inline constexpr ActionClip OnFootActions[10]{
    {"IAP_FING","mp_player_int_upperfinger","mp_player_int_finger_01_ENTER","mp_player_int_finger_01","mp_player_int_finger_01_EXIT"},
    {"IAP_ROCK","mp_player_int_upperrock","mp_player_int_rock_ENTER","mp_player_int_rock","mp_player_int_rock_EXIT"},
    {"IAP_SALU","mp_player_int_uppersalute","mp_player_int_salute_ENTER","mp_player_int_salute","mp_player_int_salute_EXIT"},
    {"IAP_WANK","mp_player_int_upperwank","mp_player_int_wank_01_ENTER","mp_player_int_wank_01","mp_player_int_wank_01_EXIT"},
    {"IAP_SMOKE","mp_player_int_uppersmoke","mp_player_int_smoke_ENTER","mp_player_int_smoke","mp_player_int_smoke_EXIT"},
    {"IAP_DRINK1","mp_player_intdrink","Intro","Loop","Outro"},
    {"IAP_DRINK2","mp_player_intdrink","Intro_bottle","Loop_bottle","Outro_bottle"},
    {"IAP_EAT1","mp_player_inteat@burger","mp_player_int_eat_burger_enter","mp_player_int_eat_burger","mp_player_int_eat_exit_burger"},
    {"IAP_EAT2","mp_player_inteat@burger","mp_player_int_eat_burger_enter","mp_player_int_eat_burger","mp_player_int_eat_exit_burger"},
    {"IAP_EAT3","mp_player_inteat@burger","mp_player_int_eat_burger_enter","mp_player_int_eat_burger","mp_player_int_eat_exit_burger"}};
constexpr int ActionCount=10;
constexpr float AnimBlendIn=8.0f,AnimBlendOut=-8.0f;   // freemode's TASK_PLAY_ANIM literals (0x62cb5: 8.0, -8.0, -1, flag, 0, 0, 0, 0)
constexpr std::uint32_t AnimFlagUpper=48;              // INFERRED: upper body (16) + secondary (32), from the "upper" dictionaries
constexpr std::uint32_t AnimLoadFrames=150,AnimStageFrames=300,AnimGraceFrames=3;
// Quick GPS (0x1efc4): values PIM_QGPS0..6 = None, Home, ATM, Gun Shop, Mod Shop, Clothes Store, the fixed street point. The disc
// finds the nearest entry of its 90-entry ATM table / 44-entry shop table (3-D distance, start 9999.9: 0x1fc1f, 0x1f616). Those tables
// and the fixed point are game data: the glue reads them from the player's disc at run time (GpsSource); this header has none.
enum GpsKind : int {GpsNone=0,GpsHome=1,GpsAtm=2,GpsGunShop=3,GpsModShop=4,GpsClothes=5,GpsFixed=6,GpsCount=7};
inline constexpr const char* GpsLabels[GpsCount]{"PIM_QGPS0","PIM_QGPS1","PIM_QGPS2","PIM_QGPS3","PIM_QGPS4","PIM_QGPS5","PIM_QGPS6"};
// Snacks (inventory rows PIM_TSNK1..5, help PIM_HSNK1..5). The disc's heal amounts are Online tunables (global 262144 + 91..97,
// never written offline), so these shares of the maximum health are ours (owner default: "close to Online's feel"; INFERRED).
inline constexpr const char* SnackLabels[5]{"PIM_TSNK1","PIM_TSNK2","PIM_TSNK3","PIM_TSNK4","PIM_TSNK5"};
inline constexpr const char* SnackHelp[5]{"PIM_HSNK1","PIM_HSNK2","PIM_HSNK3","PIM_HSNK4","PIM_HSNK5"};
inline constexpr float SnackHealShare[5]{0.10f,0.25f,0.50f,0.15f,0.20f};
constexpr int SnackCap=10;   // INFERRED carry cap; Sandbox refills to it every time the menu opens (owner default: free refills)
// Smoke trails (0x15672): 0 none (white, trail off), red, orange, yellow, blue, black (the crew colour has no offline source).
struct Rgb {int r,g,b;};
inline constexpr const char* SmokeLabels[6]{"PIM_NCTRA","PIM_CTRED","PIM_CTORA","PIM_CTYEL","PIM_CTBLU","PIM_CTBLK"};
inline constexpr Rgb SmokeColours[6]{{255,255,255},{255,0,0},{255,165,0},{255,255,0},{0,0,255},{0,0,0}};
// Weather / time page: our own names for trainer.h WeatherList (same order) plus the game's own cycle (ClearWeather).
inline constexpr const char* WeatherNames[14]{"Extra Sunny","Clear","Clouds","Smog","Foggy","Overcast","Rain","Thunder","Clearing",
    "Neutral","Snow","Blizzard","Light Snow","Game Cycle"};
static_assert(std::size(WeatherNames)==trainer::WeatherList.size()+1);
inline constexpr const char* HourNames[24]{"00:00","01:00","02:00","03:00","04:00","05:00","06:00","07:00","08:00","09:00","10:00","11:00",
    "12:00","13:00","14:00","15:00","16:00","17:00","18:00","19:00","20:00","21:00","22:00","23:00"};

// --------------------------------------------------------------------------------------------------------- the glue's side
// What the glue knows that is not a native (all cheap fields it already has).
struct Context {
    bool sandbox=false;          // recomp::sandbox::active(): Story mode never opens the menu
    bool freemodeRunning=false;  // a real freemode thread runs: the disc's own am_pi_menu takes over, ours stands down
    bool phoneUp=false;          // recomp::phonecall Up bit (IS_PHONE_ONSCREEN is not registered)
    bool onlineCharacter=false;  // trainer state bit 20: a freemode model is the player
    int gender=-1;               // 0 male / 1 female freemode model, -1 a story character
    bool invincible=false;       // trainer toggle God (trainer state bit 0, SET_PLAYER_INVINCIBLE every tick): Kill Yourself greys out
};
// The trainer's request path (glue: trainer().request + the status line, as property_doors does in v972).
struct Requests {
    virtual ~Requests()=default;
    virtual int request(int action,int a0,int a1)=0;               // >= 0 sequence number, else trainer::ErrOff / ErrArgs / ErrFull
    virtual int poll(int seq,char* detail,int cap)=0;               // 0 not yet, 1 ok, 2 failed / dropped / lost (detail written)
};
// Quick GPS targets read from the disc at run time (glue). nearest(): the disc's rule (3-D distance from the player, start 9999.9).
struct GpsSource {
    virtual ~GpsSource()=default;
    virtual bool available(int kind) const noexcept=0;
    virtual bool target(int kind,float px,float py,float pz,float out[3]) noexcept=0;
};
inline bool nearest(const float (*pts)[3],int count,float px,float py,float pz,float out[3]) noexcept {
    float best=9999.9f;int at=-1;   // 0x1fc24
    for(int i=0;i<count;++i){const float dx=pts[i][0]-px,dy=pts[i][1]-py,dz=pts[i][2]-pz;const float d=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(d<best){best=d;at=i;}}    // 0x1fc61 FLT: the first of equal distances stays
    if(at<0)return false;
    out[0]=pts[at][0];out[1]=pts[at][1];out[2]=pts[at][2];return true;
}
// Our Sandbox lists the glue owns (online_world.h PlaceList, activities.h Catalog): count + a static label per index.
struct List {int count=0;const char* (*label)(int)=nullptr;};
struct Lists {List places,activities;};
// What persists (the glue keeps it in the Sandbox files root; never a story save, never an MP stat).
// snackReadyAt is a GET_GAME_TIMER value: the timer restarts every session, so the model clamps a stale one (sanitize).
struct Store {
    int mood=MoodNormal,action=0,smokeTrail=0,gps=GpsNone;bool passive=false;int snacks[5]{SnackCap,SnackCap,SnackCap,SnackCap,SnackCap};
    std::uint32_t snackReadyAt=0;bool snackTimed=false;
    // A store read from an older or damaged Sandbox file: every array index back in range, the snack timer at most one cool-down
    // ahead of the current game timer. No natives; called by the model before it uses the store.
    void sanitize(std::uint32_t now) noexcept {
        if(mood<0 || mood>=MoodsAll)mood=MoodNormal;
        if(action<0 || action>=ActionCount)action=0;
        if(smokeTrail<0 || smokeTrail>=int(std::size(SmokeLabels)))smokeTrail=0;
        if(gps<0 || gps>=GpsCount)gps=GpsNone;
        for(int& c:snacks)c=c<0?0:(c>SnackCap?SnackCap:c);
        if(snackTimed && std::int32_t(snackReadyAt-now)>std::int32_t(SnackCooldownMs))snackReadyAt=now;   // stale: ready now
    }
};

// Sandbox row texts (ours; FivePocket naming rule: no game or publisher names).
namespace txt {
inline constexpr const char* Character="Online Character";inline constexpr const char* Creator="Creator";
inline constexpr const char* Places="Places";inline constexpr const char* Activities="Activities";
inline constexpr const char* Vehicles="Vehicles";inline constexpr const char* World="Weather and Time";
inline constexpr const char* TitleCharacter="ONLINE CHARACTER";inline constexpr const char* TitlePlaces="PLACES";
inline constexpr const char* TitleActivities="ACTIVITIES";inline constexpr const char* TitleVehicles="VEHICLES";
inline constexpr const char* TitleWorld="WEATHER AND TIME";
inline constexpr const char* Male="Male";inline constexpr const char* Female="Female";inline constexpr const char* Story="Story Character";
inline constexpr const char* Spawn="Vehicle";inline constexpr const char* Fix="Repair Vehicle";
inline constexpr const char* Weather="Weather";inline constexpr const char* Hour="Time";
inline constexpr const char* HelpPassive="Sandbox Passive Mode, free: holsters your weapon, and people and the police ignore you.";
inline constexpr const char* HelpCharacter="Play as an online character, or go back to the story character.";
inline constexpr const char* HelpCreator="Edit your online character's look.";
inline constexpr const char* HelpCreatorOff="Switch to an online character first.";
inline constexpr const char* HelpPlaces="Go to an apartment or garage.";
inline constexpr const char* HelpActivities="Start a freeroam activity.";
inline constexpr const char* HelpVehicles="Spawn a vehicle beside you, or repair the one you are in.";
inline constexpr const char* HelpWorld="Set the weather and the time of day.";
inline constexpr const char* HelpFixOff="You are not in a vehicle.";
inline constexpr const char* HelpOnFoot="Only on foot.";
inline constexpr const char* HelpGpsOff="Not available here.";
inline constexpr const char* HelpKillGod="Turn off the trainer's God mode first.";
}

// ------------------------------------------------------------------------------------------------------------------ the model
class Model {
public:
    enum class State : std::uint8_t {Closed,Holding,Refused,Open};
    using LogFn=void(*)(const char*);
    explicit Model(LogFn log=nullptr) noexcept:log_(log) {}
    void setLists(const Lists& l) noexcept {lists_=l;}
    void setGps(GpsSource* g) noexcept {gps_=g;}
    Store& store() noexcept {return store_;}
    const Store& store() const noexcept {return store_;}
    bool storeDirty() const noexcept {return storeDirty_;}
    void clearStoreDirty() noexcept {storeDirty_=false;}
    State state() const noexcept {return state_;}
    bool open() const noexcept {return state_==State::Open;}
    bool jobRunning() const noexcept {return job_.kind!=Job::None;}
    const View& view() const noexcept {return view_;}
    // Sounds of this tick (the renderer plays them: PLAY_SOUND_FRONTEND(-1, name, SoundSet)).
    int soundCount() const noexcept {return soundCount_;}
    Sound sound(int i) const noexcept {return sounds_[i];}
    const char* lastClose() const noexcept {return closeReason_;}
    // The player ped changed (the glue's Sandbox tick already tracks it for its weapons): mood and Passive Mode are applied again.
    void notePed(std::uint32_t ped) noexcept {if(ped && ped!=appliedPed_)reapply_=true;}
    // v989 (glue, script thread): Back from the pad read's gate (imenu_back_gate.h) instead of IS_DISABLED_CONTROL_PRESSED(0,0): the
    // gate withholds Back from the game in the Interaction Menu style (E3), so the game's control 0 never sees the hold. No native.
    void setBackSource(const imenugate::BackGate* g) noexcept {backSrc_=g;}
    // v989: the hold-Back opener on (style "Interaction Menu"; the lane default) or off (style "FivePocket": the glue sets it from the style).
    void setOpener(bool on) noexcept {opener_=on;}
    bool opener() const noexcept {return opener_;}
    // v989 (any thread: recomp_imenu_v1(1), the touch gesture): open as a completed hold would, on the next closed tick (same checks).
    void requestOpen() noexcept {openReq_.store(true,std::memory_order_release);}
    bool openRequested() const noexcept {return openReq_.load(std::memory_order_relaxed);}
    void requestClose() noexcept {closeReq_.store(true,std::memory_order_release);}   // v989: recomp_imenu_v1(2), served on the next open tick
    bool cooling() const noexcept {return cooling_;}

    // One tick per game frame at the trainer's safe point (Sandbox mode). Returns true when the renderer should draw view() now.
    bool tick(Natives& nat,const Context& ctx,Requests& req) noexcept {
        soundCount_=0;
        if(!ctx.sandbox || ctx.freemodeRunning){            // Story mode, or the real menu's owner is running: stand down
            if(state_==State::Open)close(nat,ctx.sandbox?"freemode":"story-mode",false);
            if(job_.kind!=Job::None)dropJob(nat,"stand-down");   // a job never freezes with player control off
            state_=State::Closed;prevBack_=false;return false;
        }
        const bool back=backSrc_?backSrc_->rawDown():pressed(nat,GroupGame,CtlBack,true);   // the closed path's only read (v989: no native with the gate)
        const bool backDown=back && !prevBack_;prevBack_=back;
        if(!back)needRelease_=false;                           // after a close, Back must be let go before a new hold (INFERRED)
        const bool asked=state_==State::Closed && openReq_.load(std::memory_order_relaxed);   // v989
        if(state_==State::Closed && !back && !asked && !cooling_ && job_.kind==Job::None && !reapply_)return false;
        now_=call1(nat,n::GetGameTimer,{});
        if(backDown)backDownAt_=now_;
        store_.sanitize(now_);                                 // no natives: indexes in range, a stale snack timer clamped
        if(reapply_)reapplyOnPed(nat);
        if(job_.kind!=Job::None)runJob(nat,ctx);
        if(cooling_)cooldown(nat);
        // freemode 0xcb377..0xcb39d: control 0 is disabled every frame Back stays held past 350 ms, whether or not the menu opened
        // (refused, during the cool-down, or still held after a close), so releasing it does not change the camera. Open: openTick.
        const bool backLong=back && !backDown && now_-backDownAt_>=HoldOpenMs;
        if(opener_ && backLong && state_!=State::Open && state_!=State::Holding)call0(nat,n::DisableControl,{GroupGame,CtlBack});   // v989: style on only
        switch(state_) {
        case State::Closed:
            if(asked && openReq_.exchange(false,std::memory_order_acq_rel)) {   // v989: the app / touch open (no hold, same refusals)
                if(const char* why=refusal(nat,ctx)){logf("RENDER_IMENU open=refused reason=%s via=request",why);return false;}
                openMenu(nat,ctx);backLatched_=back;return true;
            }
            if(opener_ && back && !cooling_ && !needRelease_){state_=State::Holding;holdAt_=now_;}
            return false;
        case State::Holding:
            if(!back){state_=State::Closed;return false;}   // a tap: the game keeps it (camera)
            if(now_-holdAt_<HoldOpenMs)return false;
            if(const char* why=refusal(nat,ctx)){
                state_=State::Refused;call0(nat,n::DisableControl,{GroupGame,CtlBack});
                logf("RENDER_IMENU open=refused reason=%s",why);return false;}
            openMenu(nat,ctx);
            return true;
        case State::Refused:
            if(!back)state_=State::Closed;                  // asked again only after a release
            return false;
        case State::Open:
            if(closeReq_.exchange(false,std::memory_order_acq_rel)){close(nat,"request",true);return false;}   // v989
            return openTick(nat,ctx,req,back);
        }
        return false;
    }

private:
    struct Job {
        enum Kind : std::uint8_t {None,Anim,Kill} kind=None;
        int action=0,stage=0;std::uint32_t frames=0,ped=0,player=0;bool pistol=false,fired=false;
        static constexpr int KillDeathWait=9;   // Kill: health 0 was set; waiting to see the death
    };
    struct Inventory {
        int propCount[2]{},prop[2]{-1,-1};   // [0] hats (anchor 0), [1] glasses (anchor 1): count, current (-1 none)
        int maskCount=0,mask=0;bool parachute=false,parachuteOn=false;
    };

    // ---- native helpers (every call is checked against Used[] by the tests)
    static std::uint32_t call1(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a,bool* ok=nullptr) noexcept {
        std::uint32_t out[3]{};const bool r=nat.call(h,a.begin(),unsigned(a.size()),out);if(ok)*ok=r;return r?out[0]:0u;
    }
    static void call0(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};(void)nat.call(h,a.begin(),unsigned(a.size()),out);
    }
    static bool pressed(Natives& nat,int group,int control,bool disabled) noexcept {
        return call1(nat,disabled?n::IsDisabledPressed:n::IsPressed,{std::uint32_t(group),std::uint32_t(control)})!=0;
    }
    template<class... A> void logf(const char* f,A... a) const noexcept {if(!log_)return;char l[200];std::snprintf(l,sizeof l,f,a...);log_(l);}
    void play(Sound s) noexcept {if(soundCount_<4)sounds_[soundCount_++]=s;}

    // ---- open / close
    // The disc's self-open checks (freemode 0xcb3ed: not ragdoll, control on, no warning message) on top of the trainer's gate and
    // the asked-for refusals (cutscene, pause, phone). The first failing check is the reason.
    const char* refusal(Natives& nat,const Context& ctx) noexcept {
        if(ctx.phoneUp)return "phone";
        bool ok=false;player_=call1(nat,n::PlayerId,{},&ok);if(!ok)return "natives-unavailable";
        ped_=call1(nat,n::PlayerPedId,{});
        if(call1(nat,n::NetworkGame,{}))return "network-game";
        if(!call1(nat,n::IsPlayerPlaying,{player_}) || !ped_)return "no-player";
        if(call1(nat,n::IsPauseMenuActive,{}))return "pause-menu";
        if(call1(nat,n::IsCutsceneActive,{}))return "cutscene";
        if(call1(nat,n::IsSwitchInProgress,{}))return "character-switch";
        if(call1(nat,n::IsScreenFadedOut,{}))return "faded-out";
        if(call1(nat,n::IsEntityDead,{ped_}))return "player-dead";
        if(call1(nat,n::IsPedRagdoll,{ped_}))return "ragdoll";
        if(!call1(nat,n::IsPlayerControlOn,{player_}))return "control-off";
        if(call1(nat,n::IsWarningActive,{}))return "warning-message";
        return nullptr;
    }
    void openMenu(Natives& nat,const Context& ctx) noexcept {
        state_=State::Open;backLatched_=true;backHolding_=false;delay_=-1;pendingSeq_=-1;reason_[0]=0;
        closeReq_.store(false,std::memory_order_relaxed);   // v989: a close asked while closed is stale
        page_=Page::Main;sel_=0;top_=0;parentSel_=0;
        // The disc reads accept / cancel / left / right with IS_CONTROL_JUST_PRESSED (0x13e3e, 0x13949, 0x29a92, 0x29c01): a button
        // already held when the menu opens (A for sprint, B, the d-pad) must not act. Each of the six menu controls held now counts
        // as stale until it is released (6 reads, on the open frame only).
        stale_=0;
        for(int i=0;i<6;++i)if(pressed(nat,GroupFrontend,MenuControls[i],false))stale_|=1u<<i;
        prevNext_=prevPrev_=prevLeft_=prevRight_=prevAccept_=prevCancel_=false;
        for(int& s:store_.snacks)if(s!=SnackCap){s=SnackCap;storeDirty_=true;}   // free refills (owner default)
        nameAddr_=call1(nat,n::GetPlayerName,{player_});
        refresh(nat,ctx);
        controls(nat);
        call0(nat,n::DisableControl,{GroupGame,CtlBack});   // freemode 0xcb34f: the camera does not change on the opening hold
        logf("RENDER_IMENU open page=main rows=%d",view_.count);
    }
    void close(Natives& nat,const char* why,bool cool) noexcept {
        if(state_!=State::Open)return;
        state_=State::Closed;closeReason_=why;needRelease_=true;
        if(cool){cooling_=true;closedAt_=now_;cooldown(nat);}
        logf("RENDER_IMENU close reason=%s",why);
    }
    // The 1000 ms after a close (0x2bda5..0x2be0d): the cancel button stays the menu's, attack / phone controls stay off.
    void cooldown(Natives& nat) noexcept {
        if(now_-closedAt_>=CooldownMs){cooling_=false;return;}
        call0(nat,n::SetInputExclusive,{GroupFrontend,CooldownExclusive});
        for(int c:CooldownDisabled)call0(nat,n::DisableControl,{GroupGame,std::uint32_t(c)});
    }
    void controls(Natives& nat) noexcept {   // 0x2bc95, every frame while open
        for(int c:ExclusiveControls)call0(nat,n::SetInputExclusive,{GroupFrontend,std::uint32_t(c)});
        for(int c:DisabledControls)call0(nat,n::DisableControl,{GroupGame,std::uint32_t(c)});
        if(page_==Page::Inventory)call0(nat,n::DisableControl,{GroupGame,InventoryDisabledControl});
    }

    // ---- the open frame
    bool openTick(Natives& nat,const Context& ctx,Requests& req,bool back) noexcept {
        if(const char* why=stillAllowed(nat,ctx)){close(nat,why,true);return false;}
        controls(nat);
        // Back: the opening hold stays the menu's until released (control 0 disabled each frame: no camera change); a new 350 ms
        // hold closes (0x13a84: IS_DISABLED_CONTROL_PRESSED(0,0) without the opening latch, timer 350, CANCEL).
        if(back)call0(nat,n::DisableControl,{GroupGame,CtlBack});
        if(backLatched_){if(!back)backLatched_=false;}
        else if(back){
            if(!backHolding_){backHolding_=true;backAt_=now_;}
            else if(now_-backAt_>=HoldCloseMs){play(Sound::Cancel);close(nat,"back-held",true);return false;}
        } else backHolding_=false;
        const bool next=fresh(nat,0),prev=fresh(nat,1),left=fresh(nat,2),right=fresh(nat,3),accept=fresh(nat,4),cancel=fresh(nat,5);
        const bool acceptEdge=accept && !prevAccept_,cancelEdge=cancel && !prevCancel_;
        const bool leftEdge=left && !prevLeft_,rightEdge=right && !prevRight_;
        prevNext_=next;prevPrev_=prev;prevLeft_=left;prevRight_=right;prevAccept_=accept;prevCancel_=cancel;
        if(pendingSeq_>=0){waitResult(nat,ctx,req);return state_==State::Open;}   // input waits for the request's result
        if(now_-refreshedAt_>=RefreshMs)refresh(nat,ctx);
        if(cancelEdge){
            if(page_==Page::Main){play(Sound::Cancel);close(nat,"cancel",true);return false;}   // 0x13975
            play(Sound::Back);page_=Page::Main;sel_=parentSel_;reason_[0]=0;view_.count=0;refresh(nat,ctx);   // 0x1398f
            return true;
        }
        // 0x2989d: the four directions share one repeat timer (first press at once, then every 250 ms while held; -1 when none is held)
        const RowView* row=view_.count?&view_.rows[sel_]:nullptr;
        if(next || prev){
            if(repeatDue()){play(Sound::UpDown);moveSelection(next?1:-1);}
        } else if(row && row->cycles && (leftEdge || rightEdge || ((left || right) && repeatLR(row->id)))){
            if((left || right) && repeatDue()){play(Sound::LeftRight);cycle(nat,ctx,row->id,left?-1:1);}
        } else delay_=-1;
        if(acceptEdge && view_.count)accept_(nat,ctx,req,view_.rows[sel_]);
        if(state_==State::Open)build(ctx);
        return state_==State::Open;
    }
    const char* stillAllowed(Natives& nat,const Context& ctx) noexcept {
        if(ctx.phoneUp)return "phone";
        player_=call1(nat,n::PlayerId,{});ped_=call1(nat,n::PlayerPedId,{});
        if(call1(nat,n::IsPauseMenuActive,{}))return "pause-menu";
        if(call1(nat,n::IsCutsceneActive,{}))return "cutscene";
        if(call1(nat,n::IsSwitchInProgress,{}))return "character-switch";
        if(call1(nat,n::IsScreenFadedOut,{}))return "faded-out";
        if(!ped_ || call1(nat,n::IsEntityDead,{ped_}))return "player-dead";
        if(!call1(nat,n::IsPlayerControlOn,{player_}))return "control-off";   // 0x1392b
        return nullptr;
    }
    // A menu control read (IS_CONTROL_PRESSED(2, c)); one held since the open reads as released until it really is released.
    static constexpr int MenuControls[6]{CtlNext,CtlPrev,CtlLeft,CtlRight,CtlAccept,CtlCancel};
    bool fresh(Natives& nat,int i) noexcept {
        const bool p=pressed(nat,GroupFrontend,MenuControls[i],false);
        if(!(stale_&(1u<<i)))return p;
        if(!p)stale_&=~(1u<<i);
        return false;
    }
    bool repeatDue() noexcept {
        if(delay_>=0 && std::int32_t(now_-repeatAt_)<delay_)return false;
        delay_=RepeatMs;repeatAt_=now_;return true;
    }
    // Left / right hold-repeat only on the rows 0x2a07a allows (main: Action, Mood; inventory: the cycling items) and on our
    // Sandbox value rows; Quick GPS and the smoke trail step once per press (INFERRED row mapping of 0x2a07a's indexes).
    static bool repeatLR(int id) noexcept {
        return id==ActionRow || id==MoodRow || id==InvGlasses || id==InvHats || id==InvMasks || id==VehSpawn || id==WorldWeather || id==WorldTime;
    }
    void moveSelection(int d) noexcept {   // wraps both ways (0x298de / 0x299df)
        if(!view_.count)return;
        sel_=(sel_+d+view_.count)%view_.count;reason_[0]=0;
        if(sel_<top_)top_=sel_;else if(sel_>=top_+MaxVisible)top_=sel_-MaxVisible+1;
    }

    // ---- context read (on open, page change, after an action, and every RefreshMs while open)
    static constexpr std::uint32_t RefreshMs=500;
    void refresh(Natives& nat,const Context& ctx) noexcept {
        refreshedAt_=now_;ctx_=ctx;
        inVehicle_=call1(nat,n::IsPedInAnyVehicle,{ped_,0})!=0;
        if(page_==Page::Inventory)readInventory(nat,ctx);
        build(ctx);
    }
    void readInventory(Natives& nat,const Context& ctx) noexcept {
        const int anchors[2]{HatAnchor,GlassesAnchor};
        for(int k=0;k<2;++k){inv_.propCount[k]=int(call1(nat,n::PropCount,{ped_,std::uint32_t(anchors[k])}));
            inv_.prop[k]=int(call1(nat,n::GetPropIndex,{ped_,std::uint32_t(anchors[k])}));
            if(inv_.prop[k]>=inv_.propCount[k])inv_.prop[k]=-1;}
        inv_.maskCount=int(call1(nat,n::DrawableCount,{ped_,MaskComponent}));
        inv_.mask=int(call1(nat,n::GetDrawable,{ped_,MaskComponent}));
        inv_.parachute=ctx.onlineCharacter && call1(nat,n::HasPedGotWeapon,{ped_,WeaponParachute,0})!=0;
        inv_.parachuteOn=inv_.parachute && call1(nat,n::GetDrawable,{ped_,ParachuteComponent})==std::uint32_t(ParachuteDrawable);
    }
    int moods() const noexcept {return ctx_.onlineCharacter && ctx_.gender==0?MoodsAll:MoodsStory;}
    bool snackReady() const noexcept {return !store_.snackTimed || std::int32_t(now_-store_.snackReadyAt)>=0;}

    // ---- the view (no natives; fixed arrays)
    void add(int id,Text label,Text value,bool enabled,bool cycles,Text help) noexcept {
        if(view_.count>=View::MaxRows)return;
        RowView& r=view_.rows[view_.count++];r.id=id;r.label=label;r.value=value;r.enabled=enabled;r.cycles=cycles;r.help=help;
    }
    static bool needsOtherPlayers(int id) noexcept {   // hidden offline (MENUS-PLAN part 1 table; NOTES 4)
        return id==HighlightPlayer || id==ImpromptuRace || id==EmptyVehicle || id==InvCash || id==InvSmokes || id==InvDunceCap;
    }
    void build(const Context& ctx) noexcept {
        const int keepId=view_.count?view_.rows[sel_<view_.count?sel_:0].id:0;
        view_.count=0;view_.page=page_;view_.name=Text::guestString(nameAddr_);view_.crew=Text::label("CREW_NONE");
        view_.help={};view_.buttonCount=0;
        switch(page_) {
        case Page::Main: {
            view_.title={};
            const int g=store_.gps<0||store_.gps>=GpsCount?0:store_.gps;
            add(QuickGps,Text::label("PIM_TQGPS"),Text::label(GpsLabels[g]),gpsSelectable(g),true,
                gpsSelectable(g)?Text::label("PIM_HQGPS"):Text::literal(txt::HelpGpsOff));
            add(InventoryRow,Text::label("PIM_TINVE"),{},true,false,Text::label("PIM_HINVE"));
            add(ActionRow,Text::label(inVehicle_?"PIM_TANIMP":"PIM_TANIMF"),Text::label(OnFootActions[store_.action].label),
                !inVehicle_ && job_.kind!=Job::Anim,true,inVehicle_?Text::literal(txt::HelpOnFoot):Text::label("PIM_HANIM"));
            add(MoodRow,Text::label("PIM_TMOODN"),Text::label(MoodLabels[store_.mood>=0 && store_.mood<moods()?store_.mood:MoodNormal]),true,true,
                Text::label("PIM_HMOODN"));
            add(PassiveRow,Text::label(store_.passive?"PIM_DPASI1":"PIM_DPASI0"),{},true,false,Text::literal(txt::HelpPassive));
            for(int id:{int(HighlightPlayer),int(ImpromptuRace),int(EmptyVehicle)})if(!needsOtherPlayers(id))add(id,{},{},false,false,{});
            add(KillYourself,Text::label("PIM_TKILS"),{},killAllowed(ctx),false,
                ctx.invincible?Text::literal(txt::HelpKillGod):Text::label("PIM_HKILS"));
            add(SbCharacter,Text::literal(txt::Character),{},true,false,Text::literal(txt::HelpCharacter));
            add(SbCreator,Text::literal(txt::Creator),{},ctx.onlineCharacter,false,
                Text::literal(ctx.onlineCharacter?txt::HelpCreator:txt::HelpCreatorOff));
            add(SbPlaces,Text::literal(txt::Places),{},lists_.places.count>0,false,Text::literal(txt::HelpPlaces));
            add(SbActivities,Text::literal(txt::Activities),{},lists_.activities.count>0,false,Text::literal(txt::HelpActivities));
            add(SbVehicles,Text::literal(txt::Vehicles),{},true,false,Text::literal(txt::HelpVehicles));
            add(SbWorld,Text::literal(txt::World),{},true,false,Text::literal(txt::HelpWorld));
            break;}
        case Page::Inventory: {
            view_.title=Text::label("PIM_TITLE2");
            if(!needsOtherPlayers(InvCash))add(InvCash,{},{},false,false,{});
            if(inv_.propCount[1]>0)add(InvGlasses,Text::label("PIM_TGLAS"),propValue(1,"PIM_NGLAS"),true,true,Text::label("PIM_HGLAS"));
            if(inv_.propCount[0]>0)add(InvHats,Text::label("PIM_THELM"),propValue(0,"PIM_NHELM"),true,true,Text::label("PIM_HHELM"));
            if(inv_.maskCount>1)add(InvMasks,Text::label("PIM_TMASK"),inv_.mask<=0?Text::label("PIM_NGLAS"):Text::labelInt("NUMBER",inv_.mask),
                true,true,Text::label("PIM_HMASK"));
            if(inv_.parachute)add(InvParachute,Text::label("PIM_TCHUT"),Text::label(inv_.parachuteOn?"PIM_DHIGH1":"PIM_DHIGH0"),true,true,
                Text::label("PIM_HCHUT"));
            add(InvSmokeTrail,Text::label("PIM_TCTRA"),Text::label(SmokeLabels[store_.smokeTrail]),true,true,Text::label("PIM_HCTRA"));
            for(int k=0;k<5;++k)if(store_.snacks[k]>0)
                add(InvSnack+k,Text::label(SnackLabels[k]),Text::labelInt("NUMBER",store_.snacks[k]),!inVehicle_ && snackReady(),false,
                    inVehicle_?Text::literal(txt::HelpOnFoot):Text::label(SnackHelp[k]));
            break;}
        case Page::Character:
            view_.title=Text::literal(txt::TitleCharacter);
            add(CharMale,Text::literal(txt::Male),{},true,false,Text::literal(txt::HelpCharacter));
            add(CharFemale,Text::literal(txt::Female),{},true,false,Text::literal(txt::HelpCharacter));
            add(CharStory,Text::literal(txt::Story),{},ctx.onlineCharacter,false,Text::literal(txt::HelpCharacter));
            break;
        case Page::Places:
            view_.title=Text::literal(txt::TitlePlaces);
            for(int i=0;i<lists_.places.count && i<View::MaxRows;++i)
                add(PlaceBase+i,Text::literal(lists_.places.label?lists_.places.label(i):""),{},!inVehicle_,false,
                    inVehicle_?Text::literal(txt::HelpOnFoot):Text::literal(txt::HelpPlaces));
            break;
        case Page::Activities:
            view_.title=Text::literal(txt::TitleActivities);
            for(int i=0;i<lists_.activities.count && i<View::MaxRows;++i)
                add(ActivityBase+i,Text::literal(lists_.activities.label?lists_.activities.label(i):""),{},true,false,
                    Text::literal(txt::HelpActivities));
            break;
        case Page::Vehicles:
            view_.title=Text::literal(txt::TitleVehicles);
            add(VehSpawn,Text::literal(txt::Spawn),Text::literal(trainer::VehicleList[std::size_t(vehicle_)].label),true,true,
                Text::literal(txt::HelpVehicles));
            add(VehFix,Text::literal(txt::Fix),{},inVehicle_,false,Text::literal(inVehicle_?txt::HelpVehicles:txt::HelpFixOff));
            break;
        case Page::World:
            view_.title=Text::literal(txt::TitleWorld);
            add(WorldWeather,Text::literal(txt::Weather),Text::literal(WeatherNames[weather_]),true,true,Text::literal(txt::HelpWorld));
            add(WorldTime,Text::literal(txt::Hour),Text::literal(HourNames[hour_]),true,true,Text::literal(txt::HelpWorld));
            break;
        }
        // keep the selected row by id across a rebuild (a row may have appeared or gone)
        if(view_.count==0){sel_=0;top_=0;}
        else {
            int at=-1;for(int i=0;i<view_.count;++i)if(view_.rows[i].id==keepId){at=i;break;}
            if(at>=0)sel_=at;else if(sel_>=view_.count)sel_=view_.count-1;
            if(sel_<top_)top_=sel_;else if(sel_>=top_+MaxVisible)top_=sel_-MaxVisible+1;
            if(top_>view_.count-MaxVisible)top_=view_.count>MaxVisible?view_.count-MaxVisible:0;
        }
        view_.selected=sel_;view_.top=top_;
        view_.counter=view_.count>MaxVisible;view_.counterCurrent=sel_+1;view_.counterTotal=view_.count;
        if(view_.count){
            const RowView& r=view_.rows[sel_];
            view_.help=reason_[0]?Text::literal(reason_):r.help;
            buttonsFor(r);
        }
    }
    Text propValue(int k,const char* none) const noexcept {return inv_.prop[k]<0?Text::label(none):Text::labelInt("NUMBER",inv_.prop[k]+1);}
    // Instructional buttons (labels PIM_CSEL/CUSE/CPLY/CEXI from am_pi_menu 0x259b5; the per-row choice is INFERRED): only buttons
    // that do something on the highlighted row, Back always.
    void buttonsFor(const RowView& r) noexcept {
        view_.buttonCount=0;
        if(r.enabled && r.id!=MoodRow && r.id!=InvSmokeTrail && r.id!=InvGlasses && r.id!=InvHats && r.id!=InvMasks && r.id!=InvParachute){
            const char* l=r.id==ActionRow?"PIM_CPLY":(r.id>=InvSnack && r.id<InvSnack+5)?"PIM_CUSE":"PIM_CSEL";
            view_.buttons[view_.buttonCount++]={CtlAccept,l};
        }
        view_.buttons[view_.buttonCount++]={CtlCancel,"PIM_CEXI"};
    }
    bool gpsSelectable(int g) const noexcept {return g==GpsNone || (gps_ && gps_->available(g));}
    bool killAllowed(const Context& ctx) const noexcept {return job_.kind==Job::None && !ctx.invincible;}   // God on: health 0 would not kill

    // ---- left / right
    void cycle(Natives& nat,const Context& ctx,int id,int d) noexcept {
        const auto wrap=[](int v,int count){return ((v%count)+count)%count;};
        switch(id) {
        case QuickGps: store_.gps=wrap(store_.gps+d,GpsCount);storeDirty_=true;break;
        case ActionRow: store_.action=wrap(store_.action+d,ActionCount);storeDirty_=true;break;   // stored only (accept plays it)
        case MoodRow: store_.mood=wrap((store_.mood>=0 && store_.mood<moods()?store_.mood:MoodNormal)+d,moods());storeDirty_=true;applyMood(nat);break;
        case InvGlasses: case InvHats: {
            const int k=id==InvHats?0:1;inv_.prop[k]=wrap(inv_.prop[k]+1+d,inv_.propCount[k]+1)-1;   // -1 (none) .. count-1
            const std::uint32_t anchor=std::uint32_t(k==0?HatAnchor:GlassesAnchor);
            if(inv_.prop[k]<0)call0(nat,n::ClearPedProp,{ped_,anchor});   // 0x1ec8x: -1 clears, else index, texture 0, network flag 0
            else call0(nat,n::SetPropIndex,{ped_,anchor,std::uint32_t(inv_.prop[k]),0,0});
            break;}
        case InvMasks: inv_.mask=wrap(inv_.mask+d,inv_.maskCount);call0(nat,n::SetComponent,{ped_,MaskComponent,std::uint32_t(inv_.mask),0,0});break;
        case InvParachute: inv_.parachuteOn=!inv_.parachuteOn;
            call0(nat,n::SetComponent,{ped_,ParachuteComponent,inv_.parachuteOn?std::uint32_t(ParachuteDrawable):0u,0,0});break;
        case InvSmokeTrail: store_.smokeTrail=wrap(store_.smokeTrail+d,6);storeDirty_=true;applySmoke(nat);break;
        case VehSpawn: vehicle_=wrap(vehicle_+d,int(trainer::VehicleList.size()));break;
        case WorldWeather: weather_=wrap(weather_+d,int(std::size(WeatherNames)));break;
        case WorldTime: hour_=wrap(hour_+d,24);break;
        default: break;
        }
        reason_[0]=0;build(ctx);
    }

    // ---- accept
    void accept_(Natives& nat,const Context& ctx,Requests& req,const RowView& r) noexcept {
        if(!r.enabled){play(Sound::Error);return;}
        switch(r.id) {
        case QuickGps: quickGps(nat,ctx);return;
        case InventoryRow: enter(nat,ctx,Page::Inventory);return;
        case ActionRow: play(Sound::Select);startAnim(store_.action);close(nat,"action",true);return;
        case MoodRow: return;   // left / right only (0x796)
        case PassiveRow: play(Sound::Select);store_.passive=!store_.passive;storeDirty_=true;applyPassive(nat,true);build(ctx);return;
        case KillYourself: if(!killAllowed(ctx)){play(Sound::Error);return;}killYourself(nat);return;
        case SbCharacter: enter(nat,ctx,Page::Character);return;
        case SbCreator: send(nat,ctx,req,trainer::CharacterLook,3,0,true);return;
        case SbPlaces: enter(nat,ctx,Page::Places);return;
        case SbActivities: enter(nat,ctx,Page::Activities);return;
        case SbVehicles: enter(nat,ctx,Page::Vehicles);return;
        case SbWorld: enter(nat,ctx,Page::World);return;
        case CharMale: case CharFemale: case CharStory: send(nat,ctx,req,trainer::OnlineCharacter,r.id-CharMale,0,true);return;
        case VehSpawn: send(nat,ctx,req,trainer::SpawnVehicle,vehicle_,1,true);return;
        case VehFix: send(nat,ctx,req,trainer::FixVehicle,0,0,true);return;
        case WorldWeather: if(weather_==int(trainer::WeatherList.size()))send(nat,ctx,req,trainer::ClearWeather,0,0,false);
                           else send(nat,ctx,req,trainer::SetWeather,weather_,0,false);return;
        case WorldTime: send(nat,ctx,req,trainer::SetTime,hour_,0,false);return;
        default: break;
        }
        if(r.id>=InvSnack && r.id<InvSnack+5){eatSnack(nat,ctx,r.id-InvSnack);return;}
        if(r.id>=PlaceBase && r.id<PlaceBase+lists_.places.count){send(nat,ctx,req,trainer::OnlinePlace,r.id-PlaceBase,0,true);return;}
        if(r.id>=ActivityBase && r.id<ActivityBase+lists_.activities.count){send(nat,ctx,req,trainer::Activity,r.id-ActivityBase,0,true);return;}
        if(r.cycles)return;   // value rows without an accept (inventory items)
        play(Sound::Error);
    }
    void enter(Natives& nat,const Context& ctx,Page p) noexcept {
        play(Sound::Select);parentSel_=sel_;page_=p;sel_=0;top_=0;reason_[0]=0;view_.count=0;refresh(nat,ctx);
    }
    // A trainer request: the menu waits for its result; ok closes (closeOnOk) or stays, fail stays open with the reason (plan item 1).
    void send(Natives& nat,const Context& ctx,Requests& req,int action,int a0,int a1,bool closeOnOk) noexcept {
        const int seq=req.request(action,a0,a1);
        if(seq<0){play(Sound::Error);std::snprintf(reason_,sizeof reason_,"Not available (%s).",seq==trainer::ErrFull?"busy":"refused");
            build(ctx);return;}
        play(Sound::Select);pendingSeq_=seq;pendingClose_=closeOnOk;pendingFrames_=0;
        logf("RENDER_IMENU request seq=%d action=%s a0=%d a1=%d",seq,trainer::actionName(action),a0,a1);
        (void)nat;
    }
    void waitResult(Natives& nat,const Context& ctx,Requests& req) noexcept {
        char d[64]{};const int r=req.poll(pendingSeq_,d,sizeof d);
        if(r==0 && ++pendingFrames_<PendingFramesMax)return;
        const int seq=pendingSeq_;pendingSeq_=-1;
        if(r==1){logf("RENDER_IMENU result seq=%d ok",seq);if(pendingClose_)close(nat,"done",true);else build(ctx);return;}
        play(Sound::Error);
        std::snprintf(reason_,sizeof reason_,"Not done: %s",r==0?"no answer":d[0]?d:"failed");
        logf("RENDER_IMENU result seq=%d fail %s",seq,reason_);
        refresh(nat,ctx);
    }
    static constexpr std::uint32_t PendingFramesMax=200;   // > the trainer's BlockedFramesMax (150): its "dropped" line comes first

    // ---- the rows' own effects (ours, on the disc's natives)
    void quickGps(Natives& nat,const Context& ctx) noexcept {
        const int g=store_.gps;
        if(g==GpsNone){call0(nat,n::WaypointOff,{});play(Sound::Select);close(nat,"quick-gps",true);return;}   // 0x1f005
        std::uint32_t out[3]{};const std::uint32_t a[2]{ped_,1};
        float t[3]{};
        if(!gps_ || !nat.call(n::GetEntityCoords,a,2,out) ||
           !gps_->target(g,asFloat(out[0]),asFloat(out[1]),asFloat(out[2]),t)){
            play(Sound::Error);std::snprintf(reason_,sizeof reason_,"%s",txt::HelpGpsOff);build(ctx);return;}
        call0(nat,n::SetNewWaypoint,{bits(t[0]),bits(t[1])});   // 0x1f078..0x1f134: SET_NEW_WAYPOINT(x, y)
        play(Sound::Select);close(nat,"quick-gps",true);
    }
    // 0x14066..0x14253: health = min(health + heal, max), the count down by one, a 30 s timer. No animation (the disc plays none).
    void eatSnack(Natives& nat,const Context& ctx,int k) noexcept {
        if(inVehicle_ || !snackReady() || store_.snacks[k]<=0){play(Sound::Error);return;}
        const int health=int(call1(nat,n::GetEntityHealth,{ped_})),maxHealth=int(call1(nat,n::GetEntityMaxHealth,{ped_}));
        const int heal=int(std::lround(SnackHealShare[k]*float(maxHealth)));
        call0(nat,n::SetEntityHealth,{ped_,std::uint32_t(health+heal>maxHealth?maxHealth:health+heal)});
        --store_.snacks[k];store_.snackReadyAt=now_+SnackCooldownMs;store_.snackTimed=true;storeDirty_=true;
        play(Sound::Select);build(ctx);
    }
    void applyMood(Natives& nat) noexcept {
        const int m=store_.mood>=0 && store_.mood<moods()?store_.mood:MoodNormal;const std::uint32_t clip=nat.text(MoodClips[m]);
        if(clip)call0(nat,n::SetFacialIdle,{ped_,clip,0});   // freemode 0xab53: (ped, clip, 0 = no dictionary)
    }
    void applySmoke(Natives& nat) noexcept {
        const Rgb& c=SmokeColours[store_.smokeTrail];
        call0(nat,n::SmokeTrailColour,{player_,std::uint32_t(c.r),std::uint32_t(c.g),std::uint32_t(c.b)});
        call0(nat,n::SmokeTrailOn,{player_,store_.smokeTrail>0?1u:0u});
    }
    // The disc's passive switch (0x20d79, network game only there): dragged-out off + unarmed on enable, dragged-out on when off.
    // The Sandbox version adds "people and the police ignore you" (owner pick, INFERRED reinterpretation; no friendly-fire option
    // offline: NETWORK_SET_FRIENDLY_FIRE_OPTION is a network setting and is not called).
    void applyPassive(Natives& nat,bool holster) noexcept {
        const bool on=store_.passive;
        call0(nat,n::SetPedCanBeDraggedOut,{ped_,on?0u:1u});
        if(on && holster)call0(nat,n::SetCurrentPedWeapon,{ped_,WeaponUnarmed,1});
        call0(nat,n::SetEveryoneIgnorePlayer,{player_,on?1u:0u});
        call0(nat,n::SetPoliceIgnorePlayer,{player_,on?1u:0u});
    }
    void reapplyOnPed(Natives& nat) noexcept {
        reapply_=false;player_=call1(nat,n::PlayerId,{});ped_=call1(nat,n::PlayerPedId,{});appliedPed_=ped_;
        if(!ped_)return;
        if(store_.mood!=MoodNormal)applyMood(nat);
        if(store_.passive)applyPassive(nat,false);
    }
    // am_pi_menu 0x1ed69 (= freemode 0x6956c): CLEAR_PED_TASKS, SET_PLAYER_CONTROL(player, FALSE, 900), stealth off; the fee
    // (NETWORK_BUY_HEALTHCARE, unregistered) is skipped. Then freemode's death step (runJob, Kill). Three unnamed natives of that path
    // are NOT called because their role is unknown: 0x1359292f (a local-player float, 0.0 here and 1.0 at the cleanup 0x695dc),
    // 0x2b83f43b(ped) (before each death) and 0xee292b91(hash from 0x6950d, 1.0) (after each death). The story hospital fee may apply.
    void killYourself(Natives& nat) noexcept {
        call0(nat,n::ClearPedTasks,{ped_});
        call0(nat,n::SetPlayerControl,{player_,0,KillControlFlags});
        call0(nat,n::SetPedStealth,{ped_,0,0});
        job_=Job{};job_.kind=Job::Kill;job_.ped=ped_;job_.player=player_;
        play(Sound::Select);close(nat,"kill-yourself",true);
    }
    // freemode 0x6922d..0x694ac, one step per tick.
    void killStep(Natives& nat) noexcept {
        const std::uint32_t ped=job_.ped;
        // Ours: control was taken (SET_PLAYER_CONTROL(player, 0, 900)) and only the death and respawn give it back. If the death is
        // not seen within KillDeathWaitFrames (invincible, or health 0 refused), control is given back here and the job ends.
        if(job_.stage==Job::KillDeathWait){
            if(call1(nat,n::IsEntityDead,{ped})){logf("RENDER_IMENU kill-yourself end=%s","dead");job_=Job{};return;}
            if(++job_.frames>=KillDeathWaitFrames){
                call0(nat,n::SetPlayerControl,{job_.player,1,0});
                logf("RENDER_IMENU kill-yourself end=%s control=restored","not-dead");job_=Job{};}
            return;
        }
        const std::uint32_t dict=nat.text(SuicideDict);
        const auto die=[&](bool ragdoll){
            if(ragdoll)(void)call1(nat,n::SetPedToRagdoll,{ped,0,std::uint32_t(RagdollTimeMs),0,0,0,0});   // 0x69451
            call0(nat,n::SetEntityHealth,{ped,0});                                                      // 0x69463 / 0x6928d
            if(job_.stage>0 && dict)call0(nat,n::RemoveAnimDict,{dict});                                // 0x695ea
            logf("RENDER_IMENU kill-yourself health0 stage=%d pistol=%d",job_.stage,job_.pistol?1:0);
            job_.stage=Job::KillDeathWait;job_.frames=0;
        };
        if(job_.stage==0){   // 0x6923f: before the clip starts, a vehicle, water or the air means an instant death
            if(call1(nat,n::IsPedInAnyVehicle,{ped,0}) || call1(nat,n::IsEntityInWater,{ped}) || call1(nat,n::IsEntityInAir,{ped})){die(false);return;}
            if(!dict){die(false);return;}
            call0(nat,n::RequestAnimDict,{dict});                                                       // 0x694ed
            if(!call1(nat,n::HasAnimDictLoaded,{dict})){if(++job_.frames>=AnimLoadFrames)die(false);return;}
            job_.pistol=call1(nat,n::HasPedGotWeapon,{ped,WeaponPistol,0}) && call1(nat,n::GetAmmoInPedWeapon,{ped,WeaponPistol})>0
                && !call1(nat,n::HasWeaponComponent,{ped,WeaponPistol,PistolSuppressor});                    // 0x692b6..0x692ee
            call0(nat,n::SetCurrentPedWeapon,{ped,job_.pistol?WeaponPistol:WeaponUnarmed,1});                // 0x69324 / 0x6935e
            const std::uint32_t clip=nat.text(job_.pistol?SuicidePistol:SuicidePill);
            if(!clip){die(false);return;}
            call0(nat,n::TaskPlayAnim,{ped,dict,clip,bits(AnimBlendIn),bits(AnimBlendOut),0xFFFFFFFFu,SuicideAnimFlags,0,0,0,0});   // 0x6939f
            job_.stage=1;job_.frames=0;return;
        }
        const std::uint32_t clip=nat.text(job_.pistol?SuicidePistol:SuicidePill);
        if(!dict || !clip || !call1(nat,n::IsEntityPlayingAnim,{ped,dict,clip,3})){die(true);return;}      // 0x693c4 -> 0x69477
        if(!job_.fired && call1(nat,n::HasAnimEventFired,{ped,FireEvent})){                                  // 0x693e9
            if(job_.pistol)call0(nat,n::ShootsAtCoord,{ped,bits(0.0f),bits(0.0f),bits(0.0f),0});              // 0x69414
            job_.fired=true;
        }
        const float t=asFloat(call1(nat,n::AnimCurrentTime,{ped,dict,clip}));                                // 0x69435
        if(t>(job_.pistol?SuicidePistolPhase:SuicidePillPhase))die(true);
        else if(++job_.frames>=AnimStageFrames)die(true);   // (ours: a bound if the clip never reaches its phase)
    }
    // A job ended from outside (stand-down: freemode took over, or Story): an action's dictionary is released; a Kill job gives player
    // control back (it was taken at the accept and only a death and respawn would return it).
    void dropJob(Natives& nat,const char* why) noexcept {
        if(job_.kind==Job::Anim){if(const std::uint32_t d=nat.text(OnFootActions[job_.action].dict))call0(nat,n::RemoveAnimDict,{d});}
        else if(job_.kind==Job::Kill){
            if(job_.stage>0 && job_.stage!=Job::KillDeathWait){if(const std::uint32_t d=nat.text(SuicideDict))call0(nat,n::RemoveAnimDict,{d});}
            call0(nat,n::SetPlayerControl,{job_.player,1,0});
        }
        logf("RENDER_IMENU job dropped kind=%d reason=%s",int(job_.kind),why);job_=Job{};
    }
    void startAnim(int action) noexcept {job_=Job{};job_.kind=Job::Anim;job_.action=action;job_.ped=ped_;}
    // The chosen action played now (enter, idle, exit), after its dictionary loads (REQUEST_ANIM_DICT / HAS_ANIM_DICT_LOADED, E5).
    void runJob(Natives& nat,const Context& ctx) noexcept {
        if(job_.kind==Job::Kill){killStep(nat);return;}
        const ActionClip& a=OnFootActions[job_.action];
        const std::uint32_t dict=nat.text(a.dict);
        if(!dict){job_=Job{};return;}
        const std::uint32_t ped=call1(nat,n::PlayerPedId,{});
        const auto stop=[&](const char* why){call0(nat,n::RemoveAnimDict,{dict});logf("RENDER_IMENU action=%s end=%s",a.label,why);job_=Job{};};
        if(ped!=job_.ped || ctx.phoneUp){stop("interrupted");return;}
        // The player got into a vehicle, died or fell: the enter / idle / exit chain stops (no new TASK_PLAY_ANIM on that ped).
        if(call1(nat,n::IsPedInAnyVehicle,{ped,0}) || call1(nat,n::IsEntityDead,{ped}) || call1(nat,n::IsPedRagdoll,{ped})){stop("interrupted");return;}
        ++job_.frames;
        if(job_.stage==0){
            call0(nat,n::RequestAnimDict,{dict});
            if(call1(nat,n::HasAnimDictLoaded,{dict})){job_.stage=1;job_.frames=0;}
            else if(job_.frames>=AnimLoadFrames)stop("not-loaded");
            return;
        }
        const char* clips[3]{a.enter,a.idle,a.exit};
        const int k=job_.stage-1;const std::uint32_t clip=nat.text(clips[k]);
        if(!clip){stop("no-scratch");return;}
        if(job_.frames==1){   // start this stage's clip (freemode's TASK_PLAY_ANIM argument pattern, 11 words)
            call0(nat,n::TaskPlayAnim,{ped,dict,clip,bits(AnimBlendIn),bits(AnimBlendOut),0xFFFFFFFFu,AnimFlagUpper,0,0,0,0});
            return;
        }
        if(job_.frames<=AnimGraceFrames)return;   // INFERRED: the task needs a few frames before the clip reports playing
        if(job_.frames<AnimStageFrames && call1(nat,n::IsEntityPlayingAnim,{ped,dict,clip,3}))return;
        if(job_.stage==3){stop("done");return;}
        ++job_.stage;job_.frames=0;
    }

    LogFn log_=nullptr;Lists lists_{};GpsSource* gps_=nullptr;Store store_{};bool storeDirty_=false;
    State state_=State::Closed;Page page_=Page::Main;Context ctx_{};View view_{};Inventory inv_{};Job job_{};
    std::uint32_t now_=0,holdAt_=0,closedAt_=0,backAt_=0,backDownAt_=0,stale_=0,repeatAt_=0,refreshedAt_=0,player_=0,ped_=0,appliedPed_=0,nameAddr_=0;
    std::uint32_t pendingFrames_=0;std::int32_t delay_=-1;
    int sel_=0,top_=0,parentSel_=0,pendingSeq_=-1,vehicle_=0,weather_=0,hour_=12;
    const imenugate::BackGate* backSrc_=nullptr;bool opener_=true;std::atomic<bool> openReq_{false},closeReq_{false};   // v989
    bool cooling_=false,needRelease_=false,prevBack_=false,backLatched_=false,backHolding_=false,pendingClose_=false,inVehicle_=false,reapply_=false;
    bool prevNext_=false,prevPrev_=false,prevLeft_=false,prevRight_=false,prevAccept_=false,prevCancel_=false;
    Sound sounds_[4]{};int soundCount_=0;const char* closeReason_="";char reason_[64]{};
};

// The glue's adapter onto the trainer (v972 property_doors pattern: the status line "seq=N ok|fail|dropped ..." read back).
class TrainerRequests final : public Requests {
public:
    explicit TrainerRequests(trainer::Trainer& t) noexcept:t_(t) {}
    int request(int action,int a0,int a1) override {return t_.request(action,a0,a1,0);}
    int poll(int seq,char* detail,int cap) override {
        char s[160]{};const int at=t_.status(s,sizeof s);
        if(at<seq)return 0;
        if(at>seq){if(detail && cap>0)std::snprintf(detail,std::size_t(cap),"result-lost");return 2;}   // a later result replaced ours
        int got=0;char kind[16]{};int used=0;
        if(std::sscanf(s,"seq=%d %15s %n",&got,kind,&used)<2 || got!=seq){if(detail && cap>0)std::snprintf(detail,std::size_t(cap),"bad-status");return 2;}
        if(std::strcmp(kind,"ok")==0)return 1;
        if(detail && cap>0){const char* rest=s+used;const char* sp=std::strchr(rest,' ');std::snprintf(detail,std::size_t(cap),"%s",sp?sp+1:rest);}
        return 2;
    }
private:
    trainer::Trainer& t_;
};

} // namespace recomp::imenu
