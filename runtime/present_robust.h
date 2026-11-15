#pragma once
// present_robust.h (v1690, lane present-robust, 2026-11-13; v1691 lane present-latch: (5)-(7); PLAN U3 + U4, Discord D7: Samsung Xclipse picture frozen on the first title
// card while audio runs). Our own code; the external port supplied only the observation that a driver returned VK_TIMEOUT from a fence
// wait before the timeout it was given, and that acquire blocked with frames in flight on a small swapchain.
//
// (1) waitFences: one vkWaitForFences with the caller's timeout, as before. Only when it returns VK_TIMEOUT before the caller's own
//     deadline (an early return; the Vulkan spec allows VK_TIMEOUT only once the timeout has expired) does it wait again for the
//     time that is left (UINT64_MAX callers: 1 s slices, never a single infinite call after an early return). A real error, VK_SUCCESS
//     or the full deadline end the wait exactly as one call did. Each early return is counted and logged (rate limited):
//       RENDER_DIRECT_PRESENT early-timeout site=<s> n=<count> waited_us=<call's time> asked_us=<call's timeout>
//     Kill switch files/private-trace/diag-present-robust-off: read only at the first early return (no cost before one); with the file
//     an early VK_TIMEOUT is returned to the caller as in v1670.
// (2) acquireImage: the same for vkAcquireNextImageKHR (VK_TIMEOUT / VK_NOT_READY before its timeout: acquire again for the rest; no
//     image was acquired and no semaphore signalled by such a return, so a second call is valid).
// (3) imageCount: the swapchain's image request. Default (no file): minImageCount + 1 as v1670. With files/private-trace/
//     diag-present-images-extra: minImageCount + 1 + MaxAcquiredUnpresented (the presenter's own worst case: a frame-generation pair's
//     stashed first picture + the present thread's held picture = 2), capped at maxImageCount (0 = no limit).
// (5) Latch (v1691): the names of the presenter's latch sites. Every latch ends in Presenter::latch (direct_present.h): one log line
//     `RENDER_DIRECT_PRESENT broken=<site> result=<VkResult> injected=<0|1> gpu=0 handoff=next-frame ...`, broken, gpuOn false. gpuOn
//     false is the hand-off: the renderer's next frame takes the readback path (generic_renderer.cpp `direct` reads gpuReady()), that
//     frame reaches DirectDisplayBridge::consume (trace_runner.cpp), presentCpu refuses it, usable() is false, giveBack() releases the
//     window and the app's renderer draws it and every later frame (`RENDER_DIRECT_PRESENT handed_to_app=1`). v1670 left gpuOn
//     true on 7 of the 9 sites (v1690: 6): every later frame went to presentGpu, which refused it, and the picture froze (D7).
// (6) Held (v1691): from a successful acquire until presentLocked returns, the presenter holds a swapchain image whose acquire
//     semaphore will be signalled; it cannot drop the frame there. A presenter fence deadline in that span waits again (the same
//     DeadlineLatch rule: the 3rd consecutive deadline latches `image-slot-wait`) instead of v1690's drop-then-latch.
// (7) Inject (v1691, TEST ONLY, default off): files/private-trace/diag-present-latch-inject = "<site> [seconds]" (host builds:
//     RECOMP_PRESENT_LATCH_INJECT), read once at the presenter's claim; <site> = fence-wait | acquire | image-index | image-slot-wait |
//     descriptor | record | submit. `seconds` (default 100) after the claim, the named site's real
//     failure branch runs once with result InjectedResult (VK_ERROR_UNKNOWN) - exactly one latch per run. Absent file: one compare per
//     site per frame against Latch::None.
// (4) Minute: the per-minute presenter state line (presents, drops, acquire timeouts, early returns, fence deadlines, recreations),
//     emitted from the presenter's own calls, so a frozen picture (every frame dropped: no 256-present stats line) still logs.
#include <vulkan/vulkan.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unistd.h>
#if defined(__ANDROID__)
#include <android/log.h>
#include "recomp_app_dir.h" // RECOMP_APP_PATH (one definition of the switch readers in every translation unit)
#endif

