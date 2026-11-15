#pragma once
// v1590 (thor-vehmods lane A, 2026-11-12): + the saved-vehicle block. GTA Online keeps the personal vehicles in the script save block
// G115227 (14 slots x 53 words: w0 plate index, w1-4 plate text, w5/w6 colours, w9.. mods, w42 model, w51 flags; START_SAVE_DATA, saved
// inside save_char0001 = our mpstats-1.save). Decoded carmod_shop: the plate menu (0x31734) buys (0x12d59) without touching the slot; the
// slot is written by 0x2b328 when the mod menu is left (0x396d1) and nothing asks a save for a plain mod on the player's own car (only
// sales / new slots set static 61 -> ingamehud request 11), so the real game relied on its periodic cloud save. On v1565 the periodic
// stand-in saved 5 s after the purchase with the menu still open and the session ended 15 s after the exit: the slot never reached a save
// (t-lsc8 A: NETWORK_BUY_ITEM(50,...) 20:34:19.7, save asked 20:34:24.7, exit ~20:34:30, leave 20:34:46; slot w0 0 before and after).
//  - READ ONLY diagnostic, Online mode, default ON (files/private-trace/diag-vehslot-probe-off turns it off): RENDER_ONLINE_VEHSLOT, one
//    line per change (at most 60) of the current personal-vehicle slot (G115227+1603+2) words w0 w5 w6 w42 w51, the block hash and the
//    La Mesa..LSC browse bits (G80929+1015 elements 39-43 bit 6, the shops' own "browsing" bit, as door_chain_probe.h).
//  - THE FIX, DEFAULT ON since v1670 (files/private-trace/diag-stats-shopexit-save-off turns it off; v1590 had it default off behind
//    diag-stats-shopexit-save; the stats kill switches still win): when the vehicle
//    block differs from what the last save carried, no LSC menu is open and has not been for SettleFrames, the game's periodic deadline is
//    armed, the stats request object is idle, at least MinGapMs since the last save, and the player is in plain free roam, ask the game's
//    own STAT_SAVE(0, FALSE, 0) once (the call the stand-in already makes). No stat write, no global write: the game serializes the block.
// periodic_save.h (v1564: the stats clock comes from the caller, read through the guest scalar path: on v1563 the trainer's raw load of
// *(0x82000A38)+16 = 0x62004210 read 0 (a special range PPC_LOAD_U32 emulates; t-onlbarber-A-20261112-0242 heartbeat "clock=62004200 now=0"), so
// cheap() never left not-due; the heartbeat is capped at 5 lines. v1563: + a tick heartbeat line (first tick, then every 1200 game frames; read only) - on v1562 the saver logged
// nothing on the Thor (t-onlbarber-A-20261112-0215: only its first-tick money reads were seen); v1562, thor-v1540-promotion lane 2026-11-12; coordinator-approved): the local stand-in for GTA Online's CLOUD periodic MP
// stats save, Online mode only. Default ON; kill switch files/private-trace/diag-stats-periodic-off (read once), as the Online spoof's other
// default-on answers (diag-stats-reload-off, diag-online-respawn-off).
// Why (probes v1560 / v1561, t-onlsaveprobe-20261112-0131): sub_824F7E30's periodic save passes all its own checks every 120 s, then
// sub_82AAFA90 = sub_82FACA50 (save manager 0x8381DF70) refuses because the per-user ROS client 0x83C09440 is not logged in (sub_82281378
// != 4: no ROS ticket, which our local mode never fakes); its request sub_82AAFA80(1) would start an rlRos task anyway. So in Online local
// mode nothing that is not followed by a script's own STAT_SAVE was ever written: a barber haircut (t-onlbarber-B-20261112-0004) and
// Ammu-Nation ammo (t-onlammu-B-20261112-0039) were lost on restart. No gate is answered and no ticket is made: when the game's own
// deadline has passed, the stats request object is idle, the stats are dirty and the player is in plain free roam, this asks for the save
// with the game's own STAT_SAVE(0, FALSE, 0), the call the clothes shop makes after its NETWORK_BUY_ITEM (t-onlshopcl-A-20261111-2141:
// RENDER_DIAG_NATIVE_EVENT hash=e10a7ca4 a0=0 a1=0 a2=0; slots 0 and 1 written ~4 s later). The game's own save path does the rest
// (sub_82AAF8A0 -> sub_82E13678 queues it, the stats update services it through the existing local legs).
//  - Deadline: the game's own periodic deadline R+80 (the stats request object R = 0x83A31F8C; set by sub_824F7E30 at ~20 s to now +
//    120000) has passed AND 120000 ms (sub_824F7E30's interval, 0x1D4C0) since the last save anyone queued (seen as R+8 != 0) or asked.
//  - Idle: R+8 (requested mask) == 0, byte R+16 (a running request) == 0, byte R+72 (a cloud task) == 0.
//  - Dirty: a stat write native (STAT_SET_* / STAT_INCREMENT, counted after each call) or a wallet / bank change since that last save.
//  - Plain free roam: in a network game, not in a transition, no player switch, no cutscene, screen faded in, no activity session (job),
//    no mission flag, the player playing. Never while a save is queued or running. Story / Sandbox: never (the caller is Online-only, and
//    decide() checks the latch too).
// decide() is pure (host test periodic_save_test.cpp); tick() reads the guest words and natives through recomp::trainer::Natives.
#include "trainer.h"
#include "stat_write_natives.h"   // StatWriteNatives / statWriteHash (also used by indirect_call_resolver.cpp)
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <unistd.h>

