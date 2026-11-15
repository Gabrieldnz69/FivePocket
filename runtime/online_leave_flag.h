#pragma once
// online_leave_flag.h (v1085, trainer-mods; owner 2026-11-08, playing t-onlrev-v1082: "now when quitting the session she tp'd to michaels
// house"; ONLINE-MODE-SPEC 1.6: leave -> clouds -> clean session leave -> back to the launcher, never into Story).
// t-onlrev82-20261108-0245: the pause menu's quit (maintransition UI 27 / transition 64) ended the session (gip 1 -> 0) and 0.3 s later
// `main` started `initial` (the Story init: every Story controller, then the Story spawn at Michael's house).
//  - hold: set by the OnlineMode controller once the online character is confirmed (Online mode only). While set, the Sandbox launch
//    policy holds the Story init scripts (StoryInit below: started, then ended, as its other held names). `initial` also runs at boot on
//    the creator path, so the hold starts only at the confirmation.
//  - done: the controller's Leave state finished (the screen covered, the spot saved): ALLOCATION state bit 26 StateOnlineLeaveDone, on
//    which the app (qol10+) finishes the game window and returns to the launcher.
#include <array>
#include <atomic>
#include <cstdint>
namespace recomp::online_leave {
inline std::atomic<bool> hold{false},done{false};
constexpr std::uint32_t joaatL(const char* s) noexcept {std::uint32_t h=0;for(;*s;++s){char c=*s;if(c>='A' && c<='Z')c=char(c-'A'+'a');
    h+=std::uint8_t(c);h+=h<<10;h^=h>>6;}h+=h<<3;h^=h>>11;h+=h<<15;return h;}
inline constexpr std::array<std::uint32_t,2> StoryInit{joaatL("initial"),joaatL("startup_positioning")};
// v1086 INTERIM (lead 2026-11-08; t-onlrev82-20261108-0245): on a later visit maintransition never finishes (its end, 0x620ad -> 66, needs
// 0x61dfa to launch freemode; freemode cannot run here: its tick's first call NETWORK_GET_VC_WALLET_BALANCE is unregistered on this
// executable (a guest trap) and the MP stats load only from the cloud - freemode-route NOTES 1.2 / 1.3, the "Level 2b" project). Parked at
// transition 4 / UI 3 its join frontend keeps reading the pad: B = "Return to Grand Theft Auto V" -> Story. endTransition (set by the
// controller on Freemode) makes the Sandbox policy end maintransition (the sweep) and hold it. Replace with freemode's own launch once
// freemode can run.
inline std::atomic<bool> endTransition{false};
inline bool transitionScript(const char* name) noexcept {return name && *name && joaatL(name)==joaatL("maintransition");}
inline bool storyInit(const char* name) noexcept {
    if(!name || !*name)return false;const std::uint32_t h=joaatL(name);
    for(auto v:StoryInit)if(v==h)return true;return false;
}
}
