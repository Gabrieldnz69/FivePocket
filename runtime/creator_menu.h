#pragma once
// creator_menu.h (PROPOSED, Update 1 lane upd1-creator-menu/creator-controller; NOTES.md in the lane has the evidence and the device
// checks). The in-game character creator menu of Sandbox mode, drawn and driven by the game library: owner 2026-11-07 "for the character
// customizer, it should zoom in on the player, lock them in place, then show the customization options live, using the game's own
// in-game menu assets". Built on character_creator.h (the look, its fields, the apply job, the record) and run as ONE trainer job:
// trainer action 17 a0 = 4 (PROPOSED OpenMenu) starts it, its step runs once per game frame at the trainer's safe point while the menu
// is open, and the trainer's gate (pause menu, cutscene, death, fade, switch, trainer off) aborts it, which restores everything.
// Pure: no guest memory, no files, no platform calls; natives through trainer::Natives on the script thread; no allocation.
//
// What the player's own disc says (names-only bytecode reads of hairdo_shop_sp / clothes_shop_sp / am_pi_menu and every decoded
// script; tools/menu_scan.py, tools/control_hist.py; NOTES.md section 1). OBSERVED unless marked:
//  - Lock: CLEAR_PED_TASKS(ped); SET_PLAYER_CONTROL(player, FALSE, 0) (clothes_shop_sp 0x36b40 / 0x36b72, hairdo_shop_sp 0x19d09 /
//    0x19d3b); SET_ENTITY_HEADING(ped, h); FREEZE_ENTITY_POSITION(ped, TRUE) (clothes 0x36d52 / 0x36d6b, hairdo 0x19f32 / 0x19f4b).
//    Exit: SET_CAM_ACTIVE(cam, FALSE); DESTROY_CAM(cam, FALSE); RENDER_SCRIPT_CAMS(FALSE, FALSE, 3000, TRUE, FALSE);
//    SET_PLAYER_CONTROL(player, TRUE, 0); FREEZE_ENTITY_POSITION(ped, FALSE) (clothes 0x42b32..0x42bef).
//  - Camera: CREATE_CAM("DEFAULT_SCRIPTED_CAMERA", FALSE); SET_CAM_COORD; POINT_CAM_AT_COORD; SET_CAM_FOV (47 at the barber);
//    SET_CAM_ACTIVE(cam, TRUE); RENDER_SCRIPT_CAMS(TRUE, FALSE, 3000, TRUE, FALSE) (hairdo 0x15d92..0x15ea9, clothes 0x3611b..0x367a5).
//    Moves between two cameras: SET_CAM_ACTIVE_WITH_INTERP(to, from, ms, 1, 1) (golf_mp 0x3e9e3: 500 ms, 1, 1).
//  - Menu input, pad 2 (the frontend pad): 162 = up (selection - 1, wraps to the last row), 161 = down (hairdo 0x10caf / 0x10d5b),
//    163 / 164 = the left / right pair (am_pi_menu's value changes), 174 = accept, 175 = cancel (the most used pair, 174 / 125
//    sites), 180 / 181 = LT / RT. The names are INFERRED from the pairs and the final game's ordering; the uses are OBSERVED.
//    The left stick: GET_CONTROL_NORMAL(2, 185 / 186) * 127, beyond +-100 = a press, back inside +-75 = released, y < 0 = up (hairdo
//    0x4f0b / 0x10cbf). Held: a step every 500 ms, every 125 ms (500 * 0.25) while RT is held (hairdo 0x4c29 with 500 at 0x10c77).
//    We read the IS_DISABLED_* / GET_DISABLED_* forms (disc sites on pad 2: 161, 162, 163, 164, 174, 175, 181) so a script that
//    disables the frontend controls cannot take the menu's input away; held repeats every 125 ms after the first 500 ms (a choice:
//    the shop's own fast rate), because LT / RT turn the character here (the owner's request) instead of speeding the list up.
//  - Sounds: PLAY_SOUND_FRONTEND(-1, "NAV_UP_DOWN" / "NAV_LEFT_RIGHT" / "SELECT" / "BACK", "HUD_FRONTEND_DEFAULT_SOUNDSET")
//    (hairdo 0x10cf5, am_pi_menu), "ERROR" with "HUD_FRONTEND_CLOTHESSHOP_SOUNDSET" (clothes_shop_sp).
//  - HIDE_HUD_AND_RADAR_THIS_FRAME every frame while the shop menu is up (hairdo 0x1a3ba, clothes 0x3c383).
//  - Bones: 31086 = head (384 GET_PED_BONE_COORDS sites, face_camera.h), 24818 = the upper spine (stripclub_mp 0x450e0..0x4511b; the
//    name SKEL_Spine3 is INFERRED). Beard = overlay 1, eyebrows = overlay 2 (maintransition 0x3955b / 0x3956f, CC lane NOTES);
//    makeup = overlay 4 (INFERRED: the public slot order). Tops = jbib (11), undershirt = accs (8, INFERRED meaning), legs = lowr (4),
//    shoes = feet (6), hair = 2, hair colour = the hair texture (no hair colour native is registered on this build: CC lane NOTES).
//    Hats = p_head (prop slot 0, anchor 0), glasses = p_eyes (prop slot 1, anchor 1).
//
// The C2 rule (docs/fable DISCOVERIES 2026-11-06, the C2cam freeze: a head-blend edit with the creator's close camera on hung the GPU,
// twice; 14 overlay edits and hair / clothes / prop edits with the camera on all passed, C3cam / C4cam): the menu never edits the head
// blend, and every full apply (StartEditing at open, SaveEdits / DiscardEdits at close, the revert of a forced close) runs with no
// script camera rendering: the camera starts after the open apply finished and is released before any close apply begins - the exact
// order of the device-proven 17 3 / 17 1 / 17 2 path (v959 Actions). Only overlay / component / prop calls run under the camera.
//
// Argument slots (our recompiled handlers, validated-ppc; tools/check_menu_natives.py re-checks each against the corpus and the disc's
// own (args, returns) shapes): see that tool's EXPECT table.
//
// Frame rate: closed = no code at all (no trainer job). Open = about 17 native calls per frame (one timer, 12 input reads, the HUD
// hide, one camera existence check, the drawer's own) plus 1-4 per edit and one SET_ENTITY_HEADING per frame while turning. The view
// is rebuilt only on a change. No allocation anywhere: fixed members, static strings, the trainer's per-tick scratch for strings.
#include "character_creator.h"
#include "menu_view.h"
#if __has_include("menu_draw.h")
#include "menu_draw.h"      // lane upd1-creator-menu/shop-menu-look: menudraw::Renderer (load / draw(Page) / close), the shop look
#else
#include "menu_draw_stub.h" // until that header is merged: the same API, draws nothing
#endif
#include <atomic>   // v975 (G3)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::creatormenu {
using trainer::Natives;
using trainer::joaat;
using trainer::bits;
using trainer::asFloat;
namespace cr=recomp::creator;

