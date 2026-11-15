#pragma once
// prewarm_quiesce.h (v1711, lane candidate-v1710 2026-11-13): the generic renderer's warm-up thread stops before the Vulkan device goes.
// Thor run t-gstop-20261113-0432 (v1710, diag-guest-stop-sim in free roam): the guest stop unwound the run; V1620Revoke ran, the pipeline
// cache was saved and ~ProbeDevice destroyed the cache and the device while the detached warm-up thread (startRecipes' lambda, compiling
// the recorded recipes on the little cores; still running 32 s into free roam) was inside vkCreateGraphicsPipelines -> 0.5 s after the
// revoke: FORTIFY "pthread_mutex_lock called on a destroyed mutex" in vulkan.adreno <- compileRecipe <- the warm-up thread, SIGABRT,
// exit-info APP CRASH(NATIVE). (v1670 run 0107 stopped while loading, after its warm-up had ended, and crashed later in freezewatch.)
// The thread holds enter()/leave() around all of its device work (one recipe at a time, and its final cleanup) and checks stopRequested()
// before each recipe; trace_runner's V1620Revoke calls quiesce() (sets stop, waits for the thread to leave) before ~VulkanEngine. seq_cst
// on all four operations (the touch lease's argument): either the thread's enter precedes quiesce's load (quiesce waits) or its stop load
// follows quiesce's store (it stops). While the run lives: one relaxed-path atomic RMW pair per recipe on the warm-up thread only.
#include <atomic>
#include <sched.h>

namespace recomp::gpu::prewarmq {
struct State { std::atomic<bool> stop{false}; std::atomic<int> inside{0}; std::atomic<unsigned> stopped{0}; };
inline State& state() noexcept { static State s; return s; }
// The warm-up thread: true = go on (inside counted); false = stop requested (not counted).
inline bool enter() noexcept {
    auto& s = state();
    s.inside.fetch_add(1, std::memory_order_seq_cst);
    if (s.stop.load(std::memory_order_seq_cst)) { s.inside.fetch_sub(1, std::memory_order_seq_cst); return false; }
    return true;
}
inline void leave() noexcept { state().inside.fetch_sub(1, std::memory_order_seq_cst); }
inline bool stopRequested() noexcept { return state().stop.load(std::memory_order_seq_cst); }
// The run's teardown (V1620Revoke), before the device is released: no warm-up device call after it returns.
inline void quiesce() noexcept {
    auto& s = state();
    s.stop.store(true, std::memory_order_seq_cst);
#if !defined(RECOMP_PREWARMQ_MUTANT_NO_WAIT)
    while (s.inside.load(std::memory_order_seq_cst) != 0) sched_yield();
#endif
    s.stopped.fetch_add(1, std::memory_order_relaxed);
}
} // namespace recomp::gpu::prewarmq
