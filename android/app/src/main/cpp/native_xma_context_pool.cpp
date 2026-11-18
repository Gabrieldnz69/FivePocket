// XMA layout, packet traversal and PCM conversion adapted from Xenia:
// src/xenia/apu/{xma_context.cc,xma_helpers.h,xma_decoder.cc}.
// Output-block scheduling also follows ReXGlue src/audio/xma_context.cpp
// (Tom Clay, 2026). BSD-3-Clause; see licenses/Xenia-BSD-3-Clause.txt.
#include "native_xma_context_pool.h"
#include "native_xma_pcm_blocks.h"
#include <algorithm>
#include <cstring>

namespace recomp::audio {
namespace {
constexpr unsigned PacketBits = 2048 * 8;
std::uint32_t readBe(const std::uint8_t* p) {
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 |
           std::uint32_t(p[2]) << 8 | p[3];
}
void writeBe(std::uint8_t* p, std::uint32_t n) {
    p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n;
}
unsigned bit(const std::uint8_t* p, unsigned offset) {
    return (p[offset / 8] >> (7 - offset % 8)) & 1;
}
unsigned firstFrame(const std::uint8_t* packet) {
    return 32 + ((packet[0] & 3) << 13) + (packet[1] << 5) + (packet[2] >> 3);
}
struct Words {
    std::array<std::uint32_t, 16> w;
    explicit Words(const std::uint8_t* p) { for (unsigned i=0;i<16;++i) w[i]=readBe(p+4*i); }
    void store(std::uint8_t* p) const { for (unsigned i=0;i<16;++i) writeBe(p+4*i,w[i]); }
    unsigned get(unsigned word, unsigned shift, std::uint32_t mask) const { return (w[word] >> shift) & mask; }
    void set(unsigned word, unsigned shift, std::uint32_t mask, unsigned value) {
        w[word] = (w[word] & ~(mask << shift)) | ((value & mask) << shift);
    }
    unsigned current() const { return w[4] >> 31; }
    bool valid(unsigned b) const { return get(0,20+b,1); }
    unsigned packets() const { return get(current(),0,4095); }
    unsigned offset() const { return get(2,0,0x3FFFFFF); }
    void offset(unsigned bits) { set(2,0,0x3FFFFFF,bits); }
    void swap() { set(0,20+current(),1,0); w[4] ^= 0x80000000U; offset(0); }
};
}
struct NativeXmaContextPool::Context {
    NativeXmaFrameDecoder decoder;
    std::array<std::uint8_t, 4096> compressed{};
    unsigned assembled = 0, length = 0, skipPackets = 0;
    NativeXmaFrameDecoder::Frame pcm;
    unsigned pcmBlock = 0, pcmEndBlock = 0;
    bool loopStartSkipPending = false, frameIsLoopEnd = false;
};
NativeXmaContextPool::NativeXmaContextPool(std::uint32_t v, std::uint32_t p,
        std::uint8_t* storage, MapPhysical map)
    : virtualBase_(v), physicalBase_(p), storage_(storage), map_(std::move(map)) {
    if (ready()) std::memset(storage_,0,PoolBytes);
}
NativeXmaContextPool::~NativeXmaContextPool() = default;
bool NativeXmaContextPool::ready() const {
    return storage_ && map_ && virtualBase_ && physicalBase_ &&
        !(virtualBase_ & 255) && !(physicalBase_ & 255) &&
        std::uint64_t(virtualBase_) + PoolBytes <= 0x100000000ULL &&
        std::uint64_t(physicalBase_) + PoolBytes <= 0x100000000ULL;
}
bool NativeXmaContextPool::fail(const char* why) { failure_ = why; return false; }
std::uint32_t NativeXmaContextPool::allocate() {
    if (!ready()) return 0;
    for (unsigned i=0;i<ContextCount;++i) if (!contexts_[i]) {
        auto c=std::make_unique<Context>();
        if (!c->decoder.available()) return 0;
        std::memset(storage_+i*ContextBytes,0,ContextBytes);
        contexts_[i]=std::move(c);
        return virtualBase_+i*ContextBytes;
    }
    return 0;
}
bool NativeXmaContextPool::release(std::uint32_t v) {
    if (v < virtualBase_ || (v-virtualBase_)%ContextBytes || v-virtualBase_>=PoolBytes) return false;
    const auto i=(v-virtualBase_)/ContextBytes;
    if (!contexts_[i]) return false;
    contexts_[i].reset(); pending_[i]=false;
    std::memset(storage_+i*ContextBytes,0,ContextBytes); return true;
}
bool NativeXmaContextPool::access(std::uint32_t address, bool write, std::uint32_t& value) {
    failure_.clear(); failureContext_={};
    if (!ready() || (address&3)) return fail("XMA register binding/alignment");
    if (!write) {
        if (address==0x7FEA1800) { value=__builtin_bswap32(physicalBase_); return true; }
        if (address==0x7FEA1804) { value=__builtin_bswap32(control_); return true; }
        // No decoder can be active while the guest reads: kicks execute to
        // completion under the same gate. Keep the actual last processed ID,
        // without an active (0x200) bit; never rotate IDs to satisfy polling.
        if (address==0x7FEA1818) { value=__builtin_bswap32(lastContext_); return true; }
        return fail("Unimplemented XMA register read");
    }
    if (address==0x7FEA1804) {
        const auto control=__builtin_bswap32(value);
        // Original 8365A420 brackets its complete context-copy sweep with 2/3.
        // Native equivalent: quiesce/resume. Under the executor gate all prior
        // synchronous work is already finished; defer new kicks while paused.
        // No guest context flags or completion counters are invented here.
        if (control!=2 && control!=3) return fail("Unsupported XMA global control mode");
        control_=control;
        if (control_==3) for (unsigned i=0;i<ContextCount;++i) if (pending_[i]) {
            if (!decode(i)) { failureContext_=Words(storage_+i*ContextBytes).w; return false; }
            pending_[i]=false;lastContext_=i;
        }
        return true;
    }
    unsigned command=0, first=0;
    if (address>=0x7FEA1940 && address<0x7FEA1968) { command=1; first=(address-0x7FEA1940)*8; }
    if (address>=0x7FEA1A40 && address<0x7FEA1A68) { command=2; first=(address-0x7FEA1A40)*8; }
    if (address>=0x7FEA1A80 && address<0x7FEA1AA8) { command=3; first=(address-0x7FEA1A80)*8; }
    if (!command) return fail("Unimplemented XMA register write");
    const auto mask=__builtin_bswap32(value);
    for (unsigned b=0;b<32;++b) if ((mask&(1U<<b)) && !contexts_[first+b])
        return fail("XMA command targets unallocated context");
    for (unsigned b=0;b<32;++b) if (mask&(1U<<b)) {
        const auto i=first+b;
        if (command==1) {
            if (control_==2) { pending_[i]=true; continue; }
            if (!decode(i)) { failureContext_=Words(storage_+i*ContextBytes).w; return false; }
            lastContext_=i;
        }
        // Lock is immediate because no asynchronous decode remains in flight.
        if (command==2 || command==3) pending_[i]=false;
        if (command==3) {
            Words w(storage_+i*ContextBytes);
            w.set(0,20,3,0); w.set(0,27,31,0); w.set(1,31,1,0); w.set(9,0,31,0);
            w.store(storage_+i*ContextBytes);
            auto& c=*contexts_[i]; c.decoder.reset();
            c.assembled=c.length=c.skipPackets=0; c.pcmBlock=c.pcmEndBlock=0;
            c.loopStartSkipPending=c.frameIsLoopEnd=false;
        }
    }
    return true;
}

bool NativeXmaContextPool::decode(unsigned index) {
    auto& c=*contexts_[index];
    auto* stored=storage_+index*ContextBytes;
    Words w(stored);
    if (!w.get(1,31,1)) return true;
    // Hardware interrupts need their own semantics; don't manufacture one.
    if (w.get(9,30,3)) return fail("XMA stop/interrupt mode not implemented");
    const auto loops=w.get(0,12,255);
    const auto loopStart=w.get(3,0,0x3FFFFFF), loopEnd=w.get(4,0,0x3FFFFFF);
    if (loops && (loopStart<32 || loopStart>loopEnd || w.get(1,17,7)>4 ||
        (loopStart==loopEnd && w.get(1,17,7)>w.get(1,12,3))))
        return fail("XMA invalid or empty loop window");
    const unsigned channels=1+w.get(1,29,1), capacity=w.get(0,22,31)*256;
    unsigned read=w.get(9,0,31)*256, write=w.get(0,27,31)*256;
    if (!capacity || read>=capacity || write>=capacity) return fail("XMA output ring bounds");
    auto* output=map_(w.w[7],capacity,true);
    if (!output) return fail("XMA output physical allocation unavailable");
    unsigned freeBytes=write>=read ? capacity-write+read : read-write;
    // The SDK count is a consumption quantum in 256-byte blocks, NOT a limit
    // on the whole kick and NOT 128 samples per channel. Stereo has 8 blocks.
    const unsigned count=std::max(1U,w.get(1,20,15));
    const unsigned reserve=w.get(1,24,7)*256;
    if (freeBytes<count*256+reserve) return true;
    constexpr unsigned rates[]{24000,32000,44100,48000};
    if (c.pcmBlock<c.pcmEndBlock && (c.pcm.channels!=channels || c.pcm.sampleRate!=rates[w.get(1,27,3)]))
        return fail("XMA format changed while decoded PCM remains pending");
    if (!c.decoder.configure(rates[w.get(1,27,3)],channels)) return fail("XMA codec configure failed");
    unsigned steps=0;
    while (freeBytes>=count*256+reserve) {
        if (++steps>32768) return fail("XMA packet traversal exceeded physical buffer bound");
        if (c.pcmBlock<c.pcmEndBlock) {
            const auto blocks=std::min(count,c.pcmEndBlock-c.pcmBlock);
            if (!writeXmaPcmBlocks(c.pcm,c.pcmBlock,blocks,output,capacity,write))
                return fail("XMA decoded PCM block bounds");
            c.pcmBlock+=blocks; freeBytes-=256*blocks;
            if (c.pcmBlock==c.pcmEndBlock) freeBytes-=reserve;
            writtenPcmBytes_+=256*blocks; w.set(0,27,31,write/256);
            if (write==read) w.set(1,31,1,0);
            w.store(stored);
            continue;
        }
        if (!w.valid(w.current())) {
            // Starvation leaves the output buffer valid. Only a full ring
            // (write==read after writing, above) clears bit31: the title's
            // available-data query (sub_836A96D8) reads a clear bit31 as a
            // FULL ring, and its gather loop then waits for PCM that was
            // never decoded (diagnostic runs 8dstjbv1/7dco19zd).
            if (!w.valid(w.current()^1)) { w.store(stored); break; }
            w.swap();
        }
        if (!w.packets()) return fail("XMA valid input has no packets");
        const auto inputBytes=w.packets()*2048;
        const auto* input=map_(w.w[5+w.current()],inputBytes,false);
        if (!input) return fail("XMA input physical allocation unavailable");
        unsigned offset=w.offset();
        if (offset>=inputBytes*8) return fail("XMA input bit offset outside allocation");
        if (c.skipPackets) {
            const auto packetsLeft=w.packets()-offset/PacketBits;
            const auto skipped=std::min(c.skipPackets,packetsLeft);
            c.skipPackets-=skipped; offset+=skipped*PacketBits;
            if (offset>=inputBytes*8) { w.swap(); w.store(stored); continue; }
            w.offset(offset);
        }
        const unsigned packetIndex=offset/PacketBits;
        const auto* packet=input+packetIndex*2048;
        if ((packet[2]&7)>1) return fail("XMA unknown packet metadata");
        w.set(4,26,31,packet[2]&7);
        const bool continuation=c.assembled!=0;
        const auto first=firstFrame(packet);
        const bool newFrames=first<PacketBits &&
            ((packet[2]&7)!=1 || (packet[0]>>2)!=0);
        unsigned within=offset%PacketBits;
        if (!within) within=continuation ? 32 : (newFrames ? first : PacketBits);
        if (within>=PacketBits) {
            // This stream has no new frame starting in this packet.
            c.skipPackets=packet[3];
            const auto next=(packetIndex+1)*PacketBits;
            if (next>=inputBytes*8) w.swap(); else w.offset(next);
            w.store(stored); continue;
        }
        if (within<32) return fail("XMA frame offset inside packet header");
        if (!continuation) {
            // Loop end is the START of the final compressed frame, not its
            // end bit. Decode it before jumping, including when it spans packets.
            c.frameIsLoopEnd=w.get(0,12,255) && packetIndex*PacketBits+within==loopEnd;
        }
        while (within<PacketBits && (!c.length || c.assembled<c.length)) {
            const unsigned v=bit(packet,within++), dst=c.assembled++;
            c.compressed[dst/8]=(c.compressed[dst/8]&~(1U<<(7-dst%8))) | (v<<(7-dst%8));
            if (c.assembled==15) {
                c.length=(unsigned(c.compressed[0])<<7)|(c.compressed[1]>>1);
                if (c.length<16 || c.length>=0x7FFF) return fail("XMA invalid compressed frame length");
            }
        }
        if (!c.length || c.assembled<c.length) {
            c.skipPackets=packet[3];
            const auto next=(packetIndex+1)*PacketBits;
            if (next>=inputBytes*8) {
                if (c.frameIsLoopEnd)
                    return fail("XMA loop-end frame crosses input buffer ownership");
                w.swap();
            } else w.offset(next);
            w.store(stored); continue;
        }
        if (continuation && newFrames && first!=within)
            return fail("XMA split-frame end disagrees with next packet frame offset");
        if (c.decoder.decode(c.compressed.data(),c.compressed.size(),0,c.length,c.pcm)
                !=NativeXmaFrameDecoder::Result::Decoded) return fail("XMA actual compressed frame rejected by codec");
        ++decodedFrames_;
        // Each subframe is 128 samples PER CHANNEL. The output ring's blocks
        // contain 128 scalar samples, so stereo skips/limits use twice as many.
        // Skip belongs only to the first frame AFTER a loop jump (including the
        // last finite repeat when the stored count has just become zero).
        c.pcmBlock=c.loopStartSkipPending ? w.get(1,17,7)*channels : 0;
        c.pcmEndBlock=c.frameIsLoopEnd ? (w.get(1,12,3)+1)*channels : 4*channels;
        c.loopStartSkipPending=false;
        const bool follows=bit(c.compressed.data(),c.length-1);
        c.length=c.assembled=0;
        unsigned next=packetIndex*PacketBits+within;
        if (c.frameIsLoopEnd) {
            next=loopStart;
            c.skipPackets=0;
            c.loopStartSkipPending=true;
            if (w.get(0,12,255)<255) w.set(0,12,255,w.get(0,12,255)-1);
        } else if (continuation && newFrames) {
            // The trailer describes the packet where this frame STARTED.
            // A continuation packet can also contain newly starting frames,
            // even when the spanning frame's trailer is zero (Xenia frame_idx=-1).
            next=packetIndex*PacketBits+first;
        } else if (continuation || !follows || within==PacketBits) {
            c.skipPackets=packet[3]; next=(packetIndex+1)*PacketBits;
        }
        if (next>=inputBytes*8) w.swap(); else w.offset(next);
        w.store(stored);
    }
    return true;
}
}
