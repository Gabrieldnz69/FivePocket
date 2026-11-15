// c2_region.h (C2 DIAGNOSTIC, bugfix-stability 2026-11-07): which guest memory holds the unkicked commands. Pure C++ (host-tested:
// c2_region_test.cpp, with the ring words of t-creatorgo-20261107-2005).
// v1045 took the NEWEST INDIRECT_BUFFER's end as the start of the unkicked region. At the freeze that was a small 35-word buffer
// (0x07d61120) ABOVE D3D's command write pointer (dev+48 = 0x07d4d5fc), so the region was empty. The ring interleaves the main
// command buffer (rising: ... 0x07d2d2c0 + 0xb4e words = 0x07d2fff8) with small 6-35-word buffers that sit inside the range the
// main buffer later grows into (0x07d3cc60 + 11 words = 0x07d3cc8c lies between the main end and the write pointer), so no "IB end"
// rule names the start reliably. The dump therefore takes a fixed window ending at the write pointer (it covers the main buffer's
// last end, the small buffers and everything written after them), and reports the highest IB end at or below the write pointer for
// the host decoder; the decoder's fence-value scan does not depend on packet alignment.
#pragma once
#include <cstdint>

namespace recomp::c2region {

struct Region {std::uint32_t start = 0, end = 0, bytes = 0, ibEndBelow = 0;};

// ringWord(i): the ring's i-th word, called with i in [0, words) only (wrapped here). rw: the read word. words: ring size in words. writePtr: dev+48 masked
// to a physical address (0 = unknown). window: bytes taken below the write pointer.
template<class RingWord>
Region unkicked(RingWord ringWord, std::uint32_t rw, std::uint32_t words, std::uint32_t writePtr, std::uint32_t window) {
    Region r;
    if (!writePtr || !words) return r;
    for (std::uint32_t back = 1; back <= 512 && back < words; ++back) {
        const std::uint32_t i = (rw + words - back) % words, h = ringWord(i);
        if ((h>>30) != 3u || ((h>>8)&0x7Fu) != 0x3Fu || ((h>>16)&0x3FFFu) < 1u) continue;
        const std::uint32_t end = (ringWord((i+1)%words)&0x1FFFFFFFu) + (ringWord((i+2)%words)&0xFFFFFu)*4u;
        if (end <= writePtr && end > r.ibEndBelow) r.ibEndBelow = end;
    }
    r.end = writePtr;
    r.start = writePtr > window ? writePtr - window : 0u;
    r.bytes = r.end - r.start;
    return r;
}

} // namespace recomp::c2region
