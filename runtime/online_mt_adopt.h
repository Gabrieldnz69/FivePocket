#pragma once
// online_mt_adopt.h (v1102, trainer-mods; lead, round 5 stage C, update-1/FREEMODE-GATE-5-2026-11-08.md): maintransition's T18 adopts our
// local L1 host. In T18 the game asks NETWORK_SESSION_ENTER (ip 0x666b1, a0 == 0 = freemode) and NETWORK_SESSION_HOST (6 ips); on this disc
// ENTER's tail (sub_8305C058) is gutted and HOST is hard 0 (sub_82543A90), so T18 ended our own L1 session (CAN_SESSION_END -> SESSION_END,
// t-onlgate100-20261108-1307). The console's HOST hosts in the native frame (sub_82DA5CC0 -> sub_82F92910 -> sub_83061D70); stage C does the
// same with our proven L1 host: L1 stops hosting on its own schedule in Online mode and the ENTER / HOST call runs hostOnce() in-frame and
// returns the REAL host result through [[r3]]. CAN_ENTER, IS_SESSION_ACTIVE, CAN_SESSION_END, SESSION_END and VALIDATE_JOIN stay original.
// Active only with files/private-trace/diag-fm-route-on (online_route_switch.h); files/private-trace/diag-mt-adopt-off = v1100 behaviour (A/B).
// Pure: the decision, for the host test.
#include <atomic>
#include <cstdint>
#include <unistd.h>
namespace recomp::mt_adopt {
constexpr std::uint32_t Maintransition=0x5FF2841Fu,EnterIp=0x666B1u;
inline constexpr std::uint32_t HostIps[6]{0x66525u,0x6654Fu,0x66582u,0x665E5u,0x66649u,0x6667Bu};
constexpr std::uint32_t MtGlobal=1344034u;constexpr int MtT18=18;
inline std::atomic<bool> entered{false};
inline std::atomic<std::uint64_t> enteredMs{0};
inline std::atomic<std::uint32_t> enteredIp{0};
enum class Adopt : std::uint8_t {Original,Host};
inline bool hostIp(std::uint32_t ip) noexcept {for(std::uint32_t h:HostIps)if(h==ip)return true;return false;}
// onlineRoute = Online mode AND diag-fm-route-on; off = diag-mt-adopt-off
inline Adopt decide(bool onlineRoute,bool off,std::uint32_t script,std::uint32_t ip,int mt,bool awaitingGame,std::uint32_t a0) noexcept {
    if(!onlineRoute || off || script!=Maintransition || mt!=MtT18 || !awaitingGame)return Adopt::Original;
    if(ip==EnterIp)return a0==0u?Adopt::Host:Adopt::Original;
    return hostIp(ip)?Adopt::Host:Adopt::Original;
}
#ifdef RECOMP_APP_PATH
inline bool off() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-mt-adopt-off"),F_OK)==0;return o;}
#endif
}
