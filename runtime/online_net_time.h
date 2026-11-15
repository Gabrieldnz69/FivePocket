#pragma once
// online_net_time.h (v1129, trainer-mods; t-onldeathroute-20261109-0511 = t-onldeathtrace128, v1128 with the death-armed freemode trace): after a
// death on the freemode route freemode's respawn steps (0x91552..0x9241a, 0xaa37, 0xb8cd3, 0xbe4ce, ...) each wait on GET_TIME_DIFFERENCE(
// GET_NETWORK_TIME(), start) and every GET_NETWORK_TIME read 0 - so no wait ever elapsed, no WASTED shard, no respawn search (v1126 probe:
// the engine search never active), the player stayed dead (50 s+).
// OBSERVED (code, this executable): GET_NETWORK_TIME 998103c2 = sub_82DA96E8 -> sub_82D98CB0: the net time object *0x8381CF14 with word +4
// non-zero (sub_831E9A50) ? the network clock word 0x83B13B0C (sub_82F91D08), kept monotonic in 0x839BB538 : 0. GET_NETWORK_TIME_ACCURATE
// 98aa48e5 = sub_82DA9720 -> sub_82D98D30. In our local session (no Rockstar network, no clock sync) the object's +4 stays 0: every network
// time reads 0. On a console the session host's network time runs with its own clock.
// Answer (the result word only): Online local mode (no diag-cloud-real), on the freemode route, our local session up, the engine's clock not
// running (object +4 == 0) and the original 0, not files/private-trace/diag-net-time-real -> the game timer (GET_GAME_TIMER's word
// 0x83C5D108, ms, monotonic), the host's own clock. Anything else: the original. Story / Sandbox never reach it.
#include <cstdint>
namespace recomp::net_time {
constexpr std::uint32_t NetTimeObjPtr=0x8381CF14u,ReadyOff=4u,GameTimer=0x83C5D108u;
inline bool answer(bool onlineLocal,bool route,bool real,bool session,bool clockRunning,std::uint32_t orig) noexcept {
    return onlineLocal && route && !real && session && !clockRunning && orig==0u;
}
}
