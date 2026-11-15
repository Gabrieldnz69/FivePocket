#pragma once
// session_env.h (v1018 read only; v1019 natives recorded; v1020 the clock / weather hold + the player / camera fields).
// t-senv-20261107-1444 (v1019): before Host the clock ran 04:35 -> 05:08 (SMOG -> EXTRASUNNY); at the gip edge the weather stepped
// (EXTRASUNNY -> CLEAR) and within 150 frames the clock read 17:08 (+12 h, minutes continuous); it stayed 17:xx after the leave. The
// player was out of every in-session shot. Lead (3): the session keeps the player's clock and weather (Sandbox continuity).
// Logged only while the L1 run is armed (files/private-trace/diag-online-l1): one line every EnvFrames, and after each gip edge one line
// every BurstFrames for BurstLines lines (to see what moves the player / camera). Each line: gip, clock, prev / next weather, rain, the
// player ped, its position and IS_ENTITY_VISIBLE, the gameplay camera position.
// v1020 hold (only on the 0 -> 1 gip edge while the local L1 session is up, recomp::local_session::active()): the last pre-edge clock
// (h, m, s) is put back with NETWORK_OVERRIDE_CLOCK_TIME (freemode's own clock call, 0xe0960: 3 -> 0) and the last pre-edge weather
// (prev) with SET_OVERRIDE_WEATHER(name) (freemode 0xd8f98: 1 -> 0; the handler sub_82D663A0 hashes a char* name via sub_823C0348 and
// looks it up in the weather table, refusing an unknown one). While held, a reading more than DriftMinutes away from the held time
// re-applies it (at most MaxReapply times, each logged). On the 1 -> 0 edge: the current (held) clock is read,
// NETWORK_CLEAR_CLOCK_TIME_OVERRIDE (freemode 0x62107: 0 -> 0), SET_CLOCK_TIME(h, m, s) (agency_heist3b: 3 -> 0), CLEAR_OVERRIDE_WEATHER
// (am_armwrestling: 0 -> 0). A weather hash outside the name table is not overridden (logged).
// Other counts: GET_CLOCK_HOURS / MINUTES (freemode, 0 -> 1), GET_CLOCK_SECONDS (abigail1, 0 -> 1), GET_PREV / NEXT_WEATHER_TYPE_HASH_NAME
// (agency_heist3b / appinternet, 0 -> 1), GET_RAIN_LEVEL (launcher_tennis, 0 -> 1), PLAYER_PED_ID (0 -> 1), GET_ENTITY_COORDS (2 -> 3),
// IS_ENTITY_VISIBLE (1 -> 1), GET_GAMEPLAY_CAM_COORD (0 -> 3) (freemode); all in natives/registrations.json.
#include "trainer.h"
#include "local_session_flag.h"
#include "online_clock.h"   // v1100: Online mode's clock (lead 12:21)
#include <ctime>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::session_env {
namespace n {
using recomp::trainer::joaat;
constexpr std::uint32_t Hours=joaat("GET_CLOCK_HOURS"),        // 0 -> 1
    Minutes=joaat("GET_CLOCK_MINUTES"),                          // 0 -> 1
    Seconds=joaat("GET_CLOCK_SECONDS"),                          // 0 -> 1
    PrevWeather=joaat("GET_PREV_WEATHER_TYPE_HASH_NAME"),        // 0 -> 1
    NextWeather=joaat("GET_NEXT_WEATHER_TYPE_HASH_NAME"),        // 0 -> 1
    Rain=joaat("GET_RAIN_LEVEL"),                                // 0 -> 1 (float)
    Gip=joaat("NETWORK_IS_GAME_IN_PROGRESS"),                    // 0 -> 1
    PlayerPed=joaat("PLAYER_PED_ID"),                            // 0 -> 1
    EntityCoords=joaat("GET_ENTITY_COORDS"),                     // 2 -> 3 (entity, 1)
    EntityVisible=joaat("IS_ENTITY_VISIBLE"),                    // 1 -> 1
    CamCoord=joaat("GET_GAMEPLAY_CAM_COORD"),                    // 0 -> 3
    OverrideClock=joaat("NETWORK_OVERRIDE_CLOCK_TIME"),          // 3 -> 0 (h, m, s)
    ClearClock=joaat("NETWORK_CLEAR_CLOCK_TIME_OVERRIDE"),       // 0 -> 0
    SetClock=joaat("SET_CLOCK_TIME"),                            // 3 -> 0 (h, m, s)
    OverrideWeather=joaat("SET_OVERRIDE_WEATHER"),               // 1 -> 0 (char* name)
    ClearWeather=joaat("CLEAR_OVERRIDE_WEATHER"),                // 0 -> 0
// v1022 (t-shold-20261107-1502: in session the shots show another view, no Michael, while his position, IS_ENTITY_VISIBLE and the GAMEPLAY
// camera position never changed): which camera renders. GET_RENDERING_CAM sub_82D586B8 () -> cam (-1 = gameplay), IS_GAMEPLAY_CAM_RENDERING
// sub_82D595B0 () -> BOOL (online_intro.h, read from the handlers); GET_FINAL_RENDERED_CAM_COORD 0 -> 3, IS_ENTITY_ON_SCREEN 1 -> 1,
// IS_PLAYER_CONTROL_ON 1 -> 1 (player), NETWORK_IS_IN_MP_CUTSCENE 0 -> 1, PLAYER_ID 0 -> 1 (freemode).
    RenderingCam=joaat("GET_RENDERING_CAM"),                     // 0 -> 1
    GameplayRendering=joaat("IS_GAMEPLAY_CAM_RENDERING"),        // 0 -> 1
    FinalCamCoord=joaat("GET_FINAL_RENDERED_CAM_COORD"),         // 0 -> 3
    OnScreen=joaat("IS_ENTITY_ON_SCREEN"),                       // 1 -> 1
    ControlOn=joaat("IS_PLAYER_CONTROL_ON"),                     // 1 -> 1
    MpCutscene=joaat("NETWORK_IS_IN_MP_CUTSCENE"),               // 0 -> 1
    CutsceneActive=joaat("IS_CUTSCENE_ACTIVE"),                  // 0 -> 1 (v1053: the hold stands down)
    PlayerId=joaat("PLAYER_ID"),                                 // 0 -> 1
// v1030 (lead: the black sedan parked by Michael is gone once the session starts, t-route-20261107-1700 replica-open vs real-open):
// the closest vehicle within CarRadius is captured once before the gip edge and then followed by its handle on every line. Counts from
// vehicle_gen_controller's own NATIVE instructions (GET_CLOSEST_VEHICLE 0x14c11: x, y, z, 200.0, model, 0x87e).
    ClosestVehicle=joaat("GET_CLOSEST_VEHICLE"),                 // 6 -> 1
    EntityExists=joaat("DOES_ENTITY_EXIST"),                     // 1 -> 1
    MissionEntity=joaat("IS_ENTITY_A_MISSION_ENTITY"),           // 1 -> 1
    EntityModel=joaat("GET_ENTITY_MODEL"),                       // 1 -> 1
// v1031 (t-car-20261107-1731: the car, a mission entity (tailgater, Michael's), is gone from the first gip=1 line): which script owns it.
// GET_ENTITY_SCRIPT (cheat_controller's own NATIVE: 2 -> 1: entity, &instance -> char* name), NETWORK_GET_ENTITY_IS_NETWORKED (1 -> 1).
    EntityScript=joaat("GET_ENTITY_SCRIPT"),                     // 2 -> 1
    EntityNetworked=joaat("NETWORK_GET_ENTITY_IS_NETWORKED"),    // 1 -> 1
// v1034 (t-carin-20261107-1753: Michael INSIDE his tailgater, released by vehicle_gen_controller (owner=none, mission=0), and the car is
// still deleted on the gip edge from under him): the player's vehicle is tracked first, and a developer switch (diag-car-net) registers it
// as networked once between Host and the edge. GET_VEHICLE_PED_IS_IN (2 -> 1, ped, 0), GET_PLAYERS_LAST_VEHICLE (abigail1 0 -> 1),
// NETWORK_REGISTER_ENTITY_AS_NETWORKED (golf_mp 1 -> 0).
    VehicleOfPed=joaat("GET_VEHICLE_PED_IS_IN"),                 // 2 -> 1
    LastVehicle=joaat("GET_PLAYERS_LAST_VEHICLE"),               // 0 -> 1
    RegisterNetworked=joaat("NETWORK_REGISTER_ENTITY_AS_NETWORKED"); // 1 -> 0
}
// v1019: the glue records a handler only for hashes in a module's UsedNatives (trainer_glue.inc nativeHandler / the record).
inline constexpr std::uint32_t UsedNatives[]{n::Hours,n::Minutes,n::Seconds,n::PrevWeather,n::NextWeather,n::Rain,n::Gip,n::PlayerPed,
    n::EntityCoords,n::EntityVisible,n::CamCoord,n::OverrideClock,n::ClearClock,n::SetClock,n::OverrideWeather,n::ClearWeather,
    n::RenderingCam,n::GameplayRendering,n::FinalCamCoord,n::OnScreen,n::ControlOn,n::MpCutscene,n::PlayerId,n::CutsceneActive,   // v1022; v1053 CutsceneActive
    n::ClosestVehicle,n::EntityExists,n::MissionEntity,n::EntityModel,   // v1030
    n::EntityScript,n::EntityNetworked,   // v1031
    n::VehicleOfPed,n::LastVehicle,n::RegisterNetworked};   // v1034