struct Named {const char* name;std::uint32_t hash;};
namespace n {
constexpr std::uint32_t PlayerPedId=joaat("PLAYER_PED_ID"),Freeze=joaat("FREEZE_ENTITY_POSITION"),PlayerControl=joaat("SET_PLAYER_CONTROL"),
    ClearTasks=joaat("CLEAR_PED_TASKS"),SetHeading=joaat("SET_ENTITY_HEADING"),GetHeading=joaat("GET_ENTITY_HEADING"),
    EntityCoords=joaat("GET_ENTITY_COORDS"),GameplayCamCoord=joaat("GET_GAMEPLAY_CAM_COORD"),HeadingFrom2D=joaat("GET_HEADING_FROM_VECTOR_2D"),
    BoneCoords=joaat("GET_PED_BONE_COORDS"),OffsetInWorld=joaat("GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS"),CreateCam=joaat("CREATE_CAM"),
    CamCoord=joaat("SET_CAM_COORD"),PointAtCoord=joaat("POINT_CAM_AT_COORD"),CamFov=joaat("SET_CAM_FOV"),CamActive=joaat("SET_CAM_ACTIVE"),
    CamInterp=joaat("SET_CAM_ACTIVE_WITH_INTERP"),RenderScriptCams=joaat("RENDER_SCRIPT_CAMS"),DestroyCam=joaat("DESTROY_CAM"),
    DoesCamExist=joaat("DOES_CAM_EXIST"),JustPressed=joaat("IS_DISABLED_CONTROL_JUST_PRESSED"),Pressed=joaat("IS_DISABLED_CONTROL_PRESSED"),
    ControlNormal=joaat("GET_DISABLED_CONTROL_NORMAL"),GameTimer=joaat("GET_GAME_TIMER"),HideHud=joaat("HIDE_HUD_AND_RADAR_THIS_FRAME"),
    PlaySound=joaat("PLAY_SOUND_FRONTEND"),InVehicle=joaat("IS_PED_IN_ANY_VEHICLE"),MissionFlag=joaat("GET_MISSION_FLAG"),
    Ragdoll=joaat("IS_PED_RAGDOLL"),Falling=joaat("IS_PED_FALLING"),Swimming=joaat("IS_PED_SWIMMING"),EntityExists=joaat("DOES_ENTITY_EXIST"),
    Drawables=joaat("GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS"),Textures=joaat("GET_NUMBER_OF_PED_TEXTURE_VARIATIONS"),
    PropDrawables=joaat("GET_NUMBER_OF_PED_PROP_DRAWABLE_VARIATIONS"),PropTextures=joaat("GET_NUMBER_OF_PED_PROP_TEXTURE_VARIATIONS"),
    OverlayNum=joaat("GET_PED_HEAD_OVERLAY_NUM");
}
inline constexpr Named Used[]{{"PLAYER_PED_ID",n::PlayerPedId},{"FREEZE_ENTITY_POSITION",n::Freeze},{"SET_PLAYER_CONTROL",n::PlayerControl},
    {"CLEAR_PED_TASKS",n::ClearTasks},{"SET_ENTITY_HEADING",n::SetHeading},{"GET_ENTITY_HEADING",n::GetHeading},
    {"GET_ENTITY_COORDS",n::EntityCoords},{"GET_GAMEPLAY_CAM_COORD",n::GameplayCamCoord},{"GET_HEADING_FROM_VECTOR_2D",n::HeadingFrom2D},
    {"GET_PED_BONE_COORDS",n::BoneCoords},{"GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS",n::OffsetInWorld},{"CREATE_CAM",n::CreateCam},
    {"SET_CAM_COORD",n::CamCoord},{"POINT_CAM_AT_COORD",n::PointAtCoord},{"SET_CAM_FOV",n::CamFov},{"SET_CAM_ACTIVE",n::CamActive},
    {"SET_CAM_ACTIVE_WITH_INTERP",n::CamInterp},{"RENDER_SCRIPT_CAMS",n::RenderScriptCams},{"DESTROY_CAM",n::DestroyCam},
    {"DOES_CAM_EXIST",n::DoesCamExist},{"IS_DISABLED_CONTROL_JUST_PRESSED",n::JustPressed},{"IS_DISABLED_CONTROL_PRESSED",n::Pressed},
    {"GET_DISABLED_CONTROL_NORMAL",n::ControlNormal},{"GET_GAME_TIMER",n::GameTimer},{"HIDE_HUD_AND_RADAR_THIS_FRAME",n::HideHud},
    {"PLAY_SOUND_FRONTEND",n::PlaySound},{"IS_PED_IN_ANY_VEHICLE",n::InVehicle},{"GET_MISSION_FLAG",n::MissionFlag},
    {"IS_PED_RAGDOLL",n::Ragdoll},{"IS_PED_FALLING",n::Falling},{"IS_PED_SWIMMING",n::Swimming},{"DOES_ENTITY_EXIST",n::EntityExists},
    {"GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS",n::Drawables},{"GET_NUMBER_OF_PED_TEXTURE_VARIATIONS",n::Textures},
    {"GET_NUMBER_OF_PED_PROP_DRAWABLE_VARIATIONS",n::PropDrawables},{"GET_NUMBER_OF_PED_PROP_TEXTURE_VARIATIONS",n::PropTextures},
    {"GET_PED_HEAD_OVERLAY_NUM",n::OverlayNum}};
inline constexpr std::uint32_t UsedNatives[]{n::PlayerPedId,n::Freeze,n::PlayerControl,n::ClearTasks,n::SetHeading,n::GetHeading,
    n::EntityCoords,n::GameplayCamCoord,n::HeadingFrom2D,n::BoneCoords,n::OffsetInWorld,n::CreateCam,n::CamCoord,n::PointAtCoord,n::CamFov,
    n::CamActive,n::CamInterp,n::RenderScriptCams,n::DestroyCam,n::DoesCamExist,n::JustPressed,n::Pressed,n::ControlNormal,n::GameTimer,
    n::HideHud,n::PlaySound,n::InVehicle,n::MissionFlag,n::Ragdoll,n::Falling,n::Swimming,n::EntityExists,n::Drawables,n::Textures,
    n::PropDrawables,n::PropTextures,n::OverlayNum};
static_assert(std::size(Used)==std::size(UsedNatives));

// ---------------------------------------------------------------------------------------------------- constants
constexpr int OpenMenuOp=4;   // PROPOSED: trainer action 17 a0 = 4 opens this menu (17 0..3 stay character_creator.h's LookOp)
constexpr std::uint32_t Pad=2;   // the frontend pad
constexpr std::uint32_t CtlDown=161,CtlUp=162,CtlLeft=163,CtlRight=164,CtlAccept=174,CtlCancel=175,CtlLT=180,CtlRT=181,AxisX=185,AxisY=186,AxisRX=187;   // v975: 187 = the right stick X the shops read (pad 2)
constexpr std::uint32_t RepeatDelayMs=500,RepeatMs=125;
constexpr float StickPress=100.0f/127.0f,StickRelease=75.0f/127.0f;
constexpr float TurnDegPerSecond=120.0f;          // a choice (NEEDS DEVICE CHECK for feel)
constexpr float TurnStickDead=0.25f;              // v975: right-stick dead zone (the game ignores steering under ~0.25)
constexpr std::uint32_t TurnStepMaxMs=100;        // a long frame turns at most this much time's worth
constexpr std::uint32_t CamInterpMs=500,RenderEaseMs=3000;
constexpr int CamRebuildMax=2;                    // a camera the engine took away is made again at most this often per session
constexpr std::uint32_t HeadBone=31086,SpineBone=24818;
constexpr int OpacitySteps=10;                    // opacity row: 10 % .. 100 %

enum class Framing:std::uint8_t {Face=0,Upper=1,Full=2};
// The three shots, in the ped's own frame at the moment of the lock (x = the ped's right, y = its front, z = up from its origin).
// The camera stands at (shift, dist, z + raise) and looks at (shift, 0, z): the ped faces it and appears right of the screen centre,
// clear of the menu at the top left. All values INFERRED (NEEDS DEVICE CHECK M3): the barber's FOV is 47, am_pi_menu's face shot 0.7 m.
struct Shot {float dist,shift,raise,fov;};
inline constexpr Shot Shots[3]{{0.85f,0.12f,0.00f,40.0f},{1.45f,0.25f,0.05f,45.0f},{2.90f,0.45f,0.10f,45.0f}};
constexpr float DefaultHeadZ=0.65f,DefaultSpineZ=0.35f,FullZ=-0.15f,UpperRaiseZ=0.05f;

// ---------------------------------------------------------------------------------------------------- the menu tree
enum Cat : int {CatGender,CatHair,CatHairColour,CatBeard,CatEyebrows,CatMakeup,CatTops,CatUndershirt,CatLegs,CatShoes,CatHats,CatGlasses,
    CatSave,CatExit,CatCount};
