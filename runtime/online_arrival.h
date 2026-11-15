#pragma once
// online_arrival.h (v1046, trainer-mods; the owner-approved Online launcher mode, ALLOCATION section 18; lead 2026-11-07: the first visit
// is the game's real fm_intro ending at LSIA's drop-off, later visits the last saved spot, all behind the loading screen).
// Record: <generation>/online/arrival.json through character_store.h's FileStore (the online-mode or sandbox-mode generation only; atomic
// write, fsync, rename):  {"intro":0|1,"spot":[x,y,z,heading]}  ("spot" absent until one was saved).
// Armed by Online mode or files/private-trace/diag-online-arrival (Sandbox tests), with the L1 path on; the session is ours
// (local_session::active()). Order in the glue: car keep, this, then the curtain, which waits while holding() is true.
//  - First visit (no record, or intro 0): once gip has been 1 for StartFrames and the car keep is idle, the game's own fm_intro is started
//    (the trainer's action 19 a0 = 101 path, activities.h FmIntroProbe; t-fmil2-20261107-1855: sky tour, LSIA, Lamar's drive, control back
//    at -206.8,305.4,96.9 about 270 s later). holding() stays true until its MP cutscene runs (NETWORK_IS_IN_MP_CUTSCENE), at most
//    IntroWaitFrames (then logged and released: never a stuck loading screen). intro=1 is written once that cutscene has ended with the
//    control on: a visit left mid-intro replays it next time.
//  - Later visits (intro 1 with a spot): the player is placed at the spot behind the curtain: SET_ENTITY_COORDS (the disc's 8-word form)
//    on his vehicle when he drives one, else on his ped, SET_ENTITY_HEADING, REQUEST_COLLISION_AT_COORD; holding() for SettleFrames more.
//    intro 1 without a spot: where the story left him.
//  - The spot: in the session with the control on and no MP cutscene, read every SaveEveryFrames and written when it moved more than
//    MoveMeters (so at most one write per SaveEveryFrames), and once more on the leave edge (gip 1 -> 0) with the last reading.
// A record that does not parse (or a spot outside the world bounds) counts as no record (logged); nothing is deleted.
#include "trainer.h"
#include "character_creator.h"   // creator::Store (FileStore in the glue, a mock in the tests)
#include "local_session_flag.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace recomp::online_arrival {
namespace n {
using recomp::trainer::joaat;
constexpr std::uint32_t Gip=joaat("NETWORK_IS_GAME_IN_PROGRESS"),MpCut=joaat("NETWORK_IS_IN_MP_CUTSCENE"),PlayerId=joaat("PLAYER_ID"),
    PlayerPed=joaat("PLAYER_PED_ID"),ControlOn=joaat("IS_PLAYER_CONTROL_ON"),VehicleOfPed=joaat("GET_VEHICLE_PED_IS_IN"),
    Coords=joaat("GET_ENTITY_COORDS"),Heading=joaat("GET_ENTITY_HEADING"),SetCoords=joaat("SET_ENTITY_COORDS"),
    SetHeading=joaat("SET_ENTITY_HEADING"),RequestCollision=joaat("REQUEST_COLLISION_AT_COORD"),
    CutscenePlaying=joaat("IS_CUTSCENE_PLAYING"),GameplayCam=joaat("IS_GAMEPLAY_CAM_RENDERING"),   // v1084
    CutsceneTime=joaat("GET_CUTSCENE_TIME"),FadedOut=joaat("IS_SCREEN_FADED_OUT");   // v1087
}
inline constexpr std::uint32_t UsedNatives[]{n::Gip,n::MpCut,n::PlayerId,n::PlayerPed,n::ControlOn,n::VehicleOfPed,n::Coords,n::Heading,
    n::SetCoords,n::SetHeading,n::RequestCollision,n::CutscenePlaying,n::GameplayCam,n::CutsceneTime,n::FadedOut};
