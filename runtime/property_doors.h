#pragma once
#include <atomic>   // v975 (G2)
// property_doors.h (Update 1, lane property-doors): Sandbox mode gets what GTA Online has around its properties, built only from
// the game's own natives and the disc's own property tables: one map icon per property building, a walk-in trigger at each entrance
// (on foot, standing in the door's area and facing it, as online), a small chooser when one entrance leads to several places, and a
// leave trigger inside that brings the player back out to that building. Pure policy + Natives calls; no guest writes, no script
// globals, no network or stats natives. The trips themselves are the trainer's (action 16 OnlinePlace: a0 = PlaceList index to go
// in, a0 = exitArg(...) to come back out); this header only asks for them through Hooks and watches their results.
//
// Disc evidence (OBSERVED unless marked; offsets in the decoded scripts, NOTES.md has the full list):
//  - mp_prop_global_block builds the 66 property records: property switch 0x4e / 0xb4 (building +35, garage size = 0x3b44's
//    argument, garage-only +34), building function 0x43b7 / 0x43dc (+ 0xb004 / 0xb00b for buildings 34-56): blip points +3[i],
//    entrance count +36, entrances +38[i] (27 words: angled area A +0, B +3, width +6, facing heading +7, type +26: 1 foot,
//    2 garage), foot leave point +120[0]+8 / heading +11, garage leave point +893+24 / +27; apartment classes 0xe27 (high 0xef9,
//    mid 0x188c, low 0x1eb6): exit area +120[0]; garage sizes 0x3b44: garage exit area +893.
//  - am_mp_property_ext 0xb95e: the entry test is IS_ENTITY_IN_ANGLED_AREA(ped, A, B, width, 0, 1, 1) at 0xbc54 and the player's
//    heading within 30 degrees of +7 (0xbc7d -> 0xbcb0); building 1's garage area is replaced by 0xc291's (0xb9a3).
//  - am_mp_property_ext 0x5fa1: the entry menu reads control group 2: 174 select (sound "SELECT"), 175 back ("CANCEL"), 162 up
//    (selection - 1), 161 down (+ 1), each made exclusive first (SET_INPUT_EXCLUSIVE 0x5fb3..0x5fd1).
//  - am_mp_property_int 0x16052: the leave test: apartments IS_ENTITY_IN_ANGLED_AREA on +120[0] (0x16115), garages on +893 with
//    the heading within 60 degrees of +893+7 (0x160ac, 0x10fcf); 0x17f84 / 0x17fbb put the player at +893+24 / +120[0]+8.
//  - freemode 0x23563: property icons are sprite 40 (apartment) / 357 (garage); the short-range look is SET_BLIP_AS_SHORT_RANGE(1),
//    SET_BLIP_COLOUR(3) and SET_BLIP_PRIORITY(0x23ca2(4) = 2) (0x238cf..0x23924 and 0x23975..0x239c9).
//  - freemode 0x5d91e: help text = BEGIN_TEXT_COMMAND_DISPLAY_HELP(label), ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(text),
//    END_TEXT_COMMAND_DISPLAY_HELP(0, 0, 1, duration); "STRING" is the plain-text label the scripts use with that component.
// Design (NOTES.md "Design"): one icon per building at its +3[0] (40 when the building has an apartment, else 357); the walk-in
// trigger opens the chooser (the game's own controls) even for one place, so walking past never teleports; select asks the trainer
// for the place; inside, standing in the leave area asks for the trip back to that building's leave point. Markers (DRAW_MARKER) are
// not drawn: it takes 24 argument words and the glue's Natives::call carries at most 12.
//
// Ownership (lane lester-blip's rule, sandbox_contact_blips.h): ADD_BLIP_FOR_COORD makes the blip a resource of the CURRENT script
// thread, and the tick runs inside whichever script made the frame's first PLAYER_PED_ID call; so icons are created only while a
// persistent script hosts the tick (sandbox::contact::persistentHost; after FallbackRefreshes refreshes any host), a vanished one is created
// again, and REMOVE_BLIP is always bracketed by SET_THIS_SCRIPT_CAN_REMOVE_BLIPS_CREATED_BY_ANY_SCRIPT(1 / 0).
// Cost: work only every CheckFrames game frames (PLAYER_PED_ID, GET_ENTITY_COORDS, a distance loop over a fixed table, up to
// BlipChecksPerTick DOES_BLIP_EXIST; one IS_ENTITY_IN_ANGLED_AREA only when the player is near an entrance or inside a place). While
// the chooser is open: 6 SET_INPUT_EXCLUSIVE + 4 IS_CONTROL_PRESSED per frame (as the game's own menu). No allocation anywhere.
#include "trainer.h"
#include "sandbox_contact_blips.h"   // sandbox::contact::persistentHost (one ownership rule for every Sandbox icon)
#include "menu_draw.h"               // v977: online's property menu = the commonmenu shop list without a banner (shared drawer)
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::propdoors {
inline std::atomic<bool> chooserOpen{false};   // v975 (G2): Doors::tick publishes "the door chooser is open" (17 4 refuses meanwhile)
using recomp::trainer::Natives;
using recomp::trainer::joaat;
using recomp::trainer::bits;
using recomp::trainer::asFloat;

namespace n {
// Argument use (registrations.json handler, its argument reads in the v962+ recompiled corpus; tests/check_natives.py checks them):
// ADD_BLIP_FOR_COORD c6f43d0e sub_82D61CE8 (3 floats via sub_82D4EE18) -> blip; SET_BLIP_SPRITE / SET_BLIP_COLOUR / SET_BLIP_PRIORITY
// (blip, int); SET_BLIP_AS_SHORT_RANGE (blip, BOOL); DOES_BLIP_EXIST (blip) -> BOOL; REMOVE_BLIP d8c3c1cd: 1 word = a POINTER to the
// handle; SET_THIS_SCRIPT_CAN_REMOVE_BLIPS_CREATED_BY_ANY_SCRIPT (BOOL); GET_THIS_SCRIPT_NAME () -> char*; PLAYER_PED_ID () -> ped;
// GET_ENTITY_COORDS 1647f1cb sub_82D773A0 (entity, BOOL) -> 3 words; GET_ENTITY_HEADING 972cc383 sub_82D774E0 (entity) -> float;
// IS_PED_IN_ANY_VEHICLE 3b0171ee sub_82DD12F8 (ped, BOOL); IS_ENTITY_IN_ANGLED_AREA 883622fa sub_82D77CF8 -> sub_82545EE8: 11 words
// (entity, A 3 floats, B 3 floats, width float, BOOL, BOOL, int); IS_CONTROL_PRESSED 517a4384 sub_82DACBE8 (pad, control) -> BOOL;
// SET_INPUT_EXCLUSIVE 4e8e29e6 sub_82DAD0B0 (pad, control 0..260); BEGIN_TEXT_COMMAND_DISPLAY_HELP b245fc10 sub_82D60CD0 (char*);
// ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME 27a244d8 sub_82D60E40 (char*; copied into the text pool by sub_82EF3E98, a 16-bit id is
// kept); END_TEXT_COMMAND_DISPLAY_HELP b59b530d sub_82D60CE0 (int, BOOL, BOOL, int); CLEAR_HELP e6d85741 sub_82D61938 (BOOL).
constexpr std::uint32_t AddBlipForCoord=joaat("ADD_BLIP_FOR_COORD"),SetBlipSprite=joaat("SET_BLIP_SPRITE"),
    SetBlipColour=joaat("SET_BLIP_COLOUR"),SetBlipPriority=joaat("SET_BLIP_PRIORITY"),SetBlipShortRange=joaat("SET_BLIP_AS_SHORT_RANGE"),
    DoesBlipExist=joaat("DOES_BLIP_EXIST"),RemoveBlip=joaat("REMOVE_BLIP"),
    CanRemoveAnyBlip=joaat("SET_THIS_SCRIPT_CAN_REMOVE_BLIPS_CREATED_BY_ANY_SCRIPT"),ThisScriptName=joaat("GET_THIS_SCRIPT_NAME"),
    PlayerPedId=joaat("PLAYER_PED_ID"),GetEntityCoords=joaat("GET_ENTITY_COORDS"),GetEntityHeading=joaat("GET_ENTITY_HEADING"),
    IsPedInAnyVehicle=joaat("IS_PED_IN_ANY_VEHICLE"),InAngledArea=joaat("IS_ENTITY_IN_ANGLED_AREA"),
    IsControlPressed=joaat("IS_CONTROL_PRESSED"),SetInputExclusive=joaat("SET_INPUT_EXCLUSIVE"),
    BeginHelp=joaat("BEGIN_TEXT_COMMAND_DISPLAY_HELP"),AddSubstring=joaat("ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME"),
    EndHelp=joaat("END_TEXT_COMMAND_DISPLAY_HELP"),ClearHelp=joaat("CLEAR_HELP");
}
inline constexpr std::uint32_t UsedNatives[]{n::AddBlipForCoord,n::SetBlipSprite,n::SetBlipColour,n::SetBlipPriority,n::SetBlipShortRange,
    n::DoesBlipExist,n::RemoveBlip,n::CanRemoveAnyBlip,n::ThisScriptName,n::PlayerPedId,n::GetEntityCoords,n::GetEntityHeading,
    n::IsPedInAnyVehicle,n::InAngledArea,n::IsControlPressed,n::SetInputExclusive,n::BeginHelp,n::AddSubstring,n::EndHelp,n::ClearHelp};

// The game's own constants (see the header comment for where each comes from).
constexpr std::uint32_t ShortRangeColour=3,ShortRangePriority=2;
constexpr std::uint32_t ControlPad=2,CtlDown=161,CtlUp=162,CtlLeft=163,CtlRight=164,CtlSelect=174,CtlBack=175;
inline constexpr std::array<std::uint32_t,6> ExclusiveControls{CtlBack,CtlSelect,CtlLeft,CtlRight,CtlUp,CtlDown};   // 0x5fb3..0x5fd1 order
constexpr float EntryHeadingTolerance=30.0f;   // am_mp_property_ext 0xbc78
constexpr std::uint8_t NoPlace=255;

// ---------------------------------------------------------------------------------------------------------------- the table
struct Area {float ax=0,ay=0,az=0,bx=0,by=0,bz=0,width=0,heading=0;};
enum Kind : std::uint8_t {None=0,Foot=1,Garage=2};
struct Entrance {
    Area area{};Kind kind=None;
    float leaveX=0,leaveY=0,leaveZ=0,leaveH=0;           // where online puts the player when he leaves through this kind of door
    std::array<std::uint8_t,4> places{NoPlace,NoPlace,NoPlace,NoPlace};std::uint8_t nplaces=0;   // PlaceList indices (online_world.h)
};
struct Building {std::uint8_t id=0;float bx=0,by=0,bz=0;std::uint16_t sprite=0;std::uint8_t garageSize=0,ne=0;std::array<Entrance,2> e{};};
struct PlaceExit {bool valid=false;Area area{};float headingTolerance=0;};   // 0 = no heading test (the game has none for apartments)

// The decoded table (generated by tools/gen_table.py from work/decoded.json; tests/check_table.py re-derives it from the decoded
// scripts when work/ is present): Buildings (the 56 property buildings, disc ids 1-56), PlaceExits and PlaceLabels (PlaceList order).
#include "property_doors_table.inc"
static_assert(PlaceExits.size()==24 && PlaceLabels.size()==24,"PlaceList has 24 places (online_world.h)");

// ---------------------------------------------------------------------------------------------------------------- pure helpers
// am_mp_property_ext 0xbcb0 / am_mp_property_int 0x10fcf: is heading h strictly within tol of target (degrees, the game's wrap)?
inline bool headingWithin(float h,float target,float tol) noexcept {
    float lo=target-tol;if(lo<0.f)lo+=360.f;
    float hi=target+tol;if(hi>=360.f)hi-=360.f;
    if(hi>lo)return h>lo && h<hi;
    return h<hi || h>lo;
}
// Cheap pre-filter before the game's own area test: the point is within the area's bounding circle (plus a margin) and its height.
inline bool nearArea(const Area& a,float x,float y,float z,float margin) noexcept {
    const float cx=(a.ax+a.bx)*0.5f,cy=(a.ay+a.by)*0.5f,cz=(a.az+a.bz)*0.5f;
    const float half=std::sqrt((a.bx-a.ax)*(a.bx-a.ax)+(a.by-a.ay)*(a.by-a.ay))*0.5f+a.width*0.5f+margin;
    const float dz=std::fabs(a.bz-a.az)*0.5f+margin;
    return (x-cx)*(x-cx)+(y-cy)*(y-cy)<=half*half && std::fabs(z-cz)<=dz;
}
inline float distTo(const Area& a,float x,float y,float z) noexcept {
    const float cx=(a.ax+a.bx)*0.5f,cy=(a.ay+a.by)*0.5f,cz=(a.az+a.bz)*0.5f;
    return std::sqrt((x-cx)*(x-cx)+(y-cy)*(y-cy)+(z-cz)*(z-cz));
}
// Trainer action 16's a0 for the trip back out: ExitArgBase + building index * 2 + entrance index (a PROPOSED extension of the
// accepted action 16; PlaceList 0..23 and the sweep 99 are unchanged).
constexpr int ExitArgBase=1000;
constexpr int exitArg(int building,int entrance) noexcept {return ExitArgBase+building*2+entrance;}
inline bool validExitArg(int a0) noexcept {
    const int k=a0-ExitArgBase;
    if(k<0 || k>=int(Buildings.size())*2)return false;
    const Building& b=Buildings[std::size_t(k/2)];
    return k%2<b.ne;
}
// Where the trip back out puts the player (false: not an exit argument).
inline bool exitTarget(int a0,float& x,float& y,float& z,float& h) noexcept {
    if(!validExitArg(a0))return false;
    const int k=a0-ExitArgBase;const Entrance& e=Buildings[std::size_t(k/2)].e[std::size_t(k%2)];
    x=e.leaveX;y=e.leaveY;z=e.leaveZ;h=e.leaveH;return true;
}

// ---------------------------------------------------------------------------------------------------------------- the runtime
constexpr std::uint32_t CheckFrames=10;            // the throttle: one look every 10 game frames
constexpr std::uint32_t BlipChecksPerTick=4;       // DOES_BLIP_EXIST per look (round robin: every icon about every 140 frames)
constexpr std::uint32_t CreatePerTick=8;           // icons created per look (56 at the first host: 7 looks)
constexpr std::uint32_t FallbackRefreshes=20;      // looks without a persistent host before any host may own the icons
constexpr std::uint32_t TripFramesMax=900;         // a trip whose result never comes: given up (the trainer drops a blocked one at 150)
constexpr float NearMargin=2.0f,AbandonRadius=120.0f;
using LogFn=void(*)(const char*);
// The integrator's side (trainer_glue.inc): ask the trainer for a trip (>= 0: its sequence number, else the trainer's error) and
// poll a trip's result (0 running / not known yet, 1 ok, 2 failed or lost).
struct Hooks {
    int (*requestPlace)(int place)=nullptr;
    int (*requestExit)(int a0)=nullptr;
    int (*pollTrip)(int seq)=nullptr;
};

class Doors {
public:
    enum class State : std::uint8_t {Idle,Choosing,Travelling,Inside,Leaving};
    Doors(LogFn log,Hooks hooks) noexcept:log_(log),hooks_(hooks) {}
    bool choosing() const noexcept {return state_==State::Choosing;}   // v975 (G2)

    // Every game frame from the Sandbox tick (Sandbox mode only). gameplay: the trainer's gate (player playing, no pause / cutscene /
    // switch / fade); without it the chooser closes and nothing new triggers, the icons stay.
    void tick(Natives& nat,std::uint32_t frame,bool gameplay) noexcept {
        if(state_==State::Choosing && gameplay)input(nat);
        if(started_ && frame-last_<CheckFrames)return;
        started_=true;last_=frame;++looks_;
        icons(nat);
        if(state_==State::Travelling || state_==State::Leaving)poll(frame);
        if(!gameplay){if(state_==State::Choosing)closeChooser(nat,"gate",true);return;}
        std::uint32_t r[3]{};
        if(!nat.call(n::PlayerPedId,nullptr,0,r) || !r[0])return;
        const std::uint32_t ped=r[0];
        std::uint32_t c[3]{};const std::uint32_t ca[2]{ped,1};
        if(!nat.call(n::GetEntityCoords,ca,2,c))return;
        const float x=asFloat(c[0]),y=asFloat(c[1]),z=asFloat(c[2]);
        if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))return;
        switch(state_) {
        case State::Idle:idle(nat,ped,x,y,z);break;
        case State::Choosing:if(!inArea(nat,ped,entrance().area) || inVehicle(nat,ped))closeChooser(nat,"walk-away",false);break;
        case State::Inside:inside(nat,ped,x,y,z);break;
        default:break;
        }
    }
    // Removes every icon of ours and closes the chooser (Sandbox teardown). Icons whose handle cannot be passed stay listed.
    void clear(Natives& nat,const char* why) noexcept {
        if(state_==State::Choosing)closeChooser(nat,why,false);
        unsigned removed=0;
        for(auto& s:slots_)if(s && remove(nat,s))++removed;
        logf("RENDER_SANDBOX property-door clear why=%s removed=%u left=%u",why,removed,count());
        state_=State::Idle;needLeave_=-1;
    }
    unsigned count() const noexcept {unsigned k=0;for(auto s:slots_)k+=s!=0;return k;}
    std::uint32_t handle(std::size_t building) const noexcept {return building<slots_.size()?slots_[building]:0;}
    State state() const noexcept {return state_;}
    int building() const noexcept {return b_;}
    int entranceIndex() const noexcept {return e_;}
    int selection() const noexcept {return sel_;}
    int place() const noexcept {return place_;}
    bool armed() const noexcept {return armed_;}
    unsigned added() const noexcept {return added_;}
    unsigned vanished() const noexcept {return vanished_;}
    std::uint32_t looks() const noexcept {return looks_;}
