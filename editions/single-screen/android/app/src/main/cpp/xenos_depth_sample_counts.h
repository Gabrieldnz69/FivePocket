#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace recomp::gpu {
// Layout adapted from Xenia95a5c3ee, src/xenia/gpu/xenos.h,
// xe_gpu_depth_sample_counts. Copyright 2015 Ben Vanik, BSD-3-Clause;
// see docs/licenses/XENIA.txt. This is a byte codec, NOT query execution.
// In particular, do not adopt command_processor.cc's fixed-count workaround.
struct XenosDepthSampleCounts {
    enum Counter : unsigned { Total=0, DepthFail=1, DepthPass=2, StencilFail=3 };
    std::array<std::array<std::uint32_t,2>,4> values{};
    bool operator==(const XenosDepthSampleCounts&) const = default;
};
inline std::optional<XenosDepthSampleCounts> decodeXenosDepthSampleCounts(
        std::span<const std::uint8_t> bytes) {
    if(bytes.size()!=32)return std::nullopt;
    XenosDepthSampleCounts result;
    for(unsigned i=0;i<8;++i) {
        const auto at=i*4;
        result.values[i/2][i%2]=std::uint32_t{bytes[at]}|
            (std::uint32_t{bytes[at+1]}<<8)|(std::uint32_t{bytes[at+2]}<<16)|
            (std::uint32_t{bytes[at+3]}<<24);
    }
    return result;
}
// Return COUNTER-order deltas, using the original consumer's 32-bit wrapping
// arithmetic. No claim that these are measured, synchronized or complete.
inline std::array<std::uint32_t,4> xenosDepthSampleDelta(
        const XenosDepthSampleCounts& begin,const XenosDepthSampleCounts& end) {
    std::array<std::uint32_t,4> result{};
    for(unsigned i=0;i<4;++i)
        result[i]=end.values[i][0]+end.values[i][1]-begin.values[i][0]-begin.values[i][1];
    return result;
}
}
