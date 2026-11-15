#pragma once
#include "gta5_color8_clear.h"
#include <algorithm>

namespace recomp::gpu {
inline std::optional<std::uint32_t> xenosUnorm8FloatBits(unsigned value) {
    if(value>255)return std::nullopt;
    if(!value)return 0;
    if(value==255)return 0x3F800000u;
    unsigned k=0;for(unsigned v=value;v>1;v>>=1)++k;
    // Repeating eight-bit fraction. The fourth leading bit is always the
    // guard bit, followed by a nonzero repeating tail: round upward once.
    return ((k+118)<<23)+(value<<(23-k))+(value<<(15-k))+(value<<(7-k))+1;
}
// Correctly rounded binary32 for a canonical 10-bit UNORM value. For 0<v<1023,
// v/1023 has the repeating binary expansion .vvvvvvvvvvvvvvvvvvvvvv... . Build
// its 24-bit significand and guard bit using integers; a set guard always has
// a nonzero repeating tail, so there is no half-way tie. This mirrors the GPU
// byte-decode implementation without relying on approximate reciprocal/divide.
inline std::optional<std::uint32_t> xenosUnorm10FloatBits(unsigned value) {
    if(value>1023)return std::nullopt;
    if(!value)return 0;
    if(value==1023)return 0x3F800000u;
    unsigned k=0;for(unsigned v=value;v>1;v>>=1)++k;
    unsigned significand=(value<<(23-k))|(value<<(13-k));
    significand|=k<=3?value<<(3-k):value>>(k-3);
    if(k>=4 && ((value>>(k-4))&1))++significand;
    return ((k+116)<<23)+significand;
}
// Render-target conversion, NOT GTA V's CPU clear packing. Matches the
// TRUNC(linear * piece_scale) + piece_offset construction in the pinned
// Xenia-derived Vulkan render_target_cache.cpp LinearToPWLGamma. No preliminary
// nearest-even 10-bit quantization, and no hardware sRGB approximation.
// Finite blend outputs saturate to [0,1]. Alpha/ordinary UNORM use explicit
// nearest-even byte quantization. This is a reference policy, not Xbox capture
// conformance. In particular decode/encode is NOT assumed lossless for pixels
// untouched by a draw; those require preservation of their original bytes.
inline std::optional<std::array<std::uint8_t,4>> packXenosColor8Render(
        const std::array<float,4>& rgba,unsigned format) {
    if(format>1)return std::nullopt;
    std::array<std::uint8_t,4> result{};
    for(unsigned c=0;c<4;++c) {
        if(!std::isfinite(rgba[c]))return std::nullopt;
        const float f=std::clamp(rgba[c],0.f,1.f);
        if(format==1 && c<3) {
            const unsigned shift=f<64.f/1023.f?0:f<128.f/1023.f?1:f<512.f/1023.f?2:3;
            const unsigned offset=shift?16u<<shift:0;
            const volatile float scaled=f*(1023.f/float(1u<<shift));
            result[c]=static_cast<unsigned>(scaled)+offset;
        } else {
            const volatile float scaled=f*255.f;
            result[c]=roundPositiveFloatEven(scaled);
        }
    }
    return result;
}
}
