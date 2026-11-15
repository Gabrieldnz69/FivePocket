// hs_sky_skip.h (bugfix-stability 2026-11-07; lead decision (b): the lamp-cone hole, docs/fable/blockers/2026-11-07-lamp-cone-hi-stencil/).
// v1079: default ON (dawn A/B passed 2026-11-08); files/private-trace/diag-hs-sky-skip-off restores the earlier behaviour.
// 2026-11-08 creator fix: the rule is limited to STENCIL-ONLY passes that write no depth (colour mask 0, depth write off). The v1079 rule
// also matched the GTA Online creator's ped geometry (ped#17p etc.: dc 00708767 = depth write on, colour writes on, RB_HIZCONTROL bit 2)
// drawn over the creator's empty, cleared depth, so the depth-bounds test removed every portrait and preview ped (t-bgcensus-20261108-0158:
// 38 skipped draws, all ped / hair / palette geometry plus two damage_blits colour draws into the offscreen damage target). In the world
// (t-fpaint79-20261108-0146 fm_intro census) all 79 skipped draws were spot-light marking passes with no colour target, colour mask 0000
// and depth write off, so the world behaviour is unchanged by the narrower rule.
// Pure rule (host-tested: hs_sky_skip_test.cpp) for which draws skip their stencil effect on pixels where nothing was ever drawn.
//
// The street lamp's spot-light marking passes (spot#20v mark A: depth LESS, stencil zfail ZERO; mark B: depth GEQUAL, stencil fail ZERO;
// both RB_HIZCONTROL bit 2) zero the sky's stencil marker (7) under the light volume, so the clouds (stencil EQUAL 7/7) skip the
// footprint: a flat cone-shaped hole in the clouds (t-lampcone5 census 4). The 360 shows no such hole (NotgdPslY4Q t574/t577, lit lamps
// against the sky). The register fields do not say how the console avoids it. The effect is that of a hierarchical cull of the empty
// sky tiles. This models that effect per pixel: for these passes, pixels whose stored depth is still the cleared far value (0, reverse Z;
// nothing drawn there) are excluded before the stencil step (a Vulkan depth-bounds test, [smallest positive, 1]).
// A pass that writes no colour and no depth only changes stencil, and a light-volume mark only matters where there is geometry to light,
// so excluding never-drawn pixels from it cannot remove anything visible except the hole. Geometry (depth write on) and colour draws are
// never affected. A model of the console's effect, not a register decode (the remaining uncertainty is recorded in DISCOVERIES).
//
// Interior check (thor-play-1021 house censuses text-1 / char-wheel-1 / michael-torso-1, 58 censuses): besides the light marks, only
// damage_blits#9p (dc 28949773: stencil only, colour mask 0, depth write off, into the offscreen ped-damage target) could still match; it
// and the creator's damage_blits#8p use depth ALWAYS, every light mark LESS or GEQUAL, so a depth function of NEVER / ALWAYS is excluded.
//
// The draws: depth test and stencil test on, depth write OFF, depth function neither NEVER nor ALWAYS, colour mask 0, RB_HIZCONTROL bit 2,
// and a stencil write (front write mask
// non-zero and some front operation not KEEP). The light passes (ops KEEP), the hi-stencil priming passes (depth off), the clouds,
// ordinary geometry, peds and every colour draw never qualify.
#pragma once
#include <cstdint>

namespace recomp::hs_sky_skip {

// dc RB_DEPTHCONTROL, front RB_STENCILREFMASK (write mask bits 16-23), hiz RB_HIZCONTROL, colorMask RB_COLOR_MASK (low 16 bits, all four
// targets); enabled: the switch and the device feature.
inline bool applies(std::uint32_t dc, std::uint32_t front, std::uint32_t hiz, std::uint32_t colorMask, bool enabled) noexcept {
    if (!enabled) return false;
    if (!(dc & 1u) || !(dc & 2u)) return false;          // stencil and depth test on
    if (dc & 4u) return false;                           // depth write on: geometry (the creator's peds), never skipped
    if (colorMask & 0xFFFFu) return false;               // any colour write: a visible draw, never skipped
    const std::uint32_t zfunc = (dc >> 4) & 7u;
    if (zfunc == 0u || zfunc == 7u) return false;        // depth NEVER / ALWAYS: not tested against the scene depth (damage_blits)
    if (!(hiz & 4u)) return false;                       // the hierarchical-stencil marking passes
    if (!((front >> 16) & 0xFFu)) return false;          // no stencil write
    const std::uint32_t fail = (dc >> 11) & 7u, zpass = (dc >> 14) & 7u, zfail = (dc >> 17) & 7u;
    return fail || zpass || zfail;                       // some operation is not KEEP (0)
}

} // namespace recomp::hs_sky_skip
