#pragma once
// finish_trap_diag.h (v1620, lane fold-finish-trap 2026-11-12; notes: local-analysis/update-1/subagents-2026-11-12/fold-finish-trap/).
// Two things, neither changes what the game does:
//
// 1. READ-ONLY evidence for the guest trap of run-v1575-11121954 (Fold, Online race "Cutting Coroners", ~55 s after the finish, while the
//    next transition loaded): RENDER_GUEST_TRAP host_off=d1658dc = __imp__sub_834881E8 +0x40c, the return address of its only trap
//    (`twi 31,r0,22` at guest 0x83488214). Original control flow (ppc_recomp.301.cpp / .297.cpp, same text in every corpus since v474):
//      sub_834881E8(container r3, units r4): block = sub_8344D450(*(u32*)0x83C9AEE4, units); container+0 = block;
//        block == 0 -> twi 31,r0,22 (the trap), then container+4 = 0, +18 = 0;  else container+4 = block+12 (address), +18 = block+4.
//      *(0x83C9AEE4) is a 20-byte pool made once by sub_829A00A0(32000) from sub_829B7EA8 (sub_83444CB0): +0 capacity 32000 units of
//        48 bytes (+4 = new(32000*48)), +8 free-range array {start, size} x (+12 u16 count, +14 u16 capacity), +16 allocation list
//        (8-byte links {block, next}; block = 16 bytes {+0 link, +4 units, +8 start, +12 address}).
//      sub_8344D450 returns 0 in exactly two ways: (a) no free range >= units AND the sum of all free ranges < units (it compacts with
//        sub_8344B980 when the sum suffices); (b) sub_8255DC68(16,16), the game heap, returns 0 for the 16-byte block.
//      Callers: sub_824D5920 (append one 48-byte record: entity pointer + bounds from the callback at 0x83C9B20C = sub_823A57E8, which
//        switches on entity type byte +20; the entity's +36 gets the container), sub_834883B0 and sub_83488698 (bulk builds). The 48-byte
//        records with an entity back-pointer are the game's scene-graph entity containers.
//    On the trap this logs the pool (RENDER_FINISH_TRAP_POOL) and the native frame chain (RENDER_FINISH_TRAP_FRAMES: return-address
//    offsets in the library; llvm-nm names the guest functions = the guest call chain). Periodic: with diag-telemetry on (the chain
//    runs' switch), one RENDER_DIAG_FWEC line every 5 s when the pool changed (main thread: the pool's own thread).
//    Free total ~0 at the trap = the pool is full (cause (a): count/size growth or a leak, the periodic lines tell which); free total
//    >= any plausible request = cause (b) (game heap) or an out-of-range request.
//
// 2. The touch-context lease: recomp_touch_context_v1 (the app's QolTouch.poll, every 100 ms on the UI thread) read guest memory through
//    trace_runner's v280GuestBase, which nothing cleared when the run ended. In run-v1575-11121954 the guest stop unwound the run,
//    ~GuestAddressSpace munmapped the 4 GB space, and 0.39 s later the next poll faulted at base + 0x8394B8B0 (the player-slot global;
//    tombstone x9 = base 0x6feb259000, x8 = 0x8394b8b0, SEGV_MAPERR): a clean stop became a SIGSEGV. The lease is published with the
//    address space and revoked (readers drained) before it is released; a reader then sees null and returns -1 ("no guest memory yet").
#include <atomic>
#include <cstdint>
#include <cstring>
#include <sched.h>

