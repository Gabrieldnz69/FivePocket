#pragma once
// sandbox_contact_blips.h (Update 1 wave 3, lane lester-blip): Sandbox mode keeps the story's contact / mission-trigger icons on the map
// while the missions themselves stay held. Pure policy + Natives calls; no guest writes. Evidence: the decoded disc scripts and the
// v962 recompiled handlers (lane tools/, work/ private). The game's own mechanism (OBSERVED, offsets in the decoded scripts):
//  - flow_controller (kept) registers each available mission trigger in the trigger table (global 78549, 17-word entries, count at
//    78548, at most 8: flow 0x30772..0x30808; word 0 = in use, word 5 = mission, word 9 = static blip) and prepares that static
//    blip (flow 0x45e19: bits 8/10 + words 17/18 = the trigger's characters, 0x460ae; bit 16 + word 22 = 3 or 0, 0x45ef7).
//  - main (kept) starts mission_triggerer_A..D by story progress (main 0x3fa3: A until mission 62 is done, then B, C, D); the
//    triggerer is HELD in Sandbox. Each frame the triggerer (A: 0x1295, call at 0x1408) activates the static blip of the trigger's
//    mission (mission table global 72553, 34-word entries, word 10) with SET_STATIC_BLIP_ACTIVE (A: 0x682e: flags bits 0, 15, 3),
//    unless the mission's word 15 has bit 11 or the mission's 75750 slot is set. With the triggerer held this never happens.
//  - blip_controller (kept) draws an active static blip (global 19741, 23-word entries) unless one of its per-entry conditions hides
//    it (0x1df..0x3cc), with ADD_BLIP_FOR_COORD + SET_BLIP_SPRITE (0x539..0x567) and the property function 0xe13.
// So the Sandbox reads those tables (read only) and draws the blip blip_controller would draw for an activated trigger: same
// coordinates, sprite, conditions and properties, in 0xe13's order. Never when the game's own static blip is active (flags bit 0).
// Removed when the trigger goes away, a condition hides it, the player is not a story character, or an online character is the
// player. Blips are runtime objects (never saved); the process (= the Sandbox session, play_mode latch) ends with them.
//
// Ownership: ADD_BLIP_FOR_COORD makes the blip a resource (type 15) of the CURRENT script thread, and the tick runs inside whichever
// script made the frame's first PLAYER_PED_ID call. A blip owned by a short-lived script vanishes with it. So a blip is created only
// while a persistent script hosts the tick (PersistentHosts), falling back to any host after FallbackRefreshes refreshes; a vanished
// blip (DOES_BLIP_EXIST false) is recreated at the next refresh. REMOVE_BLIP is bracketed by
// SET_THIS_SCRIPT_CAN_REMOVE_BLIPS_CREATED_BY_ANY_SCRIPT(1/0), as the game's scripts do (REMOVE_BLIP's worker sub_82D4F2F0 removes
// through the current thread's handler unless thread byte 216, set by that native's sub_82D67BB8, is on).
//
// Known differences from blip_controller (not reproduced): the unidentified native 6aa6a1cc(blip, 1) at 0xe21; the name (word 20,
// needs the unregistered label check 6ecae560; the contact icons register an empty name); entries with flags bits 1, 2 or 27 (their
// conditions read blip_controller statics) are skipped; the triggerer's per-scene callback (A 0x1365) cannot be evaluated.
#include "trainer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::sandbox::contact {
using recomp::trainer::Natives;
using recomp::trainer::joaat;
using recomp::trainer::bits;
using recomp::trainer::asFloat;

