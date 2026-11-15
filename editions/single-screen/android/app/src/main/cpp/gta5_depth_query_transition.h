#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace recomp::gpu {
struct Gta5DepthQueryTransition {
    bool end;
    std::uint32_t device,slotVirtual,slotPhysical,snapshotPhysical;
    std::uint32_t commandBegin,commandLastWord,submittedFence;
};
// Description of a RETURNED original82210888(type9) transition, not an executor.
// All snapshots must be stable under the caller's gate. The caller must resolve
// slotVirtual -> slotPhysical from its actual current allocation; never derive
// ownership by masking an address. This does not pin that allocation, discharge
// any packet, execute ZPD, complete a fence or publish a sample result.
// One-slot path only. An unread earlier query requiring ordered MEM_WRITE/WAIT
// is deliberately unsupported; the direct CPU sentinel path is distinct.
inline std::optional<Gta5DepthQueryTransition> describeGta5DepthQueryTransition(
        bool end,std::uint32_t device,std::uint32_t slotVirtual,std::uint32_t slotPhysical,
        std::span<const std::uint8_t> before,std::span<const std::uint8_t> after,
        std::span<const std::uint8_t> queryBefore,std::span<const std::uint8_t> queryAfter,
        std::span<const std::uint8_t> samplesBefore,std::span<const std::uint8_t> samplesAfter,
        std::span<const std::uint8_t> commands) {
    if(!device || device%4 || !slotVirtual || slotVirtual%32 || slotPhysical%32 ||
       std::uint64_t{slotPhysical}+64>0x20000000 || std::uint64_t{slotVirtual}+64>0x100000000ULL ||
       before.size()!=13860 || after.size()!=before.size() ||
       queryBefore.size()!=156 || queryAfter.size()!=156 || samplesBefore.size()!=64 ||
       samplesAfter.size()!=64 || commands.size()!=36)return std::nullopt;
    const auto word=[](auto b,unsigned at) {
        return (std::uint32_t{b[at]}<<24)|(std::uint32_t{b[at+1]}<<16)|
            (std::uint32_t{b[at+2]}<<8)|b[at+3];
    };
    const auto cursor=word(before,48),limit=word(before,56),priorFence=word(queryBefore,24);
    if(!cursor || cursor%4 || cursor>limit || cursor>UINT32_MAX-commands.size() ||
       (before[11068]&0x20) || word(queryBefore,0)!=device || word(queryBefore,4)!=9 ||
       word(queryBefore,28)!=slotVirtual || word(queryBefore,148)!=1 || word(queryBefore,152)!=1 ||
       (queryBefore[20]&~0xC0u))return std::nullopt;
    if(end) {
        if(queryBefore[16]!=2 || (priorFence && !(queryBefore[20]&0x80)))return std::nullopt;
    } else {
        // First issue, or a previous end consumed through original GetData.
        // The captured original first-BEGIN starts at issue1/flags0/fence0,
        // not only the authored issue0 fixture. Original82210888 reproduces
        // the complete captured transition for this state without touching
        // sample bytes. Issue1 alone is NOT proof that a prior END completed:
        // unread submitted ENDs still require the separate ordered wait path.
        if(!(((queryBefore[16]==0 || queryBefore[16]==1) && queryBefore[20]==0 && !priorFence) ||
             (queryBefore[16]==1 && queryBefore[20]==0xC0)))return std::nullopt;
    }
    const auto address=slotPhysical+(end?0u:32u);
    const std::uint32_t expectedWords[]={0x2208,6,0x12000,0x20000,0,0x2325,address,0xC0005B00,21};
    for(unsigned i=0;i<9;++i)if(word(commands,i*4)!=expectedWords[i])return std::nullopt;
    std::vector<std::uint8_t> expectedDevice(before.begin(),before.end()),
        expectedQuery(queryBefore.begin(),queryBefore.end()),expectedSamples(samplesBefore.begin(),samplesBefore.end());
    const auto put=[](auto& b,unsigned at,std::uint32_t v) {
        for(unsigned i=0;i<4;++i)b[at+i]=std::uint8_t(v>>(24-i*8));
    };
    const auto dirty=((std::uint64_t{word(before,16)}<<32)|word(before,20))|0x0300000000000008ULL;
    put(expectedDevice,16,dirty>>32);put(expectedDevice,20,dirty);put(expectedDevice,48,cursor+36);
    expectedQuery[16]=end?1:2;
    std::uint32_t fence=priorFence;
    if(end) {
        expectedQuery[20]=(queryBefore[20]&0x7F)|0x40;fence=word(before,11036);
        put(expectedQuery,24,fence);put(expectedSamples,16,0xFFFFFEED);put(expectedSamples,20,0xFFFFFEED);
    }
    if(!std::equal(expectedDevice.begin(),expectedDevice.end(),after.begin()) ||
       !std::equal(expectedQuery.begin(),expectedQuery.end(),queryAfter.begin()) ||
       !std::equal(expectedSamples.begin(),expectedSamples.end(),samplesAfter.begin()))return std::nullopt;
    return Gta5DepthQueryTransition{end,device,slotVirtual,slotPhysical,address,cursor+4,cursor+36,fence};
}
}