namespace recomp::fintrap {

constexpr std::uint32_t PoolGlobal=0x83C9AEE4u;   // lis r11,-31798 ; lwz r3,-20764(r11)
constexpr std::uint32_t UnitBytes=48u;
constexpr unsigned MaxFreeRanges=8192u,MaxAllocs=40000u;

struct PoolStats {
    bool ok=false;               // the pool pointer and its arrays were readable
    std::uint32_t pool=0,capacity=0,data=0;
    unsigned freeCount=0,freeCap=0;
    std::uint64_t freeTotal=0;std::uint32_t freeLargest=0;
    unsigned allocCount=0;std::uint64_t allocTotal=0;std::uint32_t allocLargest=0;
    bool allocTruncated=false;   // the allocation walk stopped at MaxAllocs or at an unreadable link
};

// M: u32(a) big-endian guest word, ok(a) = readable aligned word (host: a fake; device: the guest space).
template<class M> PoolStats readPool(const M& m) noexcept {
    PoolStats s;
    if(!m.ok(PoolGlobal))return s;
    const std::uint32_t p=m.u32(PoolGlobal);
    if(!m.ok(p) || !m.ok(p+16u))return s;
    s.pool=p;s.capacity=m.u32(p);s.data=m.u32(p+4u);
    const std::uint32_t ranges=m.u32(p+8u),counts=m.u32(p+12u);
    s.freeCount=counts>>16;s.freeCap=counts&0xFFFFu;
    if(s.freeCount && !m.ok(ranges))return s;
    const unsigned n=s.freeCount<MaxFreeRanges?s.freeCount:MaxFreeRanges;
    for(unsigned i=0;i<n;++i) {
        const std::uint32_t e=ranges+8u*i;
        if(!m.ok(e+4u))return s;
        const std::uint32_t size=m.u32(e+4u);
        s.freeTotal+=size;if(size>s.freeLargest)s.freeLargest=size;
    }
    std::uint32_t link=m.u32(p+16u);
    while(link) {
        if(s.allocCount>=MaxAllocs || !m.ok(link) || !m.ok(link+4u)){s.allocTruncated=true;break;}
        const std::uint32_t block=m.u32(link);
        if(!m.ok(block+4u)){s.allocTruncated=true;break;}
        const std::uint32_t units=m.u32(block+4u);
        ++s.allocCount;s.allocTotal+=units;if(units>s.allocLargest)s.allocLargest=units;
        link=m.u32(link+4u);
    }
    s.ok=true;
    return s;
}

// sub_8344D450's own rule for a request of `units`: true = it returns a block, false = case (a) (no block from this pool).
inline bool poolCanServe(const PoolStats& s,std::uint32_t units) noexcept {
    return s.ok && (s.freeLargest>=units || s.freeTotal>=units);
}

inline bool samePool(const PoolStats& a,const PoolStats& b) noexcept {
    return a.ok==b.ok && a.freeTotal==b.freeTotal && a.freeCount==b.freeCount && a.allocCount==b.allocCount;
}

// The device reader: relaxed loads from the guest space (the whole 4 GB range is mapped while the run lives).
struct GuestMem {
    const std::uint8_t* base=nullptr;
    bool ok(std::uint32_t a) const noexcept {return base && !(a&3u) && a>=0x10000u && a<=0xFFFFFFF8u;}
    std::uint32_t u32(std::uint32_t a) const noexcept {
        return __builtin_bswap32(__atomic_load_n(reinterpret_cast<const std::uint32_t*>(base+a),__ATOMIC_RELAXED));
    }
};

// The guest base the trap logger reads (published with the address space, cleared before it is released).
inline std::atomic<const std::uint8_t*>& guestBase() noexcept {static std::atomic<const std::uint8_t*> b{nullptr};return b;}

// A lease on the guest base for readers outside the guest threads (the app's polls). seq_cst on all four operations: either the
// reader's increment precedes revoke()'s readers load (revoke waits for it) or the reader's base load follows the null store (it
// sees null). Readers never block; revoke() spins only while a reader is inside (a few relaxed loads).
struct Lease {
    std::atomic<const std::uint8_t*> base{nullptr};
    std::atomic<int> readers{0};
    void publish(const std::uint8_t* b) noexcept {base.store(b,std::memory_order_seq_cst);}
    void revoke() noexcept {
        base.store(nullptr,std::memory_order_seq_cst);
        while(readers.load(std::memory_order_seq_cst)!=0)sched_yield();
    }
};
struct LeaseRead {
    Lease& lease;const std::uint8_t* base;
    explicit LeaseRead(Lease& l) noexcept:lease(l) {
        lease.readers.fetch_add(1,std::memory_order_seq_cst);
        base=lease.base.load(std::memory_order_seq_cst);
    }
    ~LeaseRead(){lease.readers.fetch_sub(1,std::memory_order_seq_cst);}
    LeaseRead(const LeaseRead&)=delete;LeaseRead& operator=(const LeaseRead&)=delete;
};
inline Lease& touchLease() noexcept {static Lease l;return l;}

// The guest registers of the guest code running on this host thread (set around each guest entry in trace_runner.cpp, nested scopes
// restore the outer one). At a guest trap the generated code has stored its context (the trap call is opaque), so r1 there is the
// trapping function's frame: the guest back chain (*(r1) = caller's r1; the callee's saved LR at caller's r1 - 8, `mflr r12; stw
// r12,-8(r1)` in every framed prologue) names the guest call chain without frame pointers on the host side (the chunks omit them).
struct GuestRegs {const std::uint64_t* r1=nullptr;const std::uint64_t* r3=nullptr;const std::uint64_t* r4=nullptr;const std::uint64_t* r31=nullptr;const std::uint64_t* lr=nullptr;};
inline GuestRegs& currentRegs() noexcept {static thread_local GuestRegs r;return r;}
struct CtxScope {
    GuestRegs saved;
    template<class C> explicit CtxScope(C& cpu) noexcept:saved(currentRegs()) {
        currentRegs()=GuestRegs{&cpu.r1.u64,&cpu.r3.u64,&cpu.r4.u64,&cpu.r31.u64,&cpu.lr};
    }
    ~CtxScope(){currentRegs()=saved;}
    CtxScope(const CtxScope&)=delete;CtxScope& operator=(const CtxScope&)=delete;
};
// The guest back chain from r1: up to n return addresses (guest), innermost first. Stops at an unreadable or non-increasing link.
template<class M> int guestChain(const M& m,std::uint32_t r1,std::uint32_t* out,int n) noexcept {
    int k=0;std::uint32_t sp=r1;
    while(k<n && m.ok(sp)) {
        const std::uint32_t next=m.u32(sp);
        if(next<=sp || !m.ok(next-8u))break;
        out[k++]=m.u32(next-8u);
        sp=next;
    }
    return k;
}

} // namespace recomp::fintrap
