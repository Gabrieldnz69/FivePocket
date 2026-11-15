#pragma once
// online_mode_glue.h (v1056, trainer-mods): online_mode.h's PedOps on the game: natives through the trainer's Natives, the swap and the saved
// look through the trainer's own request queue (actions 15 and 17 0, the same jobs the test one-shots use), the saved sex from character.json
// (character_store.h FileStore of the mode's generation; parse() as the creator reads it).
#include "online_mode.h"
#include "online_route_switch.h"   // v1105: fmRoute()
#include "online_corona_probe.h"   // v1117: fmmc corona state (read only)
#include "online_arrival.h"       // v1105: introRecord() parses arrival.json
#include "local_session_flag.h"   // v1067
#include "trainer.h"
#include "character_creator.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <unistd.h>

namespace recomp::online_mode {
namespace gn {
using recomp::trainer::joaat;
constexpr std::uint32_t PlayerPed=joaat("PLAYER_PED_ID"),Model=joaat("GET_ENTITY_MODEL"),BlendDone=joaat("HAS_PED_HEAD_BLEND_FINISHED"),
    InVehicle=joaat("IS_PED_IN_ANY_VEHICLE"),ClearTasksNow=joaat("CLEAR_PED_TASKS_IMMEDIATELY"),Gip=joaat("NETWORK_IS_GAME_IN_PROGRESS"),
    PlayerId=joaat("PLAYER_ID"),PauseMenu=joaat("IS_PAUSE_MENU_ACTIVE"),
    FreeStacks=joaat("GET_NUMBER_OF_FREE_STACKS_OF_THIS_SIZE"),Threads=joaat("GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH"),
    SwitchOn=joaat("IS_PLAYER_SWITCH_IN_PROGRESS"),SwitchState=joaat("GET_PLAYER_SWITCH_STATE"),SwitchType=joaat("GET_PLAYER_SWITCH_TYPE"),   // v1077
    FirstPart=joaat("SWITCH_TO_MULTI_FIRSTPART"),SecondPart=joaat("SWITCH_TO_MULTI_SECONDPART"),FirstDone=joaat("IS_SWITCH_TO_MULTI_FIRSTPART_FINISHED"),
    StopSwitch=joaat("STOP_PLAYER_SWITCH"),SetInvincible=joaat("SET_PLAYER_INVINCIBLE"),SwitchMpFlag=0x932fdb81u,FadeOut=joaat("DO_SCREEN_FADE_OUT"),SetFrontendActive=joaat("SET_FRONTEND_ACTIVE");
// v1104: the player restore after the sweep (maintransition 0x43111(1)'s calls; argument forms as creator_route.h 699-700 / online_intro.h)
constexpr std::uint32_t IsVisible=joaat("IS_ENTITY_VISIBLE"),ControlOn=joaat("IS_PLAYER_CONTROL_ON"),GetInvincible=joaat("GET_PLAYER_INVINCIBLE"),
    Coords=joaat("GET_ENTITY_COORDS"),SetVisible=joaat("SET_ENTITY_VISIBLE"),Freeze=joaat("FREEZE_ENTITY_POSITION"),SetCollision=joaat("SET_ENTITY_COLLISION"),
    SetControl=joaat("SET_PLAYER_CONTROL"),DisplayHud=joaat("DISPLAY_HUD"),DisplayRadar=joaat("DISPLAY_RADAR");
constexpr std::uint32_t StreamingDone=joaat("HAVE_ALL_STREAMING_REQUESTS_COMPLETED");   // v1170: blendDetail (character_creator.h uses the same native)
constexpr std::uint32_t DrawableVar=joaat("GET_PED_DRAWABLE_VARIATION");   // v1105: the hair read-back (ped, comp) -> drawable   // v1082 CloudsDown (maintransition 0x666 / 0x44d11)   // v1066
}
inline constexpr std::uint32_t UsedNatives[]{gn::PlayerPed,gn::Model,gn::BlendDone,gn::InVehicle,gn::ClearTasksNow,gn::Gip,gn::PlayerId,gn::PauseMenu,gn::FreeStacks,gn::Threads,gn::SwitchOn,gn::SwitchState,gn::SwitchType,
    gn::FirstPart,gn::SecondPart,gn::FirstDone,gn::StopSwitch,gn::SetInvincible,gn::SwitchMpFlag,gn::FadeOut,gn::SetFrontendActive,
    gn::IsVisible,gn::ControlOn,gn::GetInvincible,gn::Coords,gn::SetVisible,gn::Freeze,gn::SetCollision,gn::SetControl,gn::DisplayHud,gn::DisplayRadar,gn::DrawableVar,gn::StreamingDone};   // v1104 / v1105
// v1064: the boot card's state word (sub_825B9AA0 acts only while it is 1; lis -31863 -> 0x83895C64): 1 = the card is up (to be confirmed
// on the device: the transition probe logs it beside the shots).
constexpr std::uint32_t CardStateWord=0x83895C64u;
// v1072 (t-onlnoc-20261107-2327 v1070, t-onlnoc71-20261107-2332 v1071): 0x83895C64 stays 1 after the card is gone; the word the shutdown
// tests first, 0x83895C5C (lis -31863; lwz 23644), read 1 -> 3 -> 5 through the boot, 5 on every shot with the card up (80 s) and 0 from the
// line after "release ran the shutdown" (90 s shot: the world, the online character). cardUp() = that word non-zero.
constexpr std::uint32_t CardShownWord=0x83895C5Cu;
// v1064 (lead: let the game's own frontend run; evidence before forcing): the transition probe, Online mode, logged on change. maintransition's
// two state words (freemode-route NOTES 2.6: G1344033 UI / warning states 0-28, G1344034 transition states 1-66), G19455 (its state as
// shop_controller / fm_intro read it), IS_PAUSE_MENU_ACTIVE, the card word, the player's model, gip.
// v1093 (lead 10:28, FREEMODE-GATE-2026-11-08.md): the next gates' globals, memory reads only, logged on change every 15 frames:
// G1332397 (the UGC prefetch done, 0x67082 / 0x6670d), G1332383[slot+1] and G1344312[slot+1] (array words: the size at the base, element i
// at base + 1 + i: [1] = base + 2), G1344068, G19456, G1338478 (MPPLY_LAST_MP_CHAR's copy). G1332481.17 = G1332498, already on the transition line.
class GateProbe {
public:
    template<class N> void tick(N& nat,std::uint32_t frame,void(*log)(const char*)) noexcept {
        if(!log || (started_ && frame-last_<15))return;
        last_=frame;
        const int v[9]{glob(nat,1332397u),glob(nat,1332383u),glob(nat,1332385u),glob(nat,1344068u),glob(nat,1344312u),glob(nat,1344314u),
                       glob(nat,19456u),glob(nat,1338478u),glob(nat,1344034u)};
        if(started_ && !std::memcmp(v,prev_,sizeof v))return;
        started_=true;std::memcpy(prev_,v,sizeof v);
        char l[300];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE gates frame=%u g1332397=%d g1332383n=%d g1332383[1]=%d g1344068=%d g1344312n=%d g1344312[1]=%d g19456=%d g1338478=%d mt1344034=%d",
                                  frame,v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8]);log(l);
    }
