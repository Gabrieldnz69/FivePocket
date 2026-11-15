#pragma once
// app-away (BUG 02 step 2, 2026-11-05, android-qol; v720 for the Fold from v719, v854 for the Thor from v853). The app keeps the game's
// process for 25 s after its window is left (Home, another app: PrivateTraceActivity.onStop, BackgroundGrace) and the frame surface is
// detached meanwhile. Before this the game went on at its own cap with nothing shown (the Fold: ~2.8 cores busy) and its sound played
// over the home screen. Now, while the app's window is gone, the game is held still and silent:
//  - the direct presenter (direct_present.h ensureSwapchain) asks the app for its window; absent, it marks the game away
//    (windowMissing, with the window generation it saw); a window acquired clears the mark (windowFound);
//  - the render thread, at the top of its present hook (renderer_boundary_trace.cpp awayHold, before the pace-caps hold), parks
//    in SliceNs slices through the pace-caps park (recomp_pace_park_ready / recomp_pace_park_until: the executor's delay, no spin)
//    until a window is back: the generation moved past the missing one AND a window can be acquired (released again at once;
//    the presenter acquires its own). Main parks on the render thread's acknowledgement meanwhile, as under a pace cap, so the
//    workers run dry: the game stands still;
//  - the audio callback worker (trace_runner.cpp) delivers no callback while away (audioGate: Wait) and waits AwayAudioWaitMs
//    between looks instead of 1 ms: AAudio plays out what it holds (<= the client's queue + its buffer) and then silence; the
//    stream stays started (no pause/start state change: the client's per-poll configuration check needs STARTED), so sound
//    comes back with the next callback after the return.
// A park that is refused ends the hold (the frame goes on as before: never a spin), and MaxSlices bounds a hold (a stuck mark never
// freezes the game for good). Nothing here runs while a window is there: one acquire load per present.
// The end of a stay away longer than the app's grace is the process kill: nothing here runs after it.
// Markers (RecompRenderer): RENDER_APP_AWAY hold ms= slices= back=0|1 holds= (one line per hold, at its end).
// Host test: single-screen-edition/tests/app_away_test.cpp (fake window callbacks, simulated park). v721 / v855: stillAway (the audio
// worker's own look for the return; the Thor's deadlock when main stood still on the audio engine before the render thread's hold).
#include <atomic>
#include <cstdint>

struct ANativeWindow;

namespace recomp_away {
constexpr std::int64_t SliceNs = 100'000'000; // one park: a return is seen within 100 ms
constexpr unsigned MaxSlices = 6000;           // 10 minutes in one hold at most
constexpr int AwayAudioWaitMs = 20;            // the audio worker's wait while away (1 ms otherwise: v431's backpressure wait)

struct Window { // the app's window (RecompPrivateTraceCallbacksV6), as the presenter is given it
    void* user = nullptr;
    ANativeWindow* (*acquire)(void*, std::uint64_t*) noexcept = nullptr;
    void (*release)(void*, ANativeWindow*) noexcept = nullptr;
    std::uint64_t (*generation)(void*) noexcept = nullptr;
};
struct State {
    std::atomic<bool> away{false};
    std::atomic<std::uint64_t> missingGen{0};
    std::atomic<std::uint64_t> holds{0}, slices{0}, returns{0};
    std::atomic<bool> bound{false};
    Window window;
};
inline State& state() noexcept { static State s; return s; }
// trace_runner.cpp, where the presenter is given the window (before the first frame).
inline void bind(const Window& w) noexcept {
    state().window = w;
    state().bound.store(w.acquire && w.release && w.generation, std::memory_order_release);
}
inline bool away() noexcept { return state().away.load(std::memory_order_acquire); }
// The presenter: the window asked for at generation g is absent / a window was acquired.
inline void windowMissing(std::uint64_t g) noexcept {
    state().missingGen.store(g, std::memory_order_relaxed);
    state().away.store(true, std::memory_order_release);
}
inline void windowFound() noexcept { state().away.store(false, std::memory_order_release); }
// A window is back: the generation moved past the missing one and one can be acquired (released at once).
inline bool windowBack() noexcept {
    auto& s = state();
    if (!s.bound.load(std::memory_order_acquire)) return true; // (no callbacks: never held)
    if (s.window.generation(s.window.user) == s.missingGen.load(std::memory_order_relaxed)) return false;
    std::uint64_t g = 0;
    ANativeWindow* w = s.window.acquire(s.window.user, &g);
    if (!w) { s.missingGen.store(g, std::memory_order_relaxed); return false; } // (detached again: wait for the next generation)
    s.window.release(s.window.user, w);
    return true;
}
// The render thread's hold. back(): windowBack; ready() / park(untilNs): the pace-caps park; now(): monotonic ns.
// Returns the slices parked; *returned = the hold ended because a window is back.
template <class Back, class Ready, class Park, class Now>
unsigned hold(Back&& back, Ready&& ready, Park&& park, Now&& now, bool* returned = nullptr) {
    auto& s = state();
    if (returned) *returned = false;
    if (!away()) return 0;
    s.holds.fetch_add(1, std::memory_order_relaxed);
    unsigned n = 0;
    while (away()) {
        if (back()) {
            s.away.store(false, std::memory_order_release);
            s.returns.fetch_add(1, std::memory_order_relaxed);
            if (returned) *returned = true;
            break;
        }
        if (n >= MaxSlices || !ready() || !park(now() + SliceNs)) break; // never a spin: a refused park ends the hold
        ++n;
    }
    s.slices.fetch_add(n, std::memory_order_relaxed);
    return n;
}
// Away, unless a window is back (then the mark is cleared here). v721 / v855 (the Thor, 2026-11-05, v854): main waits on the audio
// engine every frame, so with the audio held back the game can stand still BEFORE the render thread reaches its hold; the presenter
// clears the mark only when a frame comes. Neither then looks again: a deadlock after the return. So every waiter on the mark
// looks for the window itself.
inline bool stillAway() noexcept {
    if (!away()) return false;
    if (!windowBack()) return true;
    state().away.store(false, std::memory_order_release);
    state().returns.fetch_add(1, std::memory_order_relaxed);
    return false;
}
// The audio worker: no callback while away (Failed passes unchanged), and its wait between looks (it looks for the return itself).
template <class Demand>
inline Demand audioGate(Demand demand, Demand failed, Demand wait) noexcept {
    return demand == failed ? demand : (stillAway() ? wait : demand);
}
inline int audioWaitMs() noexcept { return away() ? AwayAudioWaitMs : 1; }
}