namespace n {
// Argument use (word counts from the scripts' NATIVE operands; handlers natives/registrations.json, v962 recompiled code):
// ADD_BLIP_FOR_COORD c6f43d0e -> sub_82D61CE8: 3 floats (lfs 0/4/8 of the args in sub_82D4EE18) -> blip; SET_BLIP_SPRITE 8dbbb0b9,
// SET_BLIP_COLOUR bb3c5a41, SET_BLIP_PRIORITY ce87da6f, SET_BLIP_DISPLAY 2b521f91, SET_BLIP_CATEGORY ef72f533, SET_BLIP_ALPHA:
// (blip, int); SET_BLIP_SCALE 1e6ec434: (blip, float); SET_BLIP_AS_SHORT_RANGE 5c67725e, SET_BLIP_HIGH_DETAIL d5842bff,
// SET_BLIP_AS_MISSION_CREATOR_BLIP 802fb686, SHOW_TICK_ON_BLIP 3dcf0092, SET_BLIP_HIDDEN_ON_LEGEND: (blip, BOOL); REMOVE_BLIP
// d8c3c1cd -> sub_82D62508: 1 word = a POINTER to the handle (zeroed by the worker); DOES_BLIP_EXIST ae92dd96: (blip) -> BOOL;
// GET_THIS_SCRIPT_NAME a40fd5d9: none -> char*; SET_THIS_SCRIPT_CAN_REMOVE_BLIPS_CREATED_BY_ANY_SCRIPT d06f1720: (BOOL);
// GET_ENTITY_MODEL dafcb3ec: (entity) -> hash; GET_PLAYER_WANTED_LEVEL: (player) -> int (as trainer.h).
constexpr std::uint32_t AddBlipForCoord=joaat("ADD_BLIP_FOR_COORD"),SetBlipSprite=joaat("SET_BLIP_SPRITE"),
    SetBlipColour=joaat("SET_BLIP_COLOUR"),SetBlipPriority=joaat("SET_BLIP_PRIORITY"),SetBlipDisplay=joaat("SET_BLIP_DISPLAY"),
    SetBlipCategory=joaat("SET_BLIP_CATEGORY"),SetBlipScale=joaat("SET_BLIP_SCALE"),SetBlipShortRange=joaat("SET_BLIP_AS_SHORT_RANGE"),
    SetBlipHighDetail=joaat("SET_BLIP_HIGH_DETAIL"),SetBlipCreator=joaat("SET_BLIP_AS_MISSION_CREATOR_BLIP"),
    ShowTickOnBlip=joaat("SHOW_TICK_ON_BLIP"),SetBlipAlpha=joaat("SET_BLIP_ALPHA"),SetBlipHiddenOnLegend=joaat("SET_BLIP_HIDDEN_ON_LEGEND"),
    RemoveBlip=joaat("REMOVE_BLIP"),DoesBlipExist=joaat("DOES_BLIP_EXIST"),ThisScriptName=joaat("GET_THIS_SCRIPT_NAME"),
    CanRemoveAnyBlip=joaat("SET_THIS_SCRIPT_CAN_REMOVE_BLIPS_CREATED_BY_ANY_SCRIPT"),GetEntityModel=joaat("GET_ENTITY_MODEL"),
    GetWantedLevel=joaat("GET_PLAYER_WANTED_LEVEL");
}
inline constexpr std::uint32_t UsedNatives[]{n::AddBlipForCoord,n::SetBlipSprite,n::SetBlipColour,n::SetBlipPriority,n::SetBlipDisplay,
    n::SetBlipCategory,n::SetBlipScale,n::SetBlipShortRange,n::SetBlipHighDetail,n::SetBlipCreator,n::ShowTickOnBlip,n::SetBlipAlpha,
    n::SetBlipHiddenOnLegend,n::RemoveBlip,n::DoesBlipExist,n::ThisScriptName,n::CanRemoveAnyBlip,n::GetEntityModel,n::GetWantedLevel};

// Disc facts (this disc's script globals; OBSERVED, see the header comment).
// The globals block: the VM is called with r4 = 0x83C5CE48 (sub_823BFxxx: lis r9,-31802; addi r4,r9,-12728) and GLOBAL_U16 /
// GLOBAL_U24 push *(r4) + 4 * index (VM handler loc_8343A73C). So global g lives at load(GlobalsBasePtr) + 4*g.
constexpr std::uint32_t GlobalsBasePtr=0x83C5CE48u;
constexpr std::uint32_t TriggerCount=78548,TriggerArray=78549,TriggerStride=17,TrigInUse=0,TrigMission=5,TrigBlip=9;
constexpr std::uint32_t MissionArray=72553,MissionStride=34,MisBlip=10,MisFlags=15,MisNoStaticBlip=11;
constexpr std::uint32_t BlockArray=75750,BlockStride=2;                         // triggerer 0x131b / 0x134f: slot word 0 set = no activation
constexpr std::uint32_t BlipArray=19741,BlipStride=23,BlipCoords=1,BlipFlags=11,BlipSprites=13,BlipType=16,BlipChar1=17,BlipChar2=18,
    BlipMissionType=22;
