// online_ugc_offline_hook.inc (v1110, trainer-mods): UGC_LOAD_OFFLINE_QUERY's parser sub_82FCF2E0 (ugc manager, kind) with a v1 adapter
// (online_ugc_offline.h). Included at the end of online_freemode_gate_hook.inc. Only in Online mode, on the freemode route (diag-fm-route-on),
// for kind 1 ("rstar"), without files/private-trace/diag-ugc-v1-off: the game opens and reads the list with its own stream code; a complete v1
// list becomes the exe's own 416-byte records built with the game's allocator (sub_8255DC58), atString (sub_831AEEC0 / sub_831AEF98) and
// atArray (sub_83489EF8, manager +76), the count at +72 - the same calls, sizes and order as the original. Anything else: the original runs.
// LINK CONDITION: redirect-v1110-ugcv1.ld (= redirect-v1108-tutsess.ld without the mangled alias of sub_82FCF2E0). "RENDER_ONLINE_UGCV1" proves it.
#include "online_ugc_offline.h"
PPC_FUNC_IMPL(__imp__sub_82FCF2E0);   // the exe's parser (ugc manager, kind)
PPC_FUNC_IMPL(__imp__sub_831EDB68);   // open through the search paths (paths 0x83836220, name, ext "", 0, 1) -> stream
PPC_FUNC_IMPL(__imp__sub_831DCEA8);   // stream size
PPC_FUNC_IMPL(__imp__sub_831E2DB8);   // stream read (stream, dst, n) -> n read
PPC_FUNC_IMPL(__imp__sub_831E3240);   // stream close
PPC_FUNC_IMPL(__imp__sub_8255DC58);   // record allocation (416)
PPC_FUNC_IMPL(__imp__sub_8255DC60);   // temporary allocation
PPC_FUNC_IMPL(__imp__sub_8255DCC0);   // free
PPC_FUNC_IMPL(__imp__sub_831AEEC0);   // atString constructor
PPC_FUNC_IMPL(__imp__sub_831AEF98);   // atString assign (str, src, len)
PPC_FUNC_IMPL(__imp__sub_83489EF8);   // atArray grow (array, step) -> the new slot
namespace online_ugcv1 {
constexpr std::uint32_t SearchPaths=0x83836220u,EmptyExt=0x8200101Cu,ArrayOff=76u,CountOff=72u,MaxFile=4u<<20;
inline bool off() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-ugc-v1-off"),F_OK)==0;return o;}
inline void say(const char* l) noexcept {char t[320];std::snprintf(t,sizeof t,"RENDER_ONLINE_UGCV1 %s",l);loading_hold_hook::log(t);}
struct Guest {
    PPCContext& ctx;std::uint8_t* base;
    void w32(std::uint32_t a,std::uint32_t v) noexcept {v=__builtin_bswap32(v);std::memcpy(base+a,&v,4);}
    // one guest call on a fresh frame below the caller's r1 (back chain written); `payload` bytes go to the frame at +96 (r4 may point there)
    template<class F> std::uint32_t call(F f,std::uint32_t r3,std::uint32_t r4=0,std::uint32_t r5=0,std::uint32_t r6=0,std::uint32_t r7=0,
                                         const char* payload=nullptr,std::uint32_t* payloadAt=nullptr) {
        const PPCContext saved=ctx;
        const std::uint32_t sp=(saved.r1.u32-768u)&~15u;w32(sp,saved.r1.u32);
        if(payload){std::memcpy(base+sp+96u,payload,std::strlen(payload)+1);if(payloadAt)*payloadAt=sp+96u;r4=sp+96u;}
        ctx.r1.u64=sp;ctx.r3.u64=r3;ctx.r4.u64=r4;ctx.r5.u64=r5;ctx.r6.u64=r6;ctx.r7.u64=r7;
        f(ctx,base);
        const std::uint32_t r=ctx.r3.u32;ctx=saved;return r;
    }
};
}
PPC_FUNC(sub_82FCF2E0) {
    namespace uo=::recomp::ugc_offline;namespace ou=online_ugcv1;
    const std::uint32_t mgr=ctx.r3.u32,kind=ctx.r4.u32;
    const bool on=loading_hold_hook::online();
    if(!on || !::recomp::fm_route::jobsOn() || ou::off() || kind!=uo::RstarKind) {
        __imp__sub_82FCF2E0(ctx,base);
        if(on){char l[160];std::snprintf(l,sizeof l,"original parser ran (kind=%u route=%d off=%d) count=%u",kind,::recomp::fm_route::routeOn()?1:0,ou::off()?1:0,
            unsigned(PPC_LOAD_U16(mgr+80u)));ou::say(l);}
        return;
    }
    ou::Guest g{ctx,base};
    const std::uint32_t stream=g.call(__imp__sub_831EDB68,ou::SearchPaths,0,ou::EmptyExt,0,1,uo::RstarPath);
    if(!stream){ou::say("UGC_rstar.ugc did not open -> the original parser");__imp__sub_82FCF2E0(ctx,base);return;}
    const std::uint32_t size=g.call(__imp__sub_831DCEA8,stream);
    std::vector<std::uint8_t> file;
    if(size>0 && size<=ou::MaxFile) {
        const std::uint32_t buf=g.call(__imp__sub_8255DC60,size);
        if(buf){const std::uint32_t got=g.call(__imp__sub_831E2DB8,stream,buf,size);
                if(got<=size)file.assign(base+buf,base+buf+got);g.call(__imp__sub_8255DCC0,buf);}
    }
    g.call(__imp__sub_831E3240,stream);
    std::vector<uo::Rec> recs;const char* why="";
    if(!uo::parseV1(file.data(),file.size(),recs,why)) {
        char l[200];std::snprintf(l,sizeof l,"UGC_rstar.ugc (size %u, read %zu) not taken: %s -> the original parser",size,file.size(),why);ou::say(l);
        __imp__sub_82FCF2E0(ctx,base);return;
    }
    g.w32(mgr+ou::CountOff,std::uint32_t(recs.size()));
    unsigned built=0;std::uint8_t fixed[uo::RecordSize];
    const auto text=[&](std::uint32_t str,const std::uint8_t* src,std::uint32_t len){
        const std::uint32_t t=g.call(__imp__sub_8255DC60,len);if(!t)return false;
        std::memcpy(base+t,src,len);g.call(__imp__sub_831AEF98,str,t,len);g.call(__imp__sub_8255DCC0,t);return true;};
    for(const auto& e:recs) {
        const std::uint32_t r=g.call(__imp__sub_8255DC58,uo::RecordSize);if(!r)break;
        uo::fixedFields(e,fixed);std::memcpy(base+r,fixed,uo::RecordSize);
        g.call(__imp__sub_831AEEC0,r+uo::OffDesc);g.call(__imp__sub_831AEEC0,r+uo::OffData);
        if(!text(r+uo::OffDesc,e.desc,e.descLen) || !text(r+uo::OffData,e.data,e.dataLen))break;
        const std::uint32_t slot=g.call(__imp__sub_83489EF8,mgr+ou::ArrayOff,16);if(!slot)break;
        g.w32(slot,r);++built;
    }
    char l[300];std::snprintf(l,sizeof l,"v1 list loaded: %u of %zu records (file %u bytes) count=%u array=%u first=%.22s",built,recs.size(),size,
        unsigned(PPC_LOAD_U32(mgr+ou::CountOff)),unsigned(PPC_LOAD_U16(mgr+80u)),reinterpret_cast<const char*>(recs.front().id));ou::say(l);
}
// DATAFILE_LOAD_OFFLINE_UGC's loader (v1110 read only; v1111: on a miss, the <name>_00 retry - online_ugc_offline.h jobNameFallback) sub_82D83A40 (name) -> bool opens "common:/data/ugc/%s.ugc" (format 0x82092A5C; the disc files
// are <id>_00.ugc, plain JSON read by stream size). Every new (name, result) in Online mode is logged, at most JobLogMax lines, so a failed
// per-job load names the file it looked for. The original always runs.
PPC_FUNC_IMPL(__imp__sub_82D83A40);
PPC_FUNC(sub_82D83A40) {
    if(!loading_hold_hook::online()){__imp__sub_82D83A40(ctx,base);return;}
    const std::uint32_t np=ctx.r3.u32;char name[64]{};
    if(np>=0x10000u){for(unsigned i=0;i+1<sizeof name;++i){const char c=char(base[np+i]);if(!c)break;name[i]=c;}}
    __imp__sub_82D83A40(ctx,base);
    std::uint32_t r=ctx.r3.u32&0xFFu;
    char alt[64]{};
    if(r==0 && ::recomp::fm_route::jobsOn() && !online_ugcv1::off() && ::recomp::ugc_offline::jobNameFallback(name,alt,sizeof alt)) {
        // v1111: the disc's job file is <id>_00.ugc; the same loader on the suffixed name, from a fresh guest frame below r1
        const PPCContext saved=ctx;const std::uint32_t sp=(saved.r1.u32-512u)&~15u;
        {std::uint32_t bc=__builtin_bswap32(saved.r1.u32);std::memcpy(base+sp,&bc,4);}
        std::memcpy(base+sp+96u,alt,std::strlen(alt)+1);
        ctx.r1.u64=sp;ctx.r3.u64=sp+96u;__imp__sub_82D83A40(ctx,base);
        const std::uint32_t r2=ctx.r3.u32&0xFFu;ctx=saved;ctx.r3.u64=r2;r=r2;
        char l[160];std::snprintf(l,sizeof l,"offline job file common:/data/ugc/%s.ugc -> %u (the disc name, after %s.ugc missed)",alt,unsigned(r2),name);online_ugcv1::say(l);
    }
    static online_fmgate::Seen seen;static std::atomic<unsigned> lines{0};constexpr unsigned JobLogMax=40;
    std::uint32_t h=2166136261u;for(const char* p=name;*p;++p)h=(h^std::uint8_t(*p))*16777619u;
    if(lines.load(std::memory_order_relaxed)<JobLogMax && seen.fresh(h^r)){lines.fetch_add(1,std::memory_order_relaxed);
        char l[160];std::snprintf(l,sizeof l,"offline job file common:/data/ugc/%s.ugc -> %u",name,unsigned(r));online_ugcv1::say(l);}
}