private:
    const Entrance& entrance() const noexcept {return Buildings[std::size_t(b_)].e[std::size_t(e_)];}
    static bool call(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a,std::uint32_t* out) noexcept {
        return nat.call(h,a.begin(),unsigned(a.size()),out);
    }
    static void call0(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {std::uint32_t o[3]{};(void)call(nat,h,a,o);}
    static bool ask(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {std::uint32_t o[3]{};return call(nat,h,a,o) && o[0];}
    template<class... A> void logf(const char* f,A... a) const noexcept {if(!log_)return;char l[256];std::snprintf(l,sizeof l,f,a...);log_(l);}
    static bool inArea(Natives& nat,std::uint32_t ped,const Area& a) noexcept {   // the game's own test, its arguments (0, 1, 1)
        return ask(nat,n::InAngledArea,{ped,bits(a.ax),bits(a.ay),bits(a.az),bits(a.bx),bits(a.by),bits(a.bz),bits(a.width),0,1,1});
    }
    static bool inVehicle(Natives& nat,std::uint32_t ped) noexcept {return ask(nat,n::IsPedInAnyVehicle,{ped,0});}
    static bool facing(Natives& nat,std::uint32_t ped,float target,float tol) noexcept {
        std::uint32_t o[3]{};if(!call(nat,n::GetEntityHeading,{ped},o))return false;
        return headingWithin(asFloat(o[0]),target,tol);
    }

    // ---- icons
    void icons(Natives& nat) noexcept {
        // existence: a few per look, round robin
        for(std::uint32_t k=0,seen=0;k<slots_.size() && seen<BlipChecksPerTick;++k) {
            cursor_=(cursor_+1)%slots_.size();
            std::uint32_t& s=slots_[cursor_];
            if(!s)continue;
            ++seen;
            if(!ask(nat,n::DoesBlipExist,{s})) {
                ++vanished_;
                if(vanished_<=8 || !(vanished_&(vanished_-1)))
                    logf("RENDER_SANDBOX property-door icon vanished building=%u handle=%08x vanished=%u",unsigned(Buildings[cursor_].id),s,vanished_);
                s=0;
            }
        }
        if(count()==slots_.size()){waits_=0;return;}
        char host[64];hostName(nat,host);
        const bool persistent=sandbox::contact::persistentHost(host);
        if(!persistent && ++waits_<FallbackRefreshes)return;   // wait for a session-long host
        unsigned made=0;
        for(std::size_t i=0;i<slots_.size() && made<CreatePerTick;++i)if(!slots_[i] && add(nat,i))++made;
        waits_=0;
        if(made)logf("RENDER_SANDBOX property-door icons added=%u total=%u host=%s%s",made,count(),host[0]?host:"?",persistent?"":" fallback=1");
    }
    bool add(Natives& nat,std::size_t i) noexcept {
        const Building& b=Buildings[i];std::uint32_t r[3]{};
        if(!call(nat,n::AddBlipForCoord,{bits(b.bx),bits(b.by),bits(b.bz)},r) || !r[0]){
            if(!addFailLogged_){addFailLogged_=true;logf("RENDER_SANDBOX property-door icon add-failed building=%u",unsigned(b.id));}
            return false;
        }
        const std::uint32_t h=r[0];
        call0(nat,n::SetBlipShortRange,{h,1});                    // freemode 0x238cf order: short range, sprite, colour, priority
        call0(nat,n::SetBlipSprite,{h,b.sprite});
        call0(nat,n::SetBlipColour,{h,ShortRangeColour});
        call0(nat,n::SetBlipPriority,{h,ShortRangePriority});
        slots_[i]=h;++added_;return true;
    }
    bool remove(Natives& nat,std::uint32_t& s) noexcept {
        const std::uint32_t slot=nat.word(s);
        if(!slot)return false;
        call0(nat,n::CanRemoveAnyBlip,{1});                        // as the game's scripts bracket it
        call0(nat,n::RemoveBlip,{slot});
        call0(nat,n::CanRemoveAnyBlip,{0});
        s=0;return true;
    }
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

    // ---- the doors
    void idle(Natives& nat,std::uint32_t ped,float x,float y,float z) noexcept {
        if(needLeave_>=0) {   // after a cancel, a failed trip or a trip back out: armed again only once out of that door's area
            const Entrance& e=Buildings[std::size_t(needLeave_/2)].e[std::size_t(needLeave_%2)];
            if(nearArea(e.area,x,y,z,NearMargin) && inArea(nat,ped,e.area))return;
            needLeave_=-1;
        }
        int best=-1;float bestD=1e30f;
        for(std::size_t i=0;i<Buildings.size();++i)for(std::uint8_t k=0;k<Buildings[i].ne;++k) {
            const Entrance& e=Buildings[i].e[k];
            if(!e.nplaces || !nearArea(e.area,x,y,z,NearMargin))continue;
            const float d=distTo(e.area,x,y,z);
            if(d<bestD){bestD=d;best=int(i*2+k);}
        }
        if(best<0)return;
        const Entrance& e=Buildings[std::size_t(best/2)].e[std::size_t(best%2)];
        if(!inArea(nat,ped,e.area) || inVehicle(nat,ped) || !facing(nat,ped,e.area.heading,EntryHeadingTolerance))return;
        b_=best/2;e_=best%2;sel_=0;state_=State::Choosing;
        // a button already held when the chooser opens (sprinting into the door) must be let go first
        for(std::size_t k=0;k<Watched.size();++k)held_[k]=ask(nat,n::IsControlPressed,{ControlPad,Watched[k]});
        logf("RENDER_SANDBOX property-door open building=%u entrance=%d kind=%s places=%u",unsigned(Buildings[std::size_t(b_)].id),e_,
             e.kind==Foot?"foot":"garage",unsigned(e.nplaces));
        drawMenu(nat);   // v977: the menu (replaces the old help line)
    }
    static constexpr std::array<std::uint32_t,4> Watched{CtlSelect,CtlBack,CtlUp,CtlDown};
    // v977 (owner: "the same button prompts as the rest of the game ... use gta online as a reference"): am_mp_property_ext 0x9aeb builds
    // its menu with the shop library (clear 0xa85f = hairdo 0x10789; NO banner setter), title MP_PROP_GEN1 "APARTMENT OPTIONS" /
    // MP_PROP_GEN1b "GARAGE OPTIONS", rows + DFLT_MNU_OPT "Exit", the CM_ITEM_COUNT counter, buttons (30 BB_SELECT, 31 BB_BACK) via 0x6648;
    // the labels are all in global.gxt2. Drawn every chooser frame by menudraw::Renderer with no header.
    static constexpr menudraw::Button MenuButtons[]{{30,"BB_SELECT"},{31,"BB_BACK"}};
    static constexpr const char* ExitRow="Exit";   // DFLT_MNU_OPT's text
    int rowCount() const noexcept {return int(entrance().nplaces)+1;}   // the places, then Exit
    void drawMenu(Natives& nat) noexcept {
        const Entrance& e=entrance();
        const int n=rowCount();
        for(int i=0;i<n && i<int(std::size(rows_));++i)
            rows_[std::size_t(i)]=menudraw::Row{i<int(e.nplaces)?PlaceLabels[std::size_t(e.places[std::size_t(i)])]:ExitRow};
        menudraw::Page pg;pg.header=nullptr;pg.title=e.kind==Foot?"APARTMENT OPTIONS":"GARAGE OPTIONS";
        pg.rows=rows_.data();pg.rowCount=n;pg.selected=sel_;pg.buttons=MenuButtons;pg.buttonCount=int(std::size(MenuButtons));
        if(drawer_.load(nat,nullptr))drawer_.draw(nat,pg);
    }
    void input(Natives& nat) noexcept {
        for(std::uint32_t c:ExclusiveControls)call0(nat,n::SetInputExclusive,{ControlPad,c});
        bool edge[4]{};
        for(std::size_t k=0;k<Watched.size();++k) {
            const bool p=ask(nat,n::IsControlPressed,{ControlPad,Watched[k]});
            edge[k]=p && !held_[k];held_[k]=p;
        }
        const Entrance& e=entrance();
        if(edge[1]){menudraw::Renderer::sound(nat,menudraw::Sound::Back);closeChooser(nat,"back",true);return;}
        if(edge[0] && sel_>=int(e.nplaces)){menudraw::Renderer::sound(nat,menudraw::Sound::Back);closeChooser(nat,"exit",true);return;}   // v977: Exit
        if(edge[0]) {
            menudraw::Renderer::sound(nat,menudraw::Sound::Select);   // v977
            place_=e.places[std::size_t(sel_)];
            const int seq=hooks_.requestPlace?hooks_.requestPlace(place_):-2;
            drawer_.close(nat,"enter");   // v977
            if(seq<0) {
                logf("RENDER_SANDBOX property-door enter-refused building=%u place=%d request=%d",unsigned(Buildings[std::size_t(b_)].id),place_,seq);
                state_=State::Idle;needLeave_=b_*2+e_;return;
            }
            seq_=seq;since_=last_;state_=State::Travelling;
            logf("RENDER_SANDBOX property-door enter building=%u entrance=%d place=%d label=%s request=%d",unsigned(Buildings[std::size_t(b_)].id),
                 e_,place_,PlaceLabels[std::size_t(place_)],seq);
            return;
        }
        if(edge[2] || edge[3]) {   // v977: up / down over the places and Exit (wrapping, as the shop library does)
            sel_=(sel_+(edge[3]?1:rowCount()-1))%rowCount();
            menudraw::Renderer::sound(nat,menudraw::Sound::UpDown);
        }
        drawMenu(nat);   // v977: every chooser frame
    }
    void closeChooser(Natives& nat,const char* why,bool mustLeave) noexcept {
        drawer_.close(nat,why);   // v977: releases CommonMenu + the buttons movie, resets the HUD component it moved
        logf("RENDER_SANDBOX property-door close building=%u why=%s",unsigned(Buildings[std::size_t(b_)].id),why);
        state_=State::Idle;needLeave_=mustLeave?b_*2+e_:-1;
    }
    void poll(std::uint32_t frame) noexcept {
        const int r=hooks_.pollTrip?hooks_.pollTrip(seq_):2;
        const bool leaving=state_==State::Leaving;
        if(r==0 && frame-since_<TripFramesMax)return;
        const unsigned bid=unsigned(Buildings[std::size_t(b_)].id);
        if(r==1) {
            logf("RENDER_SANDBOX property-door %s-ok building=%u place=%d request=%d",leaving?"leave":"enter",bid,place_,seq_);
            if(leaving){state_=State::Idle;needLeave_=b_*2+e_;}
            else {state_=State::Inside;armed_=false;}
            return;
        }
        logf("RENDER_SANDBOX property-door %s-failed building=%u place=%d request=%d%s",leaving?"leave":"enter",bid,place_,seq_,r==0?" timeout=1":"");
        if(leaving){state_=State::Inside;armed_=false;}
        else {state_=State::Idle;needLeave_=b_*2+e_;}
    }
    void inside(Natives& nat,std::uint32_t ped,float x,float y,float z) noexcept {
        const PlaceExit& p=PlaceExits[std::size_t(place_)];
        if(!p.valid || distTo(p.area,x,y,z)>AbandonRadius) {   // moved away by other means (another teleport, a respawn)
            logf("RENDER_SANDBOX property-door left-place building=%u place=%d",unsigned(Buildings[std::size_t(b_)].id),place_);
            state_=State::Idle;needLeave_=-1;return;
        }
        const bool in=nearArea(p.area,x,y,z,NearMargin) && inArea(nat,ped,p.area) && !inVehicle(nat,ped) &&
                      (p.headingTolerance<=0.f || facing(nat,ped,p.area.heading,p.headingTolerance));
        if(!in){if(!armed_){armed_=true;logf("RENDER_SANDBOX property-door armed place=%d",place_);}return;}
        if(!armed_)return;   // arrived standing in it: step out first
        const int a0=exitArg(b_,e_);
        const int seq=hooks_.requestExit?hooks_.requestExit(a0):-2;
        if(seq<0){logf("RENDER_SANDBOX property-door leave-refused place=%d request=%d",place_,seq);armed_=false;return;}
        seq_=seq;since_=last_;state_=State::Leaving;
        logf("RENDER_SANDBOX property-door leave building=%u place=%d a0=%d request=%d",unsigned(Buildings[std::size_t(b_)].id),place_,a0,seq);
    }

    LogFn log_;Hooks hooks_;
    std::array<std::uint32_t,Buildings.size()> slots_{};std::size_t cursor_=Buildings.size()-1;
    std::uint32_t last_=0,since_=0,waits_=0,looks_=0;unsigned added_=0,vanished_=0;bool started_=false,addFailLogged_=false;
    menudraw::Renderer drawer_;std::array<menudraw::Row,9> rows_{};   // v977 (an entrance has at most 8 places + Exit)
    State state_=State::Idle;int b_=0,e_=0,sel_=0,place_=0,seq_=0,needLeave_=-1;bool armed_=false;
    std::array<bool,4> held_{};
};
} // namespace recomp::propdoors