namespace recomp::periodic_save {
namespace n {
using recomp::trainer::joaat;
constexpr std::uint32_t StatSave=joaat("STAT_SAVE"),Gip=joaat("NETWORK_IS_GAME_IN_PROGRESS"),InTransition=joaat("NETWORK_IS_IN_TRANSITION"),
    SwitchInProgress=joaat("IS_PLAYER_SWITCH_IN_PROGRESS"),CutsceneActive=joaat("IS_CUTSCENE_ACTIVE"),CutscenePlaying=joaat("IS_CUTSCENE_PLAYING"),
    FadedIn=joaat("IS_SCREEN_FADED_IN"),ActivitySession=joaat("NETWORK_IS_ACTIVITY_SESSION"),MissionFlag=joaat("GET_MISSION_FLAG"),
    PlayerId=joaat("PLAYER_ID"),PlayerPlaying=joaat("IS_PLAYER_PLAYING"),
    Wallet=joaat("NETWORK_GET_VC_WALLET_BALANCE"),Bank=joaat("NETWORK_GET_VC_BANK_BALANCE");
}
inline constexpr std::uint32_t UsedNatives[]{n::StatSave,n::Gip,n::InTransition,n::SwitchInProgress,n::CutsceneActive,n::CutscenePlaying,
    n::FadedIn,n::ActivitySession,n::MissionFlag,n::PlayerId,n::PlayerPlaying,n::Wallet,n::Bank};
inline std::atomic<std::uint32_t> statWrites{0};
inline void noteNative(std::uint32_t h) noexcept {if(statWriteHash(h))statWrites.fetch_add(1,std::memory_order_relaxed);}

constexpr std::uint32_t Request=0x83A31F8Cu,ClockPtr=0x82000A38u,Interval=120000u;   // R, the clock sub_824AE6D8 reads, sub_824F7E30's interval
enum class Why : std::uint8_t { Save, NotOnline, Killed, Busy, NotArmed, NotDue, Clean, NoGame, Transition, Switch, Cutscene, Faded, Job, Mission, NotPlaying, Shop, Off };
inline const char* name(Why w) noexcept {
    switch(w){case Why::Save:return "save";case Why::NotOnline:return "not-online";case Why::Killed:return "kill-switch";case Why::Busy:return "busy";
        case Why::NotArmed:return "not-armed";case Why::NotDue:return "not-due";case Why::Clean:return "clean";case Why::NoGame:return "no-network-game";
        case Why::Transition:return "transition";case Why::Switch:return "player-switch";case Why::Cutscene:return "cutscene";case Why::Faded:return "faded";
        case Why::Job:return "activity-session";case Why::Mission:return "mission-flag";case Why::NotPlaying:return "not-playing";
        case Why::Shop:return "in-shop-menu";case Why::Off:return "switch-off";}
    return "?";
}
struct Cheap { bool online=false,killed=false; std::uint32_t now=0,r80=0,r8=0,r16=0,r72=0,lastSave=0; bool everSaved=false; };
struct Dirty { std::uint32_t writesSince=0; bool moneyKnown=false; std::int32_t wallet=0,bank=0,walletAt=0,bankAt=0; };
struct Session { bool gip=false,transition=true,switching=true,cutscene=true,fadedIn=false,activity=true,mission=true,playing=false; };
// the memory-only part (every 30 frames): online, kill switch, idle, the game's deadline and our interval
inline Why cheap(const Cheap& c) noexcept {
    if(!c.online)return Why::NotOnline;
    if(c.killed)return Why::Killed;
    if(c.r8!=0 || c.r16!=0 || c.r72!=0)return Why::Busy;
    if(c.r80==0)return Why::NotArmed;                                   // the game's periodic deadline is set at its first pass (~20 s)
    if(std::int32_t(c.now-c.r80)<0)return Why::NotDue;
    if(c.everSaved && std::int32_t(c.now-c.lastSave)<std::int32_t(Interval))return Why::NotDue;
    return Why::Save;
}
inline bool dirty(const Dirty& d) noexcept {return d.writesSince!=0 || (d.moneyKnown && (d.wallet!=d.walletAt || d.bank!=d.bankAt));}
inline Why session(const Session& s) noexcept {
    if(!s.gip)return Why::NoGame;
    if(s.transition)return Why::Transition;
    if(s.switching)return Why::Switch;
    if(s.cutscene)return Why::Cutscene;
    if(!s.fadedIn)return Why::Faded;
    if(s.activity)return Why::Job;
    if(s.mission)return Why::Mission;
    if(!s.playing)return Why::NotPlaying;
    return Why::Save;
}
inline Why decide(const Cheap& c,const Dirty& d,const Session& s) noexcept {
    const Why a=cheap(c);if(a!=Why::Save)return a;
    if(!dirty(d))return Why::Clean;
    return session(s);
}

// v1590: the vehicle-block save decision (pure; host test periodic_save_test.cpp)
constexpr std::uint32_t VehBlock=115227u,VehSlots=14u,VehWords=53u,VehCur=115227u+1603u+2u,ShopsG=80929u,ShopBitsOff=1015u,LscFirst=39u,LscCount=5u;
constexpr std::uint32_t SettleFrames=90u,MinGapMs=10000u;
struct Exit { bool on=false,online=false,killed=false,readable=false,changed=false,inShop=true; std::uint32_t outFrames=0,now=0,r80=0,r8=0,r16=0,r72=0,lastSave=0; bool everSaved=false; };
inline Why exitCheap(const Exit& e) noexcept {
    if(!e.online)return Why::NotOnline;
    if(e.killed)return Why::Killed;
    if(!e.on)return Why::Off;
    if(!e.readable || e.r80==0)return Why::NotArmed;                   // block not readable / the game's periodic save not armed yet
    if(!e.changed)return Why::Clean;                                   // the block is what the last save carried
    if(e.inShop || e.outFrames<SettleFrames)return Why::Shop;          // only after the shop's own exit (the slot is written there)
    if(e.r8!=0 || e.r16!=0 || e.r72!=0)return Why::Busy;
    if(e.everSaved && std::int32_t(e.now-e.lastSave)<std::int32_t(MinGapMs))return Why::NotDue;
    return Why::Save;
}
inline Why exitDecide(const Exit& e,const Session& s) noexcept {const Why a=exitCheap(e);return a!=Why::Save?a:session(s);}
inline std::uint32_t fnv(std::uint32_t h,std::uint32_t w) noexcept {for(int i=0;i<4;++i){h^=(w>>(8*i))&0xFFu;h*=16777619u;}return h;}

using LogFn=void(*)(const char*);
// v1670 (Update 1 candidate): the shop-exit save is DEFAULT ON; files/private-trace/diag-stats-shopexit-save-off is its kill switch (and
// diag-stats-periodic-off / diag-stats-route-off still win through killSwitch()). v1590 proved it with the switch on (t-vmod3 A/B) and
// the timing-matched negative control off (t-vmod6 A/B).
inline bool shopExitOn() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-stats-shopexit-save-off"),F_OK)!=0;return o;}
inline bool vehProbeOff() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-vehslot-probe-off"),F_OK)==0;return o;}
inline bool killSwitch() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-stats-periodic-off"),F_OK)==0||
                                                       ::access(RECOMP_APP_PATH("/private-trace/diag-stats-route-off"),F_OK)==0;return o;}
