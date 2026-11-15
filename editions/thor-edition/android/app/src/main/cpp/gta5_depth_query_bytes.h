#pragma once
#include "xenos_depth_sample_counts.h"

namespace recomp::gpu {
// Read-only description of ONE original type9 query slot. Original822106C0
// sends begin to slot+32 and end to slot+0, NOT the other way around.
// 8225D5C8 consumes little-endian A+B differences and returns fields in this
// order: depth pass, depth fail, stencil fail, total. This does not model its
// fence wait, flags, per-tile aggregation, or device11069 override.
struct Gta5DepthQueryBytes {
    XenosDepthSampleCounts begin,end;
    std::array<std::uint32_t,4> rawConsumerDelta{};
    // Original82210888 stores BE 0xFFFFFEED at end+16/+20 before issue-end.
    // This predicate alone NEVER establishes completion: the original also
    // checks the query's submitted fence before consuming any sample bytes.
    bool originalPendingSentinel=false;
};
inline std::optional<Gta5DepthQueryBytes> decodeGta5DepthQueryBytes(
        std::span<const std::uint8_t> bytes) {
    if(bytes.size()!=64)return std::nullopt;
    Gta5DepthQueryBytes result;
    result.end=*decodeXenosDepthSampleCounts(bytes.first(32));
    result.begin=*decodeXenosDepthSampleCounts(bytes.subspan(32,32));
    const auto delta=xenosDepthSampleDelta(result.begin,result.end);
    result.rawConsumerDelta={delta[XenosDepthSampleCounts::DepthPass],
        delta[XenosDepthSampleCounts::DepthFail],delta[XenosDepthSampleCounts::StencilFail],
        delta[XenosDepthSampleCounts::Total]};
    result.originalPendingSentinel=result.end.values[XenosDepthSampleCounts::DepthPass][0]==0xEDFEFFFFu &&
        result.end.values[XenosDepthSampleCounts::DepthPass][1]==0xEDFEFFFFu;
    return result;
}
}
