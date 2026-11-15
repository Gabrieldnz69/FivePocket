#pragma once
// online_local_glue.h (Update 1; lane upd1-spoof/session-stubs, on lane upd1-online/local-session-stubs). PROTOTYPE,
// NOT IN ANY BUILD. Patch target: v963.
// The one Service of the process and its enable latch. Included by guest_import_registry.cpp (patch 0001) and by the
// bridge include (online_local_bridge.inc, patch 0002).
//
// Enabled = files/private-trace/diag-online-local present AND the play mode is latched to Sandbox: the latch test below is
// recomp::sandbox::active()'s (sandbox_runtime.h; tests/check_fakes.py pins both texts), written out here because
// sandbox_runtime.h needs the Android log and RECOMP_APP_PATH, which this TU's host compile does not have. The decision is taken
// once, at the first call AFTER the play-mode latch has settled (while the latch is Unset nothing is decided and the
// answer is "off"), and never changes afterwards. Story mode, or no switch file: off for the life of the process.
#include "online_local.h"
#include "play_mode.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#if defined(__ANDROID__)
#include <android/log.h>
#include "recomp_app_dir.h"
#endif
#include <cstdlib>
#include <unistd.h>

namespace recomp::online_local_glue {

inline void log(const char* line) noexcept {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "RecompRenderer", "%s", line);
#else
    std::fprintf(stderr, "%s\n", line);
#endif
}

// RECOMP_PRIVATE_TRACE_DIR replaces files/private-trace (host tests), as signin_ui_bridge.inc does.
inline bool switchPresent() noexcept {
    const char* dir = std::getenv("RECOMP_PRIVATE_TRACE_DIR");
#if defined(__ANDROID__) || defined(RECOMP_APP_PATH)   // v968: host tests that define RECOMP_APP_PATH (trainer glue suites) use it too
    const std::string base = dir ? dir : RECOMP_APP_PATH("/private-trace");
#else
    const std::string base = dir ? dir : "/nonexistent-private-trace";
#endif
    return ::access((base + "/diag-online-local").c_str(), F_OK) == 0;
}

inline std::atomic<int> decided{-1}; // -1 undecided, 0 off, 1 on

inline bool enabled() noexcept {
    const int d = decided.load(std::memory_order_relaxed);
    if (d >= 0) return d == 1;
    const auto latch = ::recomp::bootstrap::play_mode::latch.load(std::memory_order_acquire);
    if (latch == ::recomp::bootstrap::play_mode::Unset) return false; // not decided yet: today's behaviour
    const bool online = latch == ::recomp::bootstrap::play_mode::LatchedOnline;   // v1040: Online mode = the switches without files
    const bool sandbox = online || latch == ::recomp::bootstrap::play_mode::LatchedSandbox
                      || latch == ::recomp::bootstrap::play_mode::LatchedSandboxFresh;
    const bool on = online || (sandbox && switchPresent()); // the switch file is read only in Sandbox
    int expected = -1;
    if (decided.compare_exchange_strong(expected, on ? 1 : 0, std::memory_order_acq_rel)) {
        char line[128];
        std::snprintf(line, sizeof line, "RENDER_ONLINE_LOCAL mode=%s switch=%s -> %s", online ? "online" : sandbox ? "sandbox" : "story",
                      online ? "not-needed" : sandbox ? (on ? "present" : "absent") : "not-read", on ? "ON" : "off");
        log(line);
        return on;
    }
    return decided.load(std::memory_order_relaxed) == 1;
}

// v991 (lead 08:2x): files/private-trace/diag-online-l0 = L0 (the game's own OpenNetwork) + E5 for this process, with the signin
// spoof OFF (localplayer.md R6: f61 = 1 under it, the auto-host could take the online host path). Only on top of enabled() (Sandbox +
// diag-online-local); decided once, at the first call after enabled() is decided. Story and the switch absent: false.
inline std::atomic<int> l0Decided{-1};
inline bool l0Enabled() noexcept {
    const int d = l0Decided.load(std::memory_order_relaxed);
    if (d >= 0) return d == 1;
    (void)enabled();
    if (decided.load(std::memory_order_relaxed) < 0) return false;   // the play-mode latch is not settled yet: decide later
    const char* dir = std::getenv("RECOMP_PRIVATE_TRACE_DIR");
#if defined(__ANDROID__) || defined(RECOMP_APP_PATH)
    const std::string base = dir ? dir : RECOMP_APP_PATH("/private-trace");
#else
    const std::string base = dir ? dir : "/nonexistent-private-trace";
#endif
    const bool on = enabled() && (::recomp::bootstrap::play_mode::online() || ::access((base + "/diag-online-l0").c_str(), F_OK) == 0);   // v1040
    int expected = -1;
    if (l0Decided.compare_exchange_strong(expected, on ? 1 : 0, std::memory_order_acq_rel)) {
        char line[96];std::snprintf(line, sizeof line, "RENDER_ONLINE_LOCAL l0-switch %s (signin spoof %s)", on ? "ON" : "off", on ? "OFF" : "as before");
        log(line);
    }
    return l0Decided.load(std::memory_order_relaxed) == 1;
}