constexpr std::uint32_t CurrentMissionType=29814,NoMissionType=12,HideGlobal19513=19513;
constexpr std::uint32_t MaxTriggers=8,MaxMissions=128,MaxStaticBlips=512,MaxGlobal=0x40000;   // bounds (flow refuses a 9th trigger)
// The flags bits blip_controller / the set-active function read (bit: meaning as the code uses it).
constexpr unsigned FActive=0,FCond1=1,FCond2=2,FDisplayA=4,FDisplayB=5,FLongRange=6,FHideOnMission=7,FForChars=8,FSecondChar=10,
    FNoLongRange=11,FHideWanted=13,FHide19513=14,FMissionType=16,FCreator=17,FPerCharacter=19,FTick=20,FCond27=27,FFaded=28,
    FColour39=29;

// The three story characters, as the triggerer's 0x69b4 derives them from the player model (0 Michael, 1 Franklin, 2 Trevor).
inline int characterOf(std::uint32_t model) noexcept {
    if(model==joaat("player_zero"))return 0;
    if(model==joaat("player_one"))return 1;
    if(model==joaat("player_two"))return 2;
    return -1;
}
// blip_controller 0x12d3 (= triggerer 0x83b): may an icon of mission type `type` show while mission type `current` runs (12 = none)?
inline bool compatible(std::uint32_t type,std::uint32_t current) noexcept {
    if(current==NoMissionType)return true;
    if(type==NoMissionType)return false;
    const auto in=[current](std::initializer_list<std::uint32_t> s){for(auto v:s)if(v==current)return true;return false;};
    switch(type) {
    case 13:return !in({8,9});
    case 0:return in({5,14});
    case 2:case 3:return in({5,6,7,14});
    case 4:return in({14});
    case 6:case 7:case 8:case 10:return in({5});
    case 9:return in({5,6,14});
    case 14:return in({14,11,5});
    case 15:case 11:return in({5,6});
    default:return false;
    }
}

// Hosts whose blips live as long as the session (started once by startup / initial and never ended in free roam).
inline constexpr std::array<const char*,4> PersistentHosts{"main","main_persistent","blip_controller","player_controller"};
inline bool persistentHost(const char* name) noexcept {
    if(!name || !*name)return false;
    const std::uint32_t h=joaat(name);
    for(const char* p:PersistentHosts)if(joaat(p)==h)return true;
    return false;
}

inline bool bit(std::uint32_t w,unsigned b) noexcept {return (w>>b)&1u;}
// One static blip entry as read from the table, and the frame's context.
struct Entry {std::uint32_t index=0,mission=0,flags=0,type=0,sprite=0,char1=0,char2=0,missionType=0;float x=0,y=0,z=0;};
struct Context {int character=-1;std::uint32_t currentType=NoMissionType,global19513=0;bool wanted=false;};
// blip_controller 0x125f: is this entry one of the current character's (bits 8 / 10, words 17 / 18)?
inline bool forCharacter(const Entry& e,int ch) noexcept {
    if(!bit(e.flags,FForChars))return false;
    if(e.char1==std::uint32_t(ch))return true;
    return bit(e.flags,FSecondChar) && e.char2==std::uint32_t(ch);
}
// blip_controller's per-entry conditions (0x1df..0x36c) for an entry the triggerer activated: "show", "hide" or "unsupported".
inline const char* condition(const Entry& e,const Context& c) noexcept {
    if(bit(e.flags,FCond1) || bit(e.flags,FCond2) || bit(e.flags,FCond27))return "unsupported";
    if(bit(e.flags,FHideWanted) && c.wanted)return "hide";                           // 0x1df: f1457 = wanted level != 0
    if(bit(e.flags,FHideOnMission) && c.currentType!=NoMissionType)return "hide";    // 0x249: f1449(12)
    if(bit(e.flags,FMissionType) && !compatible(e.missionType,c.currentType))return "hide";   // 0x270: f12c2(word 22)
    if(bit(e.flags,FHide19513) && c.global19513)return "hide";                       // 0x29f
    if(bit(e.flags,FForChars) && e.type!=4 && e.type!=8 && !forCharacter(e,c.character))return "hide";   // 0x2c3
    if(bit(e.flags,FPerCharacter) && (c.character<0 || c.character>2))return "hide"; // 0x30c
    return "show";
}