enum class Kind:std::uint8_t {Gender,Hair,HairColour,Overlay,Part,Prop,Save,Exit};
struct CatDef {const char* label;const char* header;Kind kind;int index;Framing framing;const char* help;};
inline constexpr CatDef Cats[CatCount]{
    {"Gender","GENDER",Kind::Gender,0,Framing::Full,"Change gender. Unsaved changes are discarded."},
    {"Hair","HAIR",Kind::Hair,2,Framing::Face,"Choose a hairstyle."},
    {"Hair Colour","HAIR COLOUR",Kind::HairColour,2,Framing::Face,"Choose a hair colour."},
    {"Beard","BEARD",Kind::Overlay,1,Framing::Face,"Choose facial hair."},
    {"Eyebrows","EYEBROWS",Kind::Overlay,2,Framing::Face,"Choose eyebrows."},
    {"Makeup","MAKEUP",Kind::Overlay,4,Framing::Face,"Choose makeup."},
    {"Tops","TOPS",Kind::Part,11,Framing::Upper,"Choose a top."},
    {"Undershirt","UNDERSHIRT",Kind::Part,8,Framing::Upper,"Choose an undershirt."},
    {"Legs","LEGS",Kind::Part,4,Framing::Full,"Choose legs."},
    {"Shoes","SHOES",Kind::Part,6,Framing::Full,"Choose shoes."},
    {"Hats","HATS",Kind::Prop,0,Framing::Upper,"Choose a hat."},
    {"Glasses","GLASSES",Kind::Prop,1,Framing::Upper,"Choose glasses."},
    {"Save & Exit","SAVE & EXIT",Kind::Save,0,Framing::Full,"Save this look and leave the creator."},
    {"Exit Without Saving","EXIT",Kind::Exit,0,Framing::Full,"Leave the creator. The look goes back to what it was."}};
constexpr const char* TopHelp="Right stick or LT / RT turn the character.";   // v975

// ---------------------------------------------------------------------------------------------------- input (pure)
enum Dir : int {DirUp=0,DirDown=1,DirLeft=2,DirRight=3};
inline constexpr std::uint32_t DirControl[4]{CtlUp,CtlDown,CtlLeft,CtlRight};
// v975 (lead's decision): the instructional buttons the story shops show (shop-menu-look NOTES: clothes_shop_sp 0x375c8, hairdo 0xe4ad):
// 30 ITEM_SELECT, 31 ITEM_BACK, GET_CONTROL_GROUP_INSTRUCTIONAL_BUTTON(2, 18, 1) ITEM_MOV_CAM (here the right stick turns the character).
inline constexpr menudraw::Button Buttons[]{{30,"ITEM_SELECT"},{31,"ITEM_BACK"},{0,"ITEM_MOV_CAM",2,18}};
// One frame of raw pad state, as the natives answered.
struct Raw {bool just[4]{},held[4]{};bool accept=false,cancel=false,lt=false,rt=false;float sx=0,sy=0,rx=0;std::uint32_t now=0;};
struct Events {bool dir[4]{};bool accept=false,cancel=false;float turn=0;};   // turn in [-1,1]: +1 LT (heading up), -1 RT;
                                                                              // v975: the right stick adds -x past the dead zone (lead's decision)
// A direction: fires on a fresh press, then after RepeatDelayMs, then every RepeatMs while held. One already held when the menu opens
// (a touch stick still pushed from walking, a d-pad press that opened the menu) stays silent until it is released once (review
// 2026-11-07 MINOR 2: before, it fired on the second open frame and repeated every RepeatMs with no delay).
class Repeater {
public:
    void prime(bool held,std::uint32_t now) noexcept {held_=held;latched_=held;next_=now+RepeatDelayMs;}
    bool update(bool just,bool held,std::uint32_t now) noexcept {
        if(latched_) {   // a fresh press (just) means it was released in between: it fires as usual
            if(!just){held_=held;if(!held)latched_=false;return false;}
            latched_=false;
        }
        if(just || (held && !held_)){held_=held;next_=now+RepeatDelayMs;return true;}
        held_=held;
        if(!held)return false;
        if(std::int32_t(now-next_)>=0){next_=now+RepeatMs;return true;}
        return false;
    }
private:
    bool held_=false,latched_=false;std::uint32_t next_=0;
};
// The stick as a d-pad, with the shop's hysteresis (press beyond StickPress, release inside StickRelease).
inline int stickDir(int was,float v) noexcept {
    if(!std::isfinite(v))return 0;
    if(v>StickPress)return 1;
    if(v< -StickPress)return -1;
    if(v<StickRelease && v> -StickRelease)return 0;
    return was;
}
class InputReader {
public:
    // The first frame of a session: what is already held stays silent until released, presses of this frame are swallowed.
    void prime(const Raw& r) noexcept {
        sx_=stickDir(0,r.sx);sy_=stickDir(0,r.sy);
        for(int d=0;d<4;++d)rep_[d].prime(heldWithStick(r,d),r.now);
    }
    Events update(const Raw& r) noexcept {
        sx_=stickDir(sx_,r.sx);sy_=stickDir(sy_,r.sy);
        Events e;
        for(int d=0;d<4;++d)e.dir[d]=rep_[d].update(r.just[d],heldWithStick(r,d),r.now);
        if(e.dir[DirUp] && e.dir[DirDown])e.dir[DirUp]=e.dir[DirDown]=false;
        if(e.dir[DirLeft] && e.dir[DirRight])e.dir[DirLeft]=e.dir[DirRight]=false;
        e.accept=r.accept;e.cancel=r.cancel;
        float t=float((r.lt?1:0)-(r.rt?1:0));
        if(std::isfinite(r.rx) && (r.rx>TurnStickDead || r.rx< -TurnStickDead))t-=r.rx;   // v975: stick right turns clockwise (heading down)
        e.turn=t>1.0f?1.0f:t< -1.0f?-1.0f:t;
        return e;
    }
private:
    bool heldWithStick(const Raw& r,int d) const noexcept {
        const int s=d==DirUp?(sy_<0):d==DirDown?(sy_>0):d==DirLeft?(sx_<0):(sx_>0);
        return r.held[d] || s;
    }
    Repeater rep_[4];int sx_=0,sy_=0;
};
// values lo .. lo + count - 1, wrapping like the shop's lists (hairdo 0x10d26: first - 1 -> the last)
constexpr int stepWrap(int value,int lo,int count,int delta) noexcept {
    if(count<=0)return value;
    const int i=((value-lo+delta)%count+count)%count;
    return lo+i;
}

// ---------------------------------------------------------------------------------------------------- the host (online_world.h Actions)
// What the menu needs from the Sandbox's online actions: the creator (edit buffer + apply job) and the freemode character (action 15's
// model change). Results: 0 running, 1 ok, 2 fail (detail written).
struct Host {
    virtual ~Host()=default;
    virtual cr::Creator& looks() noexcept=0;
    virtual std::uint32_t onlineModel() const noexcept=0;     // the freemode model on the player now (0: none)
    virtual bool modelBusy() const noexcept=0;                // a 15 change is running
    virtual int changeModel(Natives& nat,std::uint32_t ped,bool female,std::uint32_t frame,char* detail,int cap) noexcept=0;
    virtual int stepModel(Natives& nat,std::uint32_t player,std::uint32_t frame,char* detail,int cap) noexcept=0;
    virtual void abortModel(Natives& nat) noexcept=0;
    // A 17 3 edit session the player is in right now (v959: its face camera is on): 17 4 refuses rather than replace its unsaved edits.
    virtual bool editSessionLive() const noexcept=0;
    // Called once the menu WILL open (every refusal passed), before the lock and the open apply: v959's face camera goes (review MINOR 7).
    virtual void beforeOpen(Natives& nat) noexcept=0;
    virtual bool doorChooserOpen() const noexcept {return false;}   // v975 (G2): one Sandbox chooser / menu at a time
};

