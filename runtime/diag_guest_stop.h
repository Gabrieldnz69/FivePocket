#pragma once
// diag_guest_stop.h (v1710, lane candidate-v1710 2026-11-13): TEST ONLY, default off. files/private-trace/diag-guest-stop-sim raises ONE
// guest stop on purpose so the teardown after a guest stop can be tested (the freezewatch lease, the touch / My Music / HUD leases).
// It never acts without the file. The file holds the time in seconds after the native audio worker starts (empty = 120). At that time
// the audio worker throws, exactly where v1670's audio failure threw (trace_runner.cpp's callback loop): a host C++ exception in a guest
// entry -> EXEC_FIRST_STOP reason=8 -> the run unwinds (V1620Revoke, ~GuestAddressSpace, ~VulkanEngine). It changes nothing else.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace recomp_diag_stop {
struct State { std::atomic<std::int64_t> due{0}; std::atomic<bool> fired{false}; double seconds = 0; };
inline State& state() noexcept { static State s; return s; }
inline std::int64_t nowNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
// The audio worker's start: reads the switch once (absent = off: nothing armed, nothing logged).
inline void arm(const char* path) noexcept {
    if (!path) return;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return;
    char text[32]{};
    const std::size_t n = std::fread(text, 1, sizeof text - 1, f);
    std::fclose(f);
    text[n] = 0;
    char* end = nullptr;
    double s = std::strtod(text, &end);
    if (end == text || !(s >= 1 && s < 86400)) s = 120;
    auto& st = state();
    std::int64_t expected = 0;
    if (!st.due.compare_exchange_strong(expected, nowNs() + std::int64_t(s * 1e9))) return; // armed once per process
    st.seconds = s;
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_WARN, "RecompRenderer", "RENDER_DIAG_STOPSIM armed +%.0f s (diag-guest-stop-sim, TEST ONLY)", s);
#endif
}
// The audio worker's loop: true exactly once, at or after the armed time (one relaxed load while off).
inline bool due() noexcept {
    auto& st = state();
    const auto d = st.due.load(std::memory_order_relaxed);
    if (!d || nowNs() < d || st.fired.exchange(true, std::memory_order_acq_rel)) return false;
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_WARN, "RecompRenderer", "RENDER_DIAG_STOPSIM firing at +%.0f s: one guest stop (TEST ONLY)", st.seconds);
#endif
    return true;
}
} // namespace recomp_diag_stop
