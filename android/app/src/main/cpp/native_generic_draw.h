#pragma once
#include "device.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace recomp::gpu {
// Generic executor for offline XenosRecomp translations (descriptor32 ABI,
// patches/xenosrecomp 0005): set0/1/2 = Texture2D/Texture2DArray/TextureCube
// heaps, set3 = sampler heap, set4 bindings 0/1/2 = vertex/pixel/shared
// constant byte buffers (little-endian words), spec constant 0 = flags.
// Shared constants: 2D descriptor index for sampler register r at r*4, its
// sampler index at 192+r*4. One draw into one offscreen colour target; the
// caller supplies already endian-normalized vertex data. EXPERIMENTAL: no
// qualification, no depth/blend/MSAA yet, not wired into the live renderer.
struct GenericTexture {
    unsigned reg=0,width=1,height=1;VkFormat format=VK_FORMAT_R8G8B8A8_UNORM;
    std::vector<std::uint8_t> pixels; // Tightly packed rows of `format`.
    VkComponentMapping swizzle{VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY};
    VkFilter filter=VK_FILTER_LINEAR;VkSamplerAddressMode address=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
};
struct GenericAttribute {unsigned location=0;VkFormat format=VK_FORMAT_R32G32B32A32_SFLOAT;unsigned offset=0;};
struct GenericDraw {
    std::vector<std::uint32_t> vertexCode,pixelCode;
    std::vector<std::uint8_t> vertices;unsigned stride=0,vertexCount=0;
    std::vector<GenericAttribute> attributes;
    std::vector<std::uint8_t> vertexConstants=std::vector<std::uint8_t>(4096),pixelConstants=std::vector<std::uint8_t>(4096),
        sharedConstants=std::vector<std::uint8_t>(512);
    std::vector<GenericTexture> textures;
    unsigned width=0,height=0;VkFormat target=VK_FORMAT_R8G8B8A8_UNORM;unsigned targetBytesPerPixel=4;
    std::uint32_t specFlags=0;
};

// Returns the target's tightly packed pixels, or nullopt with *why set.
std::optional<std::vector<std::uint8_t>> renderGenericDraw(ProbeDevice&,GenericDraw&,const char** why=nullptr);
}
