// rumble_diag.inc (v953, Update 1 W1-5, diagnostic attempt A): LOG ONLY, the wrapper always runs unchanged. Included in
// renderer_boundary_trace.cpp next to sandbox_launch.inc; needs the redirect script without sub_8363DA18's wrapper
// (redirect-v462-upd1a.ld). One RENDER_DIAG_RUMBLE line per window: every 20 s while calls arrive, and from the trainer glue's
// once-per-256-frames path (recomp_diag_rumble_tick) so the last burst is written even when the game stops sending.
#include "rumble_diag.h"
#include <chrono>

namespace rumble_diag {
void log(const char* line) noexcept {__android_log_print(ANDROID_LOG_INFO,"RecompRenderer","%s",line);}
recomp::rumblediag::Counter& counter() noexcept {static recomp::rumblediag::Counter c(log);return c;}
std::atomic<bool> pending{false};std::atomic<long long> windowStartMs{0};
long long nowMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void maybeFlush(const char* why) noexcept {
    if(!pending.load(std::memory_order_relaxed))return;
    const long long now=nowMs();auto start=windowStartMs.load(std::memory_order_relaxed);
    if(now-start<20000 || !windowStartMs.compare_exchange_strong(start,now,std::memory_order_relaxed))return;
    pending.store(false,std::memory_order_relaxed);
    counter().flush(why);
}
} // namespace rumble_diag

extern "C" void recomp_diag_rumble_tick() noexcept {rumble_diag::maybeFlush("timer");}
extern "C" std::atomic<void(*)()> recomp_diag_rumble_tick_fn; // (trainer_glue.inc: called every 256 game frames once published)

PPC_FUNC_IMPL(__imp__sub_8363DA18);
PPC_FUNC(sub_8363DA18) {
    PPC_FUNC_PROLOGUE();
    static std::atomic<bool> announced{false};
    if(!announced.exchange(true,std::memory_order_relaxed)) {
        rumble_diag::log("RENDER_DIAG_RUMBLE hook=1 fn=sub_8363DA18 mode=log-only");
        rumble_diag::windowStartMs.store(rumble_diag::nowMs(),std::memory_order_relaxed);
        recomp_diag_rumble_tick_fn.store(+[]{recomp_diag_rumble_tick();},std::memory_order_release);
    }
    const std::uint32_t lr=static_cast<std::uint32_t>(ctx.lr),vib=ctx.r4.u32;
    std::uint16_t left=0,right=0;
    if(vib>=0x10000u && vib<=0xFFFF0000u){left=std::uint16_t(base[vib]<<8|base[vib+1]);right=std::uint16_t(base[vib+2]<<8|base[vib+3]);}
    __imp__sub_8363DA18(ctx,base);
    rumble_diag::counter().call(lr,left,right,ctx.r3.u32);
    rumble_diag::pending.store(true,std::memory_order_relaxed);
    rumble_diag::maybeFlush("20s");
}
