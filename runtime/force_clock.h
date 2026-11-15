// force_clock.h (bugfix-stability 2026-11-08; TEST-ONLY): files/private-trace/diag-force-clock = "HH MM" holds the online clock at that time
// (NETWORK_OVERRIDE_CLOCK_TIME, re-applied every ReapplyFrames game frames) so a run sees the street lamps lit against a dawn / dusk sky.
// The lamp-cone A/B (v1073 sky skip on/off) needs lamps on at the fm_intro shots; since v1053 fm_intro owns the clock, so they were daylit.
// Pure (host-tested: force_clock_test.cpp). The file is looked for every LookFrames frames until it is found, then latched; a run
// writes it after launch, so a one-time read at the first tick would miss it (the v1060 lesson).
#pragma once
#include <cstdint>
#include <cstdio>

namespace recomp::force_clock {

constexpr std::uint32_t LookFrames = 300, ReapplyFrames = 30;

struct Time { int h = -1, m = 0; bool valid() const noexcept { return h >= 0; } };

inline Time parse(const char* text) noexcept {
    Time t; int h = -1, m = -1;
    if (text && std::sscanf(text, "%d %d", &h, &m) == 2 && h >= 0 && h <= 23 && m >= 0 && m <= 59) { t.h = h; t.m = m; }
    return t;
}

// The per-tick decision. look(): reads the file (returns "" when absent). Returns true when the override is to be applied this frame.
class Holder {
public:
    template<class Look>
    bool tick(std::uint32_t frame, Look look) {
        if (!time_.valid()) {
            if (looked_ && frame - lastLook_ < LookFrames) return false;
            looked_ = true; lastLook_ = frame;
            time_ = parse(look());
            if (!time_.valid()) return false;
            lastApply_ = frame - ReapplyFrames;   // apply at once
        }
        if (frame - lastApply_ < ReapplyFrames) return false;
        lastApply_ = frame; ++applied_;
        return true;
    }
    Time time() const noexcept { return time_; }
    unsigned applied() const noexcept { return applied_; }
private:
    Time time_{}; bool looked_ = false; std::uint32_t lastLook_ = 0, lastApply_ = 0; unsigned applied_ = 0;
};

} // namespace recomp::force_clock
