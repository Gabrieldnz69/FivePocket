#pragma once
// v1519 (lead subagent, D1 explosions; default ON, kill switch files/private-trace/diag-inline-image-output-off): which output-pointer
// words the native indexed Begin (finishNativeInlineIndexedBegin, renderer_boundary_trace.cpp) may write. v26 accepted a word in the
// caller's argument area or a live guest heap allocation; anything else fell back to the original Begin, whose PM4 the generic
// renderer never draws. The game's particle sprite renderer (ptxd_Sprite, vertex mode 9: sub_82336D20 -> sub_82456680 ->
// BeginIndexedVertices sub_823C1088) passes the vertex output as a title-image global (lis -31817 / addi -592 = 0x83B6FDB0), so every
// mode-9 particle draw fell back and drew nothing: the explosion fireballs and smoke, fire_map's mode-9 emitters, water splashes
// (Fold run-v1538-11111411: technique begin 6, BeginVertices answered 1, EndVertices called, 0 generic draws for every mode-9 sprite
// draw; RENDER_API_INDEXED_BEGIN_FALLBACK outputs-not-owned ~146/s). The original Begin writes the same word, so a word of the
// validated title-image page map (copySerializedTitleImage: inside the image, its page map, no mapped-RAM fallback) is accepted too.
#include <cstdint>

namespace recomp::inline_output {
// argument / heap / image: the registry's three validated readers for one 4-byte word (true = readable and owned).
template<class Argument,class Heap,class Image>
bool owned(std::uint32_t address,Argument&& argument,Heap&& heap,Image&& image,bool imageAllowed) {
    return argument(address) || heap(address) || (imageAllowed && image(address));
}
}  // namespace recomp::inline_output
