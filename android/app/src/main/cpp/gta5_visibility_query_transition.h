#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace recomp::gpu {
struct Gta5VisibilityQueryTransition {
    // Original device+48 points to the LAST emitted word, not one-past-end.
    // Keep that cursor separate from a byte range. A 64-bit exclusive end also
    // avoids wrapping a valid final word at0xFFFFFFFC back to zero.
    std::uint32_t commandBegin,commandLastWord,payload,selector;
    std::uint64_t commandEndExclusive() const noexcept {
        return std::uint64_t{commandLastWord}+4;
    }
    std::uint64_t commandByteCount() const noexcept {
        return commandEndExclusive()-commandBegin;
    }
};
// Validate an ORIGINAL, returned, non-nested begin/end producer transition.
// before/after must be stable owned device snapshots; commands are the exact
// newly emitted bytes, not reconstructed desired commands. The caller must
// separately establish allocation identity, serialized ordering, GPU ownership
// and one-time consumption. No register, guest memory or ring is written here.
// Scope: 8224E340(device,id,0) and 82520190(device,0), no flush or nesting.
inline std::optional<Gta5VisibilityQueryTransition> describeGta5VisibilityQueryTransition(
        bool end,unsigned id,unsigned flags,std::span<const std::uint8_t> before,
        std::span<const std::uint8_t> after,std::span<const std::uint8_t> commands) {
    if(id>=64 || flags || before.size()!=13860 || after.size()!=before.size() ||
       commands.size()!=(end?8u:16u))return std::nullopt;
    const auto word=[](auto bytes,unsigned at) {
        return (std::uint32_t{bytes[at]}<<24)|(std::uint32_t{bytes[at+1]}<<16)|
            (std::uint32_t{bytes[at+2]}<<8)|bytes[at+3];
    };
    const auto wide=[&](auto bytes,unsigned at) {
        return (std::uint64_t{word(bytes,at)}<<32)|word(bytes,at+4);
    };
    const auto cursor=word(before,48),limit=word(before,56);
    const auto oldSelector=word(before,10672);
    const auto bit=std::uint64_t{1}<<id,dirty=std::uint64_t{1}<<35;
    if(!cursor || (cursor&3) || cursor>limit || cursor>UINT32_MAX-commands.size() ||
       (before[11072]&0x80) || before[12497]!=(end?1:0) ||
       wide(before,12424)!=(end?bit:0) ||
       (end?(oldSelector!=2*id+1 || before[12433]!=id):(oldSelector&~0x7Eu)))
        return std::nullopt;
    const auto payload=end?(0x100u|id):id,selector=2*id+unsigned(!end);
    if(end) {
        if(word(commands,0)!=0xC0002300u || word(commands,4)!=payload)return std::nullopt;
    } else if(word(commands,0)!=0x2293 || word(commands,4)!=selector ||
              word(commands,8)!=0xC0002300u || word(commands,12)!=payload)return std::nullopt;
    // Compare ALL device bytes, not just the happy-path fields. Refuse an
    // unexpected helper/flush/owner transition rather than hiding it.
    std::vector<std::uint8_t> expected(before.begin(),before.end());
    const auto put=[&](unsigned at,std::uint32_t value) {
        for(unsigned i=0;i<4;++i)expected[at+i]=std::uint8_t(value>>(24-i*8));
    };
    const auto put64=[&](unsigned at,std::uint64_t value) {put(at,value>>32);put(at+4,value);};
    put(48,cursor+commands.size());put(10672,selector);
    put64(24,end?(wide(before,24)|dirty):(wide(before,24)&~dirty));
    put64(12424,end?0:bit);expected[12497]=end?0:1;
    if(!end)expected[12433]=id;
    if(!std::equal(expected.begin(),expected.end(),after.begin()))return std::nullopt;
    return Gta5VisibilityQueryTransition{cursor+4,cursor+std::uint32_t(commands.size()),payload,selector};
}
}