// One native call of the plan: blip first (filled in at creation), then one argument (an int or float bits).
struct Op {std::uint32_t native=0,arg=0;};
using Plan=std::array<Op,24>;
// blip_controller 0x539..0x567 + 0xe13 for this entry and context, in its order (the blip argument is implicit). Returns the count.
inline unsigned plan(const Entry& e,const Context& c,Plan& p) noexcept {
    unsigned k=0;const auto op=[&](std::uint32_t nat,std::uint32_t a){if(k<p.size())p[k++]=Op{nat,a};};
    const std::uint32_t f=e.flags;const int ch=c.character;
    op(n::SetBlipSprite,e.sprite);                                             // 0x567
    op(n::SetBlipPriority,2);                                                  // 0xe28
    op(n::SetBlipColour,(f>>21)&0x3Fu);                                        // 0xe34: ca03a1e5(flags, 21, 26)
    if((e.type==4 || e.type==8) && ch>=0 && ch<3)op(n::SetBlipColour,42u+std::uint32_t(ch));   // 0xe58
    if(!bit(f,FFaded))op(n::SetBlipScale,bits(1.0f));                          // 0xe92
    const bool countsNoLong=c.currentType!=NoMissionType && !compatible(0,c.currentType);   // 0xf0f: f1221(0)
    op(n::SetBlipShortRange,bit(f,FLongRange) && !(countsNoLong && bit(f,FNoLongRange))?0u:1u);   // 0xf28
    const bool a=bit(f,FDisplayA),b=bit(f,FDisplayB);
    if(a && b)op(n::SetBlipDisplay,4);else{if(b)op(n::SetBlipDisplay,5);if(a)op(n::SetBlipDisplay,3);}   // 0xf46..0xf70
    switch(e.type) {                                                           // 0xf7d
    case 7:case 6:case 5:op(n::SetBlipPriority,2);op(n::SetBlipHighDetail,0);break;
    case 1:op(n::SetBlipPriority,3);op(n::SetBlipHighDetail,1);break;
    case 9:op(n::SetBlipPriority,1);op(n::SetBlipCategory,10);op(n::SetBlipHighDetail,0);break;
    case 4:case 8:
        op(n::SetBlipHighDetail,1);op(n::SetBlipPriority,e.type==4?7:5);
        if(bit(f,FForChars)) {                                                 // 0x101d
            if(!forCharacter(e,ch)) {                                          // another character's: their colour, smaller
                if(bit(f,FFaded))op(n::SetBlipAlpha,0);
                else {
                    if(e.char1<3)op(n::SetBlipColour,42u+e.char1);
                    if(e.type==8){op(n::SetBlipShortRange,bit(f,FDisplayB));op(n::SetBlipPriority,3);op(n::SetBlipShortRange,1);
                                  op(n::SetBlipHiddenOnLegend,1);op(n::SetBlipScale,bits(0.77f));}
                    else op(n::SetBlipScale,bits(0.72f));
                }
            } else {
                if(bit(f,FFaded))op(n::SetBlipAlpha,128);
                if(e.type==8)op(n::SetBlipHiddenOnLegend,0);
            }
        }
        break;
    default:op(n::SetBlipPriority,5);break;
    }
    if(e.index>=129 && e.index<=139)op(n::SetBlipHighDetail,1);              // 0x1125
    op(n::SetBlipCreator,bit(f,FCreator));                                     // 0x11be
    op(n::ShowTickOnBlip,bit(f,FTick));                                        // 0x11e1
    if(bit(f,FColour39))op(n::SetBlipColour,39);                               // 0x1204
    return k;
}

