#pragma once
// rumble (Update 1 W1-7, 2026-11-06, android-qol; design: docs/fable/UPDATE-1-FEATURES-DESIGN.md §1.2, review C7). Shared
// runtime code for every edition; not wired yet (the registry change and the exports come with P2; the device check after P1).
//
// The game reaches the pad motors through one import, XamInputSetState (r4 -> the 4-byte XINPUT_VIBRATION: two big-endian u16,
// left = heavy motor, right = light motor). Measured on the Thor (v953, DISCOVERIES 2026-11-06 13:27, 13 windows of 20 s):
//  - the update path (sub_83230E68's first site) sent 1,195 non-zero values, every one refused by today's 50 and re-sent on the next
//    pad update: only 536 were value changes;
//  - sub_832262D8 sent 1,844 values, all zero (satisfied either way); the stop site made 0 calls;
//  - both motors are used, the left (heavy) one dominant (right non-zero in 154 samples); no value reached 0xC000, not even at a
//    wall crash: the game's typical rumble lies in 0x4000..0xBFFF, some below 0x4000.
// Today the registry answers 0 for a zero command and ERROR_NOT_SUPPORTED (50) for any other: no motor action is ever claimed.
//
// With this header the registry's last line becomes `call.returnValue = recomp_rumble::setState(vibration);`:
//  - no consumer bound (an app without haptics, or the player chose Off before the game started): exactly today's answers;
//  - an app bound a consumer (recomp_rumble_bind_v1(1)): the command is stored as packed = left << 16 | right and answered 0
//    (the app performs it, or performs the player's "Off" as a no-op); only a changed value advances seq and wakes the app's
//    thread. The game then clears its dirty byte and stops re-sending: the 1,195 sends above become their changes.
//  - the app's haptics thread blocks in recomp_rumble_wait_v1 until seq moves or its timeout ends: no polling, nothing per frame.
//  - unbinding answers as today again and hands the waiter packed = 0 (motors stop).
//  - amplitude(packed, mode): the one strength curve every port uses (Android RumbleMapper, iOS rumble_mapper.h follow it): a dead
//    zone below 0x0800, full scale at 0xC000 (the measured ceiling, not 0xFFFF, so the game's typical values are felt), and floors
//    so the weakest rumble above the dead zone still moves a phone's actuator. Pulses, hysteresis and the 2 s cap stay in the app.
// Cost on a guest thread: the unbound path is one relaxed load; the bound path takes a short lock only when the game sends.
// seq stays in 0..0x7FFFFFFF so the wait export's int result is never negative except for its timeout (-1).
// Host test: update-1/haptics/tests/rumble_test.cpp (run_tests.sh, mutants.py); export list: tools/check_rumble_exports.py.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace recomp_rumble {
constexpr std::uint32_t NotSupported = 50;      // ERROR_NOT_SUPPORTED: today's answer to a non-zero command
constexpr std::uint32_t SeqMask = 0x7FFFFFFFu;  // seq wraps inside the int range

struct State {
    std::atomic<bool> bound{false};             // read without the lock on the unbound path
    std::mutex m;
    std::condition_variable cv;
    std::uint32_t packed = 0;                   // left << 16 | right, guarded by m
    std::uint32_t seq = 0;                      // advances on every change of packed (and on unbind), guarded by m
};
inline State& state() noexcept { static State s; return s; }

// The guest's XINPUT_VIBRATION: two big-endian u16 (left, right).
inline std::uint32_t decode(const std::uint8_t* v) noexcept {
    const std::uint32_t left = std::uint32_t(v[0]) << 8 | v[1], right = std::uint32_t(v[2]) << 8 | v[3];
    return left << 16 | right;
}
inline std::uint32_t unboundAnswer(const std::uint8_t* v) noexcept { return (v[0] | v[1] | v[2] | v[3]) ? NotSupported : 0; }
inline void advance(State& s) noexcept { s.seq = (s.seq + 1) & SeqMask; }

