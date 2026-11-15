// online_stats_hook.inc (v1088, trainer-mods; v1089: with a saved file for the slot STAT_LOAD takes the game's own load path (online_stats_cloud_hook.inc), the local mark only without one; lead 2026-11-08: "local MP stats: STAT_LOAD / STAT_LOAD_PENDING / STAT_SLOT_IS_LOADED /
// STAT_CLOUD_SLOT_LOAD_FAILED, plus the cloud gate for the stats path only"). Included by renderer_boundary_trace.cpp after char_probe_hook.inc.
// Evidence (this executable, read from the recompiled handlers, 2026-11-08):
//  - The MP stats slot state lives at 0x83A31F80: +44 a "needs load" bit per slot (1 << slot, slots 0..5; STAT_SLOT_IS_LOADED 7a299c13
//    sub_82DB4C68 = !(mask44 & bit)), +56 a "load failed" bit per slot (STAT_CLOUD_SLOT_LOAD_FAILED 0bf0f4b2 sub_82DB4CB0), +60 the failure
//    codes (STAT_CLOUD_SLOT_LOAD_FAILED_CODE ce6b62b5 sub_82DB4CF0). Many native paths test bit 0 of both masks before any MP stats work
//    (sub_82AB1838 / sub_82AB1C00 / sub_82AB2678).
//  - STAT_LOAD 9e5629f4 (sub_82DB4AD0 -> sub_82D9F588(slot)): signed in (sub_82267F78(-1, 1)), no request running (sub_82AAF860(-1),
//    sub_82AAF878), the slot's needs-load bit set, then the CLOUD check sub_824A7CB8 (signed online && 0x83B13BA7 && the user flag). Without
//    the Rockstar cloud that check fails and STAT_LOAD returns 1 having requested nothing: the slot then stays "not loaded, not pending, not
//    failed" for ever (maintransition's state 4 waits on it: the falsifiable hypothesis H-STATS).
// This hook, Online mode only (files/private-trace/diag-cloud-real keeps the real path, as online_spoof_hook.inc): after the original STAT_LOAD,
// if the slot still needs a load and no request for it is running, the slot is marked loaded locally (its needs-load and failed bits cleared)
// and STAT_LOAD answers 1. The slot then holds the game's in-memory values (defaults for a fresh process): MP stats PERSISTENCE across visits
// is NOT here (the cloud-file layer behind 0x83A31F8C is not traced yet). Story / Sandbox: the original only.
// Read only, every mode: STAT_LOAD_PENDING / STAT_SLOT_IS_LOADED / STAT_CLOUD_SLOT_LOAD_FAILED(_CODE) log each new (slot, result) once per
// native (at most StatLogMax lines each), so the run shows what maintransition asked and what it got.
// LINK CONDITION: redirect-v1088-stats.ld (= redirect-v1082-charprobe2.ld without the five stats wrappers and the economy log wrappers in
// online_econ_log.inc). "RENDER_ONLINE_STATS" lines prove it.
#include <atomic>
#include "online_stats_local.h"
#include "online_log_key.h"   // v1098: the (slot, result, script, ip) key
namespace online_stats_hook {
constexpr std::uint32_t SlotState=0x83A31F80u,NeedsLoad=SlotState+44u,LoadFailed=SlotState+56u,Request=SlotState+12u;
constexpr unsigned StatLogMax=40;   // v1098: 24 -> 40
enum Which : unsigned {Load,Pending,IsLoaded,Failed,FailedCode,WhichCount};
inline std::atomic<unsigned> lines[WhichCount]{};
inline ::recomp::online_log::Seen<160> seen[WhichCount];   // v1098: keyed on (slot, result, script, ip), not (slot, result)
inline std::uint32_t arg0(std::uint8_t* base,std::uint32_t ctxp) noexcept {const std::uint32_t a=PPC_LOAD_U32(ctxp+8u);return a?PPC_LOAD_U32(a):0xFFFFFFFFu;}
inline std::uint32_t ret(std::uint8_t* base,std::uint32_t ctxp) noexcept {const std::uint32_t r=PPC_LOAD_U32(ctxp);return r?PPC_LOAD_U32(r):0xFFFFFFFFu;}
inline bool localOn() noexcept {return loading_hold_hook::online() && !online_spoof_hook::cloudReal();}
// sub_82E13470(Request, slot): a request is running for this slot
inline bool requestFor(std::uint8_t* base,int slot) noexcept {
    return (PPC_LOAD_U8(Request+76u)&0x80u) && PPC_LOAD_U8(Request+72u) && int(PPC_LOAD_U32(Request+36u))==slot;}
inline void readLog(std::uint8_t* base,Which w,const char* name,std::uint32_t slot,std::uint32_t r,std::uint32_t lr,std::uint32_t script=0,std::uint32_t ip=0) noexcept {
    if(slot>5u || lines[w].load(std::memory_order_relaxed)>=StatLogMax)return;
    if(!seen[w].fresh(::recomp::online_log::statKey(slot,r,script,ip)))return;
    lines[w].fetch_add(1,std::memory_order_relaxed);
    char l[240];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS %s slot=%u -> %d (needs-load %08x failed %08x, mode=%s) lr=%08x script=%08x ip=%x",name,slot,int(r),
        PPC_LOAD_U32(NeedsLoad),PPC_LOAD_U32(LoadFailed),loading_hold_hook::online()?"online":"story/sandbox",lr,script,ip);   // v1096: + the calling script / IP
    loading_hold_hook::log(l);
}
}
PPC_FUNC_IMPL(__imp__sub_82DB4AD0);
PPC_FUNC(sub_82DB4AD0) {   // STAT_LOAD (slot) -> bool
    namespace sh=online_stats_hook;
    const std::uint32_t c=ctx.r3.u32,lr=std::uint32_t(ctx.lr),slot=sh::arg0(base,c);   // read before: the result goes over argument 0
    // v1089: a saved file for this slot -> the game's own load path (online_stats_cloud_hook.inc opens STAT_LOAD's cloud gate for this call)
    const bool saved=slot<=5u && sh::localOn() && online_stats_cloud::fileFor(int(slot));
    if(saved)online_stats_cloud::loadGateSlot.store(int(slot),std::memory_order_relaxed);
    __imp__sub_82DB4AD0(ctx,base);
    online_stats_cloud::loadGateSlot.store(-1,std::memory_order_relaxed);
    std::uint32_t r=sh::ret(base,c);
    const std::uint32_t t23=ctx.r23.u32,lsc=t23>=0x40000000u?PPC_LOAD_U32(t23+4u):0u,lip=t23>=0x40000000u?PPC_LOAD_U32(t23+12u):0u;   // v1098
    if(saved) {
        char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS STAT_LOAD slot=%u saved file: the game's own load path -> %d (needs-load %08x) lr=%08x",slot,int(r),PPC_LOAD_U32(sh::NeedsLoad),lr);
        loading_hold_hook::log(l);return;
    }
    if(slot<=5u && sh::localOn()) {
        const ::recomp::online_stats::Local d=::recomp::online_stats::decide(slot,PPC_LOAD_U32(sh::NeedsLoad),PPC_LOAD_U32(sh::LoadFailed),sh::requestFor(base,int(slot)));
        if(d.mark) {
            PPC_STORE_U32(sh::NeedsLoad,d.need);PPC_STORE_U32(sh::LoadFailed,d.failed);
            const std::uint32_t rp=PPC_LOAD_U32(c);if(rp)PPC_STORE_U32(rp,1u);
            char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_STATS STAT_LOAD slot=%u local: marked loaded (original answered %d, no cloud request; in-memory values, no persistence) lr=%08x",slot,int(r),lr);
            loading_hold_hook::log(l);r=1u;
            sh::lines[sh::Load].fetch_add(1,std::memory_order_relaxed);(void)sh::seen[sh::Load].fresh(::recomp::online_log::statKey(slot,r,lsc,lip));return;
        }
    }
    sh::readLog(base,sh::Load,"STAT_LOAD",slot,r,lr,lsc,lip);
}
#define ONLINE_STATS_READ(SUB,W,NAME) \
PPC_FUNC_IMPL(__imp__##SUB); \
PPC_FUNC(SUB) { \
    const std::uint32_t c=ctx.r3.u32,lr=std::uint32_t(ctx.lr),slot=online_stats_hook::arg0(base,c); \
    __imp__##SUB(ctx,base); \
    const std::uint32_t t23=ctx.r23.u32; \
    online_stats_hook::readLog(base,online_stats_hook::W,NAME,slot,online_stats_hook::ret(base,c),lr,t23>=0x40000000u?PPC_LOAD_U32(t23+4u):0u,t23>=0x40000000u?PPC_LOAD_U32(t23+12u):0u); \
}
ONLINE_STATS_READ(sub_82DB4B68,Pending,"STAT_LOAD_PENDING")
ONLINE_STATS_READ(sub_82DB4C68,IsLoaded,"STAT_SLOT_IS_LOADED")
ONLINE_STATS_READ(sub_82DB4CB0,Failed,"STAT_CLOUD_SLOT_LOAD_FAILED")
ONLINE_STATS_READ(sub_82DB4CF0,FailedCode,"STAT_CLOUD_SLOT_LOAD_FAILED_CODE")
#undef ONLINE_STATS_READ