constexpr std::uint32_t StartFrames=15,IntroWaitFrames=900,SettleFrames=45,SaveEveryFrames=300;
// v1084 (owner 2026-11-08, watching the Thor: "the loading screen stops too early as the character shows too early before the cutscene
// executes"; t-onlcre74-20261108-0205: NETWORK_IS_IN_MP_CUTSCENE was 1 21 frames after fm_intro's start, the curtain revealed the standing
// character there): the intro counts as running only once the game shows the cutscene's picture: the MP cutscene flag AND (a cutscene
// playing OR the gameplay camera no longer rendering) for PictureFrames consecutive ticks. The card release and the curtain both wait on it.
constexpr std::uint32_t PictureFrames=3;
// v1092: IntroWaitFrames back to 900 (v1091 had 3600): camOff (the tour camera) arrives at about +93 frames.
constexpr float MoveMeters=5.0f,WorldXY=8000.0f,WorldZMin=-200.0f,WorldZMax=2700.0f;
constexpr int RecordCap=160;
using LogFn=void(*)(const char*);
using StartIntroFn=bool(*)(recomp::trainer::Natives&);   // the glue: activities().request(nat, 101, Start, ...) == 1

struct Record {bool valid=false,intro=false,spot=false;float x=0,y=0,z=0,h=0;};
inline bool inWorld(float x,float y,float z,float h) noexcept {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(h) && std::fabs(x)<WorldXY && std::fabs(y)<WorldXY
        && z>WorldZMin && z<WorldZMax;
}
// Strict: exactly the shape format() writes (whitespace-free), intro 0 or 1, the spot optional; anything else = not valid.
inline Record parse(const char* t,int len) noexcept {
    Record r;if(!t || len<=0 || len>=RecordCap)return r;
    char b[RecordCap];std::memcpy(b,t,std::size_t(len));b[len]=0;
    int intro=-1,used=0;
    if(std::sscanf(b,"{\"intro\":%d%n",&intro,&used)!=1 || (intro!=0 && intro!=1))return r;
    const char* p=b+used;
    if(std::strcmp(p,"}")==0 || std::strcmp(p,"}\n")==0){r.valid=true;r.intro=intro==1;return r;}
    float x,y,z,h;int end=0;
    if(std::sscanf(p,",\"spot\":[%f,%f,%f,%f]}%n",&x,&y,&z,&h,&end)!=4 || end<=0)return r;
    if(p[end]!=0 && !(p[end]=='\n' && p[end+1]==0))return r;
    if(!inWorld(x,y,z,h))return r;
    r.valid=true;r.intro=intro==1;r.spot=true;r.x=x;r.y=y;r.z=z;r.h=h;return r;
}
inline int format(const Record& r,char* out,int cap) noexcept {
    const int k=r.spot?std::snprintf(out,std::size_t(cap),"{\"intro\":%d,\"spot\":[%.2f,%.2f,%.2f,%.2f]}\n",r.intro?1:0,double(r.x),double(r.y),double(r.z),double(r.h))
                      :std::snprintf(out,std::size_t(cap),"{\"intro\":%d}\n",r.intro?1:0);
    return k>0 && k<cap?k:-1;
}

