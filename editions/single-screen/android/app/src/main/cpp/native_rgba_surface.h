#pragma once
#include "native_swap_image.h"
#include <algorithm>

namespace recomp::gpu {
struct NativeRgbaSurfaceLayout {
    std::uint32_t width,height,left,top,imageWidth,imageHeight;
};
inline std::optional<NativeRgbaSurfaceLayout> nativeRgbaSurfaceLayout(std::uint32_t width,std::uint32_t height) noexcept {
    // Diagnostic CPU resampling, bounded to64 MiB; not the eventual fast path.
    if(!width || !height || width>4096 || height>4096)return std::nullopt;
    auto w=width,h=height;
    if(width*720<=height*1280)h=std::max(1u,width*720/1280);
    else w=std::max(1u,height*1280/720);
    return NativeRgbaSurfaceLayout{width,height,(width-w)/2,(height-h)/2,w,h};
}
// Packs an owned opaque1280x720 RGBA image for an exact native UNORM surface.
// Letterboxes and nearest-neighbor samples on CPU, optionally exchanges R/B for
// a BGRA surface. No guest memory/commands/completion or Vulkan object is touched.
inline bool packNativeRgbaSurface(const std::vector<std::uint8_t>& rgba,std::uint32_t width,
        std::uint32_t height,bool bgra,std::vector<std::uint8_t>& output) noexcept {
    const auto layout=nativeRgbaSurfaceLayout(width,height);
    if(!layout || rgba.size()!=NativeSwapImagePlan::PixelBytes)return false;
    for(std::size_t i=3;i<rgba.size();i+=4)if(rgba[i]!=255)return false;
    try {
        std::vector<std::uint8_t> packed(std::size_t{width}*height*4,0);
        for(std::size_t i=3;i<packed.size();i+=4)packed[i]=255;
        for(std::uint32_t y=0;y<layout->imageHeight;++y)for(std::uint32_t x=0;x<layout->imageWidth;++x){
            const auto source=((y*720/layout->imageHeight)*1280+x*1280/layout->imageWidth)*4;
            const auto dest=((y+layout->top)*width+x+layout->left)*4;
            packed[dest]=rgba[source+(bgra?2:0)];packed[dest+1]=rgba[source+1];
            packed[dest+2]=rgba[source+(bgra?0:2)];
        }
        output.swap(packed);return true;
    } catch(...) {return false;}
}
}