class Saver {
public:
    explicit Saver(LogFn log) noexcept:log_(log) {}
    // once per game frame from the trainer's Online branch (script context); real work every 30 frames
    // clock(): the game's stats clock (sub_824AE6D8 = *(*(0x82000A38)+16)), read by the caller with the guest scalar loader; 0 = unknown
    template<class Clock> void tick(recomp::trainer::Natives& nat,std::uint32_t frame,bool online,Clock&& clock) noexcept {
        if(!log_)return;
        if(beats_<5 && (!beats_ || frame-lastBeat_>=1200u)){lastBeat_=frame;++beats_;   // v1563 heartbeat (read only; v1564: 5 lines at most)
            const std::uint32_t co=nat.load(ClockPtr);
            char l[220];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS periodic local save tick #%u frame=%u last_work=%u online=%d killed=%d clock=%08x now=%u R+80=%u R+8=%x R+16=%x R+72=%x writes=%u reason=%s asked=%u",
                beats_,frame,lastFrame_,online?1:0,killSwitch()?1:0,co,unsigned(clock()),nat.load(Request+80u),nat.load(Request+8u),nat.load(Request+16u)>>24,
                nat.load(Request+72u)>>24,statWrites.load(std::memory_order_relaxed),name(last_),asked_);log_(l);}
        if(frame-lastFrame_<30u)return;lastFrame_=frame;
        Cheap c;c.online=online;c.killed=killSwitch();
        if(!c.online || c.killed){note(c.online?Why::Killed:Why::NotOnline,0);return;}
        c.now=clock();if(c.now==0u)return;   // v1564: the caller's guest-scalar read (0 = not yet / not readable)
        c.r80=nat.load(Request+80u);c.r8=nat.load(Request+8u);c.r16=nat.load(Request+16u)>>24;c.r72=nat.load(Request+72u)>>24;
        if(!moneyAtKnown_){Dirty base0;money(nat,base0);}                  // the session's money baseline, read as soon as the natives answer
        if(c.r8!=0){markSaved(nat,c.now,false);}                         // somebody queued a save (a shop's STAT_SAVE, ours): the interval restarts
        c.lastSave=lastSave_;c.everSaved=everSaved_;
        if(vehicle(nat,frame,c))return;                                  // v1590: the diagnostic; the shop-exit save (default off) when it asked
        Why w=cheap(c);
        if(w!=Why::Save){note(w,c.now);return;}
        Dirty d;d.writesSince=statWrites.load(std::memory_order_relaxed)-writesAt_;
        if(d.writesSince==0){money(nat,d);}
        if(!dirty(d)){note(Why::Clean,c.now);return;}
        Session s;
        s.gip=one(nat,n::Gip,{})==1u;s.transition=one(nat,n::InTransition,{})!=0u;s.switching=one(nat,n::SwitchInProgress,{})!=0u;
        s.cutscene=one(nat,n::CutsceneActive,{})!=0u || one(nat,n::CutscenePlaying,{})!=0u;s.fadedIn=one(nat,n::FadedIn,{})==1u;
        s.activity=one(nat,n::ActivitySession,{})!=0u;s.mission=one(nat,n::MissionFlag,{})!=0u;
        const std::uint32_t pid=one(nat,n::PlayerId,{});s.playing=pid!=0xFFFFFFFFu && one(nat,n::PlayerPlaying,{pid})==1u;
        w=session(s);
        if(w!=Why::Save){note(w,c.now);return;}
        const std::uint32_t a[3]{0u,0u,0u};std::uint32_t out[3]{};const bool ok=nat.call(n::StatSave,a,3,out);
        ++asked_;char l[240];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS periodic local save #%u: STAT_SAVE(0,FALSE,0) %s now=%u deadline=%u since_last=%d writes=%u wallet %d->%d bank %d->%d",
            asked_,ok?"asked":"NOT AVAILABLE",c.now,c.r80,everSaved_?int(c.now-lastSave_):-1,d.writesSince,d.walletAt,d.wallet,d.bankAt,d.bank);log_(l);
        markSaved(nat,c.now,true);last_=Why::Save;
    }
    unsigned asked() const noexcept {return asked_;}
    unsigned exitAsked() const noexcept {return exitAsked_;}
