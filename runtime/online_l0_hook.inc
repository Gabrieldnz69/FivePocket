// online_l0_hook.inc (v991, trainer-mods). v994: MOVED. v991's wrapper on sub_824B4158 never ran on the device (t-e6 / t-l0
// 2026-11-07: no "hook=1"): the attempt's chunk 8 calls __imp__sub_824B4158 directly (the recipe's direct-call pass; sub_824B4158 is
// not on its hooked list), and so does the network update's per-frame caller (chunk 106). Hooking either needs a new codegen recipe.
// v994 ticks the same sequence from the world-load hook on update-list entry #33 (sub_8223F728, world_load_hooks.inc): reached only
// through the update list's indirect call, live in this lineage (RENDER_DIAG_WL hook=N), once per frame, on the main thread, outside
// the script VM, sequential with the network update. Included by renderer_boundary_trace.cpp BEFORE world_load_hooks.inc.
// Off (Story, no diag-online-local): one predictable branch per call on a latched value; no allocation, no log, no guest access.
// On: online_l0.h's Sequence (E6 reads; with diag-online-l0 also L0 / E5), then the original with the registers it was called with.
#include "online_l0.h"
#include "online_local_glue.h"
#include "local_session_flag.h"   // v1013
#include "online_route_switch.h"   // v1102: stage C only with diag-fm-route-on
#include "online_mt_adopt.h"      // v1102: diag-mt-adopt-off (A/B)
#if defined(__ANDROID__)
inline int recompL0TidNow() noexcept {return int(::gettid());}
#else
inline int recompL0TidNow() noexcept {return 0;}
#endif

PPC_FUNC_IMPL(__imp__sub_82FD9878);   // OpenNetwork (no arguments; returns 1)
PPC_FUNC_IMPL(__imp__sub_82FD4A08);   // the game's full close ("end network game"; gated by the ADA byte itself)
// v999 L1: the host worker's own calls (sub_83061D70) and the voice host's Host call shape (sub_8305DBC0)
PPC_FUNC_IMPL(__imp__sub_824A7CA0);   // worker gate (r3 = 1)
PPC_FUNC_IMPL(__imp__sub_82FA4D80);   // worker gate (r3 = 0x8381E258): must be 0
PPC_FUNC_IMPL(__imp__sub_83055B00);   // config(cfg, attribute record, slots, priv ? slots : 0)
PPC_FUNC_IMPL(__imp__sub_8326C8C0);   // (cfg + 52, 0)
PPC_FUNC_IMPL(__imp__sub_83022278);   // attributes of cfg
PPC_FUNC_IMPL(__imp__sub_822379E0);   // r4 of Host
PPC_FUNC_IMPL(__imp__sub_830FF1F0);   // snSession::Host
PPC_FUNC_IMPL(__imp__sub_824AE6D8);   // the state stamp's clock
// v1016 L3: the game's own session end and teardown (lead session-leave.md)
PPC_FUNC_IMPL(__imp__sub_8305C420);   // NETWORK_SESSION_END's body (netobj, 0)
PPC_FUNC_IMPL(__imp__sub_830618E0);   // the netobj shutdown (netobj, 1)
PPC_FUNC_IMPL(__imp__sub_8306BA88);   // the netobj init (netobj)
PPC_FUNC_IMPL(__imp__sub_83039500);   // v1102: NETWORK_CAN_ENTER_MULTIPLAYER's worker (netobj)