private:
    template<class N> static int glob(N& nat,std::uint32_t gl) noexcept {
        const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,base=nat.load(0x83C5CE48u+4u*b),size=nat.load(0x83C5CF50u+4u*b);
        return (base && !(base&3u) && w<size)?int(nat.load(base+4u*w)):-1;
    }
    bool started_=false;std::uint32_t last_=0;int prev_[9]{};
};
// v1096 (lead 11:21, FREEMODE-GATE-2-2026-11-08.md): the offline-UGC and T17 / T18 globals, memory reads only, on change every 15 frames:
// G1572867.f4 (the offline route, set by the game at 0x63275) / .f2, G1344123.f3 (UGC_Global_Registration launched), G1332424, G1344058,
// G1332358, G1344076, G1344090, G1338480, G262144.f72 (T4's timeout), the net object's state word [[0x83B13B04] + 0x10BB0] (CAN_ENTER's
// worker sub_83039500 needs 0) and the gate bytes 0x8381E258 +20 / +22 (the same worker).
class GateProbe2 {
public:
    template<class N> void tick(N& nat,std::uint32_t frame,void(*log)(const char*)) noexcept {
        if(!log || (started_ && frame-last_<15))return;
        last_=frame;
        const std::uint32_t netobj=nat.load(0x83B13B04u),gw=nat.load(0x8381E26Cu);
        const int v[13]{glob(nat,1572871u),glob(nat,1572869u),glob(nat,1344126u),glob(nat,1332424u),glob(nat,1344058u),glob(nat,1332358u),
                        glob(nat,1344076u),glob(nat,1344090u),glob(nat,1338480u),glob(nat,262216u),
                        (netobj>=0x40000000u && !(netobj&3u))?int(nat.load(netobj+0x10BB0u)):-1,int((gw>>24)&0xFFu),int((gw>>8)&0xFFu)};
        if(started_ && !std::memcmp(v,prev_,sizeof v))return;
        started_=true;std::memcpy(prev_,v,sizeof v);
        char l[360];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE gates2 frame=%u g1572867.f4=%d g1572867.f2=%d g1344123.f3=%d g1332424=%d g1344058=%d g1332358=%d g1344076=%d g1344090=%d g1338480=%d g262144.f72=%d netstate=%d e258b20=%d e258b22=%d",
                                  frame,v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12]);log(l);
    }
