#pragma once
// v1303 (bugfix-stability 2026-11-09): the generic renderer's six descriptor sets folded into three, for GPUs that bind fewer
// than six sets at once (Adreno 6xx and many Mali / PowerVR chips report maxBoundDescriptorSets = 4; the main draw layout binds
// sets 0-5). The pack's modules declare exactly these resources:
//   set 0 binding 0  2D images      set 1 binding 0  2D-array images   set 2 binding 0  cube images
//   set 3 binding 0  samplers       set 4 bindings 0-2 the constant banks (dynamic)   set 5 binding 0  3D images
// Folded (v1304 layout): set 0 bindings 0,1 = the 2D images and the samplers (old sets 0, 3: what changes with every draw);
// set 1 bindings 0,1,2 = the 2D-array / cube / 3D images (old sets 1, 2, 5: most draws sample none, and then share one set of
// dummy views, as v341 does); set 2 bindings 0-2 = the constant banks (old set 4). v1303's layout (0,1,2,5 -> 0; 3 -> 1) re-bound
// all four image heaps at every draw change: +2.4 ms GPU on the Thor (t-gpufold1303). The mapping is one to one, so no two
// resources of a module meet, and per-stage descriptor totals are unchanged (Vulkan counts them over the whole layout).
// Only the DescriptorSet / Binding literals of OpDecorate change; every other word stays. A module with no descriptor
// decoration is returned unchanged (why "no-sets", code empty). Refused (code empty, why set): a (set, binding) outside the table,
// a resource with only one of the two decorations, a decoration group carrying either (OpGroupDecorate), a malformed module.
#include <cstdint>
#include <cstddef>
#include <vector>

namespace recomp::gpu::generic::spirv_fold {

struct Result {std::vector<std::uint32_t> code;unsigned remapped=0;const char* why=nullptr;};
struct Slot {std::uint32_t set,binding;};

// The folded slot of an original one; false outside the table.
inline bool foldSlot(std::uint32_t set,std::uint32_t binding,Slot& out) {
    switch(set) {
    case 0: if(binding!=0)return false; out={0,0}; return true;
    case 3: if(binding!=0)return false; out={0,1}; return true;
    case 1: case 2: if(binding!=0)return false; out={1,set-1}; return true;
    case 5: if(binding!=0)return false; out={1,2}; return true;
    case 4: if(binding>2)return false; out={2,binding}; return true;
    default: return false;
    }
}

inline Result foldSets(const std::uint32_t* w,std::size_t n) {
    Result r;
    if(n<5 || w[0]!=0x07230203u){r.why="header";return r;}
    constexpr unsigned OpDecorate=71,OpDecorationGroup=73,OpGroupDecorate=74;
    constexpr unsigned DecBinding=33,DecSet=34;
    const std::uint32_t bound=w[3];
    std::vector<std::size_t> setAt(bound,0),bindingAt(bound,0);   // word index of the literal; 0 = none
    std::vector<unsigned char> group(bound,0);
    bool grouped=false;
    for(std::size_t i=5;i<n;) {
        const unsigned op=w[i]&0xFFFFu,len=w[i]>>16;
        if(!len || i+len>n){r.why="truncated";return r;}
        if(op==OpDecorate && len>=4 && (w[i+2]==DecSet || w[i+2]==DecBinding)) {
            const std::uint32_t t=w[i+1];
            if(t>=bound){r.why="id";return r;}
            auto& at=w[i+2]==DecSet?setAt[t]:bindingAt[t];
            if(at){r.why="duplicate";return r;}
            at=i+3;
        } else if(op==OpDecorationGroup && len>=2 && w[i+1]<bound) group[w[i+1]]=1;
        else if(op==OpGroupDecorate) grouped=true;
        i+=len;
    }
    std::vector<std::uint32_t> out;
    for(std::uint32_t t=0;t<bound;++t) {
        if(!setAt[t] && !bindingAt[t])continue;
        if(!setAt[t] || !bindingAt[t]){r.why="half-decorated";return r;}
        if(group[t] && grouped){r.why="group-decoration";return r;}
        Slot s{};
        if(!foldSlot(w[setAt[t]],w[bindingAt[t]],s)){r.why="slot";return r;}
        if(out.empty())out.assign(w,w+n);
        out[setAt[t]]=s.set;out[bindingAt[t]]=s.binding;++r.remapped;
    }
    if(!r.remapped){r.why="no-sets";return r;}
    r.code=std::move(out);
    return r;
}

} // namespace recomp::gpu::generic::spirv_fold
