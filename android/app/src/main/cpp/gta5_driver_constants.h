#pragma once
#include "gta5_inline_draw.h"
#include <bit>

namespace recomp::gpu {
// Effective float constants, in driver submission order. The ordinary device
// shadow is insufficient: 823BB448 loads shader literals directly from shader
// resources, without updating that shadow. Later ordinary uploads may overwrite
// them, so this is an ordered state cache, not an unconditional literal overlay.
class Gta5DriverConstants {
public:
    bool upload(std::uint64_t mask,unsigned firstRegister,std::uint32_t source,
            const Gta5ResourceReader& heap) {
        if(firstRegister<0x4000 || firstRegister>=0x4800)return false;
        auto candidate=state_;
        for(unsigned bit=0;bit<64;++bit)if(mask&(std::uint64_t{1}<<(63-bit))) {
            const auto first=firstRegister-0x4000+bit*16;
            const auto address=std::uint64_t{source}+bit*64;
            if(first+16>2048 || address+64>0x100000000ULL)return false;
            const auto bytes=heap(static_cast<std::uint32_t>(address),64);
            if(!bytes || bytes->size()!=64)return false;
            for(unsigned i=0;i<16;++i)candidate.set(first+i,word(bytes->data()+i*4));
        }
        state_=std::move(candidate);return true;
    }
    // header is PS object+40 or VS object+872, exactly as passed to 823BB448.
    // Each 8-byte entry contains float4 index, DWORD count, data-relative offset.
    bool loadLiterals(std::uint32_t header,std::uint32_t dataBase,
            const Gta5ResourceReader& heap,const Gta5ResourceReader& data) {
        const auto read=[&](std::uint64_t address)->std::optional<std::uint32_t> {
            if(address>0xFFFFFFFCULL)return std::nullopt;
            const auto bytes=heap(static_cast<std::uint32_t>(address),4);
            return bytes && bytes->size()==4?std::optional<std::uint32_t>{word(bytes->data())}:std::nullopt;
        };
        const auto relative=read(std::uint64_t{header}+20);
        if(!relative)return false;
        if(!*relative)return true;
        const auto table=std::uint64_t{header}+*relative;
        const auto size=read(table+16);
        if(!size || (*size&3) || *size>65536 || table+20+*size>0x100000000ULL)return false;
        if(!*size)return true;
        const auto entries=heap(static_cast<std::uint32_t>(table+20),*size);
        if(!entries || entries->size()!=*size)return false;
        auto candidate=state_;
        for(unsigned at=0;at<*size;at+=8) {
            const auto* entry=entries->data()+at;
            const unsigned first=((unsigned(entry[0])<<8)|entry[1])*4;
            const unsigned count=(unsigned(entry[2])<<8)|entry[3];
            if(!count)break;
            // The terminating zero-count record is only four bytes: the
            // original returns before reading the optional data offset.
            if(*size-at<8)return false;
            const auto address=std::uint64_t{dataBase}+word(entry+4);
            if(first+count>2048 || address+count*4>0x100000000ULL)return false;
            const auto bytes=data(static_cast<std::uint32_t>(address),count*4);
            if(!bytes || bytes->size()!=count*4)return false;
            for(unsigned i=0;i<count;++i)candidate.set(first+i,word(bytes->data()+i*4));
        }
        state_=std::move(candidate);return true;
    }
    void apply(Gta5InlineDraw& draw)const {
        for(unsigned block=0;block<state_.present.size();++block) {
            auto mask=state_.present[block];
            while(mask) {
                const unsigned index=block*64+std::countr_zero(mask);mask&=mask-1;
                (index<1024?draw.vertexConstants[index]:draw.pixelConstants[index-1024])=state_.words[index];
            }
        }
    }
    std::size_t size()const noexcept{return state_.count;}
private:
    static std::uint32_t word(const std::uint8_t* p) {
        return (std::uint32_t{p[0]}<<24)|(std::uint32_t{p[1]}<<16)|(std::uint32_t{p[2]}<<8)|p[3];
    }
    // The original interface has exactly two1024-DWORD banks. Keep an explicit
    // presence mask: unsubmitted slots must retain the draw's existing values,
    // not become implicit zero constants. Candidate copies are bounded and
    // allocation-free, and still commit only after EVERY required read passes.
    struct State {
        std::array<std::uint32_t,2048> words{};
        std::array<std::uint64_t,32> present{};
        std::size_t count=0;
        void set(unsigned index,std::uint32_t value) {
            const std::uint64_t bit=std::uint64_t{1}<<(index%64);
            if(!(present[index/64]&bit)){present[index/64]|=bit;++count;}
            words[index]=value;
        }
    } state_;
};
}
