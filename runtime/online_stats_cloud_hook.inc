// online_stats_cloud_hook.inc (v1089, trainer-mods; lead 2026-11-08 04:2x: MP stats saved between visits, served from the Online root, defaults
// never overwrite a saved file). Included by renderer_boundary_trace.cpp BEFORE online_stats_hook.inc (its STAT_LOAD reads the gate below).
// The game's own path (host trace 2026-11-08, notes trainer-mods-stats-save-trace.md):
//  - STAT_LOAD (sub_82D9F588) and the stats request object (0x83A31F8C: start sub_82E13A48, tick sub_82E13528) queue a stats task only
//    when sub_824A7CB8 (signed online && byte 0x83B13BA7 && the user flag) answers 1; STAT_SAVE (sub_82D9F658) asks sub_824A7CA0 (the byte).
//  - The task (vtable 0x82040444, update sub_82959F70 -> sub_82959C10; slot at +8, the request status at +20): state 2 LOADS through
//    sub_82950A10(status, path, gamer, ..) = an rlCloud GET into the grow buffer at status+12; state 0 SAVES through
//    sub_82950B38(status, buf, size, path, gamer, ..) = an rlCloud POST. Status: sub_8257EDA0(status, op, code): op 1 pending, op 2 failed
//    (state 2), op 3 succeeded (state 3); the update parses a load only on state 3 (sub_829520B0) and reads a failed load's HTTP code
//    (404 / 408 / 500) at status+4.
// This hook, Online mode only (files/private-trace/diag-cloud-real keeps everything real):
//  1. the four gate sites answer 1: STAT_LOAD's (lr 0x82D9F620) only when the slot's file exists (else v1088's local mark stays: defaults on a
//     first visit), the request start / tick (0x82E13AF4 / 0x82E135FC), STAT_SAVE's sub_824A7CA0 (0x82D9F6F4). Every other caller: the original.
//  2. sub_82950A10 (load): the slot's file (online_stats_store.h) into the grow buffer through the game's own buffer calls (sub_831AEF20 init,
//     sub_831AB4E0 reserve, sub_831AB3D0 length), then op 1 + op 3 (200); no file: op 1 + op 2 (404, the game's own "no save").
//  3. sub_82950B38 (save): the buffer to the slot's file when the guard allows (online_stats_store.h Guard), then op 1 + op 3 (200);
//     refused or a write error: op 1 + op 2 (-1), nothing written.
// One log line per call (RENDER_ONLINE_STATS cloud ...). LINK CONDITION: redirect-v1089-statsave.ld (= redirect-v1088-stats.ld without the
// wrappers of sub_824A7CB8, sub_824A7CA0, sub_82950A10, sub_82950B38).
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <vector>
#include "save_sandbox.h"
#include "online_stats_store.h"
#include "online_stats_reload.h"   // v1507 (garage): the STAT_CLEAR_SLOT_FOR_RELOAD gate
PPC_FUNC(sub_82F91CD8);   // the default allocator (lwz 0x83B13B54)
PPC_FUNC(sub_831AEF20);   // grow buffer init (buf, allocator, flags, growth)
PPC_FUNC(sub_831AB4E0);   // grow buffer reserve (buf, bytes) -> 1 ok
PPC_FUNC(sub_831AB3D0);   // grow buffer set length (buf, bytes)
PPC_FUNC(sub_8257EDA0);   // netStatus set (status, op, code)
namespace online_stats_cloud {
constexpr std::uint32_t LrStatLoad=0x82D9F620u,LrReqStart=0x82E13AF4u,LrReqTick=0x82E135FCu,LrStatSave=0x82D9F6F4u;
// v1148 (MP stats persistence): the stats manager's per-frame update sub_824F6C08 asks the same gate first (lr 0x824F6C1C) and returns at once
// when it is false - offline it always was, so the queued STAT_SAVE requests (R+8, due time R+0) were never serviced and nothing reached the
// save leg below (t-onlrace-post-20261109-1125: "requested 00000001 ... task 0"). Answered 1 at that site too, Online local mode only.
constexpr std::uint32_t LrStatsUpdate=0x824F6C1Cu;
constexpr std::uint32_t LrSaveStart=0x82E13C2Cu;   // v1163: sub_82E13B68 (the save task start)
inline std::atomic<bool> saveStartLogged{false};
// v1148: the game's generic save request sub_82AB1C00(reason, delay) (6 callers: the stat sets / events that ask for a save) asks the cloud
// byte sub_824A7CA0 first (lr 0x82AB1C24) and queues nothing when it is false; only STAT_SAVE's own site was answered, so in Online local mode
// the requested mask stayed 0 (t-onlshop-quick-20261109-2010). Answered 1 at that site too.
constexpr std::uint32_t LrSaveRequest=0x82AB1C24u;
// v1153: after the update, sub_824F7E30 asks the same cloud byte (lr 0x824F7E68) before its own periodic save: every 120000 ms (r30 =
// 0x1D4C0) it calls sub_82AAFA80(1), the stats manager's save request. False offline, so Online local mode never autosaved MP stats.
// Note: v1148's LrStatsUpdate and v1149's LrUpdateCaller sites were direct body calls (__imp__) that bypassed these hooks until v1153
// routed them through the wrappers (update-1/route-hooks/route_hooked_calls.py, sites-v1153.txt).
constexpr std::uint32_t LrPeriodicSave=0x824F7E68u;
inline std::atomic<bool> requestLogged{false};
inline std::atomic<bool> periodicLogged{false};
inline std::atomic<bool> updateLogged{false};
inline bool localOn() noexcept {return loading_hold_hook::online() && !online_spoof_hook::cloudReal();}
// v1157 (lead 00:0x): ONE off switch for every stats-route answer since v1148 (update gate 0x824F6C1C, save request 0x82AB1C24, v1149 caller
// 0x824F7E4C, periodic save 0x824F7E68, the v1156 sign-in checks 0x824F6C2C / 0x824F6C50): files/private-trace/diag-stats-route-off = v1147's
// stats behaviour (the v1089 legs and gates only). Default absent = on.
inline bool routeOff() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-stats-route-off"),F_OK)==0;return o;}
inline bool routeOn() noexcept {return localOn() && !routeOff();}
// v1507: STAT_CLEAR_SLOT_FOR_RELOAD's gate answer off (= v1183): files/private-trace/diag-stats-reload-off (read once)
inline bool reloadOff() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-stats-reload-off"),F_OK)==0;return o;}
inline bool Guard_valid(int slot) noexcept {return ::recomp::online_stats_store::Guard::valid(slot);}
inline std::atomic<int> loadGateSlot{-1};      // set by STAT_LOAD around its original when the slot's file exists
inline ::recomp::online_stats_store::Guard guard;   // script thread only
inline ::recomp::online_stats_store::Files& files() {
    static ::recomp::online_stats_store::Files f([]{const char* sb=std::getenv("RECOMP_SAVES_DIR");
        return ::recomp::bootstrap::save_sandbox::filesRootFor(sb?sb:RECOMP_APP_PATH(""));}());
    return f;
}
inline bool fileFor(int slot) {return files().usable() && files().exists(slot);}
inline std::atomic<unsigned> gateLogged{0};
inline void gateLog(const char* site) noexcept {
    const unsigned bit=site[0]=='l'?1u:site[0]=='s'?2u:site[0]=='t'?4u:8u;
    if(!(gateLogged.fetch_or(bit,std::memory_order_relaxed)&bit)){char l[160];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS cloud gate %s answered 1 (Online mode; the stats path only)",site);loading_hold_hook::log(l);}
}
inline int slotOf(std::uint8_t* base,std::uint32_t status) noexcept {   // the task: status = task + 20, slot = task + 8
    const int s=int(PPC_LOAD_U32(status-12u));return ::recomp::online_stats_store::Guard::valid(s)?s:-1;}
inline void guestPath(std::uint8_t* base,std::uint32_t at,char* out,int cap) noexcept {
    int k=0;if(at>=0x40000000u)for(;k<cap-1;++k){const char c=char(PPC_LOAD_U8(at+std::uint32_t(k)));if(!c)break;out[k]=(c>=32 && c<127)?c:'?';}out[k]=0;}
inline void setStatus(PPCContext& ctx,std::uint8_t* base,std::uint32_t status,int op,int code) {
    ctx.r3.u64=status;ctx.r4.s64=op;ctx.r5.s64=code;sub_8257EDA0(ctx,base);}
}
PPC_FUNC_IMPL(__imp__sub_824A7CB8);
PPC_FUNC(sub_824A7CB8) {   // signed online && cloud byte && user flag
    namespace oc=online_stats_cloud;
    const std::uint32_t lr=std::uint32_t(ctx.lr);
    // v1507 (garage, subagents-2026-11-11/garage/REPORT.md): STAT_CLEAR_SLOT_FOR_RELOAD's gate (lr 0x82D9F530, slot in r31). maintransition
    // resets the script MP save block G115227 (garage vehicles) and then reloads the character slot through CLEAR + STAT_LOAD; with the gate
    // at 0 the clear was refused and the reload never read the file. Answered 1 only with the slot's saved file; kill switches
    // diag-stats-route-off and files/private-trace/diag-stats-reload-off.
    if(lr==::recomp::online_stats_reload::LrClearReload) {
        const int slot=int(ctx.r31.s32);
        const int a=::recomp::online_stats_reload::answer(oc::localOn(),oc::routeOff(),oc::reloadOff(),lr,slot,oc::Guard_valid(slot) && oc::fileFor(slot));
        static std::atomic<unsigned> seen{0};
        const unsigned bit=(slot>=0 && slot<6)?(1u<<unsigned(slot)):0x40u;
        if(oc::localOn() && !(seen.fetch_or(bit,std::memory_order_relaxed)&bit)) {
            char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS cloud gate clear-for-reload slot=%d (STAT_CLEAR_SLOT_FOR_RELOAD) %s (needs-load %08x)",slot,
                a==1?"answered 1 (a saved file: the reload reads it)":"original (no saved file or switched off)",PPC_LOAD_U32(0x83A31FACu));
            loading_hold_hook::log(l);}
        if(a==1){ctx.r3.u64=1;return;}
    }
    if(oc::localOn()) {
        if(lr==oc::LrStatLoad && oc::loadGateSlot.load(std::memory_order_relaxed)>=0){oc::gateLog("load (STAT_LOAD, a saved file)");ctx.r3.u64=1;return;}
        if(lr==oc::LrReqStart){oc::gateLog("start (stats request)");ctx.r3.u64=1;return;}
        if(lr==oc::LrReqTick){oc::gateLog("tick (stats request)");ctx.r3.u64=1;return;}
        if(lr==oc::LrStatsUpdate && !oc::routeOff()){   // v1148 (v1157: diag-stats-route-off)
            if(!oc::updateLogged.exchange(true,std::memory_order_relaxed))loading_hold_hook::log("RENDER_ONLINE_STATS cloud gate update (stats manager sub_824F6C08) answered 1 (Online mode; the stats path only)");
            ctx.r3.u64=1;return;}
        // v1163 (H-START): the save task's start sub_82E13B68(R, 0, 0) (called by the update at 0x6F9C once a requested slot is ready, which
        // t-onlcash162 showed every frame: sub_82E138B8 -> 1, R+8 = 1) asks the same cloud gate (lr 0x82E13C2C) and returns without starting
        // when it is false - offline always. Answered 1 at that site, Online local mode, under diag-stats-route-off.
        // v1183 (Snapmatic, t-onlcam v1182: the photo op failed at once - substate 3, error 65 - and then waits forever for the player to
        // acknowledge the save system's cloud warning, sub_82948308): the photo manager's listing request sub_8294F460 asks this gate first
        // (lr 0x8294F480) and fails when it is false - offline always. Answered 1 there, Online local mode only; the request then reaches
        // sub_82FB4438, answered by online_photo_cloud_hook.inc (an empty cloud gallery). diag-cloud-real keeps everything real.
        if(lr==0x8294F480u){static std::atomic<bool> logged{false};if(!logged.exchange(true,std::memory_order_relaxed))loading_hold_hook::log("RENDER_ONLINE_PHOTOS cloud gate (the photo manager, sub_8294F460) answered 1 (Online local mode)");
            ctx.r3.u64=1;return;}
        if(lr==oc::LrSaveStart && !oc::routeOff()){
            if(!oc::saveStartLogged.exchange(true,std::memory_order_relaxed))loading_hold_hook::log("RENDER_ONLINE_STATS cloud gate save start (sub_82E13B68) answered 1 (Online mode; the stats path only)");
            ctx.r3.u64=1;return;}
    }
    __imp__sub_824A7CB8(ctx,base);
}
// v1149 (MP stats persistence): the stats manager's per-frame update sub_824F6C08 is only called by sub_824F7E30 when sub_8253C548 is true
// (word 0x83B13BE4 != 0 = the signed-in online gamer, written only by the Live sign-in message sub_82FD8F40 -> sub_82FD68A8). Offline it is 0,
// so the update never ran and no queued save was ever serviced (t-onlshop-quick-20261109-2035: no update gate line). Answered 1 at that one
// call site (lr 0x824F7E4C) in Online local mode; the other three callers (NETWORK_IS_SIGNED_IN among them) keep the original.
// LINK CONDITION: redirect-v1149-signin.ld (= redirect-v1132-events.ld without the mangled alias of sub_8253C548). Kill switch
// files/private-trace/diag-stats-update-off (read once).
namespace online_stats_cloud {
constexpr std::uint32_t LrUpdateCaller=0x824F7E4Cu;
inline bool updateOff() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-stats-update-off"),F_OK)==0;return o;}
inline std::atomic<bool> signinLogged{false};
}
// v1150 PROBE (read only; t-onlshop-quick-20261109-2058 on v1149: no "update enabled" line - sub_824F7E30 never called sub_8253C548): the chain
// to the stats manager update, each logged at its first call (every mode, one line each): sub_82491368 (no direct caller; the call to sub_824F7FB0
// at 0x82491B2C), sub_824F7FB0 (-> sub_824F7E30(r3+12)), sub_824F7E30 (-> sub_8253C548, then the update), and the first 8 (lr, result) pairs of
// sub_8253C548. LINK CONDITION: redirect-v1150-statsprobe.ld (= redirect-v1149-signin.ld without the aliases of those three).
namespace online_stats_cloud {
inline std::atomic<unsigned> chainSeen{0};
inline void chainLog(unsigned bit,const char* what,std::uint32_t r3,std::uint32_t lr) noexcept {
    if(chainSeen.fetch_or(bit,std::memory_order_relaxed)&bit)return;
    char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS chain %s first call r3=%08x lr=%08x mode=%s",what,r3,lr,loading_hold_hook::online()?"online":"story/sandbox");
    loading_hold_hook::log(l);}
inline std::atomic<unsigned> signinPairs{0};inline std::uint32_t signinLr[8]{};
}
PPC_FUNC_IMPL(__imp__sub_82491368);
PPC_FUNC(sub_82491368) {
    online_stats_cloud::chainLog(1u,"sub_82491368",ctx.r3.u32,std::uint32_t(ctx.lr));
    {   // v1151 PROBE (read only): its three early exits before the update call - halfword 0x83A31F88 == 0, sub_823CC3D0() (= *(*(0x8394B8B0)+4))
        // == 0, that manager's +2912 == 0 - logged on change, at most 24 lines
        static std::uint32_t last[3]{0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu};static unsigned lines=0;
        const std::uint32_t h=PPC_LOAD_U16(0x83A31F88u),p=PPC_LOAD_U32(0x8394B8B0u),mgr=(p>=0x10000u)?PPC_LOAD_U32(p+4u):0u,f=(mgr>=0x10000u)?PPC_LOAD_U32(mgr+2912u):0u;
        const std::uint32_t v[3]{h,mgr,f};
        if(std::memcmp(v,last,sizeof v)!=0 && lines<24u){std::memcpy(last,v,sizeof v);++lines;char l[220];
            std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS chain sub_82491368 gates: half83A31F88=%u sessionmgr=%08x +2912=%08x -> %s mode=%s",h,mgr,f,
                h==0u?"exit (halfword 0)":mgr==0u?"exit (no session manager)":f==0u?"exit (+2912 null)":"continues",loading_hold_hook::online()?"online":"story/sandbox");
            loading_hold_hook::log(l);}
    }
    __imp__sub_82491368(ctx,base);
}
PPC_FUNC_IMPL(__imp__sub_824F7FB0);
PPC_FUNC(sub_824F7FB0) {online_stats_cloud::chainLog(2u,"sub_824F7FB0",ctx.r3.u32,std::uint32_t(ctx.lr));__imp__sub_824F7FB0(ctx,base);}
PPC_FUNC_IMPL(__imp__sub_824F7E30);
PPC_FUNC(sub_824F7E30) {online_stats_cloud::chainLog(4u,"sub_824F7E30",ctx.r3.u32,std::uint32_t(ctx.lr));__imp__sub_824F7E30(ctx,base);}
PPC_FUNC_IMPL(__imp__sub_8253C548);
PPC_FUNC(sub_8253C548) {   // word 0x83B13BE4 != 0
    namespace oc=online_stats_cloud;
    {const std::uint32_t lr=std::uint32_t(ctx.lr);unsigned n=oc::signinPairs.load(std::memory_order_relaxed);bool seen=false;
     for(unsigned i=0;i<n && i<8u;++i)if(oc::signinLr[i]==lr)seen=true;
     if(!seen && n<8u){oc::signinLr[n]=lr;oc::signinPairs.store(n+1,std::memory_order_relaxed);
        char l[180];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS chain sub_8253C548 caller lr=%08x word83B13BE4=%08x mode=%s",lr,PPC_LOAD_U32(0x83B13BE4u),loading_hold_hook::online()?"online":"story/sandbox");
        loading_hold_hook::log(l);}}   // v1150 probe
    if(oc::routeOn() && !oc::updateOff() && std::uint32_t(ctx.lr)==oc::LrUpdateCaller) {
        if(!oc::signinLogged.exchange(true,std::memory_order_relaxed))loading_hold_hook::log("RENDER_ONLINE_STATS stats manager update enabled (sub_8253C548 answered 1 at sub_824F7E30, Online local mode)");
        ctx.r3.u64=1;return;}
    __imp__sub_8253C548(ctx,base);
}
PPC_FUNC_IMPL(__imp__sub_824A7CA0);
PPC_FUNC(sub_824A7CA0) {   // the cloud byte 0x83B13BA7
    namespace oc=online_stats_cloud;
    if(oc::localOn() && std::uint32_t(ctx.lr)==oc::LrStatSave){oc::gateLog("save (STAT_SAVE)");ctx.r3.u64=1;return;}
    if(oc::routeOn() && std::uint32_t(ctx.lr)==oc::LrSaveRequest){   // v1148
        if(!oc::requestLogged.exchange(true,std::memory_order_relaxed))loading_hold_hook::log("RENDER_ONLINE_STATS cloud gate save request (sub_82AB1C00) answered 1 (Online mode; the stats path only)");
        ctx.r3.u64=1;return;}
    if(oc::routeOn() && std::uint32_t(ctx.lr)==oc::LrPeriodicSave){   // v1153
        if(!oc::periodicLogged.exchange(true,std::memory_order_relaxed))loading_hold_hook::log("RENDER_ONLINE_STATS cloud gate periodic save (sub_824F7E30, every 120 s) answered 1 (Online mode; the stats path only)");
        // v1154 READ ONLY (t-onlcash153: the gate was asked, no save in 4 min): the state sub_824F7E30 reads next. r31 = its request
        // object R (0x83A31F8C). Its save needs byte 0x83B13AEB != 0, R+76 bit 0x40 set, R+72 == 0 when R+76 & 0x80, and R+80 < now
        // (sub_824AE6D8); then it calls sub_82AAFA80(1) unless R+76 bit 0x40 stays set. Every 30 s, at most 12 lines.
        {static std::atomic<long long> next{0};static std::atomic<int> n{0};
         const long long now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
         long long due=next.load(std::memory_order_relaxed);
         if(now>=due && n.load(std::memory_order_relaxed)<12 && next.compare_exchange_strong(due,now+30000,std::memory_order_relaxed)){
            n.fetch_add(1,std::memory_order_relaxed);const std::uint32_t R=ctx.r31.u32;
            char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS periodic save state R=%08x R+72=%02x R+76=%02x R+80=%u byte83B13AEB=%02x (read only)",
                R,R>=0x10000u?unsigned(PPC_LOAD_U8(R+72u)):0u,R>=0x10000u?unsigned(PPC_LOAD_U8(R+76u)):0u,R>=0x10000u?PPC_LOAD_U32(R+80u):0u,
                unsigned(PPC_LOAD_U8(0x83B13AEBu)));
            loading_hold_hook::log(l);
            // v1161 READ ONLY (t-onlcash160: no save; R+32 = needs-load, already clear for slots 0-1): what the update sub_824F6C08 decides on
            // at 0x6E50 / 0x6FC8 / 0x6F9C: R+0 due time, R+4 slot, R+8 requested mask, R+16 cloud task in flight, R+20 retries, R+24, R+28,
            // R+32 needs-load, R+36; the busy byte sub_823951D8(0x837D05E0) reads (word==3 ? +384 : +16, then +16); the cloud task status
            // word 0x837C6F18 (sub_82940C00: 1 running, 2 done) and the HTTP code word 0x839D9B9C.
            if(R>=0x10000u){
                const std::uint32_t o=0x837D05E0u,ob=(PPC_LOAD_U32(o)==3u?o+384u:o+16u)+16u;
                char m[260];std::snprintf(m,sizeof m,"RENDER_ONLINE_STATS update state R+0=%u R+4=%d R+8=%08x R+16=%02x R+20=%u R+24=%08x R+28=%02x R+32=%08x R+36=%d busy837D05E0=%02x task=%u http=%d (read only)",
                    PPC_LOAD_U32(R),int(PPC_LOAD_U32(R+4u)),PPC_LOAD_U32(R+8u),unsigned(PPC_LOAD_U8(R+16u)),PPC_LOAD_U32(R+20u),PPC_LOAD_U32(R+24u),
                    unsigned(PPC_LOAD_U8(R+28u)),PPC_LOAD_U32(R+32u),int(PPC_LOAD_U32(R+36u)),unsigned(PPC_LOAD_U8(ob)),PPC_LOAD_U32(0x837C6F18u),int(PPC_LOAD_U32(0x839D9B9Cu)));
                loading_hold_hook::log(m);
                // v1162 READ ONLY: the clock both the queue (R+0 = now + delay) and the update's checks use (sub_824AE6D8 -> sub_8363B098)
                const std::uint32_t co=PPC_LOAD_U32(0x82000A38u),now=co>=0x10000u?PPC_LOAD_U32(co+16u):0u;
                char k[120];std::snprintf(k,sizeof k,"RENDER_ONLINE_STATS update clock now=%u (R+0=%u R+80=%u) (read only)",now,PPC_LOAD_U32(R),PPC_LOAD_U32(R+80u));
                loading_hold_hook::log(k);}}}
        ctx.r3.u64=1;return;}
    __imp__sub_824A7CA0(ctx,base);
}
// v1156 (MP stats persistence): after its cloud gate the stats update sub_824F6C08 reads two Live sign-in tables, indexed by the active user
// (word 0x8381CFE4): sub_82274A68 = byte 0x83B13934[user] (lr 0x824F6C2C; if 0, sub_82274A30 = word 0x83A0376C ends the update when 1) and
// sub_82267F78(-1, 1) = byte 0x83B1394C[user] (lr 0x824F6C50; 0 = the signed-out path, which never services the queued saves).
// t-onlcash154 (v1154): STAT_SAVE answered, periodic save held (R+76 = c0, R+80 = 0), no save leg. Offline those tables stay 0. Answered 1 at
// these two sites only, Online local mode; the original answer is logged (first call and every change). Every other caller unchanged.
namespace online_stats_cloud {
constexpr std::uint32_t LrUpdateSignedIn=0x824F6C2Cu,LrUpdateOnline=0x824F6C50u;
constexpr std::uint32_t LrStartSignedIn=0x82E13C3Cu;   // v1163: sub_82E13B68 asks the same sign-in table after its cloud gate (0 -> sub_82274A30, ends at 1)
inline std::atomic<int> lastStartSignedIn{-1};
inline void signinLog(const char* what,std::atomic<int>& last,std::uint32_t orig) noexcept {
    const int o=int(orig&0xFFu);if(last.exchange(o,std::memory_order_relaxed)==o)return;
    char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS update sign-in check %s: original %d, answered 1 (Online mode; the stats path only)",what,o);loading_hold_hook::log(l);
}
inline std::atomic<int> lastSignedIn{-1},lastOnline{-1};
}
PPC_FUNC_IMPL(__imp__sub_82274A68);
PPC_FUNC(sub_82274A68) {   // byte 0x83B13934[user]
    namespace oc=online_stats_cloud;
    if(oc::routeOn() && std::uint32_t(ctx.lr)==oc::LrUpdateSignedIn){
        __imp__sub_82274A68(ctx,base);oc::signinLog("sub_82274A68 (0x83B13934[user])",oc::lastSignedIn,ctx.r3.u32);ctx.r3.u64=1;return;}
    if(oc::routeOn() && std::uint32_t(ctx.lr)==oc::LrStartSignedIn){   // v1163 (H-START): the save task start
        __imp__sub_82274A68(ctx,base);oc::signinLog("sub_82274A68 at the save start (0x83B13934[user])",oc::lastStartSignedIn,ctx.r3.u32);ctx.r3.u64=1;return;}
    __imp__sub_82274A68(ctx,base);
}
PPC_FUNC_IMPL(__imp__sub_82267F78);
PPC_FUNC(sub_82267F78) {   // (-1, x): byte 0x83B1394C[user]
    namespace oc=online_stats_cloud;
    if(oc::routeOn() && std::uint32_t(ctx.lr)==oc::LrUpdateOnline){
        const PPCContext in=ctx;__imp__sub_82267F78(ctx,base);const std::uint32_t orig=ctx.r3.u32;
        oc::signinLog("sub_82267F78(-1) (0x83B1394C[user])",oc::lastOnline,orig);ctx=in;ctx.r3.u64=1;return;}
    __imp__sub_82267F78(ctx,base);
}
// v1162 READ ONLY (MP stats save blocker): the stats update's save-start check sub_82E138B8(R) (1 = a requested slot, R+8, whose needs-load
// bit, R+32, is clear) at its two call sites in sub_824F6C08 (lr 0x824F6F90 after a cloud load result, 0x824F6FF0 at 0x6FC8 when R+8 != 0
// and the due time R+0 has passed). t-onlcash161: R+8 = 1, R+0 = 186873 never changes - is this ever reached, and what does it answer?
// Original always runs; logged: the first 8 calls and every change of the answer.
PPC_FUNC_IMPL(__imp__sub_82E138B8);
PPC_FUNC(sub_82E138B8) {
    const std::uint32_t R=ctx.r3.u32,lr=std::uint32_t(ctx.lr);
    __imp__sub_82E138B8(ctx,base);
    static std::atomic<int> n{0},last{-1};const int ans=int(ctx.r3.u32&0xFFu);
    if(n.fetch_add(1,std::memory_order_relaxed)<8 || last.exchange(ans,std::memory_order_relaxed)!=ans) {
        const std::uint32_t co=PPC_LOAD_U32(0x82000A38u),now=co>=0x10000u?PPC_LOAD_U32(co+16u):0u;
        char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS save-start check sub_82E138B8 lr=%08x -> %d (R+8=%08x R+32=%08x R+0=%u now=%u) (read only)",
            lr,ans,R>=0x10000u?PPC_LOAD_U32(R+8u):0u,R>=0x10000u?PPC_LOAD_U32(R+32u):0u,R>=0x10000u?PPC_LOAD_U32(R):0u,now);
        loading_hold_hook::log(l);}
}
PPC_FUNC_IMPL(__imp__sub_82950A10);
PPC_FUNC(sub_82950A10) {   // the stats LOAD (rlCloud GET into status+12)
    namespace oc=online_stats_cloud;namespace os=::recomp::online_stats_store;
    if(!oc::localOn()){__imp__sub_82950A10(ctx,base);return;}
    const std::uint32_t status=ctx.r3.u32,pathAt=ctx.r4.u32;const PPCContext saved=ctx;
    const int slot=oc::slotOf(base,status);char gp[72];oc::guestPath(base,pathAt,gp,int(sizeof gp));
    PPC_STORE_U32(status+8u,1u);                                    // the original's mode word (1 = GET), read by the update's failure path
    ctx.r3.u64=status+12u;{PPCContext a=ctx;sub_82F91CD8(a,base);ctx.r4.u64=a.r3.u32;}
    ctx.r3.u64=status+12u;ctx.r5.s64=4;ctx.r6.s64=256;sub_831AEF20(ctx,base);
    std::vector<std::uint8_t> bytes;const int got=slot>=0?oc::files().read(slot,bytes):-1;
    bool ok=false;
    if(got==1) {
        ctx.r3.u64=status+12u;ctx.r4.u64=bytes.size();sub_831AB4E0(ctx,base);
        const std::uint32_t data=PPC_LOAD_U32(status+12u);
        if(ctx.r3.u32==1u && data) {
            for(std::size_t i=0;i<bytes.size();++i)PPC_STORE_U8(data+std::uint32_t(i),bytes[i]);
            // v1166 (MP stats reload, t-onlcash163 B: "cloud load slot=0 ... succeeded 200" then STAT_CLOUD_SLOT_LOAD_FAILED slot=0 -> 1,
            // failed 00000001): the grow buffer is +0 data, +4 capacity, +8 LENGTH, +12 flags. sub_831AB4E0 (reserve) sets only +0/+4;
            // +8 stays 0 from sub_831AEF20 -> sub_831AB2B0, and sub_831AB3D0 only shortens (n > +8: returns at once). So the buffer
            // read as empty: sub_82959C10's load result (state 3, bl at 0x82959E28) got sub_831A6558(task+32) == 0 and failed the task with
            // code 1 before the parser sub_829520B0 ran; the update's completion (0x824F6CA8) then cleared needs-load but set the
            // failed bit (0x824F6DFC), and nothing was applied. The game's own append sub_831AEF98 ends with +8 += n; this sets the
            // length the same way, then sub_831AB3D0 (n == +8) stores it again and writes the terminator (flags bit 4).
            PPC_STORE_U32(status+12u+8u,std::uint32_t(bytes.size()));
            ctx.r3.u64=status+12u;ctx.r4.u64=bytes.size();sub_831AB3D0(ctx,base);
            ok=PPC_LOAD_U32(status+12u+8u)==std::uint32_t(bytes.size());   // what sub_831A6558 / sub_8326C8B0 will read
        }
    }
    const std::uint32_t blen=PPC_LOAD_U32(status+12u+8u);   // v1166: the buffer length the game reads (0 = an empty result)
    oc::setStatus(ctx,base,status,1,0);
    if(ok)oc::setStatus(ctx,base,status,3,200);else oc::setStatus(ctx,base,status,2,got==0?404:-1);
    oc::guard.loaded(slot,ok?os::Load::Loaded:got==0?os::Load::Absent:os::Load::Failed);
    char l[300];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS cloud load slot=%d path=%s -> %s (%zu bytes, buffer length %u, file %s)",slot,gp,ok?"succeeded 200":got==0?"no file: failed 404":"error: failed -1",bytes.size(),
        blen,slot>=0?oc::files().path(slot).c_str():"-");loading_hold_hook::log(l);
    ctx=saved;ctx.r3.u64=1;
}
PPC_FUNC_IMPL(__imp__sub_82950B38);
PPC_FUNC(sub_82950B38) {   // the stats SAVE (rlCloud POST of buf / size)
    namespace oc=online_stats_cloud;
    if(!oc::localOn()){__imp__sub_82950B38(ctx,base);return;}
    const std::uint32_t status=ctx.r3.u32,buf=ctx.r4.u32,size=ctx.r5.u32,pathAt=ctx.r6.u32;const PPCContext saved=ctx;
    const int slot=oc::slotOf(base,status);char gp[72];oc::guestPath(base,pathAt,gp,int(sizeof gp));
    const bool exists=slot>=0 && oc::fileFor(slot);
    const bool allowed=slot>=0 && oc::files().usable() && buf && size>0 && size<=::recomp::online_stats_store::MaxBytes && oc::guard.allowSave(slot,exists);
    bool wrote=false;
    if(allowed){std::vector<std::uint8_t> bytes(size);for(std::uint32_t i=0;i<size;++i)bytes[i]=PPC_LOAD_U8(buf+i);wrote=oc::files().write(slot,bytes.data(),bytes.size());
                if(wrote)oc::guard.loaded(slot,::recomp::online_stats_store::Load::Loaded);}
    PPC_STORE_U32(status+40u,buf);PPC_STORE_U32(status+44u,size);PPC_STORE_U32(status+8u,2u);   // as the original records them
    oc::setStatus(ctx,base,status,1,0);oc::setStatus(ctx,base,status,wrote?3:2,wrote?200:-1);
    char l[240];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS cloud save slot=%d path=%s size=%u -> %s (load state %d, file existed %d)",slot,gp,size,
        wrote?"written, succeeded 200":allowed?"WRITE ERROR, failed -1":"REFUSED by the guard (a saved file this process did not load), failed -1",int(oc::guard.state(slot)),exists?1:0);
    loading_hold_hook::log(l);
    ctx=saved;ctx.r3.u64=1;
}
// v1504 (mp-stats-reload subagent 2026-11-10; lead: "find what clears slots 1-5 right after slot 0 loads; fix it the game's way"): the CHARACTER
// slot loads from mpstats-1.save. After slot 0's load completes, the stats update (0x824F6D7C, slot 0 ok) calls sub_82E139F0(R):
//  - sub_82E13788 reads MPk_CHAR_ISACTIVE (k = 0..4: hash words 0x83A043A8 / 0x83A044FC / 0x83A04BE0 / 0x83A0435C / 0x83A038DC, names at
//    0x82055308..) through sub_82AAF8C0 and, for each false one, marks character slot k+1 loaded-empty (sub_82AAF7F0(k+1,1,0)) and clears its
//    needs-load bit (R+32);
//  - sub_82E133A0(R,0,-1) picks the next slot: MPPLY_LAST_MP_CHAR (sub_82AB0F50) + 1, and for character 0 only when MP0_CHAR_ISACTIVE is true;
//  - then the game's own request tick sub_82E13528 loads that slot if its needs-load bit is still set.
// t-onlcash1502b B: needs-load 3f -> 00 at once, so mpstats-1.save (34720 bytes) was never read. The store's MP0_CHAR_ISACTIVE is blank: our
// creator flow never sets it, and the freemode gate (online_freemode_gate.h decide) answers the SCRIPTS' reads 1 in memory on a later visit.
// This answers the same stat the same way at the two native reads only (lr 0x82E137A8 in sub_82E13788 = slot 1's empty check, lr 0x82E133F8
// in sub_82E133A0 = the next-slot pick), fill blanks only (the original's 1 wins), Online local mode, fg::decide's later-visit policy (saved
// character 1, the route on, no diag-fm-seed-off), and only when mpstats-1.save exists (so the game's own load has a file to read).
// Nothing is written to the stat store. Every other caller of sub_82AAF8C0: the original. Kill switches files/private-trace/diag-stats-charslot-off
// and (with every stats-route answer) diag-stats-route-off.
// LINK CONDITION: redirect-v1504-charslot.ld (= redirect-v1164-storelocal.ld without sub_82AAF8C0).
#include "online_freemode_gate.h"
#include "online_route_switch.h"
namespace online_stats_cloud {
constexpr std::uint32_t Mp0CharIsActiveWord=0x83A043A8u,LrSlotEmptyCheck=0x82E137A8u,LrNextSlotPick=0x82E133F8u;
inline bool charSlotOff() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-stats-charslot-off"),F_OK)==0;return o;}
inline bool fmSeedOffFile() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-fm-seed-off"),F_OK)==0;return o;}
inline std::atomic<int> lastCharSlot[2]{{-1},{-1}};
}
PPC_FUNC_IMPL(__imp__sub_82AAF8C0);
PPC_FUNC(sub_82AAF8C0) {   // a stat's bool value by hash word (r3 = the word's address)
    namespace oc=online_stats_cloud;namespace fg=::recomp::freemode_gate;
    const std::uint32_t lr=std::uint32_t(ctx.lr),word=ctx.r3.u32;
    if((lr!=oc::LrSlotEmptyCheck && lr!=oc::LrNextSlotPick) || word!=oc::Mp0CharIsActiveWord){__imp__sub_82AAF8C0(ctx,base);return;}
    __imp__sub_82AAF8C0(ctx,base);
    const unsigned orig=ctx.r3.u32&0xFFu;
    const int saved=fg::savedCharacter.load(std::memory_order_acquire);
    const bool policy=fg::decide(oc::routeOn(),::recomp::fm_route::seedOff(::recomp::fm_route::routeOn(),oc::fmSeedOffFile()),saved,
                                 PPC_LOAD_U32(word),true)==fg::Answer::Bool1;
    const bool file=oc::fileFor(1);
    const bool answer=orig==0u && policy && file && !oc::charSlotOff();
    if(answer)ctx.r3.u64=1;
    const int k=lr==oc::LrSlotEmptyCheck?0:1,v=int(orig)|(answer?2:0)|(file?4:0)|(saved==1?8:0);
    if(oc::lastCharSlot[k].exchange(v,std::memory_order_relaxed)!=v) {
        char l[220];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS character slot check %s lr=%08x MP0_CHAR_ISACTIVE original %u -> %u (saved character %d, mpstats-1 %d, Online local %d)",
            k==0?"(slot 1 empty?)":"(next slot pick)",lr,orig,answer?1u:orig,saved,file?1:0,oc::localOn()?1:0);
        loading_hold_hook::log(l);}
}
