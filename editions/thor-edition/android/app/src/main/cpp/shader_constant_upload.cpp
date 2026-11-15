#include "shader_constant_upload.h"

namespace recomp::gpu {
bool decodeShaderFloatConstants(FloatConstantStage stage, std::uint32_t first,
    std::uint32_t count, const std::uint8_t* bytes, std::size_t size,
    ShaderFloatConstants& output) {
    if (stage != FloatConstantStage::Vertex && stage != FloatConstantStage::Pixel) return false;
    const std::uint32_t capacity = stage == FloatConstantStage::Vertex ? 256 : 224;
    if (first > capacity || count > capacity - first || size != std::size_t{count} * 16 ||
        (size && !bytes)) return false;
    if (!count) return true;
    auto candidate = output;
    auto* bank = stage == FloatConstantStage::Vertex ? candidate.vertex.data() : candidate.pixel.data();
    for (std::uint32_t word = 0; word < count * 4; ++word) {
        const auto* source = bytes + std::size_t{word} * 4;
        bank[first * 4 + word] = (std::uint32_t{source[0]} << 24) | (std::uint32_t{source[1]} << 16) |
            (std::uint32_t{source[2]} << 8) | source[3];
    }
    output = candidate;
    return true;
}
}