// Reads one script global; false when the address is not plausible.
struct Globals {
    Natives& nat;std::uint32_t base;
    bool ok() const noexcept {return base>=0x10000u && base<0xF0000000u && !(base&3u);}
    bool get(std::uint32_t g,std::uint32_t& v) const noexcept {if(!ok() || g>=MaxGlobal)return false;v=nat.load(base+4u*g);return true;}
};

// The icons wanted now: one per in-use trigger whose mission's static blip the triggerer would activate and blip_controller would
// show, and that is not already the game's own (active). Returns the count; why = a short reason when the tables are not readable.
inline unsigned build(const Globals& g,const Context& c,std::array<Entry,MaxTriggers>& out,const char*& why,unsigned* unsupported=nullptr) noexcept {
    why="-";unsigned k=0;std::uint32_t count=0,cap=0,mcap=0,bcap=0,blk=0;
    if(!g.ok()){why="globals-base";return 0;}
    if(!g.get(TriggerCount,count) || !g.get(TriggerArray,cap) || !g.get(MissionArray,mcap) || !g.get(BlipArray,bcap) ||
       !g.get(BlockArray,blk)){why="globals-read";return 0;}
    if(cap==0 || cap>32 || mcap==0 || mcap>MaxMissions || bcap==0 || bcap>MaxStaticBlips || blk>MaxMissions){why="table-size";return 0;}
    const std::uint32_t n=std::min({count,cap,MaxTriggers});
    for(std::uint32_t i=0;i<n;++i) {
        const std::uint32_t t=TriggerArray+1+i*TriggerStride;std::uint32_t used=0,m=0,tb=0;
        if(!g.get(t+TrigInUse,used) || !used || !g.get(t+TrigMission,m) || !g.get(t+TrigBlip,tb) || m>=mcap)continue;
        const std::uint32_t me=MissionArray+1+m*MissionStride;std::uint32_t sb=0,mflags=0,blocked=0;
        if(!g.get(me+MisBlip,sb) || !g.get(me+MisFlags,mflags))continue;
        if(bit(mflags,MisNoStaticBlip))continue;                                   // triggerer 0x13f0
        if(m<blk && g.get(BlockArray+1+m*BlockStride,blocked) && blocked)continue;  // triggerer 0x134f
        if(sb>=bcap)sb=tb;                                                         // (the triggerer uses word 10; word 9 as a fallback)
        if(sb>=bcap)continue;
        const std::uint32_t be=BlipArray+1+sb*BlipStride;Entry e;e.index=sb;e.mission=m;
        if(!g.get(be+BlipFlags,e.flags) || !g.get(be+BlipType,e.type) || !g.get(be+BlipChar1,e.char1) || !g.get(be+BlipChar2,e.char2) ||
           !g.get(be+BlipMissionType,e.missionType))continue;
        if(bit(e.flags,FActive))continue;                                          // the game's own blip is up
        const char* cond=condition(e,c);
        if(cond[0]=='u'){if(unsupported)++*unsupported;continue;}
        if(cond[0]=='h')continue;
        const std::uint32_t ci=bit(e.flags,FPerCharacter)?std::uint32_t(c.character):0u;
        std::uint32_t wx=0,wy=0,wz=0;
        if(!g.get(be+BlipSprites+ci,e.sprite) || !g.get(be+BlipCoords+3*ci,wx) || !g.get(be+BlipCoords+3*ci+1,wy) ||
           !g.get(be+BlipCoords+3*ci+2,wz))continue;
        e.x=asFloat(wx);e.y=asFloat(wy);e.z=asFloat(wz);
        if(e.sprite==0 || e.sprite>1000 || !std::isfinite(e.x) || !std::isfinite(e.y) || !std::isfinite(e.z) || std::fabs(e.x)>20000.f ||
           std::fabs(e.y)>20000.f || std::fabs(e.z)>5000.f || (e.x==0.f && e.y==0.f))continue;
        bool dup=false;for(unsigned j=0;j<k;++j)dup|=out[j].index==sb;
        if(dup || k>=out.size())continue;
        out[k++]=e;
    }
    return k;
}