class Arrival {
public:
    enum class State : std::uint8_t {Idle,WaitSession,IntroWait,IntroRunning,Placed,Done,GameIntro};   // v1105: GameIntro (FREEMODE-GATE-6 D)
    Arrival(LogFn log,recomp::creator::Store& store,StartIntroFn start) noexcept:log_(log),store_(store),start_(start) {}
    // Every Sandbox tick. Not armed or no local session (and nothing to save): returns before any native.
    // v1056: ready false (Online mode before the online character is confirmed) delays the first-visit intro / the placement.
    // v1105 (FREEMODE-GATE-6 D): fmRoute = the freemode route (the game makes the player and places it); gameIntro = freemode's own fm_intro
    // thread runs (the glue reads it only in Idle / WaitSession / GameIntro). On the route the arrival never starts the intro and never places.
    void tick(recomp::trainer::Natives& nat,std::uint32_t frame,bool armed,bool carBusy,bool ready=true,bool fmRoute=false,bool gameIntro=false) noexcept {
        if(!armed || !log_)return;
        const bool session=::recomp::local_session::active();
        if(!session && !inSession_)return;
        if(!loaded_)load();
        const std::uint32_t gip=one(nat,n::Gip,{});
        if(gip!=1u){
            if(inSession_){inSession_=false;if(haveReading_)saveSpot(last_,"leave");}   // the leave edge: the last reading
            return;
        }
        if(!inSession_){inSession_=true;gipAt_=frame;if(st_==State::Idle)st_=State::WaitSession;}
        switch(st_) {
        case State::Idle: case State::WaitSession:
            if(fmRoute && ready) {   // v1105 (D): the game's own intro / placement
                if(gameIntro){st_=State::GameIntro;startAt_=frame;say("RENDER_ONLINE_LOCAL arrival game intro running (fm_intro thread): no start, no placement");return;}
                if(rec_.intro) {
                    Reading r;const bool ok=read(nat,r);
                    if(ok){const float dx=r.x-rec_.x,dy=r.y-rec_.y,dz=r.z-rec_.z;last_=r;haveReading_=true;
                        say("RENDER_ONLINE_LOCAL arrival fm-route: the game placed the player at %.1f,%.1f,%.1f (saved spot %.1f m away; not moved)",double(r.x),double(r.y),double(r.z),
                            rec_.spot?double(std::sqrt(dx*dx+dy*dy+dz*dz)):-1.0);}
                    else say("RENDER_ONLINE_LOCAL arrival fm-route: returning visit, the game places the player (position unreadable; not moved)");
                    st_=State::Done;lastSave_=frame;return;
                }
                if(!fmNoRecAt_)fmNoRecAt_=frame;
                if(frame-fmNoRecAt_>=IntroWaitFrames){say("RENDER_ONLINE_LOCAL arrival fm-route: no record and no fm_intro thread in %u frames: done",IntroWaitFrames);st_=State::Done;lastSave_=frame;}
                return;
            }
            if(frame-gipAt_<StartFrames || carBusy || !ready)return;
            if(!rec_.intro){
                const bool ok=start_ && start_(nat);
                say("RENDER_ONLINE_LOCAL arrival first-visit intro start=%d (record %s)",ok?1:0,rec_.valid?"intro=0":"none");
                st_=ok?State::IntroWait:State::Done;startAt_=frame;return;
            }
            if(rec_.spot)place(nat,frame);
            else {say("RENDER_ONLINE_LOCAL arrival returning visit, no spot saved: where the story left him");st_=State::Done;}
            return;
        case State::IntroWait:
            {   // v1087 (lead, t-onlcre74-20261108-0323: v1084's gate passed on "gameplay_cam=0" during fm_intro's scripted shot of the player on
                // the hill, 4.5 s before the cutscene): the intro's own cutscene (fm_intro requests MP_INTRO_CONCAT with a playback list and
                // START_CUTSCENE in its stage -2, 0x8713 / 0x479c) must be playing with its time advanced: IS_CUTSCENE_PLAYING() == 1 AND
                // GET_CUTSCENE_TIME() > 0, PictureFrames ticks in a row. Every change of the inputs is logged (which condition released it).
                const bool flag=one(nat,n::MpCut,{})==1u;
                const bool cut=one(nat,n::CutscenePlaying,{})==1u;const std::uint32_t ct=cut?one(nat,n::CutsceneTime,{}):0u;
                const bool camOff=one(nat,n::GameplayCam,{})==0u,dark=one(nat,n::FadedOut,{})==1u;
                const bool timeOn=cut && ct!=0xFFFFFFFFu && int(ct)>0;
                const unsigned key=(flag?1u:0u)|(cut?2u:0u)|(timeOn?4u:0u)|(camOff?8u:0u)|(dark?16u:0u);
                if(key!=lastKey_ && inputLines_<24){lastKey_=key;++inputLines_;
                    say("RENDER_ONLINE_LOCAL arrival intro inputs frame=+%u mp_flag=%d cutscene=%d cut_time=%d gameplay_cam=%d faded_out=%d",frame-startAt_,
                        flag?1:0,cut?1:0,timeOn?int(ct):0,camOff?0:1,dark?1:0);}
                if(flag && !flagSeen_){flagSeen_=true;say("RENDER_ONLINE_LOCAL arrival intro mp-cutscene flag after %u frames (waiting for its cutscene)",frame-startAt_);}
                // v1092 (lead's correction after reading t-onlcre74-20261108-0337 frame by frame): after camOff the screen is fm_intro's own GTA Online
                // city tour (the Vinewood-sign view, downtown, the freeways with help text), which must be SEEN; the only bad frames were the
                // gameplay camera on the player at the hill between the curtain's fade-in (+23) and camOff (+93). v1087's "scripted shot of the
                // player on the hill" was a misreading. So the release is the flag AND (the cutscene with its time advanced OR the gameplay camera
                // off), PictureFrames ticks in a row (v1084's camera test back, the cutscene path kept).
                pictureRun_=(flag && (timeOn || camOff))?pictureRun_+1u:0u;
                if(pictureRun_>=PictureFrames){st_=State::IntroRunning;
                    say("RENDER_ONLINE_LOCAL arrival intro cutscene running after %u frames (cutscene=%d cut_time=%d gameplay_cam=%d)",frame-startAt_,cut?1:0,timeOn?int(ct):0,camOff?0:1);return;}
            }
            if(frame-startAt_>=IntroWaitFrames){say("RENDER_ONLINE_LOCAL arrival intro no cutscene in %u frames (released)",IntroWaitFrames);st_=State::Done;}
            return;
        case State::GameIntro:   // v1105 (D): freemode's fm_intro owns the arrival; its end writes the record as our own intro's does
            if(gameIntro)return;
            st_=State::IntroRunning;return;
        case State::IntroRunning:
            if(one(nat,n::MpCut,{})==1u)return;
            if(!controlOn(nat))return;
            rec_.intro=true;rec_.valid=true;
            {Reading r;if(read(nat,r)){rec_.spot=true;rec_.x=r.x;rec_.y=r.y;rec_.z=r.z;rec_.h=r.h;last_=r;haveReading_=true;saved_=r;}}
            say("RENDER_ONLINE_LOCAL arrival intro handed control back after %u frames at %.1f,%.1f,%.1f written=%d",frame-startAt_,
                double(rec_.x),double(rec_.y),double(rec_.z),write()?1:0);
            st_=State::Done;lastSave_=frame;return;
        case State::Placed:
            if(frame-placedAt_>=SettleFrames)st_=State::Done;
            return;
        case State::Done:
            if(frame-lastSave_<SaveEveryFrames)return;
            lastSave_=frame;
            if(one(nat,n::MpCut,{})==1u || !controlOn(nat))return;
            {Reading r;if(!read(nat,r))return;last_=r;haveReading_=true;
             if(!savedAny_ || dist2(r,saved_)>MoveMeters*MoveMeters)saveSpot(r,"moved");}
            return;
        }
    }
    bool holding() const noexcept {return st_==State::WaitSession || st_==State::IntroWait || st_==State::Placed;}
    // v1080: for the OnlineMode controller's release (lead: a world frame showed before the intro's cutscene; the card dropped 1.26 s after
    // a later visit's placement): 0 idle, 1 waiting for the session / the online character, 2 the intro asked for but its cutscene not up,
    // 3 placed and settling, 4 the intro's cutscene owns the camera or the arrival is done.
    int phase() const noexcept {
        switch(st_){case State::Idle:return 0;case State::WaitSession:return 1;case State::IntroWait:return 2;case State::Placed:return 3;
                    case State::IntroRunning:case State::Done:case State::GameIntro:return 4;}
        return 0;
    }
    State state() const noexcept {return st_;}
    const Record& record() const noexcept {return rec_;}
private:
    struct Reading {float x=0,y=0,z=0,h=0;};
    static float dist2(const Reading& a,const Reading& b) noexcept {const float dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;return dx*dx+dy*dy+dz*dz;}
    void load() noexcept {
        loaded_=true;char b[RecordCap];const int k=store_.read(b,RecordCap-1);
        if(k<=0){say("RENDER_ONLINE_LOCAL arrival record none (first visit)");return;}
        rec_=parse(b,k);
        if(!rec_.valid){rec_=Record{};say("RENDER_ONLINE_LOCAL arrival record unreadable (%d bytes): treated as a first visit",k);return;}
        if(rec_.spot){saved_={rec_.x,rec_.y,rec_.z,rec_.h};savedAny_=true;}
        say("RENDER_ONLINE_LOCAL arrival record intro=%d spot=%d %.1f,%.1f,%.1f",rec_.intro?1:0,rec_.spot?1:0,double(rec_.x),double(rec_.y),double(rec_.z));
    }
    bool write() noexcept {char b[RecordCap];const int k=format(rec_,b,RecordCap);return k>0 && store_.write(b,k);}
    void saveSpot(const Reading& r,const char* why) noexcept {
        if(!rec_.intro)return;   // the first visit's spot is the intro's drop-off, written when it hands control back
        if(!inWorld(r.x,r.y,r.z,r.h))return;
        rec_.spot=true;rec_.x=r.x;rec_.y=r.y;rec_.z=r.z;rec_.h=r.h;
        const bool ok=write();if(ok){saved_=r;savedAny_=true;}
        say("RENDER_ONLINE_LOCAL arrival spot saved why=%s at %.1f,%.1f,%.1f heading=%.0f ok=%d",why,double(r.x),double(r.y),double(r.z),double(r.h),ok?1:0);
    }
    void place(recomp::trainer::Natives& nat,std::uint32_t frame) noexcept {
        const std::uint32_t ped=one(nat,n::PlayerPed,{});
        const std::uint32_t v=one(nat,n::VehicleOfPed,{ped,0u});
        const std::uint32_t e=(v && v!=0xFFFFFFFFu)?v:ped;
        call(nat,n::SetCoords,{e,bits(rec_.x),bits(rec_.y),bits(rec_.z),1u,0u,0u,1u});
        call(nat,n::SetHeading,{e,bits(rec_.h)});
        call(nat,n::RequestCollision,{bits(rec_.x),bits(rec_.y),bits(rec_.z)});
        say("RENDER_ONLINE_LOCAL arrival placed %s at the saved spot %.1f,%.1f,%.1f heading=%.0f",e==ped?"ped":"vehicle",double(rec_.x),
            double(rec_.y),double(rec_.z),double(rec_.h));
        st_=State::Placed;placedAt_=frame;
    }
    bool controlOn(recomp::trainer::Natives& nat) noexcept {return one(nat,n::ControlOn,{one(nat,n::PlayerId,{})})==1u;}
    bool read(recomp::trainer::Natives& nat,Reading& r) noexcept {
        const std::uint32_t ped=one(nat,n::PlayerPed,{});
        const std::uint32_t v=one(nat,n::VehicleOfPed,{ped,0u});const std::uint32_t e=(v && v!=0xFFFFFFFFu)?v:ped;
        std::uint32_t c[3]{};const std::uint32_t a[2]{e,1u};if(!nat.call(n::Coords,a,2,c))return false;
        r.x=f(c[0]);r.y=f(c[1]);r.z=f(c[2]);r.h=f(one(nat,n::Heading,{e}));
        return inWorld(r.x,r.y,r.z,r.h);
    }
    static float f(std::uint32_t w) noexcept {float x;std::memcpy(&x,&w,4);return x;}
    static std::uint32_t bits(float x) noexcept {std::uint32_t w;std::memcpy(&w,&x,4);return w;}
    static std::uint32_t one(recomp::trainer::Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};return nat.call(h,a.begin(),unsigned(a.size()),out)?out[0]:0xFFFFFFFFu;}
    static void call(recomp::trainer::Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {std::uint32_t out[3]{};(void)nat.call(h,a.begin(),unsigned(a.size()),out);}
    void say(const char* l) const noexcept {log_(l);}   // a fixed line (no format)
    template<class A0,class... A> void say(const char* fmt,A0 a0,A... a) const noexcept {char l[240];std::snprintf(l,sizeof l,fmt,a0,a...);log_(l);}
    LogFn log_;recomp::creator::Store& store_;StartIntroFn start_;State st_=State::Idle;Record rec_{};bool flagSeen_=false;std::uint32_t pictureRun_=0;unsigned lastKey_=0xFFu,inputLines_=0;
    bool loaded_=false,inSession_=false,haveReading_=false,savedAny_=false;
    std::uint32_t gipAt_=0,startAt_=0,placedAt_=0,lastSave_=0,fmNoRecAt_=0;Reading last_{},saved_{};   // v1105: fmNoRecAt_
};
}
