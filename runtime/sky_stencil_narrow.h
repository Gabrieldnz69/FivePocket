// sky_stencil_narrow.h (sky-fallback lane, v1695, 2026-11-13; PLAN U1, Discord D2: holes / a grey slab near the moon at night on GPUs
// without depthBounds). DEFAULT OFF: files/private-trace/diag-sky-stencil-narrow turns it on.
//
// hs_sky_skip.h removes the lamp-cone hole with a Vulkan depth-bounds test, so it only works where the device enables depthBounds
// (device.cpp recomp_depth_bounds_enabled; Mali drivers and some others do not offer it). Without it, the light-volume marking passes
// (mark A: depth LESS, stencil ALWAYS, zpass REPLACE 0x40, fail / zfail ZERO; mark B: depth GEQUAL, stencil LESS ref 0x20 mask 0x60,
// zpass REPLACE 0x20, fail / zfail ZERO; both write mask 0xFF) zero the sky's stencil marker 7 under every light volume, and the
// clouds (stencil EQUAL 7, mask 7) skip that footprint (docs/fable/blockers/2026-11-07-lamp-cone-hi-stencil/).
//
// Fallback: such a pass gets the Vulkan stencil write mask 0x60 (the two bits the light passes test) instead of 0xFF. Its effect on
// bits 5-6 is unchanged; bits 0-4 and 7 keep their earlier value instead of being cleared. Over the sky that keeps the marker 7, so
// the clouds draw there. On GEOMETRY pixels inside a light volume the console (and the unmodified path) clears those bits, so this is
// a model deviation there: any later draw that tests those bits could behave differently. The census reader report
// (local-analysis/update-1/subagents-2026-11-13/sky-fallback/tools/) lists those readers; a device census diff decides it.
//
// The rule (pure, host-tested by sky_stencil_narrow_test.cpp over the census rows): stencil test on, one-sided stencil (dc bit 7
// clear), depth write off, colour mask 0 (RB_COLOR_MASK, all four targets), front write mask exactly 0xFF, a non-zero reference, the
// reference and the read mask within 0x60, and every front operation KEEP, ZERO or REPLACE with at least one not KEEP. With those
// operations and a reference inside 0x60, bits 5-6 come out exactly as with the full mask (INCR / DECR / INVERT are excluded so no
// carry or inversion can differ). The priming passes (reference 1 / 3 / 7), the clouds and the light (write mask 0), damage_blits
// (reference 1, read mask 0xFF), ordinary geometry and every colour draw never qualify.
#pragma once
#include <cstdint>

namespace recomp::sky_stencil_narrow {

inline constexpr std::uint32_t kNarrowMask = 0x60u;

// dc RB_DEPTHCONTROL, front RB_STENCILREFMASK (ref bits 0-7, read mask 8-15, write mask 16-23), colorMask RB_COLOR_MASK (low 16 bits).
inline bool qualifies(std::uint32_t dc, std::uint32_t front, std::uint32_t colorMask) noexcept {
    if (!(dc & 1u)) return false;                         // stencil test off
    if (dc & 0x80u) return false;                         // two-sided stencil: the back face has its own state (not covered)
    if (dc & 4u) return false;                            // depth write on: geometry
    if (colorMask & 0xFFFFu) return false;                // any colour write
    if (((front >> 16) & 0xFFu) != 0xFFu) return false;   // only the full-byte writers
    const std::uint32_t ref = front & 0xFFu, readMask = (front >> 8) & 0xFFu;
    if (ref == 0u || (ref & ~kNarrowMask) || (readMask & ~kNarrowMask)) return false;
    const std::uint32_t fail = (dc >> 11) & 7u, zpass = (dc >> 14) & 7u, zfail = (dc >> 17) & 7u;
    if (fail > 2u || zpass > 2u || zfail > 2u) return false;  // only KEEP (0), ZERO (1), REPLACE (2)
    return fail || zpass || zfail;                        // some stencil write happens
}

// The companion (same switch): the follow-up pass of a light (im#5v after mark B in capsule and some spot lights: stencil EQUAL
// reference 0x20, read mask 0xFF, depth GEQUAL / LESS, zfail ZERO, colour mask 0) expects the marked pixels to be exactly 0x20. After a
// narrowed mark B a geometry pixel holds 0x20 | its own low bits (lit geometry carries stencil 1 or 3), so that test would fail where
// the unmodified path passes, and the light would leak past the follow-up's cut. Comparing through 0x60 instead gives the unmodified
// answer on every pixel a narrowed mark touched (there the unmodified path has bits 0-4 and 7 clear), and on untouched pixels as long
// as no other writer leaves bits 5-6 set together with other bits (the census replay found no pre-mark writer of bits 5-6).
// Rule: stencil test on, one-sided, depth write off, colour mask 0, a non-zero reference within 0x60, read mask exactly 0xFF, and
// every front operation KEEP / ZERO / REPLACE. The write mask is left as the guest set it.
inline bool followUp(std::uint32_t dc, std::uint32_t front, std::uint32_t colorMask) noexcept {
    if (!(dc & 1u) || (dc & 0x80u) || (dc & 4u) || (colorMask & 0xFFFFu)) return false;
    const std::uint32_t ref = front & 0xFFu, readMask = (front >> 8) & 0xFFu;
    if (ref == 0u || (ref & ~kNarrowMask) || readMask != 0xFFu) return false;
    const std::uint32_t fail = (dc >> 11) & 7u, zpass = (dc >> 14) & 7u, zfail = (dc >> 17) & 7u;
    return fail <= 2u && zpass <= 2u && zfail <= 2u;
}

// Whether the fallback applies to a draw: the switch on, the depth-bounds path not usable (no device feature, or
// diag-force-no-depth-bounds), and the rule.
inline bool applies(std::uint32_t dc, std::uint32_t front, std::uint32_t colorMask, bool switchOn, bool depthBoundsUsable) noexcept {
    return switchOn && !depthBoundsUsable && qualifies(dc, front, colorMask);
}

} // namespace recomp::sky_stencil_narrow