namespace recomp::gpu::robust {
inline constexpr std::uint64_t Forever = ~0ull;
inline constexpr std::uint64_t ForeverSliceNs = 1'000'000'000ull; // a UINT64_MAX caller's slice after an early return
inline constexpr std::uint64_t EarlyMarginNs = 1'000'000ull;      // returns within 1 ms of the deadline count as the deadline
inline constexpr std::uint64_t QuickReturnNs = 100'000ull;        // an early return faster than this backs off before the next call
inline constexpr std::uint64_t BackoffNs = 200'000ull;
inline constexpr unsigned MaxAcquiredUnpresented = 2;              // see (3)

inline std::uint64_t nowNs() noexcept {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline void log(const char* line) noexcept {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "RecompRenderer", "%s", line);
#else
    std::fprintf(stderr, "%s\n", line);
#endif
}
// The kill switch: -1 not read yet, 0 robust paths on (default), 1 off. Read once, at the first early return or presenter fence deadline.
inline std::atomic<int>& offState() noexcept { static std::atomic<int> s{-1}; return s; }
inline bool slicingOff() noexcept {
    int s = offState().load(std::memory_order_acquire);
    if (s < 0) {
#if defined(__ANDROID__)
        s = access(RECOMP_APP_PATH("/private-trace/diag-present-robust-off"), F_OK) == 0 ? 1 : 0;
#else
        s = std::getenv("RECOMP_PRESENT_ROBUST_OFF") ? 1 : 0; // host builds (tests): the same switch as an environment variable
#endif
        offState().store(s, std::memory_order_release);
        char l[96]; std::snprintf(l, sizeof l, "RENDER_DIRECT_PRESENT robust=%s (read at the first early VK_TIMEOUT or fence deadline)", s ? "off" : "on");
        log(l);
    }
    return s == 1;
}
struct Counters {
    std::atomic<std::uint64_t> early{0}, deadline{0}, errors{0};
};
inline Counters& fences() noexcept { static Counters c; return c; }
inline Counters& acquires() noexcept { static Counters c; return c; }
inline void noteEarly(Counters& c, const char* site, std::uint64_t took, std::uint64_t asked) noexcept {
    const auto n = c.early.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 16 || !(n & 1023)) {
        char l[160];
        std::snprintf(l, sizeof l, "RENDER_DIRECT_PRESENT early-timeout site=%s n=%llu waited_us=%llu asked_us=%llu", site,
                      (unsigned long long)n, (unsigned long long)(took / 1000), (unsigned long long)(asked == Forever ? 0 : asked / 1000));
        log(l);
    }
}
// One wait with early VK_TIMEOUT returns absorbed. call(timeoutNs) performs one wait and returns its VkResult; timeout_ns is the
// caller's own timeout. Results: VK_SUCCESS, a real error (unchanged), or VK_TIMEOUT once the caller's deadline has passed.
template <class Call> VkResult waitSliced(Call&& call, std::uint64_t timeoutNs, const char* site, Counters& c) {
    const std::uint64_t t0 = nowNs();
    const bool forever = timeoutNs == Forever;
    const std::uint64_t deadline = forever ? 0 : (t0 + timeoutNs < t0 ? Forever : t0 + timeoutNs);
    std::uint64_t start = t0, asked = timeoutNs;
    for (;;) {
        const VkResult r = call(asked);
        if (r != VK_TIMEOUT && r != VK_NOT_READY) {
            if (r != VK_SUCCESS) c.errors.fetch_add(1, std::memory_order_relaxed);
            return r;
        }
        const std::uint64_t t1 = nowNs();
        if (!forever && t1 + EarlyMarginNs >= deadline) { c.deadline.fetch_add(1, std::memory_order_relaxed); return VK_TIMEOUT; }
        const std::uint64_t took = t1 - start;
        const bool sliceEnded = forever && asked != Forever && took + EarlyMarginNs >= asked; // a whole slice of a forever wait: not early
        if (!sliceEnded) {
            if (slicingOff()) return r;                               // v1670's behaviour: the early VK_TIMEOUT goes to the caller
            noteEarly(c, site, took, asked);
            if (took < QuickReturnNs) std::this_thread::sleep_for(std::chrono::nanoseconds(BackoffNs)); // never spin on a driver
        }
        start = nowNs();
        asked = forever ? ForeverSliceNs : (start >= deadline ? 0 : deadline - start);
        if (!forever && start + EarlyMarginNs >= deadline) { c.deadline.fetch_add(1, std::memory_order_relaxed); return VK_TIMEOUT; }
    }
}
inline VkResult waitFences(PFN_vkWaitForFences wait, VkDevice device, std::uint32_t count, const VkFence* list, VkBool32 all,
                           std::uint64_t timeoutNs, const char* site) {
    return waitSliced([&](std::uint64_t t) { return wait(device, count, list, all, t); }, timeoutNs, site, fences());
}
// The swapchain request (3). caps: minImageCount, maxImageCount (0: no limit).
inline std::uint32_t imageCount(std::uint32_t minImages, std::uint32_t maxImages, bool extra) noexcept {
    std::uint32_t count = minImages + 1 + (extra ? MaxAcquiredUnpresented : 0);
    if (maxImages && count > maxImages) count = maxImages;
    return count;
}
inline bool extraImagesWanted() noexcept {
#if defined(__ANDROID__)
    return access(RECOMP_APP_PATH("/private-trace/diag-present-images-extra"), F_OK) == 0; // (read at each swapchain creation)
#else
    return std::getenv("RECOMP_PRESENT_IMAGES_EXTRA") != nullptr;
#endif
}
// (4) The presenter's per-minute line (owner: the presenter, under its mutex). Values are the presenter's running totals.
struct Totals {
    std::uint64_t presents = 0, dropped = 0, acquireTimeouts = 0, outOfDate = 0, surfaceLost = 0, recreated = 0, fenceDeadline = 0;
};
struct Minute {
    static constexpr std::uint64_t PeriodNs = 60'000'000'000ull;
    std::uint64_t since = 0; Totals last{}; std::uint64_t lastEarlyFence = 0, lastEarlyAcquire = 0; std::uint64_t lines = 0;
    // True (and `out` filled) once a minute has passed since the previous line (the first call only starts the clock).
    bool due(std::uint64_t now, const Totals& t, unsigned images, unsigned minImages, bool broken, unsigned dropStreak, char* out, std::size_t size) noexcept {
        if (!since) { since = now; last = t; lastEarlyFence = fences().early.load(); lastEarlyAcquire = acquires().early.load(); return false; }
        if (now - since < PeriodNs) return false;
        const auto ef = fences().early.load(), ea = acquires().early.load();
        std::snprintf(out, size, "RENDER_DIRECT_PRESENT minute=%llu presents=+%llu dropped=+%llu acquire_timeouts=+%llu acquire_early=+%llu "
            "fence_early=+%llu fence_deadline=+%llu out_of_date=+%llu surface_lost=+%llu recreated=+%llu images=%u min_images=%u drop_streak=%u broken=%u",
            (unsigned long long)++lines, (unsigned long long)(t.presents - last.presents), (unsigned long long)(t.dropped - last.dropped),
            (unsigned long long)(t.acquireTimeouts - last.acquireTimeouts), (unsigned long long)(ea - lastEarlyAcquire),
            (unsigned long long)(ef - lastEarlyFence), (unsigned long long)(t.fenceDeadline - last.fenceDeadline),
            (unsigned long long)(t.outOfDate - last.outOfDate), (unsigned long long)(t.surfaceLost - last.surfaceLost),
            (unsigned long long)(t.recreated - last.recreated), images, minImages, dropStreak, unsigned(broken));
        since = now; last = t; lastEarlyFence = ef; lastEarlyAcquire = ea;
        return true;
    }
};
// A fence that reached its full deadline is not an error (VK_TIMEOUT is a success code; the fence may still signal): the frame is
// dropped and the slot stays submitted. Only DeadlineLatch consecutive deadlines on presenter fences (>= 6 s without that GPU work
// finishing) stop the presenter as v1670 did on the first, handing the window to the app's renderer.
inline constexpr unsigned DeadlineLatch = 3;
// A run of dropped frames is logged when it reaches these lengths (a frozen picture shows in a player's log within ~1 s).
inline constexpr bool streakLogged(unsigned n) noexcept { return n == 30 || n == 300 || (n >= 3000 && n % 3000 == 0); }
// The presenter's own robust state (one member, under the presenter's mutex).
// (5) The latch sites (Presenter::latch). Fence = a presenter slot wait with no acquired image; ImageSlotWait = the same wait while
// an image is held (6); Present = the present thread's vkQueuePresentKHR; Swapchain = ensureSwapchain's object creation.
enum class Latch : unsigned { Fence, Acquire, ImageIndex, ImageSlotWait, Descriptor, Record, Submit, Present, Swapchain, None };
inline constexpr const char* LatchNames[] = {"fence-wait", "acquire", "image-index", "image-slot-wait", "descriptor", "record", "submit",
                                             "present", "swapchain", "none"};
