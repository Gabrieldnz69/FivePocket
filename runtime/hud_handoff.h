#pragma once
// hud_handoff.h (v1740, candidate lane; lead 2026-11-13 09:4x): the Thor's lower screen at a direct-present hand-off.
//
// Before (v1670 .. v1711, and v1691's 9 latches): hud2::Screen (hud_screen.h) draws the lower screen ONLY inside the direct presenter's GPU
// frames (presentLocked: prepare / record / submitted). After any hand-off to the app's renderer (Presenter::giveBack, the CPU-audit release)
// no GPU frame comes any more, so the HUD swapchain's last picture stays on the lower screen for the rest of the run (t-preslatch1-v1691:
// a30-lower still showed the house-interior radar from before the latch while Michael stood outside). Its off flag also stays false, so the
// renderer keeps leaving the minimap box / weapon wheel / phone off the MAIN picture ("Bottom screen only", wheel=lower) for a screen that
// no longer changes.
//
// The app's own path (the direct presenter not used: no diag-direct-present, or a run that never claims the window): the library never
// draws the HUD window - prepare() never runs, so no HUD swapchain exists, mapMode stays Both and the wheel flags false - and the app's
// Presentation (SecondScreen.java Hud: a SurfaceView over a BLACK window background, or TRANSPARENT over the black panel when the trainer
// views are present) shows black; the main picture carries the game's whole HUD. So at a hand-off the lower screen now goes the same way:
//   1. one BLACK picture (the HUD render pass with its clear colour hud::Background and no draws) replaces the stale one, presented on the
//      HUD swapchain (left in place, so the black buffer stays the window's last picture);
//   2. the HUD is off for the run: wanted() false -> no replays / radar captures for it, hud5Bottom / wheel / phone back on the main picture.
// Every step is bounded and a failure only skips the black picture (step 2 always happens); one line RENDER_HUD2 handoff says which.
// Called from Presenter::destroyAll (under Presenter::m, after its own passes were waited) unless the presenter is shutting down (the run's
// end: the HUD thread is already stopped there and nothing follows). Pure: the order and the outcomes below, host-tested with mock steps
// (tests/hud_handoff_test.cpp); hud2::Screen::handoff supplies the real Vulkan steps.
#include <cstdint>
namespace recomp::gpu::hud2::handoff {
enum class Why : std::uint8_t {Blank,Again,NoSwapchain,Busy,Acquire,Record,Submit,Fence,Present};
inline const char* name(Why w) noexcept {
    switch(w) {
        case Why::Blank: return "blank"; case Why::Again: return "again"; case Why::NoSwapchain: return "no-swapchain";
        case Why::Busy: return "busy"; case Why::Acquire: return "acquire"; case Why::Record: return "record";
        case Why::Submit: return "submit"; case Why::Fence: return "fence"; case Why::Present: return "present";
    }
    return "?";
}
struct Result {bool blanked=false;Why why=Why::Again;};
// Ops (all noexcept; the caller holds the HUD's own lock where it says so):
//   bool done()                  the hand-off already ran this run       void markDone()
//   bool hasSwapchain()          a HUD swapchain exists (else nothing was ever shown: the window is the app's black)
//   bool waitIdle()              the HUD present thread has nothing pending and is not presenting (bounded)
//   bool acquire(std::uint32_t*) a HUD image (bounded)                   bool recordBlank(std::uint32_t) the clear-only pass
//   bool submit(std::uint32_t)   after the acquire, signals the image's present semaphore and fence
//   bool waitFence(std::uint32_t) bounded                                 bool present(std::uint32_t) under the present queue's lock
//   void stopDrawing()           off for the run (wanted() false) and the lower-screen flags off (the main picture keeps its HUD)
template<class Ops> Result run(Ops& o) noexcept {
    if(o.done())return {false,Why::Again};
    o.markDone();
    Result r{false,Why::Blank};
    std::uint32_t j=0;
    if(!o.hasSwapchain())r.why=Why::NoSwapchain;
    else if(!o.waitIdle())r.why=Why::Busy;
    else if(!o.acquire(&j))r.why=Why::Acquire;
    else if(!o.recordBlank(j))r.why=Why::Record;
    else if(!o.submit(j))r.why=Why::Submit;
    else if(!o.waitFence(j))r.why=Why::Fence;
    else if(!o.present(j))r.why=Why::Present;
    else r.blanked=true;
    o.stopDrawing();
    return r;
}
}
