#pragma once
// sandbox_respawn.h (v1351, update-1 session 2026-11-09; lead 21:3x; update-1/sandbox-death/NOTES.md). Who answers a death in Sandbox mode.
// OBSERVED (Thor, standing v1137, a deliberate fall):
//  - plain Sandbox (no local session): Story's own respawn - main starts respawn_controller at the death (main 0x3d93), the hospital
//    (Pillbox) in ~13 s, full health ~25 s (t-deathplain-20261109-2201);
//  - Sandbox with OUR local session up: our session's host call is followed by main's Story -> network force cleanup (flag 2, handler
//    0x1a832, TERMINATE at 0x1a87f 36 frames after "l1 host ret=1": t-deathsessionmain-20261109-2231), exactly as the game ends Story's
//    scripts on entering a network game; with main gone nobody starts respawn_controller and the player stays dead (48 s+,
//    t-deathsession-20261109-2204). In a network game the game's answer is freemode's, which does not run in the Sandbox.
// So in that one case the Sandbox uses the same answer as Online mode without freemode (online_respawn.h, v1123: the game's WASTED shard,
// a fade, NETWORK_RESURRECT_LOCAL_PLAYER at a safe coordinate near the death spot with freemode's arguments). Its own gates still apply
// (a network game in progress, no freemode thread, the player dead). Online mode keeps its own call (the controller branch); plain
// Sandbox and Story never get here (no local session). Switch: default ON since v1155 (device-proven: t-sbrespawn-20261109-2323, 6/6 on v1351); files/private-trace/diag-sandbox-respawn-off turns it off.
namespace recomp::sandbox_respawn {
inline constexpr const char* SwitchName="diag-sandbox-respawn-off";
// onlineMode: play_mode online (it has its own Respawn); session: local_session::active(); switchOn: the off file is absent.
inline bool wanted(bool onlineMode,bool session,bool switchOn) noexcept {return !onlineMode && session && switchOn;}
}