inline const char* latchName(Latch l) noexcept { return LatchNames[unsigned(l) <= unsigned(Latch::None) ? unsigned(l) : unsigned(Latch::None)]; }
inline constexpr VkResult InjectedResult = VK_ERROR_UNKNOWN; // (7): the failure an injected latch hands the site's real branch
// (7) The test-only latch injection (owner: the presenter, under its mutex).
struct Inject {
    static constexpr std::uint64_t DefaultSeconds = 100;
    Latch site = Latch::None, fired = Latch::None;
    std::uint64_t atNs = 0;
    // Reads the switch (once, at the claim). Returns true when armed; `line` gets the armed log line.
    bool arm(std::uint64_t claimNs, char* line, std::size_t size) noexcept {
        char text[64] = {};
#if defined(__ANDROID__)
        FILE* f = std::fopen(RECOMP_APP_PATH("/private-trace/diag-present-latch-inject"), "r");
        if (!f) return false;
        const std::size_t n = std::fread(text, 1, sizeof text - 1, f); std::fclose(f); text[n] = 0;
#else
        const char* e = std::getenv("RECOMP_PRESENT_LATCH_INJECT");
        if (!e) return false;
        std::snprintf(text, sizeof text, "%s", e);
#endif
        char name[32] = {}; unsigned long long seconds = DefaultSeconds;
        if (std::sscanf(text, "%31s %llu", name, &seconds) < 1) { std::snprintf(line, size, "RENDER_DIRECT_PRESENT latch-inject refused=empty"); return false; }
        for (unsigned k = 0; k <= unsigned(Latch::Submit); ++k) // (the 7 sites with an injection point; not present / swapchain)
            if (!std::strcmp(name, LatchNames[k])) site = Latch(k);
        if (site == Latch::None) { std::snprintf(line, size, "RENDER_DIRECT_PRESENT latch-inject refused=site name=%s", name); return false; }
        atNs = claimNs + std::uint64_t(seconds) * 1'000'000'000ull;
        std::snprintf(line, size, "RENDER_DIRECT_PRESENT latch-inject armed site=%s after_s=%llu (test switch diag-present-latch-inject)",
                      latchName(site), seconds);
        return true;
    }
    // At site s: true exactly once, at the first check of the armed site at or after its time.
    bool hit(Latch s) noexcept { return s == site && due(); }
    [[gnu::noinline]] bool due() noexcept {
        if (nowNs() < atNs) return false;
        fired = site; site = Latch::None;
        return true;
    }
};
struct PresenterState {
    bool held = false;               // (6) a swapchain image is acquired and not yet submitted (presentLocked)
    unsigned lateLatches = 0;        // latch calls after the first (one log line per run)
    Inject inject;                   // (7)
    unsigned deadlineRun = 0;        // consecutive presenter fence waits that reached the full deadline
    std::uint64_t fenceDeadline = 0; // all of them (minute line)
    unsigned dropStreak = 0;         // consecutive frames not shown
    unsigned minImages = 0, images = 0;
    Minute minute;
};
// (6) presentLocked's span with an image held (set after a successful acquire; cleared on every return).
struct Held {
    bool& h;
    explicit Held(bool& flag) noexcept : h(flag) { h = true; }
    ~Held() { h = false; }
    Held(const Held&) = delete;
    Held& operator=(const Held&) = delete;
};
} // namespace recomp::gpu::robust
