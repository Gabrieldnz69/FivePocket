#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace recomp::gpu {
enum class FloatConstantStage { Vertex, Pixel };
// The current offline-converter ABI, not the complete Xenos register file.
// Initialization/validity policy belongs to the eventual command backend.
struct ShaderFloatConstants {
    std::array<std::uint32_t, 256 * 4> vertex{};
    std::array<std::uint32_t, 224 * 4> pixel{};
};

// Decode an explicitly bounded BE float4 range without float arithmetic or NaN
// canonicalization. Publishes nothing on failure, even if input aliases output.
// Caller owns a stable input snapshot; this function does not dereference guest
// virtual addresses, infer register mappings, or submit GPU commands.
bool decodeShaderFloatConstants(FloatConstantStage stage, std::uint32_t first,
    std::uint32_t count, const std::uint8_t* bytes, std::size_t size,
    ShaderFloatConstants& output);
}
