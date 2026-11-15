// native_stop_takeover.h (bugfix-stability 2026-11-07; lead: "take over where native stops" for every screen held over a 3D frontend).
// Pure decision (host-tested: native_stop_takeover_test.cpp) for the end of a native inline draw (EndVertices on the loading-screen
// inline path, renderer_boundary_trace.cpp captureInline).
//
// Before the generic renderer takes over, immediate-mode draws reach the loading-screen native path, which owns the presentation image
// and the 14-program startup pack only. A draw it cannot complete (another colour target, a shader pair outside the pack, other state)
// used to stop the game: "Native EndVertices did not complete its real draw". That was v398/v459's script render targets (loading_handover.h),
// v166's tr497hii, and v1066's Online creator frontend (t-onlcre-20261107-2303: Online mode holds the boot loading screen, the creator's
// bloom / blur / upsample passes and quads run before any takeover trigger).
// The port's rule (native-present-invalid, first-tiled-scene-unrecorded, shutdown_loading_screen): take over where native stops. Here:
// the first incomplete native draw takes over (reason "native-inline-incomplete"). That draw is skipped, as the generic path skips a
// draw it refuses, and every later draw takes the generic path. With the generic renderer unavailable, the stop stays as before.
// A boot that works today never reaches this point, so its trigger order is unchanged.
#pragma once

namespace recomp::native_stop {

enum class End {
    Done,          // the native draw completed: release the buffer, as before
    TakeOverSkip,  // take over now, skip this draw (released), later draws go generic
    Stop,          // throw "Native EndVertices did not complete its real draw", as before
};

// gate: the draw ran under the gate. completed: the native path completed it (or the generic path took it). takenOver: the generic
// takeover had already happened before this draw. genericReadyAfter: the generic renderer is usable once taken over (asked only when
// the takeover is attempted).
template<class ReadyAfter>
End atEndVertices(bool gate, bool completed, bool takenOver, ReadyAfter genericReadyAfter) {
    if (gate && completed) return End::Done;
    if (!gate || takenOver) return End::Stop;   // a gate failure, or a generic draw that failed: not this rule's case
    return genericReadyAfter() ? End::TakeOverSkip : End::Stop;
}

} // namespace recomp::native_stop