private:
    template<class N> static int glob(N& nat,std::uint32_t gl) noexcept {
        const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,base=nat.load(0x83C5CE48u+4u*b),size=nat.load(0x83C5CF50u+4u*b);
        return (base && !(base&3u) && w<size)?int(nat.load(base+4u*w)):-1;
    }
    bool started_=false;std::uint32_t last_=0;int prev_[13]{};
};
// v1098 (lead 12:16, FREEMODE-GATE-3 A1): T17's lineup and the T18 inputs, memory reads only. Checked every frame while maintransition's
// transition word is 17 or 18, else every 15 frames; logged on change, at most 300 lines. Fields: G1332392 (the 0x3801 reload state),
// G1338396..401 (G1338395[0].f0..f5: the lineup parents, f2 done, f5 state), G1338473 / 474 (G1338472[0..1], the model type), G1344080,
// G1344098, G1332399 (written at 0x5727), G1355860 (G1355279.f581), G1374087 (rank), T18: G1344053, G1341606, G19455, G1344059, G59842; mt, ui.
class GateProbe3 {
public:
    template<class N> void tick(N& nat,std::uint32_t frame,void(*log)(const char*)) noexcept {
        if(!log || lines_>=300u)return;
        const int mt=glob(nat,1344034u);
        if(started_ && mt!=17 && mt!=18 && frame-last_<15)return;
        last_=frame;
        static constexpr std::uint32_t G[23]{1332392u,1338396u,1338397u,1338398u,1338399u,1338400u,1338401u,1338473u,1338474u,1344080u,1344098u,
                                            1332399u,1355860u,1374087u,1344053u,1341606u,19455u,1344059u,59842u,
                                            115074u,115075u,115154u,115155u};   // v1100: G115067.f7 / .f8 (the freemode models) and G115147.f7 / .f8
        int v[25];for(unsigned i=0;i<23u;++i)v[i]=glob(nat,G[i]);v[23]=mt;v[24]=glob(nat,1344033u);
        if(started_ && !std::memcmp(v,prev_,sizeof v))return;
        started_=true;std::memcpy(prev_,v,sizeof v);++lines_;
        char l[520];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE gates3 frame=%u mt=%d ui=%d g1332392=%d lineup[%d,%d,%d,%d,%d,%d] type[%d,%d] g1344080=%d g1344098=%d g1332399=%d g1355860=%d rank=%d t18[g1344053=%d g1341606=%d g19455=%d g1344059=%d g59842=%d] fm_models[%08x,%08x %08x,%08x]",
                                  frame,v[23],v[24],v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12],v[13],v[14],v[15],v[16],v[17],v[18],
                                  unsigned(v[19]),unsigned(v[20]),unsigned(v[21]),unsigned(v[22]));log(l);
    }
