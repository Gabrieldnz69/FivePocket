#pragma once
// car_keep.h (v1036, trainer-mods; lead decision after t-carnet-20261107-1815): the player's vehicle survives the local session's start.
// Observed: the switch to a network game (gip 0 -> 1) removes every non-networked vehicle, whoever owns it (t-carsp 1810: a trainer-spawned
// blista with owner=none; t-carin 1753: Michael INSIDE his released tailgater), and NETWORK_REGISTER_ENTITY_AS_NETWORKED before the edge
// does not take (t-carnet 1815: after=0). freemode's personal vehicles are created as networked entities, so this does the same:
//  - between Host and the edge (local_session::active(), gip 0), every SnapEvery frames: the vehicle the player is in, else his last one
//    within SnapRadius, with model, position, heading, colours, extra colours, plate, livery, dirt, window tint, the 49 mod slots, and
//    whether he drives it with the engine running;
//  - on the gip 0 -> 1 edge, if that vehicle is gone: REQUEST_MODEL, then CREATE_VEHICLE(model, x, y, z, heading, network 1, host check 0)
//    with the snapshot applied (SET_VEHICLE_MOD_KIT 0 + SET_VEHICLE_MOD per slot), the player seated as driver if he drove it, and
//    SET_MODEL_AS_NO_LONGER_NEEDED; at most LoadFramesMax frames for the model; every step logged;
//  - on the 1 -> 0 edge (the clean leave): t-carkeep-20261107-1847 showed the switch back clears non-networked vehicles too (the copy
//    gone). v1042: the player's vehicle is snapshotted during the session as well, and if the leave removed it while he DROVE it, it is
//    recreated the same way. A parked car he left is not recreated: its vehicle generator brings its own back later, as in the story,
//    and a second copy would duplicate it.
// Argument / result counts: vehicle_gen_controller's own NATIVE instructions (CREATE_VEHICLE 7 -> 1, GET / SET_VEHICLE_COLOURS 3 -> 0,
// GET / SET_VEHICLE_EXTRA_COLOURS 3 -> 0, GET_VEHICLE_NUMBER_PLATE_TEXT 1 -> 1 (char*), SET_VEHICLE_NUMBER_PLATE_TEXT 2 -> 0, GET_VEHICLE_LIVERY
// 1 -> 1, SET_VEHICLE_LIVERY 2 -> 0, GET / SET_VEHICLE_DIRT_LEVEL 1 -> 1 / 2 -> 0, GET_VEHICLE_MOD 2 -> 1, SET_VEHICLE_MOD 4 -> 0,
// SET_VEHICLE_MOD_KIT 2 -> 0, GET_IS_VEHICLE_ENGINE_RUNNING 1 -> 1, SET_VEHICLE_ENGINE_ON 3 -> 0, SET_PED_INTO_VEHICLE 3 -> 0, REQUEST_MODEL
// 1 -> 0, HAS_MODEL_LOADED 1 -> 1, SET_MODEL_AS_NO_LONGER_NEEDED 1 -> 0, GET_PED_IN_VEHICLE_SEAT 2 -> 1, GET / SET_VEHICLE_WINDOW_TINT 1 -> 1 /
// 2 -> 0, GET_ENTITY_HEADING 1 -> 1); all in natives/registrations.json. No content change: the same car, at the same place.
#include "trainer.h"
#include "local_session_flag.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::car_keep {
namespace n {
using recomp::trainer::joaat;
constexpr std::uint32_t Gip=joaat("NETWORK_IS_GAME_IN_PROGRESS"),PlayerPed=joaat("PLAYER_PED_ID"),
    VehicleOfPed=joaat("GET_VEHICLE_PED_IS_IN"),          // 2 -> 1
    LastVehicle=joaat("GET_PLAYERS_LAST_VEHICLE"),        // 0 -> 1
    Exists=joaat("DOES_ENTITY_EXIST"),                    // 1 -> 1
    Networked=joaat("NETWORK_GET_ENTITY_IS_NETWORKED"),   // 1 -> 1
    Model=joaat("GET_ENTITY_MODEL"),                      // 1 -> 1
    Coords=joaat("GET_ENTITY_COORDS"),                    // 2 -> 3
    Heading=joaat("GET_ENTITY_HEADING"),                  // 1 -> 1
    GetColours=joaat("GET_VEHICLE_COLOURS"),SetColours=joaat("SET_VEHICLE_COLOURS"),                // 3 -> 0
    GetExtra=joaat("GET_VEHICLE_EXTRA_COLOURS"),SetExtra=joaat("SET_VEHICLE_EXTRA_COLOURS"),        // 3 -> 0
    GetPlate=joaat("GET_VEHICLE_NUMBER_PLATE_TEXT"),SetPlate=joaat("SET_VEHICLE_NUMBER_PLATE_TEXT"), // 1 -> 1 / 2 -> 0
    GetLivery=joaat("GET_VEHICLE_LIVERY"),SetLivery=joaat("SET_VEHICLE_LIVERY"),                    // 1 -> 1 / 2 -> 0
    GetDirt=joaat("GET_VEHICLE_DIRT_LEVEL"),SetDirt=joaat("SET_VEHICLE_DIRT_LEVEL"),                // 1 -> 1 / 2 -> 0
    GetTint=joaat("GET_VEHICLE_WINDOW_TINT"),SetTint=joaat("SET_VEHICLE_WINDOW_TINT"),              // 1 -> 1 / 2 -> 0
    GetMod=joaat("GET_VEHICLE_MOD"),SetMod=joaat("SET_VEHICLE_MOD"),SetModKit=joaat("SET_VEHICLE_MOD_KIT"), // 2 -> 1 / 4 -> 0 / 2 -> 0
    EngineRunning=joaat("GET_IS_VEHICLE_ENGINE_RUNNING"),EngineOn=joaat("SET_VEHICLE_ENGINE_ON"),   // 1 -> 1 / 3 -> 0
    PedInSeat=joaat("GET_PED_IN_VEHICLE_SEAT"),IntoVehicle=joaat("SET_PED_INTO_VEHICLE"),           // 2 -> 1 / 3 -> 0
    RequestModel=joaat("REQUEST_MODEL"),ModelLoaded=joaat("HAS_MODEL_LOADED"),ModelDone=joaat("SET_MODEL_AS_NO_LONGER_NEEDED"),
    Create=joaat("CREATE_VEHICLE");                       // 7 -> 1
}
inline constexpr std::uint32_t UsedNatives[]{n::Gip,n::PlayerPed,n::VehicleOfPed,n::LastVehicle,n::Exists,n::Networked,n::Model,n::Coords,n::Heading,
    n::GetColours,n::SetColours,n::GetExtra,n::SetExtra,n::GetPlate,n::SetPlate,n::GetLivery,n::SetLivery,n::GetDirt,n::SetDirt,n::GetTint,n::SetTint,
    n::GetMod,n::SetMod,n::SetModKit,n::EngineRunning,n::EngineOn,n::PedInSeat,n::IntoVehicle,n::RequestModel,n::ModelLoaded,n::ModelDone,n::Create};