// The registry's XamInputSetState answer, after its own argument, connection and buffer checks.
inline std::uint32_t setState(const std::uint8_t* vibration) noexcept {
    auto& s = state();
    if (!s.bound.load(std::memory_order_acquire)) return unboundAnswer(vibration);
    const std::uint32_t p = decode(vibration);
    {
        std::lock_guard<std::mutex> l(s.m);
        if (!s.bound.load(std::memory_order_relaxed)) return unboundAnswer(vibration);  // (unbound meanwhile)
        if (p == s.packed) return 0;
        s.packed = p;
        advance(s);
    }
    s.cv.notify_all();
    return 0;
}

// The app: 1 = it performs the game's rumble from now on, 0 = it stopped (motors to zero). Returns the new state.
inline int bind(int on) noexcept {
    auto& s = state();
    bool wake = false;
    {
        std::lock_guard<std::mutex> l(s.m);
        const bool was = s.bound.load(std::memory_order_relaxed);
        if (on && !was) s.packed = 0;                                   // the game's next non-zero command is a change
        if (!on && was) { wake = s.packed != 0; s.packed = 0; if (wake) advance(s); }
        s.bound.store(on != 0, std::memory_order_release);
    }
    if (wake) s.cv.notify_all();
    return on ? 1 : 0;
}

// The app's haptics thread: blocks until seq differs from lastSeq or timeoutMs passes. Returns the new seq (packed written when
// out is given) or -1 on timeout. A timeout of 0 or less is one look.
inline int wait(std::uint32_t lastSeq, int timeoutMs, std::uint32_t* out) noexcept {
    auto& s = state();
    std::unique_lock<std::mutex> l(s.m);
    if (!s.cv.wait_for(l, std::chrono::milliseconds(timeoutMs), [&] { return s.seq != lastSeq; })) return -1;
    if (out) *out = s.packed;
    return int(s.seq);
}
// ---------------------------------------------------------------------------------------------- the strength curve

enum class Mode : int { Off = 0, Light = 1, Full = 2 };
constexpr std::uint32_t DeadZone = 0x0800;   // below this on both motors: nothing
constexpr std::uint32_t Ceiling = 0xC000;    // full strength here (v953: no game value reached it)
constexpr int FullFloor = 64, LightFloor = 40, Top = 255;  // amplitudes (0..255, Android's VibrationEffect scale)

// One motor value for a single actuator: the heavy left motor as it is, the light right one at 60 %.
constexpr std::uint32_t combined(std::uint32_t packed) noexcept {
    const std::uint32_t left = packed >> 16, right = (packed & 0xFFFFu) * 3 / 5;
    return left > right ? left : right;
}

// The amplitude (0..255) a port drives one actuator with. Off: 0. Below the dead zone: 0. Full: FullFloor..255, linear up to the
// ceiling. Light: half of Full, at least LightFloor (Light is pulses only; this is a pulse's strength).
constexpr int amplitude(std::uint32_t packed, Mode mode) noexcept {
    const std::uint32_t v = combined(packed);
    if (mode == Mode::Off || v < DeadZone) return 0;
    const std::uint32_t c = v > Ceiling ? Ceiling : v;
    const int full = FullFloor + int(((Top - FullFloor) * (c - DeadZone) + (Ceiling - DeadZone) / 2) / (Ceiling - DeadZone));
    if (mode == Mode::Full) return full;
    const int light = (full + 1) / 2;
    return light < LightFloor ? LightFloor : light;
}
}  // namespace recomp_rumble

// The two exports, defined once by the registry's translation unit (#define RECOMP_RUMBLE_EXPORTS before the include). Both must
// be listed in the attempt's private_trace.exports (tools/check_rumble_exports.py checks the list against the definitions).
#ifdef RECOMP_RUMBLE_EXPORTS
extern "C" __attribute__((visibility("default"))) int recomp_rumble_bind_v1(int on) { return recomp_rumble::bind(on); }
extern "C" __attribute__((visibility("default"))) int recomp_rumble_wait_v1(unsigned lastSeq, int timeoutMs, unsigned* packed) {
    return recomp_rumble::wait(lastSeq, timeoutMs, packed);
}
#endif
