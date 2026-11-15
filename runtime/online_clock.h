#pragma once
// online_clock.h (draft for the next integrated build after v1098; lead 12:21): Online mode's clock. Real GTA Online runs the session (network)
// clock: one in-game day = 48 real minutes, i.e. one game minute per 2 real seconds. Without a host / freemode setting it, Online mode used the
// Sandbox continuity hold (the pre-edge clock, e.g. 04:42 + SMOG: night-blue clouds in the sky descent). In play_mode=online only, the env hold
// (session_env.h) takes this clock, derived locally from the device's real time (no network): game minutes of the day = (epoch seconds / 2)
// mod 1440, seconds = (epoch seconds mod 2) * 30. Story and Sandbox keep the continuity hold; the weather is unchanged; once freemode sets the
// clock itself the hold's existing stand-down follows the game. Pure, for the host test.
#include <cstdint>
namespace recomp::online_clock {
struct Hms {std::uint32_t h,m,s;};
inline Hms fromEpoch(std::int64_t epochSeconds) noexcept {
    if(epochSeconds<0)epochSeconds=0;
    const std::int64_t gm=(epochSeconds/2)%1440;
    return Hms{std::uint32_t(gm/60),std::uint32_t(gm%60),std::uint32_t((epochSeconds%2)*30)};
}
}
