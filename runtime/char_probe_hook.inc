// char_probe_hook.inc (v1080, trainer-mods; lead 2026-11-08 01:4x: the created female character is bald. t-onlcre74-20261108-0031: after
// the creator's gender switch its menu read "Hair Short Black" (hair item Enum385 = 4) but the ped had no hair, and the game's own
// capture of the ped (packed stats 399+2 from GET_PED_DRAWABLE_VARIATION, maintransition 0x35b87 -> 0x3616f) read drawable 0; the male
// default ("Ponytail Black") rendered. The hair goes through maintransition 0x3abef (item -> catalogue index) and 0x5abf (component 2,
// the catalogue 0xd985: mp_m 0x16f0a / mp_f 0xd9c1, then SET_PED_COMPONENT_VARIATION). Lead: suspect our port first (a native on that path
// returning 0 or a wrong count). READ ONLY: every call below logs its inputs and its result, the original runs unchanged.
// Online mode only (the creator runs there); component 2 (hair) for the component natives, slots 1 / 2 (beard / eyebrows) for the
// overlay native; each native at most ProbeMax lines; diag-char-probe-off silences it.
// Handlers (the native registration table, ppc_recomp.197): SET_PED_COMPONENT_VARIATION d4f7b05c sub_82DD3AD0 (ped, comp, d, t, p);
// IS_PED_COMPONENT_VARIATION_VALID 952abd9a sub_82DD3A38 (ped, comp, d, t) -> bool; GET_PED_DRAWABLE_VARIATION 29850fe2 sub_82DD3978
// (ped, comp) -> int; GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS 9754c27d sub_82E04BA8 (ped, comp) -> int; GET_NUMBER_OF_PED_TEXTURE_VARIATIONS
// 83d9fbe7 sub_82E04BE8 (ped, comp, d) -> int; SET_PED_DEFAULT_COMPONENT_VARIATION c866a984 sub_82DD3B10 (ped); SET_PED_HEAD_OVERLAY
// d28dba90 sub_82DD3C38 (ped, slot, index, opacity float). Call context (as online_spoof_hook.inc): word 0 of r3 = the return pointer,
// word 2 = the argument block (4-byte slots).
// v1082: arguments are read BEFORE the call (the VM writes a native's result over argument 0: v1080's getter lines showed the result as
// "ped"); every line says whether the ped is the player's (char_probe_flag.h, the trainer tick's PLAYER_PED_ID) and the player's model.
// + DRAW_SPRITE 1fec16b0 sub_82D8AD78 (dict, name, x, y, w, h, heading, r, g, b, a) and GET_HUD_COLOUR 63f66a0b sub_82D614D0 (id, &r, &g,
// &b, &a), Online mode, each new (dict, name, colour) / (id, colour) once (lead + bugfix 2026-11-08: the creator backdrop sprite's vertex
// colour is grey ~0.7: navy in DRAW_SPRITE's arguments = the vertex path, grey = the colour lookup).
// LINK CONDITION: redirect-v1082-charprobe2.ld (= v1080's without sub_82D8AD78 and sub_82D614D0); was: redirect-v1080-charprobe.ld (= redirect-v1072-gosetter.ld without these seven wrappers). "RENDER_CHAR_PROBE" lines prove it.
#include <atomic>
#include "char_probe_flag.h"
namespace char_probe_hook {
constexpr unsigned ProbeMax=80;
inline bool off() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-char-probe-off"),F_OK)==0;return o;}
enum Which : unsigned {SetComp,Valid,GetDraw,NumDraw,NumTex,SetDefault,Overlay,Sprite,Hud,WhichCount};
inline std::atomic<unsigned> lines[WhichCount]{};
inline bool want(Which w) noexcept {return loading_hold_hook::online() && !off() && lines[w].load(std::memory_order_relaxed)<ProbeMax;}
inline std::uint32_t arg(std::uint8_t* base,std::uint32_t ctxp,unsigned i) noexcept {const std::uint32_t a=PPC_LOAD_U32(ctxp+8u);return a?PPC_LOAD_U32(a+4u*i):0u;}
inline std::uint32_t ret(std::uint8_t* base,std::uint32_t ctxp) noexcept {const std::uint32_t r=PPC_LOAD_U32(ctxp);return r?PPC_LOAD_U32(r):0xFFFFFFFFu;}
inline void say(Which w,const char* l) noexcept {lines[w].fetch_add(1,std::memory_order_relaxed);loading_hold_hook::log(l);}
inline const char* who(std::uint32_t ped) noexcept {return ped && ped==::recomp::char_probe::playerPed.load(std::memory_order_relaxed)?"player":"other";}
inline std::uint32_t pmodel() noexcept {return ::recomp::char_probe::playerModel.load(std::memory_order_relaxed);}
// v1082: a small seen-set for the per-frame draws (main thread only)
inline std::uint32_t seen[96]{};inline unsigned seenN=0;
inline bool fresh(std::uint32_t key) noexcept {for(unsigned i=0;i<seenN;++i)if(seen[i]==key)return false;if(seenN<96)seen[seenN++]=key;return true;}
inline std::uint32_t fnv(const char* t,std::uint32_t h=2166136261u) noexcept {for(;*t;++t)h=(h^std::uint8_t(*t))*16777619u;return h;}
}
PPC_FUNC_IMPL(__imp__sub_82DD3AD0);
#include "hair_keep.h"   // v1106 INTERIM: the creator preview's hair
namespace char_probe_hook {inline ::recomp::hair_keep::Last hairLast;inline std::atomic<unsigned> hairKept{0};}
PPC_FUNC(sub_82DD3AD0) {   // SET_PED_COMPONENT_VARIATION
    const std::uint32_t c=ctx.r3.u32,lr=std::uint32_t(ctx.lr);std::uint32_t A[5];for(unsigned k=0;k<5u;++k)A[k]=char_probe_hook::arg(base,c,k);
    const bool w=char_probe_hook::want(char_probe_hook::SetComp) && A[1]==2u;
    // v1105/v1106: the VM's thread context in r23 at the handler's entry: +4 the script hash, +12 the native's IP (which site issues the reset)
    const std::uint32_t tc=ctx.r23.u32,scr=tc>=0x40000000u?PPC_LOAD_U32(tc+4u):0u,sip=tc>=0x40000000u?PPC_LOAD_U32(tc+12u):0u;
    const bool keep=::recomp::hair_keep::decide(char_probe_hook::hairLast,loading_hold_hook::online(),scr,A[0],A[1],A[2],PPC_LOAD_U32(::recomp::hair_keep::FrameWord));
    if(keep){const unsigned n=char_probe_hook::hairKept.fetch_add(1,std::memory_order_relaxed);
        if(n<40){char l[200];std::snprintf(l,sizeof l,"RENDER_CHAR_PROBE kept hair ped=%x d=%d (a d=0 reset from script=%08x ip=%x in the same frame, not passed on; #%u)",A[0],char_probe_hook::hairLast.d,scr,sip,n+1);
            loading_hold_hook::log(l);}
        return;}
    __imp__sub_82DD3AD0(ctx,base);
    if(w){char l[220];std::snprintf(l,sizeof l,"RENDER_CHAR_PROBE SET_PED_COMPONENT_VARIATION ped=%x(%s pm=%08x) comp=2 d=%d t=%d p=%d lr=%08x script=%08x ip=%x",A[0],char_probe_hook::who(A[0]),char_probe_hook::pmodel(),
        int(A[2]),int(A[3]),int(A[4]),lr,scr,sip);char_probe_hook::say(char_probe_hook::SetComp,l);}
}
PPC_FUNC_IMPL(__imp__sub_82DD3A38);
PPC_FUNC(sub_82DD3A38) {   // IS_PED_COMPONENT_VARIATION_VALID
    const std::uint32_t c=ctx.r3.u32,lr=std::uint32_t(ctx.lr);std::uint32_t A[4];for(unsigned k=0;k<4u;++k)A[k]=char_probe_hook::arg(base,c,k);
    const bool w=char_probe_hook::want(char_probe_hook::Valid) && A[1]==2u;
    __imp__sub_82DD3A38(ctx,base);
    if(w){char l[180];std::snprintf(l,sizeof l,"RENDER_CHAR_PROBE IS_PED_COMPONENT_VARIATION_VALID ped=%x(%s pm=%08x) comp=2 d=%d t=%d -> %d lr=%08x",A[0],char_probe_hook::who(A[0]),char_probe_hook::pmodel(),
        int(A[2]),int(A[3]),int(char_probe_hook::ret(base,c)),lr);char_probe_hook::say(char_probe_hook::Valid,l);}
}
PPC_FUNC_IMPL(__imp__sub_82DD3978);
PPC_FUNC(sub_82DD3978) {   // GET_PED_DRAWABLE_VARIATION
    const std::uint32_t c=ctx.r3.u32,lr=std::uint32_t(ctx.lr);std::uint32_t A[2];for(unsigned k=0;k<2u;++k)A[k]=char_probe_hook::arg(base,c,k);
    const bool w=char_probe_hook::want(char_probe_hook::GetDraw) && A[1]==2u;
    __imp__sub_82DD3978(ctx,base);
    if(w){char l[160];std::snprintf(l,sizeof l,"RENDER_CHAR_PROBE GET_PED_DRAWABLE_VARIATION ped=%x(%s pm=%08x) comp=2 -> %d lr=%08x",A[0],char_probe_hook::who(A[0]),char_probe_hook::pmodel(),
        int(char_probe_hook::ret(base,c)),lr);char_probe_hook::say(char_probe_hook::GetDraw,l);}
}
PPC_FUNC_IMPL(__imp__sub_82E04BA8);
PPC_FUNC(sub_82E04BA8) {   // GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS
    const std::uint32_t c=ctx.r3.u32,lr=std::uint32_t(ctx.lr);std::uint32_t A[2];for(unsigned k=0;k<2u;++k)A[k]=char_probe_hook::arg(base,c,k);
    const bool w=char_probe_hook::want(char_probe_hook::NumDraw) && A[1]==2u;
    __imp__sub_82E04BA8(ctx,base);
    if(w){char l[160];std::snprintf(l,sizeof l,"RENDER_CHAR_PROBE GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS ped=%x(%s pm=%08x) comp=2 -> %d lr=%08x",A[0],char_probe_hook::who(A[0]),char_probe_hook::pmodel(),
        int(char_probe_hook::ret(base,c)),lr);char_probe_hook::say(char_probe_hook::NumDraw,l);}
}
PPC_FUNC_IMPL(__imp__sub_82E04BE8);
PPC_FUNC(sub_82E04BE8) {   // GET_NUMBER_OF_PED_TEXTURE_VARIATIONS
    const std::uint32_t c=ctx.r3.u32,lr=std::uint32_t(ctx.lr);std::uint32_t A[3];for(unsigned k=0;k<3u;++k)A[k]=char_probe_hook::arg(base,c,k);
    const bool w=char_probe_hook::want(char_probe_hook::NumTex) && A[1]==2u;
    __imp__sub_82E04BE8(ctx,base);
    if(w){char l[170];std::snprintf(l,sizeof l,"RENDER_CHAR_PROBE GET_NUMBER_OF_PED_TEXTURE_VARIATIONS ped=%x(%s pm=%08x) comp=2 d=%d -> %d lr=%08x",A[0],char_probe_hook::who(A[0]),char_probe_hook::pmodel(),
        int(A[2]),int(char_probe_hook::ret(base,c)),lr);char_probe_hook::say(char_probe_hook::NumTex,l);}
}
PPC_FUNC_IMPL(__imp__sub_82DD3B10);
PPC_FUNC(sub_82DD3B10) {   // SET_PED_DEFAULT_COMPONENT_VARIATION
    const std::uint32_t c=ctx.r3.u32,lr=std::uint32_t(ctx.lr);std::uint32_t A[1];for(unsigned k=0;k<1u;++k)A[k]=char_probe_hook::arg(base,c,k);
    const bool w=char_probe_hook::want(char_probe_hook::SetDefault);
    __imp__sub_82DD3B10(ctx,base);
    if(w){char l[140];std::snprintf(l,sizeof l,"RENDER_CHAR_PROBE SET_PED_DEFAULT_COMPONENT_VARIATION ped=%x(%s pm=%08x) lr=%08x",A[0],char_probe_hook::who(A[0]),char_probe_hook::pmodel(),lr);
        char_probe_hook::say(char_probe_hook::SetDefault,l);}
}
PPC_FUNC_IMPL(__imp__sub_82DD3C38);
PPC_FUNC(sub_82DD3C38) {   // SET_PED_HEAD_OVERLAY
    const std::uint32_t c=ctx.r3.u32,lr=std::uint32_t(ctx.lr);std::uint32_t A[4];for(unsigned k=0;k<4u;++k)A[k]=char_probe_hook::arg(base,c,k);
    const std::uint32_t slot=A[1];
    const bool w=char_probe_hook::want(char_probe_hook::Overlay) && (slot==1u || slot==2u);
    __imp__sub_82DD3C38(ctx,base);
    if(w){const std::uint32_t ob=A[3];float op;std::memcpy(&op,&ob,4);char l[170];
        std::snprintf(l,sizeof l,"RENDER_CHAR_PROBE SET_PED_HEAD_OVERLAY ped=%x(%s pm=%08x) slot=%u index=%d opacity=%.3f lr=%08x",A[0],char_probe_hook::who(A[0]),char_probe_hook::pmodel(),slot,
        int(A[2]),double(op),lr);char_probe_hook::say(char_probe_hook::Overlay,l);}
}
PPC_FUNC_IMPL(__imp__sub_82D8AD78);
PPC_FUNC(sub_82D8AD78) {   // DRAW_SPRITE (dict, name, x, y, w, h, heading, r, g, b, a): each new (dict, name, colour) once
    if(char_probe_hook::want(char_probe_hook::Sprite)) {
        const std::uint32_t c=ctx.r3.u32;char d[32]{},n[40]{};
        online_spoof_hook::guestText(base,char_probe_hook::arg(base,c,0),d,int(sizeof d));online_spoof_hook::guestText(base,char_probe_hook::arg(base,c,1),n,int(sizeof n));
        const std::uint32_t r=char_probe_hook::arg(base,c,7),g=char_probe_hook::arg(base,c,8),b=char_probe_hook::arg(base,c,9),a=char_probe_hook::arg(base,c,10);
        const std::uint32_t key=char_probe_hook::fnv(n,char_probe_hook::fnv(d))^(r*0x9E3779B1u)^(g<<8)^(b<<16)^(a<<24);
        if(char_probe_hook::fresh(key)){char l[200];std::snprintf(l,sizeof l,"RENDER_CHAR_PROBE DRAW_SPRITE dict=%s name=%s rgba=%d,%d,%d,%d lr=%08x",d,n,int(r),int(g),int(b),int(a),
            std::uint32_t(ctx.lr));char_probe_hook::say(char_probe_hook::Sprite,l);}
    }
    __imp__sub_82D8AD78(ctx,base);
}
PPC_FUNC_IMPL(__imp__sub_82D614D0);
PPC_FUNC(sub_82D614D0) {   // GET_HUD_COLOUR (id, &r, &g, &b, &a): each new (id, colour) once
    const std::uint32_t c=ctx.r3.u32,lr=std::uint32_t(ctx.lr);std::uint32_t A[5];for(unsigned k=0;k<5u;++k)A[k]=char_probe_hook::arg(base,c,k);
    const bool w=char_probe_hook::want(char_probe_hook::Hud);
    __imp__sub_82D614D0(ctx,base);
    if(w) {
        int v[4]{-1,-1,-1,-1};for(int k=0;k<4;++k)if(A[1+k]>=0x40000000u)v[k]=int(PPC_LOAD_U32(A[1+k]));
        const std::uint32_t key=0x48554400u^(A[0]*0x9E3779B1u)^std::uint32_t(v[0])^(std::uint32_t(v[1])<<8)^(std::uint32_t(v[2])<<16)^(std::uint32_t(v[3])<<24);
        if(char_probe_hook::fresh(key)){char l[160];std::snprintf(l,sizeof l,"RENDER_CHAR_PROBE GET_HUD_COLOUR id=%d -> %d,%d,%d,%d lr=%08x",int(A[0]),v[0],v[1],v[2],v[3],lr);
            char_probe_hook::say(char_probe_hook::Hud,l);}
    }
}
