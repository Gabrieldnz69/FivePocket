// sandbox_launch.inc (Update 1). Included in renderer_boundary_trace.cpp next to world_load_hooks.inc. NEEDS the link's redirect
// script without the two wrappers (local-analysis/update-1/redirect/redirect-v462-upd1a.ld); RENDER_SANDBOX hook=1 at the first call
// proves the hooks are live.
//  - Story mode: the originals run unchanged (one atomic load each); nothing is read or logged after the hook=1 line.
//  - Sandbox mode (P4, sandbox_mode.h): the thread start sub_82A9B020 is the filter. Every launch is logged at its first sighting per
//    (script, caller, via, parent) (v953's diagnostic record, sandbox_diag.h); a held script is started then ended (P1), or, when
//    sub_82AAC528 starts it again within RelaunchFrames, answered with the previous id without a new start.
//    sub_82AAC528's hook only records its own caller (via: START_NEW_SCRIPT / _WITH_ARGS / code path).
#include "sandbox_diag.h"
#include "sandbox_runtime.h"
#include <chrono>

namespace sandbox_launch {
void log(const char* line) noexcept {__android_log_print(ANDROID_LOG_INFO,"RecompRenderer","%s",line);}
recomp::sandboxdiag::Recorder& recorder() noexcept {static recomp::sandboxdiag::Recorder r(log);return r;}
thread_local std::uint32_t outerLr=0;   // sub_82AAC528's caller while it runs (its start goes through sub_82A9B020 on this thread)
std::atomic<long long> nextSummaryMs{0};
constexpr std::uint32_t FrameCounter=0x83C5D10Cu;   // GET_FRAME_COUNT's word
std::uint32_t be(const std::uint8_t* base,std::uint32_t a) noexcept {
    if(a<0x10000u || a>0xFFFF0000u || (a&3u))return 0;
    std::uint32_t v;std::memcpy(&v,base+a,4);return __builtin_bswap32(v);
}
void summaryEvery60s() noexcept {
    const long long now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto next=nextSummaryMs.load(std::memory_order_relaxed);
    if(next==0){nextSummaryMs.compare_exchange_strong(next,now+60000,std::memory_order_relaxed);return;}
    if(now>=next && nextSummaryMs.compare_exchange_strong(next,now+60000,std::memory_order_relaxed))recorder().summary();
}
} // namespace sandbox_launch

PPC_FUNC_IMPL(__imp__sub_82AAC528);
PPC_FUNC(sub_82AAC528) {
    PPC_FUNC_PROLOGUE();
    static std::atomic<bool> announced{false};
    if(!announced.exchange(true,std::memory_order_relaxed))sandbox_launch::recorder().hook("sub_82AAC528");
    if(!recomp::sandbox::active()){__imp__sub_82AAC528(ctx,base);return;}
    const std::uint32_t previous=sandbox_launch::outerLr;
    sandbox_launch::outerLr=static_cast<std::uint32_t>(ctx.lr);
    __imp__sub_82AAC528(ctx,base);
    sandbox_launch::outerLr=previous;
}
PPC_FUNC_IMPL(__imp__sub_82A9B020);
PPC_FUNC(sub_83439408);   // TERMINATE_THREAD's worker: ends the thread whose id is in r3
PPC_FUNC(sub_82A9B020) {
    PPC_FUNC_PROLOGUE();
    static std::atomic<bool> announced{false};
    if(!announced.exchange(true,std::memory_order_relaxed))sandbox_launch::recorder().hook("sub_82A9B020");
    if(!recomp::sandbox::active()){__imp__sub_82A9B020(ctx,base);return;}
    recomp::sandbox::pad::guestBase.store(base,std::memory_order_relaxed);   // v957: the pad reader's live pause byte (sandbox_pad.h)
    auto& policy=recomp::sandbox::policy();
    const std::uint32_t lr=static_cast<std::uint32_t>(ctx.lr);
    char script[64],parent[64];
    const auto read=[base](std::uint32_t a){return base[a];};
    recomp::sandboxdiag::name(read,ctx.r7.u32,script);
    const std::uint32_t current=sandbox_launch::be(base,recomp::sandboxdiag::CurrentThread);
    if(current)recomp::sandboxdiag::name(read,current+recomp::sandboxdiag::ThreadName,parent);else std::strcpy(parent,"-");
    const bool viaStart=recomp::sandboxdiag::callerOf(lr)==recomp::sandboxdiag::StartScript;
    const std::uint32_t outer=viaStart?sandbox_launch::outerLr:0;
    const std::uint32_t frame=sandbox_launch::be(base,sandbox_launch::FrameCounter);
    std::uint32_t reuse=0;
    const auto decision=policy.decide(script[0]=='?'?"":script,frame,viaStart,reuse);
    if(decision==recomp::sandbox::LaunchPolicy::Decision::Reuse) {   // (only sub_82AAC528's start: it just stores the id)
        ctx.r3.u64=reuse;
        policy.reusedLog(script,reuse);
        return;
    }
    __imp__sub_82A9B020(ctx,base);
    const std::uint32_t id=ctx.r3.u32;
    sandbox_launch::recorder().launch(script,lr,outer,parent,id);
    if(decision==recomp::sandbox::LaunchPolicy::Decision::StartThenEnd && id) {
        const PPCContext started=ctx;   // the caller sees exactly what the original returned
        ctx.r3.u64=id;
        sub_83439408(ctx,base);
        ctx=started;
        policy.ended(script,frame,id,lr);
    }
    sandbox_launch::summaryEvery60s();
}
