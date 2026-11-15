#pragma once
// freezewatch.h (v965, DIAGNOSTIC; trainer-mods 2026-11-06): a self-report for render freezes such as C2cam/c2hang (the creator's head-blend
// edit with the face camera: the game's render thread spins in its GPU wait sub_82220140 while the deferred fences / tile IRQs that the
// async Vulkan worker delivers never land; a person closed the hung game before a manual capture could run). When no present has
// happened for StallNs, a watchdog thread logs, every ReportNs and at most MaxReports times per stall:
//   RENDER_DIAG_FREEZEWATCH stalled_ms=.. presents=.. item=<kind>:<age_ms> items=.. wait=<site>:<age_ms> waits=a/b/c timeouts=a/b/c
//     lost=.. fences=<slot>:<status>,.. (status from vkGetFenceStatus, non-blocking: 0 signalled, 1 not ready, <0 error)
// It answers: is the worker inside an item (which kind, how long) or inside a fence wait (which site, how long), are those waits
// timing out (10 s each), and is the GPU completing ANY slot fence (a device-side hang: none signals). Reads only atomics; the one
// device call is vkGetFenceStatus (thread-safe; the fences are never reset or destroyed by it). Costs per item/wait: two relaxed stores.
// Kinds follow generic_renderer.cpp's AsyncKind (1 draw, 2 clear, 3 resolve, 4 invalidate, 5 unlock, 6 prepare, 7 present, 8 work).
// Wait sites: 1 the pool resetter, 2 the census split, 3 waitSlot (the frame slot fence).
// v1710 (lane candidate-v1710 2026-11-13): THE WATCHDOG IS LEASED. Thor run t-vmod-A-20261113-0107 (v1670): a guest stop (an audio route
// change) unwound the run, V1620Revoke revoked the touch lease, ~VulkanEngine destroyed the device; presents had stopped, so 6.1 s later
// this watchdog's first report called vkGetFenceStatus on the destroyed device -> SIGSEGV (libvulkan vkGetFenceStatus+16 <- line()+388),
// exit-info APP CRASH(NATIVE): a clean stop became a crash. The device pointer and the slot fences were never cleared, and the CP snapshot
// (dump) read guest memory through pointers trace_runner never cleared either. Now each watchdog tick holds the run's lease
// (recomp::fintrap::touchLease(), published with the address space and revoked by trace_runner's V1620Revoke before the guest memory
// and the Vulkan device are released, draining a reader inside): with the lease null it makes no device call and no guest read, and
// logs one "RENDER_DIAG_FREEZEWATCH quiet" line per revoke when a report would have been due. retire() (V1620Revoke, after the
// drain) clears the device and the fences. While the run lives nothing changes (same reports, same cost: the lease is two seq_cst
// RMWs per second on the watchdog thread). Host test: candidate-v1710/tests/freezewatch_lease_test.cpp (v1670's header faults).
#define RECOMP_FREEZEWATCH_LEASE 1
#include "finish_trap_diag.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <unistd.h>