// v999 (lead 10:1x): files/private-trace/diag-online-l1 = L1 (the offline host, online_l0.h) after L0, only on top of l0Enabled().
inline std::atomic<int> l1Decided{-1};
inline bool l1Enabled() noexcept {
    const int d = l1Decided.load(std::memory_order_relaxed);
    if (d >= 0) return d == 1;
    if (!l0Enabled()) { if (l0Decided.load(std::memory_order_relaxed) == 0) { int e = -1; l1Decided.compare_exchange_strong(e, 0); } return false; }
    const char* dir = std::getenv("RECOMP_PRIVATE_TRACE_DIR");
#if defined(__ANDROID__) || defined(RECOMP_APP_PATH)
    const std::string base = dir ? dir : RECOMP_APP_PATH("/private-trace");
#else
    const std::string base = dir ? dir : "/nonexistent-private-trace";
#endif
    const bool on = ::recomp::bootstrap::play_mode::online() || ::access((base + "/diag-online-l1").c_str(), F_OK) == 0;   // v1040
    int expected = -1;
    if (l1Decided.compare_exchange_strong(expected, on ? 1 : 0, std::memory_order_acq_rel)) {
        char line[96];std::snprintf(line, sizeof line, "RENDER_ONLINE_LOCAL l1-switch %s", on ? "ON (signin spoof ON)" : "off");log(line);
    }
    return l1Decided.load(std::memory_order_relaxed) == 1;
}

// v1020 (lead: run an activity long enough to see it act): files/private-trace/diag-online-l1-long on top of diag-online-l1 keeps the
// local session up L1LongMonitorSeconds instead of 60 s before the clean leave. Read once, only when L1 is on (the hook asks after l1).
inline std::atomic<int> l1LongDecided{-1};
// v1037 (the game's own fm_intro runs longer than the 210 s test session): the file may hold the session length in seconds (60..1800);
// empty or anything else = the default L1LongMonitorSeconds.
inline std::atomic<int> l1LongSecondsValue{0};
inline int l1LongSeconds() noexcept {return l1LongSecondsValue.load(std::memory_order_relaxed);}
inline bool l1LongEnabled() noexcept {
    const int d = l1LongDecided.load(std::memory_order_relaxed);
    if (d >= 0) return d == 1;
    if (!l1Enabled()) return false;
    const char* dir = std::getenv("RECOMP_PRIVATE_TRACE_DIR");
#if defined(__ANDROID__) || defined(RECOMP_APP_PATH)
    const std::string base = dir ? dir : RECOMP_APP_PATH("/private-trace");
#else
    const std::string base = dir ? dir : "/nonexistent-private-trace";
#endif
    const bool on = ::access((base + "/diag-online-l1-long").c_str(), F_OK) == 0;
    int secs = 0;
    if (on) { if (FILE* fl = std::fopen((base + "/diag-online-l1-long").c_str(), "r")) { int v = 0; if (std::fscanf(fl, "%d", &v) == 1 && v >= 60 && v <= 1800) secs = v; std::fclose(fl); } }
    int expected = -1;
    if (l1LongDecided.compare_exchange_strong(expected, on ? 1 : 0, std::memory_order_acq_rel)) {
        l1LongSecondsValue.store(secs, std::memory_order_relaxed);
        char line[96];std::snprintf(line, sizeof line, "RENDER_ONLINE_LOCAL l1-long-switch %s seconds=%d", on ? "ON" : "off", secs);log(line);
    }
    return l1LongDecided.load(std::memory_order_relaxed) == 1;
}

// v1000 (lead 10:4x after t-l1-20261107-1035 refused at the worker gate sub_824A7CA0 = the signed-online byte 0x83B13BA7): the
// signin spoof stays ON with L1 (E6 t-e6-20261107-0915: predicted_autohost 0 for 60 s with it on; online_l0.h guards the auto-host).
// L0 alone (diag-online-l0 without diag-online-l1) keeps it off as before.
inline bool signinSpoofOn() noexcept { return !l0Enabled() || l1Enabled(); }

// v1009 (lead: "log every import call with args from the host until status 2"): a 2 s import trace window that L1 opens right after
// Host returns 1. Every registry-dispatched import (signin_spoof_registry.inc) and every bridged import (online_local_bridge.inc, with
// the return value) logs one "l1 trace" line while it is open, at most 200 lines. Closed (always, unless L1 hosted): one relaxed load.
inline std::atomic<std::uint64_t> traceUntilMs{0};
inline std::atomic<int> traceLines{0};
inline std::uint64_t traceNowMs() noexcept {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline void openTrace(std::uint64_t ms) noexcept {traceLines.store(0,std::memory_order_relaxed);traceUntilMs.store(traceNowMs()+ms,std::memory_order_release);}
inline bool traceOn() noexcept {
    const std::uint64_t until=traceUntilMs.load(std::memory_order_relaxed);
    if(!until) [[likely]] return false;
    if(traceNowMs()>until){traceUntilMs.store(0,std::memory_order_relaxed);return false;}
    return traceLines.fetch_add(1,std::memory_order_relaxed)<200;
}

inline ::recomp::online_local::Service& service() noexcept {
    static ::recomp::online_local::Service s(log, enabled(), true); // constructed only after enabled() said yes
    return s;
}

inline thread_local std::uint32_t pendingWaitMicros = 0;

} // namespace recomp::online_local_glue