private:
    template<class N> static int glob(N& nat,std::uint32_t gl) noexcept {
        const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,base=nat.load(0x83C5CE48u+4u*b),size=nat.load(0x83C5CF50u+4u*b);
        return (base && !(base&3u) && w<size)?int(nat.load(base+4u*w)):-1;
    }
    bool started_=false;std::uint32_t last_=0,lines_=0;int prev_[25]{};
};
// v1102 (round 5 F, FREEMODE-GATE-5-2026-11-08.md): T21-T26, read only, logged on change every 15 frames: G1332517 (freemode script status 2),
// G1596011, G1335094[0].f0 (= 1335095), G1332502.f6[0] (= 1332509), G1341588.f7 (= 1341595), G1345771 (the helper ped), G1338480, mt, ui; and in
// the session only: GET_NUMBER_OF_FREE_STACKS_OF_THIS_SIZE(15000) and the freemode thread count (the trainer's own native path).
class GateProbe4 {
public:
    template<class N> void tick(N& nat,std::uint32_t frame,void(*log)(const char*)) noexcept {
        if(!log || (started_ && frame-last_<15) || lines_>=300u)return;
        last_=frame;
        const bool natOk=::recomp::local_session::active();
        const int v[11]{glob(nat,1332517u),glob(nat,1596011u),glob(nat,1335095u),glob(nat,1332509u),glob(nat,1341595u),glob(nat,1345771u),glob(nat,1338480u),
                        glob(nat,1344034u),glob(nat,1344033u),natOk?one(nat,gn::FreeStacks,15000u):-2,natOk?one(nat,gn::Threads,recomp::trainer::joaat("freemode")):-2};
        if(started_ && !std::memcmp(v,prev_,sizeof v))return;
        started_=true;std::memcpy(prev_,v,sizeof v);++lines_;
        char l[320];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE gates4 frame=%u mt=%d ui=%d g1332517=%d g1596011=%d g1335095=%d g1332509=%d g1341595=%d g1345771=%d g1338480=%d stacks15000=%d fm_threads=%d",
                                  frame,v[7],v[8],v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[9],v[10]);log(l);
    }
