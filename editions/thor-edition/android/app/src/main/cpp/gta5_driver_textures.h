#pragma once
#include "gta5_inline_draw.h"

namespace recomp::gpu {
using Gta5SubmittedTextureSnapshot=
    std::array<std::optional<std::array<std::uint32_t,6>>,32>;

// Failure diagnostics must retain submitted descriptors even if sampling or
// memory-layout admission rejects them. No CPU-shadow fallback, decoding or
// texture memory reads occur here. Caller holds the exclusive execution gate.
inline Gta5SubmittedTextureSnapshot captureGta5SubmittedTextures(
        const Gta5TextureFetchReader& submitted) {
    Gta5SubmittedTextureSnapshot result{};
    if(submitted)for(unsigned slot=0;slot<result.size();++slot)result[slot]=submitted(slot);
    return result;
}

// 823BA1A8 submits selected six-DWORD fetch descriptors from device+1152.
// Later CPU shadow edits are NOT GPU bindings until another upload includes
// that slot. Retain submission order, including invalid/unbound descriptors.
class Gta5DriverTextures {
public:
    bool upload(std::uint32_t device,std::uint64_t mask,const Gta5ResourceReader& heap) {
        if(!device || !mask || (mask&0xFFFFFFFFULL) || !heap)return false;
        auto candidate=slots_;
        for(unsigned slot=0;slot<32;++slot)if(mask&(std::uint64_t{1}<<(63-slot))) {
            const auto address=std::uint64_t{device}+1152+slot*24;
            if(address+24>0x100000000ULL)return false;
            const auto bytes=heap(static_cast<std::uint32_t>(address),24);
            if(!bytes || bytes->size()!=24)return false;
            std::array<std::uint32_t,6> words{};
            for(unsigned i=0;i<6;++i) {
                const auto* p=bytes->data()+i*4;
                words[i]=(std::uint32_t{p[0]}<<24)|(std::uint32_t{p[1]}<<16)|
                    (std::uint32_t{p[2]}<<8)|p[3];
            }
            candidate[slot]=words;
        }
        slots_=candidate;return true;
    }
    std::optional<std::array<std::uint32_t,6>> fetch(unsigned slot)const {
        return slot<32?slots_[slot]:std::nullopt;
    }
private:
    Gta5SubmittedTextureSnapshot slots_{};
};
}