namespace online_l0_hook {
inline void log(const char* line) noexcept {__android_log_print(ANDROID_LOG_INFO,"RecompRenderer","%s",line);}
struct GuestMem final : recomp::online_l0::Mem {
    std::uint8_t* base;explicit GuestMem(std::uint8_t* b) noexcept:base(b) {}
    std::uint32_t u32(std::uint32_t a) override {
        if(a<0x10000u || a>0xFFFFFFF8u)return 0;std::uint32_t v;std::memcpy(&v,base+a,4);return __builtin_bswap32(v);
    }
    std::uint8_t u8(std::uint32_t a) override {return (a<0x10000u)?0:base[a];}
};
// 0 = not decided yet, 1 = off for good, 2 = on (configured)
inline std::atomic<int> mode{0};
inline recomp::online_l0::Sequence& sequence() noexcept {static recomp::online_l0::Sequence s;return s;}
inline std::uint64_t nowMs() noexcept {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
// Decided once the play-mode latch has settled (online_local_glue's own rule); until then the original runs alone.
[[gnu::noinline]] inline int decide() noexcept {
    if(::recomp::online_local_glue::decided.load(std::memory_order_relaxed)<0 && !::recomp::online_local_glue::enabled()
       && ::recomp::online_local_glue::decided.load(std::memory_order_relaxed)<0)return 0;
    const bool e6=::recomp::online_local_glue::enabled(),l0=e6 && ::recomp::online_local_glue::l0Enabled();
    if(!e6){mode.store(1,std::memory_order_release);return 1;}
    const bool l1=l0 && ::recomp::online_local_glue::l1Enabled();
    sequence().configure(true,l0,l1);
    if(l1)sequence().setStartDelay(recomp::online_l0::L1StartDelayMs);   // v1014: after the harness walk-out
    if(l1 && ::recomp::online_local_glue::l1LongEnabled())   // v1020; v1037: the file's seconds when given
        sequence().setMonitorSeconds(::recomp::online_local_glue::l1LongSeconds()>0?unsigned(::recomp::online_local_glue::l1LongSeconds()):recomp::online_l0::L1LongMonitorSeconds);
    if(l1 && ::recomp::bootstrap::play_mode::online()) {   // v1040 (ALLOCATION section 18): Online mode, the session is the mode itself
        sequence().setStartDelay(recomp::online_l0::OnlineStartDelayMs);       // no harness walk-out to wait for
        sequence().setMonitorSeconds(recomp::online_l0::OnlineMonitorSeconds);  // no test leave: up until the player leaves
        // v1102 (round 5 stage C, only with diag-fm-route-on, not with diag-mt-adopt-off): L1 waits for maintransition's T18 ENTER, which hosts
        // in the native's frame on this thread (the update-list hook and the script VM share it, observed)
        if(::recomp::fm_route::routeOn() && !::recomp::mt_adopt::off()) {
            sequence().setDeferHost(true);sequence().setHookTid(recompL0TidNow());
            char d[140];std::snprintf(d,sizeof d,"RENDER_ONLINE_LOCAL l1 defers its host to maintransition's T18 ENTER (stage C) hook_tid=%d",recompL0TidNow());log(d);
        }
    }
    // lead 08:5x: the thread, once, next to the script VM's (trainer_glue.inc logs "script-vm tid=" once under the same latch); the
    // network update sub_8229C438 has no hook in this lineage (direct calls), so its thread is not logged here
    char l[128];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL hook=1 update-list-33 e6=1 l0=%d l1=%d tid=%d",int(l0),int(l1),recompL0TidNow());log(l);
    mode.store(2,std::memory_order_release);return 2;
}
// L1's guest side: every call runs on this (main) thread with the hook's registers saved and restored, on a fresh guest frame below
// the hook's r1 (the attribute record and Host's two stack arguments live in that frame, as in the worker / voice host).
struct GuestOps final : recomp::online_l0::L1Ops {
    PPCContext& ctx;std::uint8_t* base;
    GuestOps(PPCContext& c,std::uint8_t* b) noexcept:ctx(c),base(b) {}
    void w(std::uint32_t a,std::uint32_t v) noexcept {v=__builtin_bswap32(v);std::memcpy(base+a,&v,4);}
    // v1001: one "l1 step" line before each guest call (L1 runs once per process), and a stop raised inside the call is named and
    // re-thrown to the executor (NOT noexcept: v1000's noexcept turned the guest stop into std::terminate).
    static void step(const char* name) noexcept {char l[80];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l1 step %s",name);log(l);}
    template<class F> std::uint32_t call(const char* name,F f,std::uint32_t r3,std::uint32_t r4=0,std::uint32_t r5=0,std::uint32_t r6=0,std::uint32_t r7=0,
                                         std::uint32_t r8=0,std::uint32_t r9=0,std::uint32_t r10=0,std::uint32_t s84=0,std::uint32_t s92=0) {
        step(name);
        const PPCContext saved=ctx;
        const std::uint32_t sp=(saved.r1.u32-512u)&~15u;w(sp,saved.r1.u32);   // back chain
        w(sp+84u,s84);w(sp+92u,s92);
        ctx.r1.u64=sp;ctx.r3.u64=r3;ctx.r4.u64=r4;ctx.r5.u64=r5;ctx.r6.u64=r6;ctx.r7.u64=r7;ctx.r8.u64=r8;ctx.r9.u64=r9;ctx.r10.u64=r10;
        try{f(ctx,base);}
        catch(...){char l[120];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l1 stop inside %s (the executor's stop reason follows)",name);log(l);throw;}
        const std::uint32_t r=ctx.r3.u32;ctx=saved;return r;
    }
    bool netGate() override {return (call("sub_824A7CA0",__imp__sub_824A7CA0,1)&0xFFu)!=0;}
    bool busy() override {return (call("sub_82FA4D80",__imp__sub_82FA4D80,recomp::online_l0::GateObject)&0xFFu)!=0;}
    void write32(std::uint32_t a,std::uint32_t v) override {w(a,v);}
    void zeroAtomic(std::uint32_t a) override {__atomic_store_n(reinterpret_cast<std::uint32_t*>(base+a),0u,__ATOMIC_SEQ_CST);}
    void config(std::uint32_t cfg,std::uint32_t slots,std::uint32_t priv) override {
        // the worker's record at its r1+80: count 8, a u64 0, then 8 {id, value} pairs (the worker writes only the ids; values 0 here)
        const PPCContext saved=ctx;
        const std::uint32_t sp=(saved.r1.u32-512u)&~15u;w(sp,saved.r1.u32);
        const std::uint32_t rec=sp+80u;w(rec,8);w(rec+4u,0);w(rec+8u,0);
        for(int i=0;i<8;++i){w(rec+12u+8u*std::uint32_t(i),recomp::online_l0::L1AttrIds[i]);w(rec+16u+8u*std::uint32_t(i),0);}
        ctx.r1.u64=sp;ctx.r3.u64=cfg;ctx.r4.u64=rec;ctx.r5.u64=slots;ctx.r6.u64=priv?slots:0u;
        step("sub_83055B00");
        try{__imp__sub_83055B00(ctx,base);}
        catch(...){log("RENDER_ONLINE_LOCAL l1 stop inside sub_83055B00 (the executor's stop reason follows)");throw;}
        ctx=saved;
        call("sub_8326C8C0",__imp__sub_8326C8C0,cfg+52u,0);
    }
    std::uint32_t host(std::uint32_t session,std::uint32_t cfg,std::uint32_t mode,std::uint32_t slots,std::uint32_t priv,std::uint32_t flags,
                       std::uint32_t status) override {
        const std::uint32_t attrs=call("sub_83022278",__imp__sub_83022278,cfg);
        const std::uint32_t gamer=call("sub_822379E0",__imp__sub_822379E0,0);
        {   // v1002: every Host input and the pointers it dereferences, once (Host stopped on a read at 0xFFFFFFFF in t-l1 1120)
            GuestMem g0(base);const auto rd=[&](std::uint32_t a)->std::uint32_t{return recomp::online_l0::ptr(a)?g0.u32(a):0xDEADDEADu;};
            const std::uint32_t al=rd(session),vt=rd(al);
            char l[300];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l1 args s=%08x al=%08x vt=%08x alloc=%08x gamer=%08x mode=%u slots=%u priv=%u "
                "attrs=%08x a0=%08x a1=%08x a2=%08x a3=%08x flags=%u status=%08x st=%08x sp=%08x",session,al,vt,rd(vt+8u),gamer,mode,slots,priv,
                attrs,rd(attrs),rd(attrs+4u),rd(attrs+8u),rd(attrs+12u),flags,status,rd(status),ctx.r1.u32);log(l);
            if(!recomp::online_l0::ptr(attrs) || gamer>3u){log("RENDER_ONLINE_LOCAL l1 host-skipped (attrs not a guest pointer or gamer index > 3)");return 0;}
        }
        const std::uint32_t r=call("sub_830FF1F0",__imp__sub_830FF1F0,session,gamer,mode,slots,priv,attrs,flags,0,0,status)&0xFFu;
        if(r==1){::recomp::online_local_glue::openTrace(2000);   // v1009: every import for 2 s after Host (the async task fails in ~0.2 s)
                 ::recomp::local_session::hostedByL1.store(true,std::memory_order_release);}   // v1013: the trainer may act in THIS session
        return r;
    }
    std::uint32_t now() override {return call("sub_824AE6D8",__imp__sub_824AE6D8,0);}
    bool canEnter() override {GuestMem g(base);return (call("sub_83039500",__imp__sub_83039500,g.u32(recomp::online_l0::NetObjectWord))&0xFFu)!=0;}   // v1102
    std::uint32_t endSession(std::uint32_t netobj) override {return call("sub_8305C420",__imp__sub_8305C420,netobj,0)&0xFFu;}
    void netShutdown(std::uint32_t netobj) override {(void)call("sub_830618E0",__imp__sub_830618E0,netobj,1);}
    std::uint32_t netInit(std::uint32_t netobj) override {const std::uint32_t r=call("sub_8306BA88",__imp__sub_8306BA88,netobj)&0xFFu;
        ::recomp::local_session::hostedByL1.store(false,std::memory_order_release);return r;}   // v1016: the session is gone
};
[[gnu::noinline]] inline void armed(PPCContext& ctx,std::uint8_t* base) {   // v1001: not noexcept (guest stops unwind)
    GuestMem m(base);GuestOps ops(ctx,base);
    std::uint32_t frame=0;{std::uint32_t v;std::memcpy(&v,base+0x83C5D10Cu,4);frame=__builtin_bswap32(v);}   // GET_FRAME_COUNT's word
    // v1085 TEST (the leave to the launcher): files/private-trace/diag-online-leave in Online mode ends the session monitor, so our own clean
    // leave (L3a) runs; checked once per 60 frames. Not the pause menu's quit (that path is the game's; the guard is the same).
    if(::recomp::bootstrap::play_mode::online() && frame%60u==0u) {
        static bool asked=false;
        if(!asked && ::access(RECOMP_APP_PATH("/private-trace/diag-online-leave"),F_OK)==0){asked=true;sequence().setMonitorSeconds(0);
            log("RENDER_ONLINE_LOCAL l1 diag-online-leave: the session monitor ends now (the clean leave)");}
    }
    sequence().tick(m,nowMs(),frame,
        [&]()->std::uint32_t{const PPCContext saved=ctx;__imp__sub_82FD9878(ctx,base);const std::uint32_t r=ctx.r3.u32;ctx=saved;return r;},
        [&](){const PPCContext saved=ctx;__imp__sub_82FD4A08(ctx,base);ctx=saved;::recomp::local_session::hostedByL1.store(false,std::memory_order_release);},   // v1013: closed
        log,&ops);
}
} // namespace online_l0_hook

namespace online_l0_hook {
// Called first in the update-list entry #33 hook (world_load_hooks.inc). Off: one predictable branch on a latched value.
inline void tick(PPCContext& ctx,std::uint8_t* base) {   // v1001: not noexcept (guest stops unwind to the executor)
    int m=mode.load(std::memory_order_relaxed);
    if(m==1) [[likely]] return;
    if(m==0)m=decide();
    if(m==2)armed(ctx,base);
}
} // namespace online_l0_hook