// ---------------------------------------------------------------------------------------------------- small native helpers
struct V3 {float x=0,y=0,z=0;};
inline std::uint32_t c1(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a,bool* ok=nullptr) noexcept {
    std::uint32_t out[3]{};const bool r=nat.call(h,a.begin(),unsigned(a.size()),out);if(ok)*ok=r;return r?out[0]:0u;
}
inline bool c0(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
    std::uint32_t out[3]{};return nat.call(h,a.begin(),unsigned(a.size()),out);
}
inline bool c3(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a,V3& v) noexcept {
    std::uint32_t out[3]{};if(!nat.call(h,a.begin(),unsigned(a.size()),out))return false;
    V3 t{asFloat(out[0]),asFloat(out[1]),asFloat(out[2])};
    if(!std::isfinite(t.x) || !std::isfinite(t.y) || !std::isfinite(t.z))return false;
    v=t;return true;
}
inline int count(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {   // -1: unknown
    bool ok=false;const std::uint32_t v=c1(nat,h,a,&ok);return ok && v<1024u?int(v):-1;
}

// ---------------------------------------------------------------------------------------------------- the lock
// The player in place for the session: tasks cleared, control off, position frozen, facing the camera. unlock() is the one way back
// and is idempotent; a ped that no longer exists is not touched (FREEZE on a deleted handle is skipped), control always comes back.
class Lock {
public:
    bool frozen() const noexcept {return frozen_;}
    bool controlOff() const noexcept {return controlOff_;}
    float heading() const noexcept {return heading_;}
    void lock(Natives& nat,std::uint32_t player,std::uint32_t ped,float heading) noexcept {
        player_=player;ped_=ped;heading_=wrap(heading);
        c0(nat,n::ClearTasks,{ped});
        if(!controlOff_){c0(nat,n::PlayerControl,{player,0,0});controlOff_=true;}
        c0(nat,n::SetHeading,{ped,bits(heading_)});
        c0(nat,n::Freeze,{ped,1});frozen_=true;
    }
    void turn(Natives& nat,float degrees) noexcept {
        if(!frozen_ || degrees==0.0f)return;
        heading_=wrap(heading_+degrees);c0(nat,n::SetHeading,{ped_,bits(heading_)});
    }
    void unfreeze(Natives& nat) noexcept {   // the ped only (a model change replaces it; control stays off)
        if(!frozen_)return;
        frozen_=false;
        if(c1(nat,n::EntityExists,{ped_}))c0(nat,n::Freeze,{ped_,0});
    }
    void unlock(Natives& nat) noexcept {
        unfreeze(nat);
        if(controlOff_){c0(nat,n::PlayerControl,{player_,1,0});controlOff_=false;}
    }
private:
    static float wrap(float h) noexcept {if(!std::isfinite(h))return 0.0f;h=std::fmod(h,360.0f);return h<0?h+360.0f:h;}
    std::uint32_t player_=0,ped_=0;float heading_=0;bool frozen_=false,controlOff_=false;
};
// The heading that faces the gameplay camera (usually open space behind the player); the current heading when that is not known.
inline float facingHeading(Natives& nat,std::uint32_t ped) noexcept {
    bool ok=false;float h=asFloat(c1(nat,n::GetHeading,{ped},&ok));
    if(!ok || !std::isfinite(h))h=0.0f;
    V3 p,g;
    if(c3(nat,n::EntityCoords,{ped,1},p) && c3(nat,n::GameplayCamCoord,{},g)) {
        const float dx=g.x-p.x,dy=g.y-p.y;
        if(dx*dx+dy*dy>0.04f) {
            const float t=asFloat(c1(nat,n::HeadingFrom2D,{bits(dx),bits(dy)},&ok));
            if(ok && std::isfinite(t))h=t;
        }
    }
    return h;
}

// ---------------------------------------------------------------------------------------------------- the camera
// Two scripted cameras: the shown one and the next shot. plan() fixes the three shots from the ped as it stands after the lock (the ped
// turns afterwards, the cameras do not). release() is every exit: SET_CAM_ACTIVE FALSE + DESTROY_CAM for each camera that still
// exists, RENDER_SCRIPT_CAMS FALSE always; a second release calls nothing.
class MenuCamera {
public:
    bool on() const noexcept {return on_;}
    Framing framing() const noexcept {return framing_;}
    void plan(Natives& nat,std::uint32_t ped) noexcept {
        V3 p,head,spine;
        const bool base=c3(nat,n::EntityCoords,{ped,1},p);
        float headZ=DefaultHeadZ,spineZ=DefaultSpineZ;
        if(base && c3(nat,n::BoneCoords,{ped,HeadBone,bits(0.f),bits(0.f),bits(0.f)},head) && sane(head.z-p.z))headZ=head.z-p.z;
        if(base && c3(nat,n::BoneCoords,{ped,SpineBone,bits(0.f),bits(0.f),bits(0.f)},spine) && sane(spine.z-p.z))spineZ=spine.z-p.z;
        const float z[3]{headZ,spineZ+UpperRaiseZ,FullZ};
        planned_=true;
        for(int f=0;f<3;++f) {
            const Shot& s=Shots[f];
            planned_=planned_ && c3(nat,n::OffsetInWorld,{ped,bits(s.shift),bits(s.dist),bits(z[f]+s.raise)},from_[f])
                && c3(nat,n::OffsetInWorld,{ped,bits(s.shift),bits(0.0f),bits(z[f])},at_[f]);
        }
    }
    bool begin(Natives& nat,Framing f) noexcept {
        if(on_)return true;
        if(!planned_)return false;
        const std::uint32_t name=nat.text("DEFAULT_SCRIPTED_CAMERA");
        if(!name)return false;
        for(int i=0;i<2;++i) {
            cam_[i]=c1(nat,n::CreateCam,{name,0});
            if(!exists(nat,cam_[i])){for(int j=0;j<=i;++j)destroyIf(nat,cam_[j]);cam_[0]=cam_[1]=0;return false;}
        }
        active_=0;framing_=f;aim(nat,cam_[0],f);
        c0(nat,n::CamActive,{cam_[0],1});
        c0(nat,n::RenderScriptCams,{1,0,RenderEaseMs,1,0});
        on_=true;return true;
    }
    void moveTo(Natives& nat,Framing f) noexcept {
        if(!on_ || f==framing_)return;
        const int next=1-active_;
        aim(nat,cam_[next],f);
        c0(nat,n::CamInterp,{cam_[next],cam_[active_],CamInterpMs,1,1});
        active_=next;framing_=f;
    }
    bool alive(Natives& nat) noexcept {return on_ && exists(nat,cam_[active_]);}
    bool release(Natives& nat) noexcept {
        if(!on_)return false;
        on_=false;
        bool still[2]{exists(nat,cam_[0]),exists(nat,cam_[1])};
        for(int i=0;i<2;++i)if(still[i])c0(nat,n::CamActive,{cam_[i],0});
        c0(nat,n::RenderScriptCams,{0,0,RenderEaseMs,1,0});   // always: script cams are never left rendering by the menu
        for(int i=0;i<2;++i)if(still[i])c0(nat,n::DestroyCam,{cam_[i],0});
        cam_[0]=cam_[1]=0;
        return true;
    }
private:
    static bool sane(float dz) noexcept {return std::isfinite(dz) && dz>-1.5f && dz<2.0f;}
    static bool exists(Natives& nat,std::uint32_t cam) noexcept {return c1(nat,n::DoesCamExist,{cam})!=0;}
    static void destroyIf(Natives& nat,std::uint32_t cam) noexcept {if(exists(nat,cam))c0(nat,n::DestroyCam,{cam,0});}
    void aim(Natives& nat,std::uint32_t cam,Framing f) noexcept {
        const int i=int(f);const V3& a=from_[i];const V3& b=at_[i];
        c0(nat,n::CamCoord,{cam,bits(a.x),bits(a.y),bits(a.z)});
        c0(nat,n::PointAtCoord,{cam,bits(b.x),bits(b.y),bits(b.z)});
        c0(nat,n::CamFov,{cam,bits(Shots[i].fov)});
    }
    V3 from_[3]{},at_[3]{};bool planned_=false,on_=false;std::uint32_t cam_[2]{};int active_=0;Framing framing_=Framing::Full;
};

// ---------------------------------------------------------------------------------------------------- sounds (OBSERVED names)
enum class Sound:std::uint8_t {NavUpDown,NavLeftRight,Select,Back,Error};
inline void play(Natives& nat,Sound s) noexcept {
    static constexpr const char* names[]{"NAV_UP_DOWN","NAV_LEFT_RIGHT","SELECT","BACK","ERROR"};
    const char* set=s==Sound::Error?"HUD_FRONTEND_CLOTHESSHOP_SOUNDSET":"HUD_FRONTEND_DEFAULT_SOUNDSET";
    const std::uint32_t a=nat.text(names[int(s)]),b=nat.text(set);
    if(a && b)c0(nat,n::PlaySound,{0xFFFFFFFFu,a,b});
}

// ---------------------------------------------------------------------------------------------------- the controller
enum class Phase:std::uint8_t {Closed,Applying,Open,Model,Closing};
class CreatorMenu {
public:
    using LogFn=void(*)(const char*);
    using HasFn=bool(*)(std::uint32_t hash);   // the glue: is this native registered (null: assume so)
    void setLog(LogFn f) noexcept {log_=f;drawer_.setLog(f);}
    void setNativeCheck(HasFn f) noexcept {has_=f;drawer_.setNativeCheck(f);}   // (the drawer checks its own natives the same way)
    bool isOpen() const noexcept {return phase_!=Phase::Closed;}
    // v975 (G3): the open state for other threads (the app reads trainer state bit 22 on its own thread): set with every phase change
    bool openForApp() const noexcept {return openFlag_.load(std::memory_order_acquire);}
    Phase phase() const noexcept {return phase_;}
    const Menu& view() const noexcept {return view_;}
    const MenuCamera& camera() const noexcept {return cam_;}
    const Lock& lock() const noexcept {return lock_;}

    // 17 4: refusals, then the lock and the open apply (StartEditing; no camera yet). 0 running, 2 refused.
    int begin(Natives& nat,Host& host,std::uint32_t player,std::uint32_t ped,std::uint32_t frame,char* d,int cap) noexcept {
        frame_=frame;
        if(phase_!=Phase::Closed)return say(d,cap,2,"menu-already-open");
        if(host.doorChooserOpen())return say(d,cap,2,"door-chooser-open");   // v975 (G2)
        if(has_)for(const auto& x:Used)if(!has_(x.hash))return say(d,cap,2,"missing-native %s",x.name);
        const std::uint32_t model=host.onlineModel();
        if(!cr::isFreemode(model))return say(d,cap,2,"no-online-character");
        if(host.modelBusy())return say(d,cap,2,"character-busy");
        if(host.looks().busy())return say(d,cap,2,"creator-busy");
        if(!ped)return say(d,cap,2,"no-ped");
        if(c1(nat,n::InVehicle,{ped,0}))return say(d,cap,2,"in-vehicle");
        if(c1(nat,n::MissionFlag,{}))return say(d,cap,2,"on-a-mission");
        if(c1(nat,n::Ragdoll,{ped}) || c1(nat,n::Falling,{ped}))return say(d,cap,2,"not-standing");
        if(c1(nat,n::Swimming,{ped}))return say(d,cap,2,"swimming");
        if(host.editSessionLive())return say(d,cap,2,"edit-session-open");   // 17 1 / 17 2 end it first (nothing replaced silently)
        // a 17 3 session whose face camera already ended (gate, lost camera): its edits are replaced by the record, said in the log
        if(host.looks().editing())logf("RENDER_ONLINE menu takeover edit-session (unsaved 17 3 edits replaced by the record)");
        host.beforeOpen(nat);
        player_=player;ped_=ped;model_=model;openModel_=model;
        lock_.lock(nat,player,ped,facingHeading(nat,ped));
        const cr::Outcome o=host.looks().begin(nat,cr::CharacterLookAction,cr::StartEditing,ped,model,frame);
        if(o.kind==cr::Outcome::Fail){lock_.unlock(nat);return say(d,cap,2,"start-editing %s",o.detail);}
        resetSession();setPhase(Phase::Applying);
        logf("RENDER_ONLINE menu opening ped=%08x model=%08x heading=%.0f",ped,model,double(lock_.heading()));
        return 0;
    }
    // Once per game frame while the job runs (the trainer's safe point, gate open).
    int step(Natives& nat,Host& host,std::uint32_t player,std::uint32_t ped,std::uint32_t frame,char* d,int cap) noexcept {
        frame_=frame;
        switch(phase_) {
        case Phase::Closed: return say(d,cap,2,"menu-closed");
        case Phase::Applying: return stepApplying(nat,host,d,cap);
        case Phase::Open: return stepOpen(nat,host,ped,d,cap);
        case Phase::Model: return stepModel(nat,host,player,d,cap);
        case Phase::Closing: return stepClosing(nat,host,d,cap);
        }
        return say(d,cap,2,"bad-phase");
    }
    // The gate closed (pause, cutscene, death, fade, switch, trainer off) or Sandbox ended: everything back now, in this tick.
    const char* abort(Natives& nat,Host& host,const char* why) noexcept {
        if(phase_==Phase::Closed)return "idle";
        return shutdown(nat,host,why?why:"abort");
    }

private:
    // ---- phases
    int stepApplying(Natives& nat,Host& host,char* d,int cap) noexcept {
        c0(nat,n::HideHud,{});
        (void)drawer_.load(nat,header());   // the banner / CommonMenu / buttons stream in while the look applies (nothing drawn)
        const cr::Outcome o=host.looks().step(nat,frame_);
        if(o.kind==cr::Outcome::Running)return 0;
        if(o.kind==cr::Outcome::Fail)return forced(nat,host,"apply-failed",d,cap);
        openLook_=host.looks().edits();
        // Applier ends a wait of BlendWaitFramesMax frames with Ok "blend=timeout": the blend / streaming may still be in flight, the C2
        // shape (review MINOR 3). Then this session opens with NO camera (the menu still works) rather than a close camera on top of it.
        camAllowed_=std::strstr(o.detail,"blend=timeout")==nullptr;
        cam_.plan(nat,ped_);
        startCamera(nat);
        setPhase(Phase::Open);primed_=false;dirty_=true;
        logf("RENDER_ONLINE menu open ped=%08x camera=%s",ped_,cam_.on()?"on":"off");
        return 0;
    }
    int stepOpen(Natives& nat,Host& host,std::uint32_t ped,char* d,int cap) noexcept {
        if(ped!=ped_)return forced(nat,host,"ped-changed",d,cap);
        if(host.onlineModel()!=model_)return forced(nat,host,"model-changed",d,cap);
        if(cam_.on() && !cam_.alive(nat)) {   // the engine took the camera (HYPOTHESIS: the hosting script ended): make it again
            cam_.release(nat);
            const bool again=rebuilds_<CamRebuildMax && cam_.begin(nat,wanted());
            ++rebuilds_;
            logf("RENDER_ONLINE menu camera=%s reason=lost",again?"rebuilt":"off");
        }
        c0(nat,n::HideHud,{});
        const Raw r=readRaw(nat);
        if(!primed_){input_.prime(r);primed_=true;lastNow_=r.now;draw(nat,host);return 0;}
        const Events e=input_.update(r);
        if(e.turn!=0.0f) {
            std::uint32_t dt=r.now-lastNow_;if(dt>TurnStepMaxMs)dt=TurnStepMaxMs;
            lock_.turn(nat,float(e.turn)*TurnDegPerSecond*float(dt)/1000.0f);
        }
        lastNow_=r.now;
        handle(nat,host,e);
        if(phase_==Phase::Open){cam_.moveTo(nat,wanted());draw(nat,host);return 0;}
        if(phase_==Phase::Closed)return say(d,cap,closeKind_,"%s",closeDetail_);
        return 0;
    }
    int stepModel(Natives& nat,Host& host,std::uint32_t player,char* d,int cap) noexcept {
        c0(nat,n::HideHud,{});
        if(!exiting_)(void)drawer_.load(nat,header());   // (leaving: the drawer is closed and stays closed)
        char md[64]{};
        const int k=host.stepModel(nat,player,frame_,md,sizeof md);
        if(k==0)return 0;
        if(k==2){logf("RENDER_ONLINE menu gender fail %s",md);return forced(nat,host,exiting_?"gender-back-failed":"gender-failed",d,cap);}
        return exiting_?afterGenderBack(nat,host,d,cap):afterModel(nat,host,d,cap);
    }
    // Exit Without Saving after a gender change: the model at open is back; now the look at open on it (DiscardEdits: the record when
    // it is for this model, else the model's default = what StartEditing showed at open), with no camera, then the unlock (Closing).
    int afterGenderBack(Natives& nat,Host& host,char* d,int cap) noexcept {
        exiting_=false;
        const std::uint32_t ped=c1(nat,n::PlayerPedId,{});
        const std::uint32_t model=host.onlineModel();
        if(!ped || model!=openModel_)return forced(nat,host,"gender-back-no-ped",d,cap);
        ped_=ped;model_=model;
        lock_.lock(nat,player_,ped,lock_.heading());   // still while the look applies
        const cr::Outcome o=host.looks().begin(nat,cr::CharacterLookAction,cr::DiscardEdits,ped,model,frame_);
        logf("RENDER_ONLINE menu gender back ok ped=%08x model=%08x",ped,model);
        if(o.kind==cr::Outcome::Running){setPhase(Phase::Closing);return 0;}
        lock_.unlock(nat);setPhase(Phase::Closed);
        logf("RENDER_ONLINE menu close reason=%s apply=fail %s",closeWhat_,o.detail);
        return say(d,cap,2,"menu %s %s",closeWhat_,o.detail);
    }
    int afterModel(Natives& nat,Host& host,char* d,int cap) noexcept {
        const std::uint32_t ped=c1(nat,n::PlayerPedId,{});
        const std::uint32_t model=host.onlineModel();
        if(!ped || !cr::isFreemode(model))return forced(nat,host,"gender-no-ped",d,cap);
        ped_=ped;model_=model;
        lock_.lock(nat,player_,ped,lock_.heading());
        const cr::Outcome o=host.looks().begin(nat,cr::CharacterLookAction,cr::StartEditing,ped,model,frame_);
        if(o.kind==cr::Outcome::Fail)return forced(nat,host,"start-editing-failed",d,cap);
        setPhase(Phase::Applying);level_=0;dirty_=true;
        logf("RENDER_ONLINE menu gender ok ped=%08x model=%08x",ped,model);
        return 0;
    }
    int stepClosing(Natives& nat,Host& host,char* d,int cap) noexcept {
        const cr::Outcome o=host.looks().step(nat,frame_);
        if(o.kind==cr::Outcome::Running)return 0;
        lock_.unlock(nat);
        setPhase(Phase::Closed);
        logf("RENDER_ONLINE menu close reason=%s apply=%s %s",closeWhat_,o.kind==cr::Outcome::Ok?"ok":"fail",o.detail);
        return say(d,cap,o.kind==cr::Outcome::Ok?1:2,"menu %s %s",closeWhat_,o.detail);
    }

    // ---- input
    Raw readRaw(Natives& nat) noexcept {
        Raw r;
        for(int i=0;i<4;++i){r.just[i]=c1(nat,n::JustPressed,{Pad,DirControl[i]})!=0;r.held[i]=c1(nat,n::Pressed,{Pad,DirControl[i]})!=0;}
        r.accept=c1(nat,n::JustPressed,{Pad,CtlAccept})!=0;r.cancel=c1(nat,n::JustPressed,{Pad,CtlCancel})!=0;
        r.lt=c1(nat,n::Pressed,{Pad,CtlLT})!=0;r.rt=c1(nat,n::Pressed,{Pad,CtlRT})!=0;
        r.sx=asFloat(c1(nat,n::ControlNormal,{Pad,AxisX}));r.sy=asFloat(c1(nat,n::ControlNormal,{Pad,AxisY}));
        r.rx=asFloat(c1(nat,n::ControlNormal,{Pad,AxisRX}));   // v975
        r.now=c1(nat,n::GameTimer,{});
        return r;
    }
    void handle(Natives& nat,Host& host,const Events& e) noexcept {
        if(level_==0) {
            if(e.cancel) {
                if(dirty(host) && cursor_!=CatExit){cursor_=CatExit;help_=nullptr;dirty_=true;play(nat,Sound::Back);return;}
                play(nat,Sound::Back);startClose(nat,host,false);return;
            }
            if(e.accept){activate(nat,host);return;}
            if(e.dir[DirUp] || e.dir[DirDown]) {
                cursor_=stepWrap(cursor_,0,CatCount,e.dir[DirUp]?-1:1);help_=nullptr;dirty_=true;play(nat,Sound::NavUpDown);
            }
            if((e.dir[DirLeft] || e.dir[DirRight]) && cursor_==CatGender){play(nat,Sound::NavLeftRight);startGender(nat,host);}
            return;
        }
        if(e.cancel || e.accept){level_=0;help_=nullptr;dirty_=true;play(nat,e.cancel?Sound::Back:Sound::Select);return;}
        if(e.dir[DirUp] || e.dir[DirDown])moveRow(nat,host,e.dir[DirUp]?-1:1);
        if(e.dir[DirLeft] || e.dir[DirRight])change(nat,host,row_,e.dir[DirLeft]?-1:1);
    }
    void activate(Natives& nat,Host& host) noexcept {
        const CatDef& c=Cats[cursor_];
        switch(c.kind) {
        case Kind::Gender: play(nat,Sound::Select);startGender(nat,host);return;
        case Kind::Save: play(nat,Sound::Select);startClose(nat,host,true);return;
        case Kind::Exit: play(nat,Sound::Select);startClose(nat,host,false);return;
        default: break;
        }
        cat_=cursor_;level_=1;help_=nullptr;recount(nat,host);row_=0;
        if(!rowState(host,0).enabled && rows(c.kind)>1 && rowState(host,1).enabled)row_=1;
        dirty_=true;play(nat,Sound::Select);
    }
    void moveRow(Natives& nat,Host& host,int delta) noexcept {
        const int k=rows(Cats[cat_].kind);
        if(k<2)return;
        const int next=stepWrap(row_,0,k,delta);
        if(next==row_ || !rowState(host,next).enabled)return;
        row_=next;dirty_=true;play(nat,Sound::NavUpDown);
    }

    // ---- rows and live edits
    struct RowState {const char* label="";int value=0,lo=0,count=0;bool enabled=false,percent=false;};
    static int rows(Kind k) noexcept {return k==Kind::Part || k==Kind::Overlay || k==Kind::Prop?2:k==Kind::Hair || k==Kind::HairColour?1:0;}
    RowState rowState(Host& host,int r) const noexcept {
        const CatDef& c=Cats[cat_];const cr::Look& l=host.looks().edits();RowState s;
        switch(c.kind) {
        case Kind::Hair: s={"Style",l.part[2].drawable,0,styles_};break;
        case Kind::HairColour: s={"Colour",l.part[2].texture,0,variants_};break;
        case Kind::Part: {const cr::Part& p=l.part[std::size_t(c.index)];
            s=r==0?RowState{"Style",p.drawable,0,styles_}:RowState{"Variant",p.texture,0,variants_};break;}
        case Kind::Overlay: {const cr::Overlay& o=l.overlay[std::size_t(c.index)];
            if(r==0)s={"Style",o.opacity?o.index:-1,-1,styles_<0?-1:styles_+1};
            else {int v=(o.opacity+50)/100;if(v<1)v=1;if(v>OpacitySteps)v=OpacitySteps;s={"Opacity",v,1,o.opacity?OpacitySteps:0};s.percent=true;}
            break;}
        case Kind::Prop: {const cr::Prop& p=l.prop[std::size_t(c.index)];
            s=r==0?RowState{"Style",p.drawable,-1,styles_<0?-1:styles_+1}:RowState{"Variant",p.texture,0,p.drawable<0?0:variants_};break;}
        default: break;
        }
        s.enabled=s.count>1;
        return s;
    }
    // The game's own counts for the open page: on entering it and after a style change (never per frame).
    void recount(Natives& nat,Host& host) noexcept {
        const CatDef& c=Cats[cat_];const cr::Look& l=host.looks().edits();
        styles_=variants_=-1;
        switch(c.kind) {
        case Kind::Hair: case Kind::HairColour: case Kind::Part:
            styles_=count(nat,n::Drawables,{ped_,cr::u(c.index)});
            variants_=count(nat,n::Textures,{ped_,cr::u(c.index),cr::u(l.part[std::size_t(c.index)].drawable)});break;
        case Kind::Overlay: styles_=count(nat,n::OverlayNum,{cr::u(c.index)});break;
        case Kind::Prop: {const std::uint32_t anchor=cr::PropAnchor[std::size_t(c.index)];
            styles_=count(nat,n::PropDrawables,{ped_,anchor});
            const int dr=l.prop[std::size_t(c.index)].drawable;
            variants_=dr<0?0:count(nat,n::PropTextures,{ped_,anchor,cr::u(dr)});break;}
        default: break;
        }
    }
    bool edit(Natives& nat,Host& host,int field,int value) noexcept {
        const cr::Outcome o=host.looks().begin(nat,cr::CharacterEditAction,cr::encodeEdit(field,value),ped_,model_,frame_);
        if(o.kind!=cr::Outcome::Ok){logf("RENDER_ONLINE menu edit field=%d value=%d fail %s",field,value,o.detail);return false;}
        ++edits_;
        return true;
    }
    void change(Natives& nat,Host& host,int r,int delta) noexcept {
        const RowState s=rowState(host,r);
        if(!s.enabled){play(nat,Sound::Error);return;}
        const int v=stepWrap(s.value,s.lo,s.count,delta);
        const CatDef& c=Cats[cat_];const cr::Look& l=host.looks().edits();
        bool ok=false;
        switch(c.kind) {
        case Kind::Hair: {   // the colour is kept when the new style has it (a style change resets the texture: editLook)
            const int colour=l.part[2].texture;
            ok=edit(nat,host,cr::PartBase+3*2,v);
            if(ok){recount(nat,host);if(colour>0 && colour<variants_)(void)edit(nat,host,cr::PartBase+3*2+1,colour);}
            break;}
        case Kind::HairColour: ok=edit(nat,host,cr::PartBase+3*2+1,v);break;
        case Kind::Part:
            ok=edit(nat,host,cr::PartBase+3*c.index+r,v);
            if(ok && r==0)recount(nat,host);
            break;
        case Kind::Overlay: {
            const int slot=c.index,fIndex=cr::OverlayBase+2*slot,fOpacity=fIndex+1;const int was=l.overlay[std::size_t(slot)].opacity;
            if(r==1){ok=edit(nat,host,fOpacity,v*(cr::Permille/OpacitySteps));if(ok)opacity_[slot]=v*(cr::Permille/OpacitySteps);}
            else if(v<0){if(was)opacity_[slot]=was;ok=edit(nat,host,fOpacity,0);}
            else {ok=edit(nat,host,fIndex,v);if(ok && !was)ok=edit(nat,host,fOpacity,opacity_[slot]);}
            break;}
        case Kind::Prop:
            ok=edit(nat,host,cr::PropBase+2*c.index+r,v);
            if(ok && r==0)recount(nat,host);
            break;
        default: break;
        }
        dirty_=true;
        play(nat,ok?Sound::NavLeftRight:Sound::Error);
    }

    // ---- gender (action 15's model change, inside the session)
    void startGender(Natives& nat,Host& host) noexcept {
        const bool female=model_!=cr::FemaleModel;
        cam_.release(nat);          // nothing renders a script camera through the model change and the next open apply
        host.looks().abort();       // (no apply on the ped that is about to be replaced; the next StartEditing resets the buffer)
        lock_.unfreeze(nat);
        char md[64]{};
        const int k=host.changeModel(nat,ped_,female,frame_,md,sizeof md);
        logf("RENDER_ONLINE menu gender to=%s %s %s",female?"female":"male",k==0?"started":k==1?"ok":"refused",md);
        if(k==0){setPhase(Phase::Model);dirty_=true;return;}
        if(k==1){char d[64];if(afterModel(nat,host,d,sizeof d)!=0 && phase_==Phase::Closed){closeKind_=2;std::snprintf(closeDetail_,sizeof closeDetail_,"%s",d);}return;}
        // refused (on a mission, autosave hold, ...): back as it was
        lock_.lock(nat,player_,ped_,lock_.heading());
        startCamera(nat);
        help_="Gender cannot be changed now.";dirty_=true;play(nat,Sound::Error);
    }
    // ---- leaving: the camera first, then the full apply (the proven 17 1 / 17 2 order), the lock after it finished
    void startClose(Natives& nat,Host& host,bool save) noexcept {
        cam_.release(nat);
        drawer_.close(nat,save?"save":"exit");   // the menu leaves the screen with the camera (a failed save loads it again)
        if(!save && model_!=openModel_){startGenderBack(nat,host);return;}   // review MAJOR 1: Exit undoes a gender change too
        const cr::Outcome o=host.looks().begin(nat,cr::CharacterLookAction,save?cr::SaveEdits:cr::DiscardEdits,ped_,model_,frame_);
        closeWhat_=save?"saved":"discarded";
        if(o.kind==cr::Outcome::Running){setPhase(Phase::Closing);return;}
        if(save && host.looks().editing()) {   // nothing written, nothing applied: stay in the menu and say so
            logf("RENDER_ONLINE menu save fail %s",o.detail);
            startCamera(nat);
            help_="The look could not be saved.";dirty_=true;play(nat,Sound::Error);return;
        }
        lock_.unlock(nat);setPhase(Phase::Closed);closeKind_=2;
        std::snprintf(closeDetail_,sizeof closeDetail_,"menu %s %s",closeWhat_,o.detail);
        logf("RENDER_ONLINE menu close reason=%s apply=fail %s",closeWhat_,o.detail);
    }
    // Exit Without Saving with the other gender on: camera and drawer are already off; action 15's change back to the model at open
    // (Phase::Model with exiting_), then afterGenderBack. Refused (mission, autosave hold, ...): the menu stays open and says so, like a
    // failed save - leaving would keep the other gender while the record holds the old one.
    void startGenderBack(Natives& nat,Host& host) noexcept {
        closeWhat_="discarded";
        host.looks().abort();       // (nothing is applying in Open; no apply on the ped that is about to be replaced)
        lock_.unfreeze(nat);
        char md[64]{};
        const int k=host.changeModel(nat,ped_,openModel_==cr::FemaleModel,frame_,md,sizeof md);
        logf("RENDER_ONLINE menu gender back to=%s %s %s",openModel_==cr::FemaleModel?"female":"male",k==0?"started":k==1?"ok":"refused",md);
        if(k==0){exiting_=true;setPhase(Phase::Model);dirty_=true;return;}
        if(k==1) {
            exiting_=true;char d[64];
            if(afterGenderBack(nat,host,d,sizeof d)!=0 && phase_==Phase::Closed){closeKind_=2;std::snprintf(closeDetail_,sizeof closeDetail_,"%s",d);}
            return;
        }
        lock_.lock(nat,player_,ped_,lock_.heading());
        startCamera(nat);
        help_="The gender cannot be changed back now.";dirty_=true;play(nat,Sound::Error);
    }
    // A forced close: the camera off, a model change dropped, the look at open back, the lock released, all in this tick (no wait).
    // The one routine for the gate, Sandbox end and every unexpected change. ONE full apply at most (review MINOR 6):
    //  - a saved record for the model on now: the edit session is ended WITHOUT an apply (DiscardEdits with ped 0: character_creator.h
    //    clears editing_, then Applier::begin answers "no-ped" before any native call), and tickIdle (online_world.h reconcile, paused
    //    while the menu is open, runs every Sandbox frame) puts the record back on the player's ped as it is then, with
    //    FINALIZE_HEAD_BLEND, on the next frame;
    //  - no record: DiscardEdits applies the model's default look now (what StartEditing showed at open); its wait is dropped (no
    //    finalize: tickIdle has nothing to put back), as v959's own abort of a creator job does.
    // A gender changed in the menu cannot be changed back here (that takes several frames of action 15): the result says gender=kept.
    const char* shutdown(Natives& nat,Host& host,const char* why) noexcept {
        cam_.release(nat);
        drawer_.close(nat,why);
        if(phase_==Phase::Model)host.abortModel(nat);
        exiting_=false;
        cr::Creator& c=host.looks();
        const char* left="closed";
        const std::uint32_t model=host.onlineModel();
        const std::uint32_t now=c1(nat,n::PlayerPedId,{});
        if(c.editing() && cr::isFreemode(model)) {
            if(c.hasSaved() && c.saved().model==model) {
                (void)c.begin(nat,cr::CharacterLookAction,cr::DiscardEdits,0,model,frame_);   // ends the session, applies nothing
                c.noteRespawn(now?now:ped_);
                left="revert-queued";
            } else {
                const cr::Outcome o=c.begin(nat,cr::CharacterLookAction,cr::DiscardEdits,ped_,model,frame_);
                c.abort();
                left=o.kind==cr::Outcome::Fail?"revert-failed":"reverted";
            }
        } else {
            const bool closing=phase_==Phase::Closing;
            c.abort();
            if(closing){c.noteRespawn(now?now:ped_);left="saved-or-discarded";}   // the record (if any) back with its finalize
        }
        lock_.unlock(nat);
        setPhase(Phase::Closed);
        const bool kept=cr::isFreemode(model) && model!=openModel_;
        std::snprintf(left_,sizeof left_,"%s%s",left,kept?" gender=kept":"");
        logf("RENDER_ONLINE menu close reason=%s left=%s",why,left_);
        return left_;
    }
    int forced(Natives& nat,Host& host,const char* why,char* d,int cap) noexcept {
        const char* left=shutdown(nat,host,why);
        return say(d,cap,2,"menu closed reason=%s left=%s",why,left);
    }

    // ---- view
    Framing wanted() const noexcept {return Cats[level_==1?cat_:cursor_].framing;}
    // Every camera start of a session goes through here: none after an open apply that timed out (camAllowed_, review MINOR 3).
    void startCamera(Natives& nat) noexcept {
        if(!camAllowed_){logf("RENDER_ONLINE menu camera=off reason=blend-timeout");return;}
        if(!cam_.begin(nat,wanted()))logf("RENDER_ONLINE menu camera=off reason=create-failed");
    }
    bool dirty(Host& host) const noexcept {return model_!=openModel_ || !(host.looks().edits()==openLook_);}
    // The face categories wear the barber's banner, the rest the clothes shop's (the sibling lane's choice of the two shops' headers).
    const char* header() const noexcept {
        const int c=level_==1?cat_:cursor_;
        return c>=CatHair && c<=CatMakeup?menudraw::HeaderBarber:menudraw::HeaderClothes;
    }
    void draw(Natives& nat,Host& host) noexcept {
        if(dirty_){rebuild(host);dirty_=false;}
        if(drawer_.load(nat,page_.header))drawer_.draw(nat,page_);
    }
    void rebuild(Host& host) noexcept {
        Menu& m=view_;const std::uint32_t rev=m.revision+1;
        m=Menu{};m.revision=rev;m.title="Character";m.busy=phase_!=Phase::Open;
        if(level_==0) {
            m.subtitle="CHARACTER";m.count=CatCount;m.selected=cursor_;
            for(int i=0;i<CatCount;++i) {
                m.rows[i].label=Cats[i].label;
                if(Cats[i].kind==Kind::Gender){std::snprintf(m.rows[i].value,ValueChars,"%s",model_==cr::FemaleModel?"Female":"Male");m.rows[i].arrows=true;}
            }
            m.help=help_?help_:cursor_==CatGender || cursor_>=CatSave?Cats[cursor_].help:TopHelp;
        } else {
            const CatDef& c=Cats[cat_];m.subtitle=c.header;m.count=rows(c.kind);m.selected=row_;m.help=help_?help_:c.help;
            for(int r=0;r<m.count;++r) {
                const RowState s=rowState(host,r);RowView& v=m.rows[r];
                v.label=s.label;v.enabled=s.enabled;v.arrows=s.enabled;
                if(s.count<=0)std::snprintf(v.value,ValueChars,"%s","-");
                else if(s.value<0)std::snprintf(v.value,ValueChars,"%s","None");
                else if(s.percent)std::snprintf(v.value,ValueChars,"%d%%",s.value*(100/OpacitySteps));
                else std::snprintf(v.value,ValueChars,"%d/%d",s.value+1,s.lo<0?s.count-1:s.count);
            }
        }
        std::snprintf(m.counter,sizeof m.counter,"%d/%d",m.selected+1,m.count);
        // the drawer's page: the same rows (pointers into view_, valid until the next rebuild)
        for(int r=0;r<m.count;++r)rows_[r]=menudraw::Row{m.rows[r].label,m.rows[r].value[0]?m.rows[r].value:nullptr,m.rows[r].arrows,m.rows[r].enabled};
        page_=menudraw::Page{};page_.header=header();page_.title=m.subtitle;page_.rows=rows_;page_.rowCount=m.count;page_.selected=m.selected;
        page_.help=m.help;page_.buttons=Buttons;page_.buttonCount=int(std::size(Buttons));   // v975: the shops' instructional buttons
    }

    // ---- bookkeeping
    void resetSession() noexcept {
        level_=0;cursor_=CatHair;cat_=CatHair;row_=0;styles_=variants_=-1;rebuilds_=0;edits_=0;help_=nullptr;primed_=false;dirty_=true;
        exiting_=false;camAllowed_=true;
        for(int& o:opacity_)o=cr::Permille;
    }
    template<class... A> int say(char* d,int cap,int k,const char* f,A... a) noexcept {
        if(d && cap>0){if constexpr(sizeof...(a)==0)std::snprintf(d,std::size_t(cap),"%s",f);else std::snprintf(d,std::size_t(cap),f,a...);}
        return k;
    }
    template<class... A> void logf(const char* f,A... a) const noexcept {
        if(!log_)return;char line[160];
        if constexpr(sizeof...(a)==0)std::snprintf(line,sizeof line,"%s",f);else std::snprintf(line,sizeof line,f,a...);
        log_(line);
    }

    LogFn log_=nullptr;HasFn has_=nullptr;
    void setPhase(Phase p) noexcept {phase_=p;openFlag_.store(p!=Phase::Closed,std::memory_order_release);}   // v975 (G3)
    std::atomic<bool> openFlag_{false};   // v975 (G3)
    Phase phase_=Phase::Closed;Lock lock_;MenuCamera cam_;InputReader input_;Menu view_;cr::Look openLook_{};
    menudraw::Renderer drawer_;menudraw::Row rows_[MaxRows]{};menudraw::Page page_{};
    std::uint32_t player_=0,ped_=0,model_=0,openModel_=0,frame_=0,lastNow_=0;
    int level_=0,cursor_=CatHair,cat_=CatHair,row_=0,styles_=-1,variants_=-1,rebuilds_=0,edits_=0,closeKind_=1;
    int opacity_[cr::OverlaySlots]{};
    const char* help_=nullptr;const char* closeWhat_="";char closeDetail_[64]{};char left_[48]{};
    bool primed_=false,dirty_=true,exiting_=false,camAllowed_=true;
};

// The menu never calls a network, stats, cloud or save native.
constexpr bool forbiddenName(const char* s) noexcept {
    const auto starts=[](const char* a,const char* p){while(*p){if(*a!=*p)return false;++a;++p;}return true;};
    return starts(s,"NETWORK_") || starts(s,"STAT_") || starts(s,"NET_") || starts(s,"UGC_") || starts(s,"CLOUD_") || starts(s,"DATAFILE_")
        || starts(s,"SC_") || starts(s,"PLAYSTATS_") || starts(s,"SET_SAVE") || starts(s,"DO_AUTO_SAVE");
}
constexpr bool noForbidden() noexcept {for(const auto& x:Used)if(forbiddenName(x.name))return false;return true;}
static_assert(noForbidden(),"the creator menu must not call network / stats / save natives");

} // namespace recomp::creatormenu
