#pragma once
// Update 1 haptics (P2, Android app side; haptics-android lane, 2026-11-06): the loader's bridge between the game library's two rumble
// exports (rumble.h: recomp_rumble_bind_v1 / recomp_rumble_wait_v1) and the app's haptics thread (Haptics.java -> HapticsLoop).
// Plain C++ (host test: tests/rumble_port_test.cpp, against the real rumble.h).
//
// The library is opened by runTrace on the game's own thread, which then runs the game and never returns while it plays; the haptics
// thread starts earlier (onCreate). So the thread's wait first blocks on "the library is open" (a condition variable runTrace signals
// through loaded(), right after its other dlsym calls and before the game starts), then in the library's own wait. No polling.
// Answers of waitChange (HapticsLoop.Port): >= 0 = seq << 32 | packed (seq 0..0x7FFFFFFF, packed = left << 16 | right);
// Timeout -1 (no change in time); NoExport -2 (the library is open and has no rumble: the thread ends); NotLoaded -3 (not open yet).
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace recomp_rumble_port {
using BindFn = int (*)(int);
using WaitFn = int (*)(unsigned, int, unsigned*);
constexpr long long Timeout = -1, NoExport = -2, NotLoaded = -3;
constexpr int NotOpen = -100;  // bind's answer before the library is open, or without the export (the JNI's usual "absent")

struct Port {
    std::mutex m;
    std::condition_variable cv;
    int state = 0;  // 0 not open, 1 open with both exports, 2 open without (guarded by m)
    BindFn bindFn = nullptr;
    WaitFn waitFn = nullptr;

    // runTrace, once, after dlopen: both exports or neither (a library with only one of them is treated as without rumble).
    void loaded(BindFn b, WaitFn w) {
        {
            std::lock_guard<std::mutex> l(m);
            if (state != 0) return;
            const bool both = b && w;
            bindFn = both ? b : nullptr;
            waitFn = both ? w : nullptr;
            state = both ? 1 : 2;
        }
        cv.notify_all();
    }
    int bind(int on) {
        BindFn b;
        {
            std::lock_guard<std::mutex> l(m);
            b = bindFn;
        }
        return b ? b(on) : NotOpen;
    }
    long long waitChange(unsigned lastSeq, int timeoutMs) {
        WaitFn w;
        {
            std::unique_lock<std::mutex> l(m);
            if (!cv.wait_for(l, std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 0), [&] { return state != 0; })) return NotLoaded;
            if (state == 2) return NoExport;
            w = waitFn;
        }
        unsigned packed = 0;
        const int seq = w(lastSeq, timeoutMs, &packed);
        if (seq < 0) return Timeout;
        return static_cast<long long>(static_cast<std::uint64_t>(static_cast<unsigned>(seq)) << 32 | packed);
    }
};
inline Port& port() {
    static Port p;
    return p;
}
}  // namespace recomp_rumble_port
