#pragma once
// sandbox_runtime.h (Update 1 P4): the one Sandbox state of the process, shared by the filter (sandbox_launch.inc, in
// renderer_boundary_trace.cpp) and the tick (trainer_glue.inc, in indirect_call_resolver.cpp). Inline functions with static locals:
// one instance across the library. Include after <android/log.h> and recomp_app_dir.h.
#include "play_mode.h"
#include "sandbox_mode.h"
#include <mutex>
#include <unistd.h>

namespace recomp::sandbox {
// Sandbox mode = the play-mode latch says Sandbox (set by the launcher before the game thread; never changes afterwards).
inline bool active() noexcept {
    const auto l=::recomp::bootstrap::play_mode::latch.load(std::memory_order_acquire);
    return ::recomp::bootstrap::play_mode::sandboxLike(l);   // v1040: Online too
}
inline void log(const char* line) noexcept {__android_log_print(ANDROID_LOG_INFO,"RecompRenderer","%s",line);}
// The policy; flow_controller is held only with files/private-trace/diag-sandbox-hold-flow (read once, at the first use in Sandbox).
inline LaunchPolicy& policy() noexcept {
    static LaunchPolicy p(log);
    static std::once_flag once;
    std::call_once(once,[]{
        const bool hold=::access(RECOMP_APP_PATH("/private-trace/diag-sandbox-hold-flow"),F_OK)==0;
        p.setHoldFlow(hold);
        char l[96];std::snprintf(l,sizeof l,"RENDER_SANDBOX mode=sandbox held=%zu flow_controller=%s",Denylist.size(),hold?"held":"kept");log(l);
    });
    return p;
}
} // namespace recomp::sandbox