private:
    template<class N> static int glob(N& nat,std::uint32_t gl) noexcept {
        const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,base=nat.load(0x83C5CE48u+4u*b),size=nat.load(0x83C5CF50u+4u*b);
        return (base && !(base&3u) && w<size)?int(nat.load(base+4u*w)):-1;
    }
    template<class N> static int one(N& nat,std::uint32_t h,std::uint32_t a0) noexcept {std::uint32_t out[3]{};const std::uint32_t a[1]{a0};return nat.call(h,a,1u,out)?int(out[0]):-1;}
    bool started_=false;std::uint32_t last_=0,lines_=0;int prev_[11]{};
};
class TransitionProbe {
public:
    void tick(recomp::trainer::Natives& nat,std::uint32_t frame,void(*log)(const char*)) noexcept {
        if(!log || (started_ && frame-last_<15))return;
        last_=frame;
        // v1067 (t-onlctl10 x3, v1064: no MainTransition launch, 7 of 7 before; the probe was v1064's only change running before selector's
        // decision): no native call before the local session is up: memory reads only (the globals, the card words, the game-in-progress byte
        // 0x83B13AEB that NETWORK_IS_GAME_IN_PROGRESS returns); the model / pause menu / stacks / threads natives only once it is (-2 = not read).
        const bool natOk=::recomp::local_session::active();
        const std::uint32_t ped=natOk?one(nat,gn::PlayerPed,{}):0u;
        // v1066 (lead): + the card word the shutdown tests first (0x83895C5C, lis -31863; lwz 23644), selector's maintransition launch
        // conditions G1345768 (selector 0x4303) and the free 8032 stacks (START_NEW_SCRIPT("MainTransition", 8032)), maintransition threads
        const int gipByte=int((nat.load(0x83B13AE8u)>>((3-(0x83B13AEBu&3u))*8))&0xFFu);   // big-endian byte 0x83B13AEB
        int v[27]{glob(nat,1344033u),glob(nat,1344034u),glob(nat,19455u),natOk?int(one(nat,gn::PauseMenu,{})):-2,int(nat.load(CardStateWord)),
                 int(ped && ped!=0xFFFFFFFFu?one(nat,gn::Model,{ped}):0u),gipByte,int(nat.load(0x83895C5Cu)),glob(nat,1345768u),
                 natOk?int(one(nat,gn::FreeStacks,{8032u})):-2,natOk?int(one(nat,gn::Threads,{recomp::trainer::joaat("maintransition")})):-2,
                 glob(nat,14u),glob(nat,13121u),int(nat.load(0x83B13AECu)>>24),   // v1071: selector's window (G14, G13121) and the go-online byte
                 int(nat.load(0x83895C10u)&0xFFu),int(nat.load(0x83895C20u)>>24),
                 // v1077 (CloudsDown design, ONLINE-MODE-SPEC 4.B): maintransition's own sky switch (START_PLAYER_SWITCH via its helper 0x449fa
                 // (0,1,0,1)) runs behind the held card; when it is up and when it ends against the release decides whether the game's
                 // own descent can be shown. Natives only once the session is up (v1067).
                 natOk?int(one(nat,gn::SwitchOn,{})):-2,natOk?int(one(nat,gn::SwitchState,{})):-2,natOk?int(one(nat,gn::SwitchType,{})):-2,
                 // v1080 (H-SEL): G1332498 = the switch-to-multi state word (maintransition 0x51b: 1 then 2; the descent 0x449fa: 4 then 0). selector
                 // 0x41c6 waits after MainTransition while it is 1 or 4 (0x45ed), and only then re-enables the phone (0x45b4: 0x4e27(0)).
                 glob(nat,1332498u),
                 // v1082 (t-onlrev77-20261108-0210: G1332498 stayed 0, maintransition alive in state 4; first visits end it at 66 yet the phone
                 // stayed 1): the phone state G10434 (memory) on change, and who is alive: selector, startup_positioning (natives in the session only)
                 glob(nat,10434u),natOk?int(one(nat,gn::Threads,{recomp::trainer::joaat("selector")})):-2,
                 natOk?int(one(nat,gn::Threads,{recomp::trainer::joaat("startup_positioning")})):-2,   // v1072: the card's choice-frozen bytes 0x83895C13 / 0x83895C20
                 // v1166 PROPOSAL (online-spinner REPORT.md; READ ONLY, memory loads, no native): the engine's busy spinner. The byte BUSYSPINNER_IS_ON
                 // returns (0x83893D34, written only by sub_8252F6E8 at most once per 1000 ms), and the request table sub_82584410 writes
                 // (0x83893B40 + 68*slot: type word, then the resolved text): slot 1 = every END_TEXT_COMMAND_BUSYSPINNER_ON (-1 = off, which is
                 // what BUSYSPINNER_OFF writes), slot 0 for completeness, and slot 1's first 4 text bytes (who asked: "Join" = HUD_JOINING in English).
                 int(nat.load(0x83893D34u)>>24),int(nat.load(0x83893B84u)),int(nat.load(0x83893B40u)),int(nat.load(0x83893B88u))};
        if(started_ && !std::memcmp(v,prev_,sizeof v))return;
        started_=true;std::memcpy(prev_,v,sizeof v);
        char l[460];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE transition frame=%u ui1344033=%d mt1344034=%d g19455=%d pausemenu=%d card=%d model=%08x gip=%d c5c=%d g1345768=%d stacks8032=%d mt_threads=%d g14=%d g13121=%d golive=%d f5c13=%d f5c20=%d switch=%d sw_state=%d sw_type=%d g1332498=%d phone=%d selector=%d startup_pos=%d spin=%d slot1=%d slot0=%d slot1txt=%08x",
                                  frame,v[0],v[1],v[2],v[3],v[4],unsigned(v[5]),v[6],v[7],v[8],v[9],v[10],v[11],v[12],v[13],v[14],v[15],v[16],v[17],v[18],v[19],v[20],v[21],v[22],v[23],v[24],v[25],unsigned(v[26]));log(l);
    }