// The weather type names (their joaat is what GET_PREV_WEATHER_TYPE_HASH_NAME returns; SMOG / EXTRASUNNY / CLEAR seen on the Thor).
inline constexpr const char* WeatherNames[]{"EXTRASUNNY","CLEAR","CLOUDS","SMOG","FOGGY","OVERCAST","RAIN","THUNDER","CLEARING","NEUTRAL",
    "SNOW","BLIZZARD","SNOWLIGHT","XMAS"};
inline const char* weatherName(std::uint32_t h) noexcept {for(auto w:WeatherNames)if(recomp::trainer::joaat(w)==h)return w;return nullptr;}
constexpr std::uint32_t EnvFrames=150,BurstFrames=15,BurstLines=20;
constexpr int DriftMinutes=10;constexpr unsigned MaxReapply=20;
// v1053 (lead's review of t-fmil3-20261107-1936: the hold reset the clock 20 times during fm_intro, day / night flips mid-shot, blown-out
// frames): the hold exists only against the network clock jump at the session edge (t-senv: +12 h within 150 frames of the edge). It may
// re-apply only within ReapplyWindowFrames after the edge, and it stands down for good (follows the game's clock from then on, logged once)
// as soon as a cutscene (IS_CUTSCENE_ACTIVE), an MP cutscene (NETWORK_IS_IN_MP_CUTSCENE) or a script that sets the clock itself (the glue:
// fm_intro running) is in control. The release at the leave hands back the clock as it then reads once stood down.
constexpr std::uint32_t ReapplyWindowFrames=300;
using LogFn=void(*)(const char*);
class Logger {
public:
    explicit Logger(LogFn log) noexcept:log_(log) {}
    // Every Sandbox tick. v1023 (lead after t-shold 1502: the session is frame-neutral with the hold; make it part of the session path):
    // the HOLD runs whenever the local L1 session is up (recomp::local_session::active(), set only when L1's Host returned 1 in this
    // process) or still held, with or without the diagnostic switch; the env LINES stay diagnostic (armed = diag-online-l1). No session,
    // not held, not armed: returns at once (one atomic load, no native). The pre-edge reading is taken on every tick between Host and the
    // gip edge (fresh; v1020 used the last 150-frame line), and the hold applies on the first tick with gip 1.
    void setCarNet(bool on) noexcept {carNet_=on;}
    // v1100 (lead 12:21): play_mode=online only: the hold takes GTA Online's clock derived from the device's real time (online_clock.h: one game
    // minute per 2 real seconds, mod 24 h) instead of the pre-edge Sandbox clock; the weather and Story / Sandbox are unchanged.
    void setOnline(bool on) noexcept {online_=on;}   // v1034: the developer switch diag-car-net (glue, read once)
    void tick(recomp::trainer::Natives& nat,std::uint32_t frame,bool armed,bool scriptOwnsClock=false) noexcept {
        const bool session=::recomp::local_session::active();
        if(log_ && online_ && !early_)earlyClock(nat,session);   // v1115: GTA Online's clock from the start (before the session)
        if(!log_ || (!armed && !session && !held_))return;
        const std::uint32_t gip=one(nat,n::Gip);
        const bool change=started_ && gip!=lastGip_;
        started_=true;lastGip_=gip;
        if(change)burst_=BurstLines;
        if(held_ && (gip!=1u || !session)) {   // the leave (gip 0) or the session gone: hand the player's clock back
            const Reading r=read(nat);release(nat,r);have_=false;
            if(armed){line(nat,frame,gip,change,r);last_=frame;}
            return;
        }
        if(session && !held_ && gip==1u) {   // the edge
            const Reading r=read(nat);
            if(!have_){snap_=r;have_=true;log_("RENDER_ONLINE_LOCAL env hold no pre-edge reading (the edge reading is held)");}
            apply(nat,r);edgeFrame_=frame;standDown_=false;
            if(armed){line(nat,frame,gip,change,r);last_=frame;}
            return;
        }
        if(session && !held_ && gip==0u){snap_=read(nat);have_=true;
            if(carNet_ && !netTried_)registerCar(nat);}   // v1034 (developer switch): once, between Host and the edge
        if(held_ && !standDown_) {   // v1053: every tick, cheap: the stand-down conditions
            const char* why=scriptOwnsClock?"fm_intro":one(nat,n::CutsceneActive)==1u?"cutscene":one(nat,n::MpCutscene)==1u?"mp-cutscene":
                            frame-edgeFrame_>=ReapplyWindowFrames?"window":nullptr;
            if(why){standDown_=true;char l[120];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL env hold stands down why=%s frames=%u reapplied=%u (the game's clock from now on)",why,frame-edgeFrame_,reapplied_);log_(l);}
        }
        if(!held_ && !armed)return;
        if(!(change || !logged_ || frame-last_>=(burst_?BurstFrames:EnvFrames)))return;
        if(!change && burst_)--burst_;
        last_=frame;logged_=true;
        const Reading r=read(nat);
        if(held_ && !standDown_)keep(nat,r);
        else if(held_){want_.h=r.h;want_.m=r.m;want_.s=r.s;}   // v1053: stood down: follow
        if(armed)line(nat,frame,gip,change,r);
    }
    bool held() const noexcept {return held_;}
    bool earlyClockSet() const noexcept {return early_ && earlyOk_;}
private:
    // v1115 (lead 20:1x: "GTA Online's own clock rule instead of the Sandbox 04:42 clock (clouds)"). OBSERVED t-onlroute-ugc-1824: Online mode
    // read the Story save's 04:42 until the session edge, where v1100's hold set the Online clock (06:40) - everything before the edge (the
    // maintransition sky / clouds) showed 04:42 + SMOG night-blue. Now: play_mode=online only, once per process, on the first tick where the
    // world is loaded (PLAYER_PED_ID != 0) and no session / hold exists yet, SET_CLOCK_TIME to online_clock::fromEpoch(device time) - the same
    // rule the edge hold applies, so the clock is continuous across the edge. A session already up -> nothing (the edge hold owns it). Weather
    // unchanged; Story / Sandbox never reach this (online_ false).
    void earlyClock(recomp::trainer::Natives& nat,bool session) noexcept {
        if(session || held_){early_=true;return;}
        const std::uint32_t ped=one(nat,n::PlayerPed);
        if(ped==0u || ped==0xFFFFFFFFu)return;   // the world is not loaded yet: the save's clock would overwrite ours
        early_=true;
        const Reading r=read(nat);const auto t=::recomp::online_clock::fromEpoch(std::int64_t(::time(nullptr)));
        earlyOk_=set3(nat,n::SetClock,t.h,t.m,t.s);
        char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL online clock set early=%02u:%02u:%02u ok=%d (before the session; was %02u:%02u, GTA Online rate from device time)",
                                  t.h,t.m,t.s,earlyOk_?1:0,r.h,r.m);log_(l);
    }
    bool early_=false,earlyOk_=false;
    struct Reading {std::uint32_t h=0,m=0,s=0,prev=0,next=0;};
    static int mod(const Reading& r) noexcept {return int(r.h)*60+int(r.m);}
    static int drift(int a,int b) noexcept {int d=a-b;d%=1440;if(d<0)d+=1440;return d>720?1440-d:d;}
    Reading read(recomp::trainer::Natives& nat) noexcept {
        Reading r;r.h=one(nat,n::Hours);r.m=one(nat,n::Minutes);r.s=one(nat,n::Seconds);r.prev=one(nat,n::PrevWeather);r.next=one(nat,n::NextWeather);
        return r;
    }
    void apply(recomp::trainer::Natives& nat,const Reading& now) noexcept {
        if(online_){const auto t=::recomp::online_clock::fromEpoch(std::int64_t(::time(nullptr)));snap_.h=t.h;snap_.m=t.m;snap_.s=t.s;   // v1100
                    log_("RENDER_ONLINE_LOCAL env hold clock=online-realtime (GTA Online rate, device time; the weather stays the pre-edge one)");}
        held_=true;want_=snap_;reapplied_=0;
        const bool c=set3(nat,n::OverrideClock,want_.h,want_.m,want_.s);
        const char* w=weatherName(want_.prev);
        bool wk=false;if(w){const std::uint32_t t=nat.text(w);wk=t && call(nat,n::OverrideWeather,&t,1);}
        char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL env hold apply clock=%02u:%02u:%02u ok=%d weather=%s ok=%d (read at the edge %02u:%02u prev=%08x)",
                                  want_.h,want_.m,want_.s,c?1:0,w?w:"unknown",wk?1:0,now.h,now.m,now.prev);log_(l);
    }
    void keep(recomp::trainer::Natives& nat,const Reading& r) noexcept {
        if(online_){const auto t=::recomp::online_clock::fromEpoch(std::int64_t(::time(nullptr)));want_.h=t.h;want_.m=t.m;want_.s=t.s;}   // v1100: the real rate
        if(drift(mod(r),mod(want_))<=DriftMinutes){want_.h=r.h;want_.m=r.m;want_.s=r.s;return;}   // natural progress (or frozen): follow it
        if(reapplied_>=MaxReapply)return;
        ++reapplied_;const bool c=set3(nat,n::OverrideClock,want_.h,want_.m,want_.s);
        char l[160];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL env hold reapply #%u read=%02u:%02u want=%02u:%02u ok=%d",reapplied_,r.h,r.m,want_.h,want_.m,c?1:0);log_(l);
    }
    void release(recomp::trainer::Natives& nat,const Reading& r) noexcept {
        held_=false;
        const Reading back=drift(mod(r),mod(want_))<=DriftMinutes?r:want_;   // the held time, not a network time that slipped in
        const bool a=call(nat,n::ClearClock,nullptr,0),b=set3(nat,n::SetClock,back.h,back.m,back.s),c=call(nat,n::ClearWeather,nullptr,0);
        char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL env hold release clock=%02u:%02u:%02u clear_clock=%d set_clock=%d clear_weather=%d (read %02u:%02u)",
                                  back.h,back.m,back.s,a?1:0,b?1:0,c?1:0,r.h,r.m);log_(l);
    }
    void line(recomp::trainer::Natives& nat,std::uint32_t frame,std::uint32_t gip,bool change,const Reading& r) noexcept {
        float rain;const std::uint32_t rw=one(nat,n::Rain);std::memcpy(&rain,&rw,4);
        const std::uint32_t ped=one(nat,n::PlayerPed);
        std::uint32_t pos[3]{},cam[3]{};const std::uint32_t pa[2]{ped,1u};
        (void)nat.call(n::EntityCoords,pa,2,pos);(void)nat.call(n::CamCoord,nullptr,0,cam);
        const std::uint32_t vis=one1(nat,n::EntityVisible,ped);
        std::uint32_t fin[3]{};(void)nat.call(n::FinalCamCoord,nullptr,0,fin);   // v1022
        const std::uint32_t rcam=one(nat,n::RenderingCam),grend=one(nat,n::GameplayRendering),onscr=one1(nat,n::OnScreen,ped),
            ctl=one1(nat,n::ControlOn,one(nat,n::PlayerId)),mpcut=one(nat,n::MpCutscene);
        if(gip==0u && !netTried_) {   // v1034: before the edge the player's vehicle wins (in it, else his last one), else the closest car
            const std::uint32_t pa2[2]{ped,0u};std::uint32_t vo[3]{};
            std::uint32_t mine=(nat.call(n::VehicleOfPed,pa2,2,vo) && vo[0] && vo[0]!=0xFFFFFFFFu)?vo[0]:0u;
            if(!mine){const std::uint32_t lv=one(nat,n::LastVehicle);if(lv && lv!=0xFFFFFFFFu && one1(nat,n::EntityExists,lv)==1u)mine=lv;}
            if(mine)car_=mine;else if(!car_)car_=closestCar(nat,pos);
        }
        int cExists=-1,cMission=-1;std::uint32_t cModel=0,cPos[3]{};
        if(car_){cExists=int(one1(nat,n::EntityExists,car_));if(cExists==1){cMission=int(one1(nat,n::MissionEntity,car_));cModel=one1(nat,n::EntityModel,car_);
            const std::uint32_t ca[2]{car_,1u};(void)nat.call(n::EntityCoords,ca,2,cPos);}}
        char owner[40]="-";int inst=-1,netd=-1;
        if(cExists==1){   // v1031: the owning script's name (a guest char*, read back word by word) and whether the car is networked
            const std::uint32_t slot=nat.word(0xFFFFFFFFu);std::uint32_t out[3]{};const std::uint32_t a2[2]{car_,slot};
            if(slot && nat.call(n::EntityScript,a2,2,out) && out[0]){guestString(nat,out[0],owner,sizeof owner);inst=int(nat.load(slot));}
            else if(slot)std::snprintf(owner,sizeof owner,"none");
            netd=int(one1(nat,n::EntityNetworked,car_));}
        char cl[200];std::snprintf(cl,sizeof cl," car=%u exists=%d mission=%d model=%08x carpos=%.1f,%.1f,%.1f owner=%s inst=%d networked=%d",car_,cExists,cMission,cModel,
                                   double(f(cPos[0])),double(f(cPos[1])),double(f(cPos[2])),owner,inst,netd);
        char l[700];
        std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL env frame=%u gip=%u%s hour=%u min=%u prev=%08x next=%08x rain=%.2f held=%d ped=%u pos=%.1f,%.1f,%.1f vis=%d cam=%.1f,%.1f,%.1f rcam=%d grend=%d final=%.1f,%.1f,%.1f onscreen=%d control=%d mpcut=%d",
                      frame,gip,change?" changed":"",r.h,r.m,r.prev,r.next,double(rain),held_?1:0,ped,double(f(pos[0])),double(f(pos[1])),double(f(pos[2])),int(vis),
                      double(f(cam[0])),double(f(cam[1])),double(f(cam[2])),int(rcam),int(grend),double(f(fin[0])),double(f(fin[1])),double(f(fin[2])),
                      int(onscr),int(ctl),int(mpcut));
        std::strncat(l,cl,sizeof l-std::strlen(l)-1);   // v1030
        log_(l);
    }
    static float f(std::uint32_t w) noexcept {float x;std::memcpy(&x,&w,4);return x;}
    static constexpr float CarRadius=15.0f;
    void registerCar(recomp::trainer::Natives& nat) noexcept {
        netTried_=true;
        const std::uint32_t ped=one(nat,n::PlayerPed),pa2[2]{ped,0u};std::uint32_t vo[3]{};
        std::uint32_t v=(nat.call(n::VehicleOfPed,pa2,2,vo) && vo[0] && vo[0]!=0xFFFFFFFFu)?vo[0]:0u;
        if(!v){const std::uint32_t lv=one(nat,n::LastVehicle);if(lv && lv!=0xFFFFFFFFu)v=lv;}
        if(!v || one1(nat,n::EntityExists,v)!=1u){log_("RENDER_ONLINE_LOCAL car-net none (no current / last vehicle)");return;}
        const std::uint32_t before=one1(nat,n::EntityNetworked,v);
        bool called=false;if(before!=1u){std::uint32_t out[3]{};called=nat.call(n::RegisterNetworked,&v,1,out);}
        char l[160];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL car-net vehicle=%u networked before=%u called=%d after=%u",v,before,called?1:0,one1(nat,n::EntityNetworked,v));
        log_(l);car_=v;
    }
    static void guestString(recomp::trainer::Natives& nat,std::uint32_t at,char* out,std::size_t cap) noexcept {   // printable ASCII, <= cap-1
        std::size_t k=0;
        for(;k+1<cap;++k){const std::uint32_t a=at+std::uint32_t(k),w=nat.load(a&~3u);const char c=char((w>>(24-8*(a&3u)))&0xFFu);
            if(!c)break;out[k]=(c>=32 && c<127)?c:'?';}
        out[k]=0;
    }
    static std::uint32_t closestCar(recomp::trainer::Natives& nat,const std::uint32_t pos[3]) noexcept {
        for(std::uint32_t flags:{0x87eu,70u}){   // the disc's own flags first (vehicle_gen_controller), then the common "any vehicle" value
            std::uint32_t out[3]{};float r=CarRadius;std::uint32_t rb;std::memcpy(&rb,&r,4);
            const std::uint32_t a[6]{pos[0],pos[1],pos[2],rb,0u,flags};
            if(nat.call(n::ClosestVehicle,a,6,out) && out[0] && out[0]!=0xFFFFFFFFu)return out[0];
        }
        return 0u;
    }
    static bool call(recomp::trainer::Natives& nat,std::uint32_t h,const std::uint32_t* a,unsigned k) noexcept {std::uint32_t out[3]{};return nat.call(h,a,k,out);}
    static bool set3(recomp::trainer::Natives& nat,std::uint32_t h,std::uint32_t x,std::uint32_t y,std::uint32_t z) noexcept {const std::uint32_t a[3]{x,y,z};return call(nat,h,a,3);}
    static std::uint32_t one(recomp::trainer::Natives& nat,std::uint32_t h) noexcept {std::uint32_t out[3]{};return nat.call(h,nullptr,0,out)?out[0]:0xFFFFFFFFu;}
    static std::uint32_t one1(recomp::trainer::Natives& nat,std::uint32_t h,std::uint32_t a0) noexcept {std::uint32_t out[3]{};return nat.call(h,&a0,1,out)?out[0]:0xFFFFFFFFu;}
    LogFn log_;bool started_=false,have_=false,held_=false,logged_=false,standDown_=false;std::uint32_t edgeFrame_=0;   // v1053
    std::uint32_t lastGip_=0,last_=0,burst_=0;unsigned reapplied_=0;Reading snap_{},want_{};
    std::uint32_t car_=0;   // v1030: the tracked car
    bool online_=false,carNet_=false,netTried_=false;   // v1034
};
}
