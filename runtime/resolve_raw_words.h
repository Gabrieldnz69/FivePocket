#pragma once
// resolve_raw_words.h (v1194, bugfix-stability 2026-11-08): GTA Online's blended heads ("clay faces", lead's trace
// local-analysis/update-1/HEAD-BLEND-CLAY-FACES-2026-11-08.md). Pure parts, host-tested by
// local-analysis/bugfix/clayfaces/raw_words_test.cpp.
//
// What the game does (all guest code): the head blend compresses each blended head on the GPU into its own DXT1 texture.
// sub_82911618 retargets a k_16_16_16_16 render target of width/4 x height/4 onto the DXT1's BaseAddress / MipAddress, sets
// the target's colour exponent bias to -10 (sub_8323E778: RB_COLOR_INFO bits 20-25), draws skinblend.fxc PS_CompressDxt1 and
// resolves each mip wider than 16 px with flags 0x28000000 (copy exponent +10). PS_CompressDxt1's last instruction folds each
// 16-bit block word w (endpoint 0, endpoint 1, the two index halves) into a signed value: oC0 = w - (w >= 32768 ? 65536 : 0)
// (constants 32768 / 65536 in its c252.x / c253.w). The target's fixed point (s5.10: value * 2^-10, 1/1024 steps, -32..32)
// stores exactly that signed integer, and the +10 resolve writes it back as the 16-bit word: guest memory receives the DXT1
// blocks, which the head draw then samples as k_DXT1.
//
// What the port did (v1113): the target was RGBA16F without the bias (block words above 2048 lose bits), the resolve converted
// to RGBA16F scaled by 2^10 (overflow), the converted resolve was never written back, and the DXT1 fetch at that address
// decoded guest memory that nothing wrote: the pre-blend head texture.
//
// What v1194 does, ONLY for this case (a k_16_16_16_16 colour target with a non-zero colour exponent bias; every other
// target, SSAO's included, keeps its key and format):
//   1. the target has its own key, ExactFixed16x4, held as R32G32B32A32_SFLOAT (the shader's integers stay exact);
//   2. a resolve of it with target bias -10 and copy exponent +10 into a k_16_16_16_16 (26) or k_DXT1_AS_16_16_16_16 (51)
//      destination is a plain copy (no conversion) and is written back to guest memory at every level it resolves, each
//      texel's components as the 16-bit words word(v) = int16(clamp(round(v))), laid out by the destination plan of
//      rawFetch() (format 26, packed mips off: the DXT1's own mip layout, see rawFetch);
//   3. the DXT1 fetch keeps decoding guest memory (v74's bypass), which now holds the blocks, as on the 360.
// Steady-state cost: none (one more integer compare where the target key is formed); work only at head-blend resolves.
#include <array>
#include <cmath>
#include <cstdint>

namespace recomp::gpu::generic::rawwords {

inline constexpr unsigned ExactFixed16x4 = 69;   // the target key of a k_16_16_16_16 surface with a colour exponent bias
inline constexpr int HeadBlendTargetBias = -10, HeadBlendCopyExp = 10;

// RB_COLOR_INFO's colour exponent bias (bits 20-25, signed).
inline int colorExpBias(std::uint32_t info) noexcept {
    const unsigned b = (info >> 20) & 63u;
    return b < 32 ? int(b) : int(b) - 64;
}
// The target key of colour surface `info`: ExactFixed16x4 for EDRAM format 5 (k_16_16_16_16) with a non-zero bias while `on`,
// else `storage` (the renderer's usual key for the format).
inline unsigned colorKey(std::uint32_t info, unsigned storage, bool on) noexcept {
    return on && ((info >> 16) & 15u) == 5u && colorExpBias(info) != 0 ? ExactFixed16x4 : storage;
}
// Is this resolve the head blend's raw-word resolve (written back as 16-bit words at every level)?
inline bool rawResolve(unsigned targetKey, int targetBias, int copyExp, int destExpAdjust, unsigned destFormat) noexcept {
    return targetKey == ExactFixed16x4 && targetBias == HeadBlendTargetBias && copyExp == HeadBlendCopyExp && destExpAdjust == 0 &&
           (destFormat == 26u || destFormat == 51u);
}
// The 16-bit word a target value becomes (two's complement of the clamped, rounded value).
inline std::uint16_t word(float v) noexcept {
    if (!(v == v)) return 0;   // NaN
    const float r = std::nearbyint(std::fmin(std::fmax(v, -32768.0f), 32767.0f));
    return std::uint16_t(std::int32_t(r) & 0xFFFF);
}
// The destination fetch the writeback plans with: format 26 (one 8-byte texel per 4x4 DXT1 block, the decoder's raw path)
// and packed mips cleared. Why packed mips off: the 360's resolve writes level L where the DXT1 (4x the size) keeps level L;
// a 64x64 16_16_16_16 plan would pack levels 2-3 (16 / 8 texels) into a tail, while the 256x256 DXT1 packs only from 16
// texels (level 4) on, and levels 0-3 of both have the same storage (rows of max(pow2(w) >> L, 1) blocks aligned to 32).
// Fetch word 1 bits 0-5 = format; word 5 bit 11 = packed mips (xenos_texture_description.h).
inline std::array<std::uint32_t, 6> rawFetch(std::array<std::uint32_t, 6> fetch) noexcept {
    fetch[1] = (fetch[1] & ~63u) | 26u;
    fetch[5] &= ~2048u;
    return fetch;
}
} // namespace recomp::gpu::generic::rawwords
