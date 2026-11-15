#pragma once
// online_save_wait.h (v1127, trainer-mods; t-onlcre125-20261109-0430, v1126 first visit on the freemode route): after the creator, selector
// started maintransition (v1125's byte) and it stopped at T17 step 1 (g1332392 = 1) for good: its STAT_SAVE_PENDING_OR_REQUESTED read 1 where a
// later visit reads 0 (later visit t-onlgate105-20261109-0356: 0, then g1332392 2 within frames).
// OBSERVED (code, this executable): STAT_SAVE_PENDING_OR_REQUESTED a3407ca3 = sub_82DB4BE0: sub_82E134C0(R) || sub_82E138B8(R), R = the stats
// request object 0x83A31F8C. sub_82E134C0 = (R+76 bit 7) && the cloud check sub_824A7CB8 && byte R+16 (a running request); sub_82E138B8 = any
// slot 0..5 with its bit in R+8 (save requested) and not in R+32. STAT_SAVE (sub_82D9F658) queues through sub_82E13678 (sets the R+8 bit, a
// due time); since v1089 its cloud byte answers 1 in Online mode (online_stats_cloud_hook.inc), so the creator's commit (07:07:09.598 "cloud
// gate save (STAT_SAVE) answered 1") queued a save. OBSERVED (logs): in every Online run since (30+ runs) the request start / tick gates and
// "cloud save slot" never logged - nothing services the queued save, so "requested" stays 1 for ever and T17 waits for ever.
// Answer (in memory, the result word only; no stats / request write): Online local mode (no diag-cloud-real), on the freemode route, not
// files/private-trace/diag-stats-save-wait-real, the original said 1, no request task runs (R+72 == 0) and every slot that holds it is a
// queued save only -> 0, which is what maintransition reads once a save has finished. Route off / Story / Sandbox: the original.
// Known consequence (unchanged from before): the queued MP stats save is not written; the character itself is kept by character.json.
#include <cstdint>
namespace recomp::save_wait {
constexpr std::uint32_t Request=0x83A31F8Cu,RequestedOff=8u,DoneOff=32u,TaskOff=72u,RunningOff=76u,ActiveOff=16u;
// requested/done: the R+8 / R+32 masks; running: R+76 bit 7 && byte R+16 (the cloud part is false in local mode for this caller)
inline bool queuedOnly(std::uint32_t requested,std::uint32_t done) noexcept {
    for(unsigned s=0;s<6u;++s){const std::uint32_t b=1u<<s;if((requested&b) && !(done&b))return true;}return false;}
inline bool answerZero(bool onlineLocal,bool route,bool real,std::uint32_t orig,std::uint32_t requested,std::uint32_t done,bool taskRunning,bool running) noexcept {
    return onlineLocal && route && !real && orig==1u && !taskRunning && !running && queuedOnly(requested,done);
}
}