namespace recomp::freezewatch {
inline constexpr std::int64_t StallNs = 6'000'000'000, ReportNs = 5'000'000'000;
inline constexpr unsigned MaxReports = 24, Sites = 4, Slots = 8;
inline constexpr int VkTimeout = 2, VkDeviceLost = -4; // VK_TIMEOUT, VK_ERROR_DEVICE_LOST
using StatusFn = int (*)(void* device, std::uint64_t fence);
using LogFn = void (*)(const char* line);
// v1033 (bugfix-stability, C2): the CP snapshot at a stall, installed by trace_runner.cpp (the ring, the D3D fence block, the deferred
// completion stores queued vs applied). Called by the watchdog on reports 1 and 3 of a stall (~6 s and ~16 s): two snapshots show whether
// anything still moves. report = the report's number within the stall.
using DumpFn = void (*)(unsigned report);

struct State {
    std::atomic<int> itemKind{0};                   // 0: the worker is between items
    std::atomic<std::int64_t> itemSince{0};
    std::atomic<std::uint64_t> items{0};
    std::atomic<int> waitSite{0};                   // 0: in no fence wait
    std::array<std::atomic<int>, Sites> active{};    // v1033: threads inside a wait at each site now (waitSite is one racy slot)
    std::atomic<std::int64_t> waitSince{0};
    std::array<std::atomic<std::uint64_t>, Sites> waits{}, timeouts{};
    std::atomic<std::uint64_t> lost{0};
    std::atomic<std::uint64_t> presents{0};
    std::atomic<std::int64_t> lastPresentNs{0};
    std::array<std::atomic<std::uint64_t>, Slots> fences{}; // the frame slots' fences as last submitted (0: none)
    std::atomic<void*> device{nullptr};
    std::atomic<DumpFn> dump{nullptr};               // v1033
    StatusFn status = nullptr;
    LogFn log = nullptr;
    std::atomic<bool> started{false};
    std::atomic<unsigned> retired{0};                // v1710: +1 per retire() (the watchdog's quiet line, once per revoke)
};
inline State& state() noexcept { static State s; return s; }
inline std::int64_t nowNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct ItemScope { // the async worker, around one executed item
    explicit ItemScope(unsigned kind) noexcept {
        auto& s = state(); s.itemSince.store(nowNs(), std::memory_order_relaxed); s.itemKind.store(int(kind), std::memory_order_release);
    }
    ~ItemScope() { auto& s = state(); s.itemKind.store(0, std::memory_order_release); s.items.fetch_add(1, std::memory_order_relaxed); }
    ItemScope(const ItemScope&) = delete; ItemScope& operator=(const ItemScope&) = delete;
};
// Around one vkWaitForFences: wait(site, [&]{ return vkWaitForFences(...); }) returns the call's result unchanged.
template <class F> auto wait(int site, F&& call) {
    auto& s = state();
    const int prev = s.waitSite.load(std::memory_order_relaxed);
    s.waitSince.store(nowNs(), std::memory_order_relaxed); s.waitSite.store(site, std::memory_order_release);
    const bool counted = site > 0 && unsigned(site) < Sites;
    if (counted) s.active[std::size_t(site)].fetch_add(1, std::memory_order_acq_rel);
    const auto result = call();
    if (counted) s.active[std::size_t(site)].fetch_sub(1, std::memory_order_acq_rel);
    s.waitSite.store(prev, std::memory_order_release);
    if (site > 0 && unsigned(site) < Sites) {
        s.waits[std::size_t(site)].fetch_add(1, std::memory_order_relaxed);
        if (int(result) == VkTimeout) s.timeouts[std::size_t(site)].fetch_add(1, std::memory_order_relaxed);
    }
    if (int(result) == VkDeviceLost) s.lost.fetch_add(1, std::memory_order_relaxed);
    return result;
}
inline void noteSubmit(unsigned slot, std::uint64_t fence) noexcept {
    if (slot < Slots) state().fences[slot].store(fence, std::memory_order_release);
}
// One report line from the state at `now` (pure apart from the status calls; host tests pass a fake StatusFn).
inline int line(char* out, std::size_t size, std::int64_t now) noexcept {
    auto& s = state();
    const auto age = [&](std::int64_t since) { return since ? double(now - since) * 1e-6 : 0.0; };
    const int kind = s.itemKind.load(std::memory_order_acquire), site = s.waitSite.load(std::memory_order_acquire);
    int n = std::snprintf(out, size, "RENDER_DIAG_FREEZEWATCH stalled_ms=%.0f presents=%llu item=%d:%.0f items=%llu wait=%d:%.0f "
        "waits=%llu/%llu/%llu timeouts=%llu/%llu/%llu lost=%llu fences=",
        age(s.lastPresentNs.load(std::memory_order_relaxed)), (unsigned long long)s.presents.load(), kind,
        kind ? age(s.itemSince.load(std::memory_order_relaxed)) : 0.0, (unsigned long long)s.items.load(), site,
        site ? age(s.waitSince.load(std::memory_order_relaxed)) : 0.0,
        (unsigned long long)s.waits[1].load(), (unsigned long long)s.waits[2].load(), (unsigned long long)s.waits[3].load(),
        (unsigned long long)s.timeouts[1].load(), (unsigned long long)s.timeouts[2].load(), (unsigned long long)s.timeouts[3].load(),
        (unsigned long long)s.lost.load());
    bool any = false;
    for (unsigned k = 0; k < Slots && n > 0 && std::size_t(n) + 16 < size; ++k) {
        const std::uint64_t f = s.fences[k].load(std::memory_order_acquire);
        if (!f) continue;
        void* dev = s.device.load(std::memory_order_acquire);
        const int st = (s.status && dev) ? s.status(dev, f) : 99;
        n += std::snprintf(out + n, size - std::size_t(n), "%s%u:%d", any ? "," : "", k, st); any = true;
    }
    if (!any && n > 0 && std::size_t(n) + 2 < size) { out[n++] = '-'; out[n] = 0; }
    if (n > 0 && std::size_t(n) + 40 < size)   // v1033: who is inside a wait now, per site (the reliable answer; wait= can be stale)
        n += std::snprintf(out + n, size - std::size_t(n), " active=%d/%d/%d", s.active[1].load(std::memory_order_acquire),
            s.active[2].load(std::memory_order_acquire), s.active[3].load(std::memory_order_acquire));
    return n;
}
// The decision, separate for tests: report now? (a stall of at least StallNs since the last present, ReportNs apart, MaxReports per stall)
struct Pacer {
    std::uint64_t stallPresents = ~0ull; std::int64_t lastReport = 0; unsigned reports = 0;
    bool due(std::uint64_t presents, std::int64_t lastPresent, std::int64_t now) noexcept {
        if (!presents || now - lastPresent < StallNs) return false;
        if (presents != stallPresents) { stallPresents = presents; reports = 0; lastReport = 0; } // a new stall
        if (reports >= MaxReports || (lastReport && now - lastReport < ReportNs)) return false;
        lastReport = now; ++reports; return true;
    }
};
// v1305 (bugfix-stability, release blocker; lead 2026-11-10 00:3x): the snapshot's FILE (files/private-trace/diag-c2-cmdbuf-<report>.bin,
// 262 KB each) is a developer artifact. On 2026-11-09 a Fold run of v1400 wrote both during a normal load (a >= 6 s present stall), so a
// public install would collect them too. The file opens only while the switch file `switchPath` exists (checked at each snapshot, two
// per stall); absent = nullptr, nothing created. The snapshot's log lines are unchanged (logcat only, no file).
inline std::FILE* openSnapshot(const char* switchPath, const char* path) noexcept {
    if (!switchPath || !path || ::access(switchPath, F_OK) != 0) return nullptr;
    return std::fopen(path, "wb");
}
// v1033: which reports of a stall take the CP snapshot (two per stall: whether anything still moves between ~6 s and ~16 s).
inline bool snapshotReport(unsigned report) noexcept { return report == 1 || report == 3; }
// v1710: trace_runner's V1620Revoke, right after touchLease().revoke() drained the readers: no device or fence survives the run.
inline void retire() noexcept {
    auto& s = state();
    s.device.store(nullptr, std::memory_order_release);
    for (auto& f : s.fences) f.store(0, std::memory_order_release);
    s.retired.fetch_add(1, std::memory_order_acq_rel);
}
// v1710: one watchdog tick (the thread below, once a second; host tests call it too). Holds the run's lease for the whole report.
inline void tick(Pacer& pacer, char* text, std::size_t size, unsigned& quietSeen) noexcept {
    auto& st = state();
    const ::recomp::fintrap::LeaseRead lease(::recomp::fintrap::touchLease());
    const std::int64_t now = nowNs();
    const bool due = pacer.due(st.presents.load(std::memory_order_acquire), st.lastPresentNs.load(std::memory_order_relaxed), now);
    if (!lease.base) { // no live run: the device and the guest memory may be gone; no vkGetFenceStatus, no snapshot
        const unsigned r = st.retired.load(std::memory_order_acquire);
        if (due && r != quietSeen && st.log) {
            quietSeen = r;
            std::snprintf(text, size, "RENDER_DIAG_FREEZEWATCH quiet: the run's lease is revoked (no fence status, no snapshot) presents=%llu "
                          "stalled_ms=%.0f retired=%u", (unsigned long long)st.presents.load(), double(now - st.lastPresentNs.load()) * 1e-6, r);
            st.log(text);
        }
        return;
    }
    if (due && line(text, size, now) > 0) {
        st.log(text);
        if (snapshotReport(pacer.reports))   // v1033: the CP snapshot, twice per stall
            if (const DumpFn d = st.dump.load(std::memory_order_acquire)) d(pacer.reports);
    }
}
// The render thread, after every present: the clock, and the watchdog's start (once, on the first present).
inline void presented() noexcept {
    auto& s = state();
    s.lastPresentNs.store(nowNs(), std::memory_order_relaxed); s.presents.fetch_add(1, std::memory_order_release);
    bool expected = false;
    if (!s.log || !s.started.compare_exchange_strong(expected, true)) return;
    std::thread([] {
        Pacer pacer; char text[640]; unsigned quietSeen = 0;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            tick(pacer, text, sizeof text, quietSeen); // v1710: under the run's lease
        }
    }).detach();
}
} // namespace recomp::freezewatch
