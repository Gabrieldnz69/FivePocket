#pragma once
// online_tut_session.h (v1107, trainer-mods; lead 2026-11-08 ~16:10: on the freemode route Lamar's race trigger at -210.34,307.05 (freemode 0x611fe)
// starts nothing and the shops show "Go to the Race Trigger" (t-onlroute-free / -places-20261108-16xx)). freemode's trigger tutorial (SWITCH at
// 0x612ba) in state 0: with MP0_FM_INTRO_MISS_DONE set (0x9fbe) it waits for NETWORK_IS_IN_TUTORIAL_SESSION() (0x61389) and, while that reads 0
// with no change pending, calls NETWORK_ALLOW_GANG_TO_JOIN_TUTORIAL_SESSION (0x613a4); maintransition's spawn (0x4546b) uses the same natives.
// The natives read the network session manager (sub_823CC3D0()->+76): IS_IN = byte +790 != 0, CHANGE_PENDING = bit 4 of byte +855;
// ALLOW_GANG calls sub_82DA4D28 (the real network request: no tutorial session ever forms on our local session), END (sub_82FE8980) acts while
// +790 != 6. Answered IN MEMORY on the freemode route only (Online, diag-fm-route-on, maintransition's ENTER adopted), never a guest write:
// once the game has asked (ALLOW_GANG ran) and until it ends it (END ran), IS_IN reads 1 where the original reads 0 and CHANGE_PENDING reads 0
// where the original reads 1 - the solo tutorial session the console forms. files/private-trace/diag-fm-tut-answer-off turns it off.
#include <atomic>
#include <cstdint>
namespace recomp::tut_session {
inline std::atomic<bool> asked{false},ended{false};
constexpr bool answerIn(bool route,bool off,bool askedNow,bool endedNow,std::uint32_t orig) noexcept {return route && !off && askedNow && !endedNow && orig==0u;}
constexpr bool answerPending(bool route,bool off,bool askedNow,bool endedNow,std::uint32_t orig) noexcept {return route && !off && askedNow && !endedNow && orig!=0u;}
}