private:
    static int glob(recomp::trainer::Natives& nat,std::uint32_t gl) noexcept {   // the VM's block table (as shop_probe.h / fmi_probe.h)
        const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,base=nat.load(0x83C5CE48u+4u*b),size=nat.load(0x83C5CF50u+4u*b);
        return (base && !(base&3u) && w<size)?int(nat.load(base+4u*w)):-1;
    }
    static std::uint32_t one(recomp::trainer::Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};return nat.call(h,a.begin(),unsigned(a.size()),out)?out[0]:0xFFFFFFFFu;}
    bool started_=false;std::uint32_t last_=0;int prev_[27]{};
};

// v1058: one frame of an online action, driven by the controller (the glue: onlineActions().begin / step with PLAYER_ID / PLAYER_PED_ID).
using DriveFn=int(*)(recomp::trainer::Natives& nat,int action,int a0,bool start,std::uint32_t player,std::uint32_t ped,std::uint32_t frame,char* detail,int cap);
// v1066: the first-visit creator (the glue's DiscCreatorGlue route, started directly, kept dark in Online mode)
using CreatorStartFn=int(*)(recomp::trainer::Natives& nat,char* detail,int cap);
using AdoptFn=int(*)(recomp::trainer::Natives& nat,std::uint32_t ped,char* detail,int cap);   // v1105 (FREEMODE-GATE-6 B3): Actions::adoptGameCharacter
using CreatorPhaseFn=int(*)();
// v1082: guest memory for the one global write CloudsDown makes (G1332498, as maintransition writes it); set by the glue before each tick.
inline std::uint8_t* guestBase=nullptr;
class GamePedOps final : public PedOps, public ::recomp::online_descent::DescentOps {
public:
    GamePedOps(recomp::trainer::Natives& nat,recomp::creator::Store* store,DriveFn drive,std::uint32_t frame,CreatorStartFn cs=nullptr,CreatorPhaseFn cp=nullptr,AdoptFn adopt=nullptr) noexcept
        :nat_(nat),store_(store),drive_(drive),frame_(frame),cs_(cs),cp_(cp),adopt_(adopt) {}
    // v1105 (FREEMODE-GATE-6 B3)
    bool fmRoute() override {return ::recomp::fm_route::routeOn() && ::recomp::mt_adopt::entered.load(std::memory_order_acquire);}
    bool routeSwitch() override {return ::recomp::fm_route::routeOn();}   // v1125
    int adoptGameCharacter(char* d,int cap) override {if(!adopt_){if(d && cap>0)std::snprintf(d,std::size_t(cap),"no-adopt-fn");return 2;}return adopt_(nat_,ped(),d,cap);}
    int introRecord() override {   // v1105 (FREEMODE-GATE-6 C4): <online gen>/online/arrival.json, read once here
        if(!arrival_)return -1;
        char b[256];const int k=arrival_->read(b,int(sizeof b)-1);if(k<=0)return 0;
        const auto r=::recomp::online_arrival::parse(b,k);return r.valid && r.intro?1:0;
    }
    void setArrivalStore(recomp::creator::Store* s) noexcept {arrival_=s;}
    bool lookHair(int& now,int& applied) override {   // GET_PED_DRAWABLE_VARIATION(ped, 2) against the saved record's hair
        now=applied=-1;const std::uint32_t p=ped();if(!p || !store_)return false;
        const std::uint32_t r=one(gn::DrawableVar,{p,2u});if(r==0xFFFFFFFFu)return false;now=int(r);
        char b[2048];const int k=store_->read(b,int(sizeof b));if(k<=0)return false;
        recomp::creator::Look l;if(!recomp::creator::parse(b,k,l))return false;applied=l.part[2].drawable;return true;
    }
    int creatorStart(char* detail,int cap) override {if(!cs_){if(detail && cap>0)std::snprintf(detail,std::size_t(cap),"no-route");return 2;}return cs_(nat_,detail,cap);}
    int creatorPhase() override {return cp_?cp_():0;}
    // v1070: files/private-trace/diag-online-nocreator: the first visit takes the default character (the default path: the boot-card byte,
    // the swap, the reveal), for the selector / card-word samples while the creator waits for the renderer fix (bugfix-stability)
    bool creatorAvailable() override {static const bool off=::access(RECOMP_APP_PATH("/private-trace/diag-online-nocreator"),F_OK)==0;return !off && cs_!=nullptr && cp_!=nullptr && cp_()>=0;}
    std::uint32_t playerModel() override {const std::uint32_t p=ped();return p?one(gn::Model,{p}):0u;}
    bool blendDone() override {const std::uint32_t p=ped();return p && one(gn::BlendDone,{p})==1u;}
    void blendDetail(char* d,int cap) override {   // v1170 (read only): the native's streaming half and the player switch
        if(!d || cap<=0)return;const std::uint32_t p=ped();
        std::snprintf(d,std::size_t(cap),"streaming=%d switch=%d sw_state=%d sw_type=%d",p?int(one(gn::StreamingDone,{p})&0xFFu):-1,
                      int(one(gn::SwitchOn,{})&0xFFu),int(one(gn::SwitchState,{})),int(one(gn::SwitchType,{})));
    }
    bool inVehicle() override {const std::uint32_t p=ped();return p && one(gn::InVehicle,{p,0u})==1u;}
    void leaveVehicleNow() override {const std::uint32_t p=ped();if(p)(void)one(gn::ClearTasksNow,{p});}
    bool cardUp() override {return nat_.load(CardShownWord)!=0u;}   // v1072
    bool gameFlow() override {static const bool on=::access(RECOMP_APP_PATH("/private-trace/diag-online-gameflow"),F_OK)==0;return on;}
    int savedSex() override {
        if(!store_)return -1;
        char b[2048];const int k=store_->read(b,int(sizeof b));
        if(k<=0)return -1;
        recomp::creator::Look l;if(!recomp::creator::parse(b,k,l))return -1;
        return l.model==recomp::creator::FemaleModel?1:0;
    }
    // v1082: CloudsDown (online_descent.h), the game's own natives and word (maintransition 0x51b / 0x449fa)
    ::recomp::online_descent::DescentOps* descentOps() override {return guestBase?this:nullptr;}
    void coverNow() override {(void)one(gn::FadeOut,{0u});}   // v1085: DO_SCREEN_FADE_OUT(0)
    bool pauseMenuUp() override {return (one(gn::PauseMenu,{})&0xFFu)==1u;}   // v1086 INTERIM
    void releaseFrontend() override {(void)one(gn::SetFrontendActive,{0u});}
    int freemodeThreads() override {return int(one(gn::Threads,{recomp::trainer::joaat("freemode")}));}          // v1088
    int transitionThreads() override {return int(one(gn::Threads,{recomp::trainer::joaat("maintransition")}));}  // v1088
    int mtState() override {std::uint32_t a=0;return globAddr(1344034u,a)?int(nat_.load(a)):-1;}   // v1093: G1344034
    int uiState() noexcept {std::uint32_t a=0;return globAddr(1344033u,a)?int(nat_.load(a)):-1;}   // v1109: G1344033, maintransition's UI (4 = the MP character select)
    int playerRestore(bool apply,char* d,int cap) override {   // v1104: maintransition 0x43111(1) after the sweep, then a read-back
        const std::uint32_t pl=one(gn::PlayerId,{}),pd=ped();
        if(pl==0xFFFFFFFFu || !pd){if(d && cap>0)std::snprintf(d,std::size_t(cap),"no player / ped");return -1;}
        int k=0;const auto put=[&](const char* f,auto... v){if(d && k<cap){const int n=std::snprintf(d+k,std::size_t(cap-k),f,v...);if(n>0)k+=n;}};
        const auto ok=[&](std::uint32_t h,std::initializer_list<std::uint32_t> a){std::uint32_t out[3]{};return nat_.call(h,a.begin(),unsigned(a.size()),out);};
        const auto readState=[&](const char* tag){
            std::uint32_t c[3]{};const std::uint32_t ca[2]{pd,1u};const bool hc=nat_.call(gn::Coords,ca,2,c);float x=0,y=0,z=0;std::memcpy(&x,&c[0],4);std::memcpy(&y,&c[1],4);std::memcpy(&z,&c[2],4);
            const int vis=int(one(gn::IsVisible,{pd})&0xFFu),ctl=int(one(gn::ControlOn,{pl})&0xFFu),inv=int(one(gn::GetInvincible,{pl})&0xFFu);
            put("%s vis=%d ctl=%d inv=%d pos=%.2f,%.2f,%.2f%s; ",tag,vis,ctl,inv,double(x),double(y),double(z),hc?"":"?");return vis==1 && ctl==1;};
        if(!apply)return readState("read")?1:0;
        readState("before");
        const int c1=ok(gn::SetVisible,{pd,1u}),c2=ok(gn::Freeze,{pd,0u}),c3=ok(gn::SetCollision,{pd,1u,0u}),c4=ok(gn::SetInvincible,{pl,0u}),   // (in this order, as 0x43111)
                  c5=ok(gn::SetControl,{pl,1u,0u}),c6=ok(gn::DisplayHud,{1u}),c7=ok(gn::DisplayRadar,{1u});
        put("calls visible=%d unfreeze=%d collision=%d invincible0=%d control=%d hud=%d radar=%d; ",c1,c2,c3,c4,c5,c6,c7);
        return readState("after")?1:0;
    }
    bool cloudsDown() override {static const bool on=::access(RECOMP_APP_PATH("/private-trace/diag-online-cloudsdown"),F_OK)==0;return on;}
    int mpState() override {std::uint32_t a=0;return globAddr(1332498u,a)?int(nat_.load(a)):-1;}
    void putMpState(int v) override {std::uint32_t a=0;if(guestBase && globAddr(1332498u,a)){const std::uint32_t be=__builtin_bswap32(std::uint32_t(v));std::memcpy(guestBase+a,&be,4);}}
    bool switchOn() override {return (one(gn::SwitchOn,{})&0xFFu)==1u;}
    bool firstDone() override {return (one(gn::FirstDone,{})&0xFFu)==1u;}
    void firstPart(unsigned flags,int type) override {(void)one(gn::SwitchMpFlag,{1u});const std::uint32_t p=ped();if(p)(void)one(gn::FirstPart,{p,flags,std::uint32_t(type)});}
    void secondPart() override {const std::uint32_t p=ped();if(p)(void)one(gn::SecondPart,{p});}
    void stop() override {(void)one(gn::StopSwitch,{});}
    void cleanup() override {(void)one(gn::SetInvincible,{one(gn::PlayerId,{}),0u});(void)one(gn::SwitchMpFlag,{0u});}
    int driveAction(int action,int a0,bool start,char* detail,int cap) override {
        if(!drive_)return 2;
        return drive_(nat_,action,a0,start,one(gn::PlayerId,{}),ped(),frame_,detail,cap);
    }
private:
    std::uint32_t ped() noexcept {const std::uint32_t p=one(gn::PlayerPed,{});return p==0xFFFFFFFFu?0u:p;}
    std::uint32_t one(std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {std::uint32_t out[3]{};return nat_.call(h,a.begin(),unsigned(a.size()),out)?out[0]:0xFFFFFFFFu;}
    bool globAddr(std::uint32_t gl,std::uint32_t& a) noexcept {   // the VM's block table (as TransitionProbe::glob)
        const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,bs=nat_.load(0x83C5CE48u+4u*b),sz=nat_.load(0x83C5CF50u+4u*b);
        if(!bs || (bs&3u) || w>=sz)return false;a=bs+4u*w;return true;}
    recomp::trainer::Natives& nat_;recomp::creator::Store* store_;DriveFn drive_;std::uint32_t frame_;CreatorStartFn cs_;CreatorPhaseFn cp_;AdoptFn adopt_;recomp::creator::Store* arrival_=nullptr;
};
}
