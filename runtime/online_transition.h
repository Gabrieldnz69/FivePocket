#pragma once
// online_transition.h (v1114, trainer-mods; lead 20:1x "answer the job's transition session ... local, no network"). Step 1 of the race's transition.
// OBSERVED (t-onlroute-ugc-1824, v1112): walking into Lamar's race corona, fmmc_launcher loads the job, then calls NETWORK_DO_TRANSITION_QUICKMATCH
// (0,8,502,0) -> 0 at ip 6e6eb, forever. OBSERVED (fmmc code): the caller 0x17c6f calls 0x6e56f with "matchmake" while tries +110 < a random limit
// +111 (1..20); 0x6e56f increments +110 only when QUICKMATCH returns 1 (matchmaking started), and on the next calls waits while
// NETWORK_IS_TRANSITION_BUSY, then counts the attempt; at the limit it calls NETWORK_HOST_TRANSITION(...) instead = the game's own
// "matchmaking found nobody -> host the job yourself". The engine refuses QUICKMATCH at once offline, so +110 never grows.
// v1114: on the freemode route, fmmc's QUICKMATCH that the engine refused reads 1 ("started"); the engine state is untouched, so BUSY / IN_TRANSITION
// keep their real answers (not busy, not in one) = "found nobody", and fmmc reaches its own HOST_TRANSITION, which runs the original (logged).
// Kill switch files/private-trace/diag-fm-trans-answer-off. Pure: the decision, for the host test.
#include <atomic>
#include <cstdint>
namespace recomp::fm_transition {
constexpr std::uint32_t FmmcLauncher=0xA3FB8A5Eu;   // joaat("fmmc_launcher")
constexpr bool answerQuickmatch(bool online,bool route,bool off,std::uint32_t script,std::uint32_t original) noexcept {
    return online && route && !off && script==FmmcLauncher && original==0;
}
// v1116 (t-onlroute-ugc-2038, v1114): one QUICKMATCH call only - after "started" and BUSY 0 fmmc treats the transition as JOINED (applies its
// parameters, NETWORK_APPLY_TRANSITION_PARAMETER(_STRING)), then asks NETWORK_IS_TRANSITION_HOST -> 0 (ip 713ca / 713f9) and waits; it never loops
// back to hosting. So the answered matchmaking is a SOLO transition: no one else was found, we are its only member = its host. From the QUICKMATCH
// answer until NETWORK_LEAVE_TRANSITION (or the Online leave), NETWORK_IS_TRANSITION_HOST and NETWORK_IS_IN_TRANSITION read 1 for fmmc_launcher and
// FM_Maintain_Transition_Players where the engine reads 0; BUSY keeps the engine's 0. Same kill switch.
constexpr std::uint32_t FmMaintainTransitionPlayers=0xF11BB0F7u;   // joaat("fm_maintain_transition_players")
inline std::atomic<bool> solo{false};
constexpr bool transitionScript(std::uint32_t s) noexcept {return s==FmmcLauncher || s==FmMaintainTransitionPlayers;}
constexpr bool answerSolo(bool online,bool route,bool off,bool soloOn,std::uint32_t script,std::uint32_t original) noexcept {
    return online && route && !off && soloOn && transitionScript(script) && original==0;
}
// v1130 (lead 06:0x: Lamar's race): the engine's own script events of a transition. OBSERVED (t-onlroute-ugc-20261109-0603, v1129): the corona
// reaches state 6 and holds the intro camera; G1341588 bit 0 and .f62 stay 0. OBSERVED (code): main_persistent's network-event switch 0x18d64:
// event 195 (GET_EVENT_DATA 2 words) sub-code 25 -> 0x1a67b G1341588.f62 = 1; event 194 (1 word, non-zero) -> 0x1a973 SET_BIT(G1341588, 0),
// then NETWORK_IS_TRANSITION_HOST paths. Engine: 195 = the session event class (vtable 0x820A30A8, type getter sub_82A774E8 -> 195, sub-code at
// +16); sub_830625B8 (the transition quickmatch result walker) raises 195/12 per result it tries and 195/25 when no result is left ("found
// nobody"); 194 = vtable 0x820A3078 (getter sub_82A773F8 -> 194, payload +16), raised by sub_824F3908. Both are added with the network event
// group's own add (sub_82295FD8() vtable [1]). Offline the engine never searches or hosts, so neither event ever comes.
// v1130: when fmmc's QUICKMATCH is answered (v1114) -> 195/25 (the search found nobody); when fmmc's NETWORK_HOST_TRANSITION is refused by the
// engine -> it reads 1 (hosted) and 194/1 is raised (the solo transition of v1116 is ours). Same route / jobs gate; kill switch
// files/private-trace/diag-fm-trans-events-off (the events) on top of diag-fm-trans-answer-off (everything).
constexpr std::uint32_t SessionEventVt=0x820A30A8u,TransitionStartedVt=0x820A3078u,EventBaseVt=0x82011B58u,NoResultsCode=25u;
constexpr bool raiseNoResults(bool quickmatchAnswered,bool eventsOff) noexcept {return quickmatchAnswered && !eventsOff;}
constexpr bool answerHost(bool online,bool route,bool off,std::uint32_t script,std::uint32_t original) noexcept {
    return online && route && !off && script==FmmcLauncher && original==0;
}
// v1133 (t-onlroute-ugc-20261109-0739, v1132 with the fmmc trace): after the answered quickmatch fmmc reads IS_TRANSITION_HOST 1 (0x713f9, v1116's
// solo answer) = a started transition it hosts, so it never calls NETWORK_HOST_TRANSITION, and corona state 6 waits (0x17cc5) for G1341588 bit 0,
// which only event 194 with a non-zero payload sets (main_persistent 0x1a973). The solo transition IS the hosted one: when the quickmatch answer
// starts it (solo was off), 194/1 is raised too, once per solo transition; the same events switch.
constexpr bool raiseSoloStarted(bool quickmatchAnswered,bool soloBefore,bool eventsOff) noexcept {return quickmatchAnswered && !soloBefore && !eventsOff;}
// v1134 (t-onlroute-ugc-20261109-0802, v1133: bit 0 set, fmmc's own NETWORK_HOST_TRANSITION answered, corona state 6 -> 16, the game's "Launching
// session" spinner; at 0x2e8aa NETWORK_LAUNCH_TRANSITION -> 0, the engine refusing offline): the solo transition we host launches - for fmmc,
// with the solo transition on, a refused LAUNCH reads 1 (fmmc 0x2e8ca then sets up the job), and from then until LEAVE_TRANSITION
// NETWORK_IS_TRANSITION_STARTED reads 1 for the transition scripts where the engine reads 0 (a launched transition has started). Same switches.
inline std::atomic<bool> launched{false};
constexpr bool answerLaunch(bool online,bool route,bool off,bool soloOn,std::uint32_t script,std::uint32_t original) noexcept {
    return online && route && !off && soloOn && script==FmmcLauncher && original==0;
}
constexpr bool answerStarted(bool online,bool route,bool off,bool launchedOn,std::uint32_t script,std::uint32_t original) noexcept {
    return online && route && !off && launchedOn && transitionScript(script) && original==0;
}
}