constexpr std::uint32_t SnapEvery=30,LoadFramesMax=300;constexpr int ModSlots=49;constexpr float SnapRadius=30.0f;
using LogFn=void(*)(const char*);

struct Snapshot {
    bool valid=false,driver=false,engine=false;std::uint32_t veh=0,model=0;float x=0,y=0,z=0,heading=0,dirt=0;
    std::uint32_t c1=0,c2=0,pearl=0,wheel=0;int livery=-1,tint=-1;char plate[12]{};int mods[ModSlots]{};
};

class Keeper {
public:
    explicit Keeper(LogFn log) noexcept:log_(log) {}
    // Every Sandbox tick. Outside the local session with nothing in flight: one atomic load, no native.
    void tick(recomp::trainer::Natives& nat,std::uint32_t frame) noexcept {
        const bool session=::recomp::local_session::active();
        if(!log_ || (!session && st_==State::Idle && !watching_ && !(snap_.valid && lastGip_==1u)))return;   // v1042: a session snapshot waits for the leave
        const std::uint32_t gip=one(nat,n::Gip,{});
        const bool edgeUp=started_ && gip==1u && lastGip_!=1u,edgeDown=started_ && gip!=1u && lastGip_==1u;
        started_=true;lastGip_=gip;
        if(edgeDown && (watching_ || snap_.valid)){   // the clean leave
            const std::uint32_t v=snap_.valid?snap_.veh:copy_;
            const int ex=v?int(one(nat,n::Exists,{v})):-1;
            say("RENDER_ONLINE_LOCAL car-keep after-leave vehicle=%u exists=%d driver=%d",v,ex,snap_.driver?1:0);
            watching_=false;copy_=0;
            if(ex!=1 && snap_.valid && snap_.driver) {   // v1042: he drove it: bring it back
                call(nat,n::RequestModel,{snap_.model});st_=State::Loading;loadStart_=frame;leaving_=true;
                say("RENDER_ONLINE_LOCAL car-keep recreate model=%08x at %.1f,%.1f,%.1f (vehicle %u removed by the leave)",snap_.model,
                    double(snap_.x),double(snap_.y),double(snap_.z),v);
                return;
            }
            snap_=Snapshot{};return;
        }
        if(st_==State::Loading){load(nat,frame);return;}
        if(!session)return;
        if(gip==0u){if(!snapped_ || frame-lastSnap_>=SnapEvery){lastSnap_=frame;snapped_=true;snapshot(nat);}return;}
        if(!edgeUp && frame-lastSnap_>=SnapEvery){lastSnap_=frame;snapshot(nat);}   // v1042: in the session too (the leave clears again)
        if(edgeUp && snap_.valid) {
            if(one(nat,n::Exists,{snap_.veh})==1u){say("RENDER_ONLINE_LOCAL car-keep kept vehicle=%u (still exists after the edge)",snap_.veh);
                copy_=snap_.veh;watching_=true;return;}
            call(nat,n::RequestModel,{snap_.model});st_=State::Loading;loadStart_=frame;
            say("RENDER_ONLINE_LOCAL car-keep recreate model=%08x at %.1f,%.1f,%.1f (vehicle %u removed by the network start)",snap_.model,
                double(snap_.x),double(snap_.y),double(snap_.z),snap_.veh);
        }
    }
    const Snapshot& snapshotForTest() const noexcept {return snap_;}
    std::uint32_t copy() const noexcept {return copy_;}
    bool busy() const noexcept {return st_==State::Loading;}   // v1043: the curtain waits for a recreate in flight
private:
    enum class State : std::uint8_t {Idle,Loading};
    void snapshot(recomp::trainer::Natives& nat) noexcept {
        const std::uint32_t ped=one(nat,n::PlayerPed,{});
        std::uint32_t v=one(nat,n::VehicleOfPed,{ped,0u});bool in=valid(v);
        if(!in){v=one(nat,n::LastVehicle,{});if(!valid(v) || one(nat,n::Exists,{v})!=1u){snap_.valid=false;return;}}
        std::uint32_t p[3]{},c[3]{};pos(nat,ped,p);pos(nat,v,c);
        if(!in){const float dx=f(p[0])-f(c[0]),dy=f(p[1])-f(c[1]),dz=f(p[2])-f(c[2]);if(dx*dx+dy*dy+dz*dz>SnapRadius*SnapRadius){snap_.valid=false;return;}}
        Snapshot s;s.valid=true;s.veh=v;s.model=one(nat,n::Model,{v});s.x=f(c[0]);s.y=f(c[1]);s.z=f(c[2]);s.heading=f(one(nat,n::Heading,{v}));
        s.driver=in && one(nat,n::PedInSeat,{v,0xFFFFFFFFu})==ped;s.engine=one(nat,n::EngineRunning,{v})!=0;
        pair(nat,n::GetColours,v,s.c1,s.c2);pair(nat,n::GetExtra,v,s.pearl,s.wheel);
        s.livery=int(one(nat,n::GetLivery,{v}));s.tint=int(one(nat,n::GetTint,{v}));s.dirt=f(one(nat,n::GetDirt,{v}));
        if(const std::uint32_t pl=one(nat,n::GetPlate,{v}))guestString(nat,pl,s.plate,sizeof s.plate);
        for(int k=0;k<ModSlots;++k)s.mods[k]=int(one(nat,n::GetMod,{v,std::uint32_t(k)}));
        const bool changed=!snap_.valid || snap_.veh!=s.veh;
        snap_=s;
        if(changed){int m=0;for(int k=0;k<ModSlots;++k)m+=s.mods[k]>=0;
            say("RENDER_ONLINE_LOCAL car-keep snapshot vehicle=%u model=%08x driver=%d engine=%d colours=%u,%u extra=%u,%u livery=%d tint=%d plate=%s mods=%d",
                s.veh,s.model,s.driver?1:0,s.engine?1:0,s.c1,s.c2,s.pearl,s.wheel,s.livery,s.tint,s.plate,m);}
    }
    void load(recomp::trainer::Natives& nat,std::uint32_t frame) noexcept {
        if(one(nat,n::ModelLoaded,{snap_.model})!=1u) {
            if(frame-loadStart_<LoadFramesMax){call(nat,n::RequestModel,{snap_.model});return;}
            say("RENDER_ONLINE_LOCAL car-keep fail model=%08x not loaded in %u frames",snap_.model,LoadFramesMax);
            call(nat,n::ModelDone,{snap_.model});st_=State::Idle;return;
        }
        // v1038 (t-carkeep-20261107-1833: CREATE_VEHICLE with network 1 returned 0 — the trainer's host thread is no network script):
        // the networked create first, then a plain one (after the switch's one-time clear), whichever the game accepts; logged.
        std::uint32_t v=0;int net=-1;
        for(std::uint32_t isNet:{1u,0u}){
            const std::uint32_t a[7]{snap_.model,bits(snap_.x),bits(snap_.y),bits(snap_.z),bits(snap_.heading),isNet,0u};std::uint32_t out[3]{};
            v=nat.call(n::Create,a,7,out)?out[0]:0u;
            if(valid(v)){net=int(isNet);break;}
            say("RENDER_ONLINE_LOCAL car-keep CREATE_VEHICLE network=%u returned %u",isNet,v);
        }
        st_=State::Idle;
        if(!valid(v)){say("RENDER_ONLINE_LOCAL car-keep fail CREATE_VEHICLE model=%08x (networked and plain)",snap_.model);call(nat,n::ModelDone,{snap_.model});return;}
        createdNet_=net;
        call(nat,n::SetColours,{v,snap_.c1,snap_.c2});call(nat,n::SetExtra,{v,snap_.pearl,snap_.wheel});
        if(snap_.livery>=0)call(nat,n::SetLivery,{v,std::uint32_t(snap_.livery)});
        if(snap_.tint>=0)call(nat,n::SetTint,{v,std::uint32_t(snap_.tint)});
        call(nat,n::SetDirt,{v,bits(snap_.dirt)});
        if(snap_.plate[0])if(const std::uint32_t t=nat.text(snap_.plate))call(nat,n::SetPlate,{v,t});
        call(nat,n::SetModKit,{v,0u});int m=0;
        for(int k=0;k<ModSlots;++k)if(snap_.mods[k]>=0){call(nat,n::SetMod,{v,std::uint32_t(k),std::uint32_t(snap_.mods[k]),0u});++m;}
        if(snap_.driver){call(nat,n::IntoVehicle,{one(nat,n::PlayerPed,{}),v,0xFFFFFFFFu});if(snap_.engine)call(nat,n::EngineOn,{v,1u,1u});}
        call(nat,n::ModelDone,{snap_.model});
        copy_=v;watching_=!leaving_;
        if(!leaving_)snap_.veh=v;   // v1042: the copy is now the player's vehicle (the leave checks it until the next session snapshot)
        say("RENDER_ONLINE_LOCAL car-keep restored vehicle=%u created_network=%d networked=%d driver=%d mods=%d plate=%s frames=%u%s",v,createdNet_,
            int(one(nat,n::Networked,{v})),snap_.driver?1:0,m,snap_.plate,frame-loadStart_,leaving_?" (after the leave)":"");
        if(leaving_){leaving_=false;snap_=Snapshot{};}
    }
    static bool valid(std::uint32_t v) noexcept {return v && v!=0xFFFFFFFFu;}
    static float f(std::uint32_t w) noexcept {float x;std::memcpy(&x,&w,4);return x;}
    static std::uint32_t bits(float x) noexcept {std::uint32_t w;std::memcpy(&w,&x,4);return w;}
    static std::uint32_t one(recomp::trainer::Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};return nat.call(h,a.begin(),unsigned(a.size()),out)?out[0]:0xFFFFFFFFu;}
    static void call(recomp::trainer::Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {std::uint32_t out[3]{};(void)nat.call(h,a.begin(),unsigned(a.size()),out);}
    static void pos(recomp::trainer::Natives& nat,std::uint32_t e,std::uint32_t out[3]) noexcept {const std::uint32_t a[2]{e,1u};(void)nat.call(n::Coords,a,2,out);}
    static void pair(recomp::trainer::Natives& nat,std::uint32_t h,std::uint32_t v,std::uint32_t& x,std::uint32_t& y) noexcept {   // two out pointers
        const std::uint32_t px=nat.word(0),py=nat.word(0);if(!px || !py)return;
        const std::uint32_t a[3]{v,px,py};std::uint32_t out[3]{};if(nat.call(h,a,3,out)){x=nat.load(px);y=nat.load(py);}
    }
    static void guestString(recomp::trainer::Natives& nat,std::uint32_t at,char* out,std::size_t cap) noexcept {
        std::size_t k=0;
        for(;k+1<cap;++k){const std::uint32_t ad=at+std::uint32_t(k),w=nat.load(ad&~3u);const char c=char((w>>(24-8*(ad&3u)))&0xFFu);
            if(!c)break;out[k]=(c>=32 && c<127)?c:'?';}
        out[k]=0;
    }
    template<class... A> void say(const char* fmt,A... a) const noexcept {char l[300];std::snprintf(l,sizeof l,fmt,a...);log_(l);}
    LogFn log_;State st_=State::Idle;Snapshot snap_{};bool started_=false,snapped_=false,watching_=false;
    std::uint32_t lastGip_=0,lastSnap_=0,loadStart_=0,copy_=0;int createdNet_=-1;   // v1038
    bool leaving_=false;   // v1042: the recreate in flight is the leave's
};
}
