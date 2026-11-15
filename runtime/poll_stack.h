#pragma once
// poll_stack.h (v1193, bugfix-stability 2026-11-08; DIAGNOSTIC ONLY, LOG ONLY). The intermittent GPU-wait freeze (C2 face camera;
// the GTA Online arrival cutscene in t-onlshow-20261108-1451 v1104 and -1916 v1113): the guest render thread spins in D3D's GPU-wait
// poll sub_82220140 while the native renderer is idle. sub_82220140 has six callers (sub_8250D988 BlockOnFence, sub_823BC018, sub_8232DEF8
// ring space, sub_822D41E8, sub_836BDEB0, sub_836BE2F8) and most call it through __imp__ (a wrapper on them would miss them). The poll
// itself calls sub_82220130 (the TLS getter) through the wrapper, so a hook there sees every poll iteration with the poll's own r1/r31:
//   r1  = the poll's frame (stwu -112); [r1] = the wait function's sp; the poll saved the wait function's r30/r31 (std at -24/-16 of
//         that sp, __savegprlr_29) and its return address into the wait function at sp-8;
//   r31 = the wait record (sub_8251E820): [0] device, [4] reason (1 ring space, 2 sub_823BC018, the caller's r5 for BlockOnFence),
//         [8] CP block word 0 at the start, [12]/[16] tick, [20] timebase.
// At a freeze report the stack is walked from r1 by its back chain (frame k: next = [sp], return address = [next-8]) and logged with
// the words saved below each frame; the D3D device words around the fence, kick and tiling state are logged beside it.
// Pure parts (walk) are host-tested by local-analysis/bugfix/c2/poll_stack_test.cpp.
#include <atomic>
#include <cstdint>
#include <pthread.h>

namespace recomp::pollstack {
inline constexpr unsigned Slots = 8, MaxFrames = 20;
inline constexpr std::uint32_t PollReturn = 0x822201C0u;   // the return address of the poll's call to sub_82220130

struct Slot {
    std::atomic<std::uint64_t> owner{0};
    std::atomic<std::uint32_t> sp{0}, record{0};
    std::atomic<std::uint64_t> polls{0};
    std::uint64_t lastReported = 0;   // read and written only by the freeze dump (one watchdog thread)
};
inline Slot slots[Slots];
inline thread_local unsigned mySlot = ~0u;

// The hook, on every poll iteration of this thread: two relaxed stores and one add. A thread past the first Slots pollers is ignored.
inline void note(std::uint32_t sp, std::uint32_t record) noexcept {
    if (mySlot == ~0u) {
        mySlot = Slots;
        const std::uint64_t me = std::uint64_t(pthread_self()) | 1u;
        for (unsigned k = 0; k < Slots; ++k) {
            std::uint64_t expected = 0;
            if (slots[k].owner.compare_exchange_strong(expected, me, std::memory_order_relaxed)) { mySlot = k; break; }
        }
    }
    if (mySlot >= Slots) return;
    auto& s = slots[mySlot];
    s.sp.store(sp, std::memory_order_relaxed); s.record.store(record, std::memory_order_relaxed);
    s.polls.fetch_add(1, std::memory_order_release);
}

struct Frame { std::uint32_t next = 0, ret = 0; };
// The back-chain walk from sp. Read32(address) returns the big-endian guest word, or -1 when unreadable. Stops at an unreadable or
// zero back pointer, at one that does not move up the stack (next <= sp), or at a frame larger than 64 KiB. Returns the frame count.
template <class Read32>
unsigned walk(std::uint32_t sp, Read32 read32, Frame* out, unsigned max) {
    unsigned n = 0;
    while (n < max) {
        const long long next = read32(sp);
        if (next < 0 || std::uint32_t(next) <= sp || std::uint32_t(next) - sp > 0x10000u) break;
        const long long ret = read32(std::uint32_t(next) - 8u);
        out[n].next = std::uint32_t(next); out[n].ret = ret < 0 ? 0u : std::uint32_t(ret);
        sp = std::uint32_t(next); ++n;
    }
    return n;
}
} // namespace recomp::pollstack