private:
    // v1590: read the vehicle block (guest loads through the VM's global table, as door_chain_probe.h), log changes, and ask the
    // shop-exit save when exitDecide() says so. Returns true when it asked (the caller's periodic logic then waits for the next tick).
    bool vehicle(recomp::trainer::Natives& nat,std::uint32_t frame,const Cheap& c) noexcept {
        const std::uint32_t base=nat.load(0x83C5CE48u),size=nat.load(0x83C5CF50u);    // global block 0 (115227 and 80929 are < 1<<18)
        const bool readable=base && !(base&3u) && size>VehCur && size>ShopsG+ShopBitsOff+1u+LscFirst+LscCount;
        std::uint32_t h=2166136261u,cur=0xFFFFFFFFu,w[5]{};int lsc=0;
        if(readable){for(std::uint32_t i=0;i<VehSlots*VehWords;++i)h=fnv(h,nat.load(base+4u*(VehBlock+1u+i)));
            cur=nat.load(base+4u*VehCur);const std::uint32_t s=cur<VehSlots?cur:0u,e=base+4u*(VehBlock+1u+s*VehWords);
            w[0]=nat.load(e);w[1]=nat.load(e+20u);w[2]=nat.load(e+24u);w[3]=nat.load(e+168u);w[4]=nat.load(e+204u);
            for(std::uint32_t i=0;i<LscCount;++i){const std::uint32_t b=nat.load(base+4u*(ShopsG+ShopBitsOff+1u+LscFirst+i));if(b!=0xFFFFFFFFu && ((b>>6)&1u))lsc|=1<<i;}}
        if(lsc){lastInShop_=frame;firstOut_=0;}else if(lastInShop_ && !firstOut_)firstOut_=frame;   // the first tick seen out of the menu
        if(readable && c.r80!=0 && !vehBase_){vehBase_=true;savedHash_=h;}   // the baseline: the block as loaded, once the game's save is armed
        const bool changed=vehBase_ && h!=savedHash_;
        if(!vehProbeOff() && vehLines_<60 && readable && (h!=lastHash_ || lsc!=lastLsc_ || cur!=lastCur_)){
            ++vehLines_;lastHash_=h;lastLsc_=lsc;lastCur_=cur;char l[240];std::snprintf(l,sizeof l,
            "RENDER_ONLINE_VEHSLOT frame=%u cur=%d w0=%d w5=%d w6=%d w42=%08x w51=%08x hash=%08x saved=%08x changed=%d lsc=%d switch=%d (read only)",
            frame,int(cur),int(w[0]),int(w[1]),int(w[2]),w[3],w[4],h,vehBase_?savedHash_:0u,changed?1:0,lsc,shopExitOn()?1:0);log_(l);}
        curHash_=h;curReadable_=readable;
        Exit e;e.on=shopExitOn();e.online=c.online;e.killed=c.killed;e.readable=readable;e.changed=changed;e.inShop=lsc!=0;
        e.outFrames=firstOut_?frame-firstOut_:0u;e.now=c.now;e.r80=c.r80;e.r8=c.r8;e.r16=c.r16;e.r72=c.r72;e.lastSave=lastSave_;e.everSaved=everSaved_;
        if(lastInShop_==0 && !lsc)e.outFrames=SettleFrames;                // never in a shop this session
        Why x=exitCheap(e);
        if(x==Why::Save){Session s;
            s.gip=one(nat,n::Gip,{})==1u;s.transition=one(nat,n::InTransition,{})!=0u;s.switching=one(nat,n::SwitchInProgress,{})!=0u;
            s.cutscene=one(nat,n::CutsceneActive,{})!=0u || one(nat,n::CutscenePlaying,{})!=0u;s.fadedIn=one(nat,n::FadedIn,{})==1u;
            s.activity=one(nat,n::ActivitySession,{})!=0u;s.mission=one(nat,n::MissionFlag,{})!=0u;
            const std::uint32_t pid=one(nat,n::PlayerId,{});s.playing=pid!=0xFFFFFFFFu && one(nat,n::PlayerPlaying,{pid})==1u;x=exitDecide(e,s);}
        if(e.on && e.changed && x!=lastExit_ && exitLines_<30){lastExit_=x;++exitLines_;if(x!=Why::Save){char l[140];
            std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS shop-exit save held: %s now=%u hash=%08x saved=%08x lsc=%d",name(x),c.now,h,savedHash_,lsc);log_(l);}}
        if(x!=Why::Save)return false;
        const std::uint32_t a[3]{0u,0u,0u};std::uint32_t out[3]{};const bool ok=nat.call(n::StatSave,a,3,out);++exitAsked_;
        char l[220];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS shop-exit save #%u: STAT_SAVE(0,FALSE,0) %s now=%u vehicle block %08x -> %08x (slot %d w0=%d) out of the shop %u frames",
            exitAsked_,ok?"asked":"NOT AVAILABLE",c.now,savedHash_,h,int(cur),int(w[0]),e.outFrames);log_(l);
        markSaved(nat,c.now,true);last_=Why::Save;return true;
    }
    static std::uint32_t one(recomp::trainer::Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};return nat.call(h,a.begin(),unsigned(a.size()),out)?out[0]:0xFFFFFFFFu;}
    void money(recomp::trainer::Natives& nat,Dirty& d) noexcept {
        const std::uint32_t w=one(nat,n::Wallet,{0xFFFFFFFFu}),b=one(nat,n::Bank,{});
        if(w==0xFFFFFFFFu || b==0xFFFFFFFFu)return;
        if(!moneyAtKnown_){walletAt_=std::int32_t(w);bankAt_=std::int32_t(b);moneyAtKnown_=true;}   // the first read is the baseline
        d.moneyKnown=true;d.wallet=std::int32_t(w);d.bank=std::int32_t(b);d.walletAt=walletAt_;d.bankAt=bankAt_;
    }
    void markSaved(recomp::trainer::Natives& nat,std::uint32_t now,bool ours) noexcept {
        if(!ours && everSaved_ && std::int32_t(now-lastSave_)<5000)return;   // the same queued request seen again
        lastSave_=now;everSaved_=true;writesAt_=statWrites.load(std::memory_order_relaxed);
        if(vehBase_ && curReadable_)savedHash_=curHash_;   // v1590: what this save carries (the block as of this tick)
        const std::uint32_t w=one(nat,n::Wallet,{0xFFFFFFFFu}),b=one(nat,n::Bank,{});
        if(w!=0xFFFFFFFFu && b!=0xFFFFFFFFu){walletAt_=std::int32_t(w);bankAt_=std::int32_t(b);moneyAtKnown_=true;}
        if(!ours){char l[120];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS periodic local save: a game save was queued (R+8) now=%u; the interval restarts",now);log_(l);}
    }
    void note(Why w,std::uint32_t now) noexcept {   // one line per change of the reason (after the first deadline), at most 60
        if(w==last_ || lines_>=60)return;
        last_=w;if(w==Why::NotArmed || w==Why::NotOnline)return;
        ++lines_;char l[120];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS periodic local save held: %s now=%u",name(w),now);log_(l);
    }
    std::uint32_t lastInShop_=0,firstOut_=0,savedHash_=0,curHash_=0,lastHash_=0,lastCur_=0xFFFFFFFEu,vehLines_=0,exitAsked_=0,exitLines_=0;int lastLsc_=-1;
    bool vehBase_=false,curReadable_=false;Why lastExit_=Why::NotArmed;
    LogFn log_=nullptr;std::uint32_t lastBeat_=0,beats_=0,lastFrame_=0,lastSave_=0,writesAt_=0,asked_=0,lines_=0;bool everSaved_=false,moneyAtKnown_=false;
    std::int32_t walletAt_=0,bankAt_=0;Why last_=Why::NotArmed;
};
}