constexpr std::uint32_t RefreshFrames=30,FallbackRefreshes=20;
using LogFn=void(*)(const char*);

class ContactBlips {
public:
    explicit ContactBlips(LogFn log) noexcept:log_(log) {}
    // Every game frame from the Sandbox tick; works once per RefreshFrames. playing / online / player / ped: as the tick knows them.
    void tick(Natives& nat,std::uint32_t frame,bool playing,bool online,std::uint32_t player,std::uint32_t ped) noexcept {
        if(started_ && frame-last_<RefreshFrames)return;
        started_=true;last_=frame;
        if(online){if(count())clear(nat,"online-character");return;}
        if(!playing)return;                                            // (death / fade: keep what is up, decide later)
        std::uint32_t out[3]{};Context c;
        c.character=nat.call(n::GetEntityModel,&ped,1,out)?characterOf(out[0]):-1;
        if(c.character<0){if(count())clear(nat,"not-story-character");return;}
        Globals g{nat,nat.load(GlobalsBasePtr)};
        (void)g.get(CurrentMissionType,c.currentType);(void)g.get(HideGlobal19513,c.global19513);
        std::uint32_t wl[3]{};c.wanted=nat.call(n::GetWantedLevel,&player,1,wl) && wl[0]!=0;
        std::array<Entry,MaxTriggers> want{};const char* why="-";unsigned unsupported=0;
        const unsigned w=build(g,c,want,why,&unsupported);
        if(why[0]!='-' && !warned_){warned_=true;logf("RENDER_SANDBOX contact-blip tables=%s base=%08x",why,g.base);}
        if(unsupported && !warnedUnsupported_){warnedUnsupported_=true;logf("RENDER_SANDBOX contact-blip unsupported=%u",unsupported);}
        // drop ours that are no longer wanted, whose look changed (character switch), or that the game removed
        for(auto& s:slots_) {
            if(!s.handle)continue;
            std::uint32_t exist[3]{};const bool there=call(nat,n::DoesBlipExist,{s.handle},exist) && exist[0];
            if(!there){++vanished_;logf("RENDER_SANDBOX contact-blip vanished index=%u handle=%08x vanished=%u",s.e.index,s.handle,vanished_);s=Slot{};continue;}
            const Entry* e=find(want,w,s.e.index);
            if(!e || !same(*e,c,s))remove(nat,s,e?"changed":"not-wanted");
        }
        if(w!=lastWanted_){lastWanted_=w;logf("RENDER_SANDBOX contact-blip wanted=%u character=%d mission_type=%u wanted_level=%d",w,c.character,
                                                c.currentType,c.wanted?1:0);}
        bool missing=false;for(unsigned i=0;i<w;++i)missing|=!findSlot(want[i].index);
        if(!missing){waits_=0;return;}
        char host[64];hostName(nat,host);
        const bool persistent=persistentHost(host);
        if(!persistent && ++waits_<FallbackRefreshes)return;          // wait for a session-long host
        for(unsigned i=0;i<w;++i)if(!findSlot(want[i].index))add(nat,want[i],c,host,!persistent);
        waits_=0;
    }
    // Removes every icon of ours (an online character, Sandbox teardown).
    void clear(Natives& nat,const char* why) noexcept {for(auto& s:slots_)if(s.handle)remove(nat,s,why);}
    unsigned count() const noexcept {unsigned k=0;for(auto& s:slots_)k+=s.handle!=0;return k;}
    unsigned added() const noexcept {return added_;}
    unsigned vanished() const noexcept {return vanished_;}
    std::uint32_t handleFor(std::uint32_t index) const noexcept {for(auto& s:slots_)if(s.handle && s.e.index==index)return s.handle;return 0;}
private:
    struct Slot {Entry e{};Plan p{};unsigned np=0;std::uint32_t handle=0;};
    static bool call(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a,std::uint32_t* out) noexcept {
        return nat.call(h,a.begin(),unsigned(a.size()),out);
    }
    static void call0(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {std::uint32_t o[3]{};(void)call(nat,h,a,o);}
    template<class... A> void logf(const char* f,A... a) const noexcept {if(!log_)return;char l[256];std::snprintf(l,sizeof l,f,a...);log_(l);}
    static const Entry* find(const std::array<Entry,MaxTriggers>& want,unsigned w,std::uint32_t index) noexcept {
        for(unsigned i=0;i<w;++i)if(want[i].index==index)return &want[i];
        return nullptr;
    }
    // the icon up is still the one wanted: same position, sprite and call plan (a character switch changes colour / scale)
    static bool same(const Entry& e,const Context& c,const Slot& s) noexcept {
        if(e.sprite!=s.e.sprite || e.x!=s.e.x || e.y!=s.e.y || e.z!=s.e.z)return false;
        Plan p{};const unsigned np=plan(e,c,p);
        if(np!=s.np)return false;
        for(unsigned i=0;i<np;++i)if(p[i].native!=s.p[i].native || p[i].arg!=s.p[i].arg)return false;
        return true;
    }
    Slot* findSlot(std::uint32_t index) noexcept {for(auto& s:slots_)if(s.handle && s.e.index==index)return &s;return nullptr;}
    static void hostName(Natives& nat,char (&out)[64]) noexcept {
        out[0]=0;std::uint32_t r[3]{};
        if(!nat.call(n::ThisScriptName,nullptr,0,r))return;
        const std::uint32_t at=r[0];
        if(at<0x10000u || at>0xFFFF0000u)return;
        for(std::uint32_t i=0;i<63;++i) {
            const std::uint32_t a=at+i,word=nat.load(a&~3u);const char ch=char((word>>(24-8*(a&3u)))&0xFFu);
            if(!ch){out[i]=0;return;}
            if(ch<32 || ch>126){out[0]=0;return;}
            out[i]=ch;
        }
        out[0]=0;
    }
    void add(Natives& nat,const Entry& e,const Context& c,const char* host,bool fallback) noexcept {
        Slot* s=nullptr;for(auto& x:slots_)if(!x.handle){s=&x;break;}
        if(!s)return;
        std::uint32_t r[3]{};
        if(!call(nat,n::AddBlipForCoord,{bits(e.x),bits(e.y),bits(e.z)},r) || !r[0]){logf("RENDER_SANDBOX contact-blip add-failed index=%u",e.index);return;}
        const std::uint32_t b=r[0];Plan p{};const unsigned np=plan(e,c,p);
        for(unsigned i=0;i<np;++i)call0(nat,p[i].native,{b,p[i].arg});
        s->e=e;s->p=p;s->np=np;s->handle=b;++added_;
        logf("RENDER_SANDBOX contact-blip add index=%u mission=%u sprite=%u at=%.2f,%.2f,%.2f character=%d host=%s%s handle=%08x added=%u",
             e.index,e.mission,e.sprite,double(e.x),double(e.y),double(e.z),c.character,host[0]?host:"?",fallback?" fallback=1":"",b,added_);
    }
    void remove(Natives& nat,Slot& s,const char* why) noexcept {
        const std::uint32_t slot=nat.word(s.handle);
        if(!slot){logf("RENDER_SANDBOX contact-blip remove-later index=%u handle=%08x why=%s no-scratch",s.e.index,s.handle,why);return;}
        call0(nat,n::CanRemoveAnyBlip,{1});                            // (as the game's scripts bracket it)
        call0(nat,n::RemoveBlip,{slot});
        call0(nat,n::CanRemoveAnyBlip,{0});
        logf("RENDER_SANDBOX contact-blip remove index=%u handle=%08x why=%s",s.e.index,s.handle,why);
        s=Slot{};
    }
    LogFn log_;std::array<Slot,MaxTriggers> slots_{};std::uint32_t last_=0,waits_=0;unsigned added_=0,vanished_=0,lastWanted_=~0u;
    bool started_=false,warned_=false,warnedUnsupported_=false;
};
} // namespace recomp::sandbox::contact
