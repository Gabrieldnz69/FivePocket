#pragma once
// online_l0.h (v991, trainer-mods 2026-11-07; lead decisions 08:2x: "YES to the link-time redirect wrapper on sub_824B4158", "both, in
// one build"). Sandbox + files/private-trace/diag-online-local only; L0 additionally needs files/private-trace/diag-online-l0.
//
//  E6 (read-only, diag-online-local): the per-frame auto-host sub_824F55E8 (called from the session state machine sub_824B4158 itself)
//     calls sub_82FF9800 (snSession::Host in mode sub_83517870()) when: (sub_824F5588([netobj+13648]) || sub_824F5588([netobj+13644])
//     || byte netobj+0x1ACC8 || byte netobj+0x1E8B0) && [netobj+0] == 0 && [netobj+13480] != 1. sub_824F5588(s) = state word s+2320
//     in {2,3} || s+15792 in {2,3} (sub_824F5560: [x+8] in 2..3, x = s+2312 / s+15784). E6 logs those inputs and the predicted call
//     each second for 60 s, with 0x83C00F61, AE9, ADA and [pm+128] (lead-0607 localplayer.md R6). Reads only.
//  L0 (diag-online-l0): the game's own OpenNetwork, sub_82FD9878() (no arguments; the only path to netPlayerMgrBase::Init sub_834B9408
//     that writes [pm+128]), once, on the main thread from this wrapper, before the state machine runs, after re-checking on the same
//     tick (localplayer.md §5.1): netobj != 0, state [netobj+0x10BB0] == 0, AE9 == ADA == 0, [pm+128] == 0, FindPlayerPed
//     [[0x8394B8B0]+4] != 0, the ROS creds byte 0x83B12D10[idx] == 0, the signed-online byte 0x83B13BA7 == 0 (diag-online-l0 keeps the
//     signin spoof off: R6, f61 = 1 under it). Post-checks (all logged): AE9 == ADA == 1, AEA == AEB == 0, [pm+124] == 7,
//     byte [pm+764] & 0x80 (v998: lbz / stb r,764(pm) in sub_834B9408; v991..v997 tested the word, failed=1 on t-l0 0940), lp = [pm+128] in [pm+768, pm+8832) on a 448-byte step, [lp] == 0x8209BB2C, [lp+140] == 10, byte [lp+41] == 255,
//     0x83B13AF8 / AFC / B00 != 0.
//  E5 (diag-online-l0, after L0): reversibility. The game's own full close sub_82FD4A08() (gated by ADA itself), the zeros asserted
//     (AE9 == ADA == 0, [pm+128] == 0), then ONE re-open with the same checks. The phases (closed / open / closed / open, HoldMs each)
//     are logged with their start frame so the frame-time A/B is read from the renderer's own timing lines.
//  L1 (v999, diag-online-l1 on top of diag-online-l0; lead 10:1x after t-l0-20261107-1012 proved L0 + E5): the gutted host worker
//     sub_83061D70's body redone once from here, per lead-0607 session.md "Corrected proposal" item 4 (worker bytes: v491 corpus
//     237.cpp sub_83061D70; voice host sub_8305DBC0 for the Host call shape). Sequence: 30 s closed, L0, 10 s open, L1, 60 s of
//     one state line per second (E5 is skipped: proven). L1 itself, on one tick:
//       preconditions (read, nothing written on a refusal): AE9 == 1 (L0 open), netobj, state 0, status = [[netobj+13652]] != 1,
//       main session [netobj+13644] != 0, player ped, [pm+128] != 0;
//       the worker's own gates (calls, no writes): state < 6, sub_824A7CA0(1) != 0, !sub_82FA4D80(0x8381E258), byte +22 || +20 of
//       0x8381E258;
//       the worker's writes: word +0x10EA4 = (mode == 0), word 0x83B13AF0 = mode; the attribute record (8 ids 0x10000001/13/14/17/
//       18/1A/1B/1C) -> sub_83055B00(netobj+0x10BB8, rec, slots=1, priv=1) -> sub_8326C8C0(cfg+52, 0); word +0x10F20 = 0 (the
//       worker's gutted "&0x80 -> sub_824A7CA0(1)" tail is not repeated: its result was dropped);
//       the status word = 0 (atomic, as the voice host does), then sub_830FF1F0([netobj+13644], sub_822379E0(), mode 0, 1, 1,
//       sub_83022278(cfg), r9 = 2 (CHOICE: the original value was compiled out; the voice host's), r10 = 0, [84] = 0, [92] = status);
//       only on ret == 1: state = 2 and word +0x10ECC = sub_824AE6D8() | 1.
//     FALSIFIERS (lead): no state past 2 within 30 s of L1, or any crash.
// Every step logs one RENDER_ONLINE_LOCAL line with before / after values; a failed precondition logs and changes nothing.
#include "online_curtain_flag.h"   // v1043: the clean leave waits for the loading screen
#include <cstdint>
#include <cstdio>
#include <cstring>   // v1048: strncmp

namespace recomp::online_l0 {

constexpr std::uint32_t NetObjectWord=0x83B13B04u,AE9=0x83B13AE9u,ADA=0x83B13ADAu,AEA=0x83B13AEAu,AEB=0x83B13AEBu,F61=0x83C00F61u,
    PlayerMgrWord=0x8381CF1Cu,PedWord=0x8394B8B0u,RosIndexWord=0x8381CFE4u,RosByteBase=0x83B12D10u,SignedOnlineByte=0x83B13BA7u,
    AF8=0x83B13AF8u,AFC=0x83B13AFCu,B00=0x83B13B00u,LocalPlayerVtable=0x8209BB2Cu;
constexpr std::uint32_t StateOffset=0x10BB0u,MainSession=13644u,TransitionSession=13648u,HostGuard=13480u,
    ActiveFlagA=0x1ACC8u,ActiveFlagB=0x1E8B0u;
constexpr std::uint32_t E6Seconds=60,HoldMs=45000,StartDelayMs=30000;
// L1 (v999)
constexpr std::uint32_t StatusPtrOffset=13652u,ModeFlagOffset=0x10EA4u,CfgOffset=0x10BB8u,ClearOffset=0x10F20u,StampOffset=0x10ECCu,
    PlayerCountOffset=292u,GateObject=0x8381E258u,ModeWord=0x83B13AF0u;
constexpr std::uint32_t SessionInContainer=8,NetInitFlagOffset=0x10EBCu,AllocatorGlobal=0x83B13BDCu,AllocatorGlobal2=0x83B13BD8u;   // v1005
// v1014 (t-l1s-20261107-1253, v1012): the in-run A/B close (the game's full close sub_82FD4A08 while the session is live) stopped the game:
// sub_82277F58 still runs per frame in session and calls slot 6 of the local net player [[0x8381CF1C]+128], which the close freed
// (invalid indirect target 0, lr 0x82278328). Off until the session is ended through the game's own path first; the A/B is then made
// by alternating session / no-session runs.
constexpr bool L1AbClose=false;
// v1016 (lead session-leave.md): the clean leave after the 60 s session. L3a: sub_8305C420(netobj, 0) once (it refuses at state 0, when busy,
// at 16 / 17; else it allocates snEndTask and sets state 16; the dispatcher's case 16 on status 3 calls sub_8306C028, which stores state 0
// and then calls sub_82FD4A08 itself). Wait for state 0 (at most 20 s). L3b at state 0: sub_830618E0(netobj, 1), then sub_82FD4A08 only if
// ADA is still set AND the guard holds (state 0, byte netobj+0x1ACD9 clear: else the voice mode 3 persists and sub_82277F58 would call
// the freed local player), then sub_8306BA88. Then 30 s with no session (the in-run A/B against the session window).
constexpr bool L1Leave=true;
// v1024 (lead: the "Connection to the game lost due to an unknown network error" alert after every clean leave). Traced on the disc:
// the text is HUD_MPBAILMESG (global.gxt2 3bfa89e4), shown only by maintransition 0x12c6 (the default of its bail-reason switch on
// global 1332523), from its state machine 0xfad state 25 (global 1344033). 0x67c95 (its main loop, every frame) sets state 25 when
// global 1344104 != 0. main_persistent's event handler for event type 152 (switch at 0x18d64 -> 0x1b3d6) sets 1344104 = 1 when the
// event's data is not 1 / 4 and global 1344077 == 0, and resets 1344077 to 0 (0x1b4b9). The game's own deliberate leave,
// maintransition 0x4338d(0), calls NETWORK_SESSION_LEAVE_SINGLE_PLAYER and sets 1344077 = 1 first, so that handler does not bail.
// L3 follows the same protocol: before sub_8305C420 it writes 1344077 0 -> 1 (logged before / after); after the teardown the hold
// lines log 1344077, 1344104 and 1344033; if the game never consumed the 1 (no event), L3 puts the 0 back at the end of the hold.
constexpr std::uint32_t GlobalTable=0x83C5CE48u,GlobalSizes=0x83C5CF50u,LeaveFlagGlobal=1344077u,BailGlobal=1344104u,TransitionStateGlobal=1344033u;
constexpr std::uint32_t VoiceByteOffset=0x1ACD9u,L3WaitMs=20000;
constexpr std::uint32_t L1StartDelayMs=90000;   // v1014: L0 / L1 after the harness walk-out (the autopilot stops in session: t-l1s 1253)
// v1102: in Online mode with the route on, L1 waits for maintransition's T18 ENTER at most L1DeferMaxMs, and self-hosts after it only when
// maintransition is neither at T17 nor at T18 (a self-host there would arm the game's 0x43363 SESSION_END again)
constexpr std::uint64_t L1DeferMaxMs=90000;
constexpr std::uint32_t L1Mode=0,L1Slots=1,L1Priv=1,L1HostFlags=2,L1OpenHoldMs=10000,L1MonitorSeconds=60,L1LongMonitorSeconds=210,
    OnlineMonitorSeconds=1u<<30,OnlineStartDelayMs=15000,CurtainWaitMs=3000,L1RetryMs=1000,L1RetryMax=120;   // v1048 L1Retry*: the worker-gate wait (2 min)   // v1043 CurtainWaitMs: L3a waits at most this long for the loading screen (online_curtain.h)   // v1040 Online mode: no test leave (~34 years), start 15 s after the first armed tick   // v1020: diag-online-l1-long (v1023: 210 s, room for the 3-minute session soak)
inline constexpr std::uint32_t L1AttrIds[8]{0x10000001u,0x10000013u,0x10000014u,0x10000017u,0x10000018u,0x1000001Au,0x1000001Bu,0x1000001Cu};

// Guest memory, big-endian, read only from here.
struct Mem {
    virtual ~Mem()=default;
    virtual std::uint32_t u32(std::uint32_t a)=0;   // 0 for an address outside guest memory
    virtual std::uint8_t u8(std::uint32_t a)=0;
};
inline bool ptr(std::uint32_t a) noexcept {return a>=0x40000000u && a<0xF0000000u && !(a&3u);}
using LogFn=void(*)(const char*);

// The auto-host's inputs and its predicted call (E6).
struct AutoHost {
    std::uint32_t netobj=0,state=0,w0=0,guard=0,main=0,trans=0,mainA=0,mainB=0,transA=0,transB=0;
    int flagA=0,flagB=0,f61=0,ae9=0,ada=0;std::uint32_t local=0;
    static bool active(std::uint32_t w) noexcept {return w==2 || w==3;}
    bool cond(std::uint32_t a,std::uint32_t b) const noexcept {return active(a) || active(b);}
    bool predicted() const noexcept {return (cond(transA,transB) || cond(mainA,mainB) || flagA || flagB) && w0==0 && guard!=1;}
};
inline AutoHost readAutoHost(Mem& m) noexcept {
    AutoHost h;h.netobj=m.u32(NetObjectWord);
    h.f61=m.u8(F61);h.ae9=m.u8(AE9);h.ada=m.u8(ADA);
    const std::uint32_t pm=m.u32(PlayerMgrWord);h.local=ptr(pm)?m.u32(pm+128u):0u;
    if(!ptr(h.netobj))return h;
    h.state=m.u32(h.netobj+StateOffset);h.w0=m.u32(h.netobj);h.guard=m.u32(h.netobj+HostGuard);
    h.flagA=m.u8(h.netobj+ActiveFlagA);h.flagB=m.u8(h.netobj+ActiveFlagB);
    h.main=m.u32(h.netobj+MainSession);h.trans=m.u32(h.netobj+TransitionSession);
    if(ptr(h.main)){h.mainA=m.u32(h.main+2320u);h.mainB=m.u32(h.main+15792u);}
    if(ptr(h.trans)){h.transA=m.u32(h.trans+2320u);h.transB=m.u32(h.trans+15792u);}
    return h;
}
inline void formatAutoHost(const AutoHost& h,char* out,int cap,unsigned second) noexcept {
    std::snprintf(out,std::size_t(cap),"RENDER_ONLINE_LOCAL e6 t=%u state=%u w0=%u guard=%u main=%08x a=%u b=%u trans=%08x a=%u b=%u "
        "flags=%d,%d predicted_autohost=%d f61=%d ae9=%d ada=%d local=%08x",second,h.state,h.w0,h.guard,h.main,h.mainA,h.mainB,h.trans,
        h.transA,h.transB,h.flagA,h.flagB,int(h.predicted()),h.f61,h.ae9,h.ada,h.local);
}

// The guest side of L1 (the hook implements it with the game's own functions; tests with a model).
struct L1Ops {
    virtual ~L1Ops()=default;
    virtual bool netGate()=0;                                   // sub_824A7CA0(1) != 0
    virtual bool busy()=0;                                      // sub_82FA4D80(GateObject) != 0
    virtual void write32(std::uint32_t a,std::uint32_t v)=0;    // big-endian guest word
    virtual void zeroAtomic(std::uint32_t a)=0;                 // the status word, atomically
    virtual void config(std::uint32_t cfg,std::uint32_t slots,std::uint32_t priv)=0;   // record + sub_83055B00 + sub_8326C8C0
    virtual std::uint32_t host(std::uint32_t session,std::uint32_t cfg,std::uint32_t mode,std::uint32_t slots,std::uint32_t priv,
                               std::uint32_t flags,std::uint32_t status)=0;            // sub_830FF1F0; returns r3 & 0xFF
    virtual std::uint32_t now()=0;                              // sub_824AE6D8()
    // v1016 L3 (lead session-leave.md, verified): the game's own session end and teardown
    virtual std::uint32_t endSession(std::uint32_t netobj)=0;   // sub_8305C420(netobj, 0) = NETWORK_SESSION_END's body; r3 & 0xFF
    virtual void netShutdown(std::uint32_t netobj)=0;           // sub_830618E0(netobj, 1)
    virtual std::uint32_t netInit(std::uint32_t netobj)=0;      // sub_8306BA88(netobj); r3 & 0xFF
    // v1102 (round 5 stage C): NETWORK_CAN_ENTER_MULTIPLAYER's own second gate, sub_83039500([NetObjectWord]) & 0xFF (true by default for models)
    virtual bool canEnter() {return true;}
};
// L1's preconditions and the worker's gates. nullptr = all hold.
inline const char* preHost(Mem& m,L1Ops& ops,char* why,int cap) noexcept {
    const std::uint32_t netobj=m.u32(NetObjectWord),pm=m.u32(PlayerMgrWord);
    const std::uint32_t pedHolder=m.u32(PedWord),ped=ptr(pedHolder)?m.u32(pedHolder+4u):0u;
    if(m.u8(AE9)!=1){std::snprintf(why,std::size_t(cap),"network-not-open ae9=%d",m.u8(AE9));return why;}
    if(!ptr(netobj)){std::snprintf(why,std::size_t(cap),"netobj=%08x",netobj);return why;}
    const std::uint32_t state=m.u32(netobj+StateOffset),status=m.u32(netobj+StatusPtrOffset),main=m.u32(netobj+MainSession);
    if(state!=0){std::snprintf(why,std::size_t(cap),"state=%u",state);return why;}
    if(!ptr(status)){std::snprintf(why,std::size_t(cap),"status-ptr=%08x",status);return why;}
    if(m.u32(status)==1){std::snprintf(why,std::size_t(cap),"status=1");return why;}
    if(!ptr(main)){std::snprintf(why,std::size_t(cap),"main-session=%08x",main);return why;}
    // v1002 / v1005: Host's first loads are the session's allocator [g+0] and its vtable (sub_830FF1F0: lwz r3,0(r30); lwz r11,0(r3);
    // lwz r10,8(r11) = alloc(732)). v1005 (lead session-allocator.md, verified): [netobj+13644] = X is a CONTAINER (ctor sub_83105D88:
    // X+0 / X+4 = -1, snSession at X+8, party session at X+13480); every game wrapper passes X+8 (Start sub_83100760, IsHost, sub_830FD048).
    // v1002..v1004 read X+0 (always -1) and passed X to Host.
    {const std::uint32_t g=main+SessionInContainer,al=m.u32(g),vt=ptr(al)?m.u32(al):0u,fn=ptr(vt)?m.u32(vt+8u):0u;
     if(!ptr(al) || !ptr(vt) || !fn){std::snprintf(why,std::size_t(cap),"session-allocator g=%08x al=%08x vt=%08x alloc=%08x init80=%d",g,al,vt,fn,
                                                    int((m.u8(netobj+NetInitFlagOffset)&0x80u)!=0));return why;}}
    if(!ped){std::snprintf(why,std::size_t(cap),"no-player-ped");return why;}
    if(!ptr(pm) || !m.u32(pm+128u)){std::snprintf(why,std::size_t(cap),"no-local-net-player");return why;}
    // the worker's own gates, in its order
    if(!ops.netGate()){std::snprintf(why,std::size_t(cap),"worker-gate sub_824A7CA0(1)=0");return why;}
    if(ops.busy()){std::snprintf(why,std::size_t(cap),"worker-gate sub_82FA4D80=1");return why;}
    if(!m.u8(GateObject+22u) && !m.u8(GateObject+20u)){std::snprintf(why,std::size_t(cap),"worker-gate gate+22=0 gate+20=0");return why;}
    return nullptr;
}
// v1005 E8 (lead session-allocator.md expectations, read only, at the L1 tick): g = X+8; [g] = [0x83B13BDC] != 0, [[g]] = 0x820086DC,
// [[[g]]+8] = 0x823C5F48, [0x83B13BD8] != 0, byte [g+13464] bit 0x08 (snSession::Init ran), byte [netobj+0x10EBC] bit 0x80
// (CNetworkSession::Init sub_8306BA88 ran), [[g]+8760] = 0 (the allocator's lock word), [g+13436] logged only.
inline void formatE8(Mem& m,char* out,int cap) noexcept {
    const std::uint32_t netobj=m.u32(NetObjectWord),x=ptr(netobj)?m.u32(netobj+MainSession):0u,g=ptr(x)?x+SessionInContainer:0u;
    const std::uint32_t al=ptr(g)?m.u32(g):0u,vt=ptr(al)?m.u32(al):0u,vt8=ptr(vt)?m.u32(vt+8u):0u,cs=ptr(al)?m.u32(al+8760u):0xFFFFFFFFu;
    std::snprintf(out,std::size_t(cap),"RENDER_ONLINE_LOCAL l1 e8 x=%08x g=%08x g0=%08x a3bdc=%08x vt=%08x vt8=%08x a3bd8=%08x g13464=%02x init80=%d "
        "cs8760=%08x g13436=%08x x0=%08x (expect g0=a3bdc!=0 vt=820086dc vt8=823c5f48 a3bd8!=0 bit08 init80=1 cs8760=0)",x,g,al,m.u32(AllocatorGlobal),vt,vt8,
        m.u32(AllocatorGlobal2),ptr(g)?m.u8(g+13464u):0u,ptr(netobj)?int((m.u8(netobj+NetInitFlagOffset)&0x80u)!=0):-1,cs,ptr(g)?m.u32(g+13436u):0u,ptr(x)?m.u32(x):0u);
}

// v1007 (t-l1-20261107-1201: Host ret 1, then status 1 -> 2 within ~3 s with no S2 / S7 / XGI call): read-only views of the host task
// and of HostOffline's gates. snSession::Host's 732-byte task goes into the session's queue at g+13428 (sub_830FCFE0; count at g+13436).
// Task: +100 step (0..5, sub_8312CF18), +104 inner status, +84 session, +112 gamer, +120 mode, +124 slots. Step 0 in mode 0 calls
// HostOffline sub_832B98C0(rl = [task+84]+2304, gamer, slots, task+132, task+104), which needs gamer 0..3, [rl+8] == 0, [rl+1084] == 0
// and the rline allocator [0x83C06A04] != 0 (then a 376-byte command).
constexpr std::uint32_t TaskQueueOffset=13428u,RlineAllocatorWord=0x83C06A04u,GamerInfoBase=0x83C314C8u,GamerInfoStride=19768u;
inline void formatE9(Mem& m,char* out,int cap) noexcept {
    const std::uint32_t netobj=m.u32(NetObjectWord),x=ptr(netobj)?m.u32(netobj+MainSession):0u,g=ptr(x)?x+SessionInContainer:0u,rl=g+2304u;
    const std::uint32_t gamer=m.u32(0x8381CFE4u),gi=gamer<4?GamerInfoBase+gamer*GamerInfoStride:0u;
    std::snprintf(out,std::size_t(cap),"RENDER_ONLINE_LOCAL l1 e9 rl=%08x rl8=%08x rl1084=%08x rline_alloc=%08x gamer=%u gi=%08x gi0=%08x gi4=%08x q=%08x,%08x,%08x",
        rl,ptr(g)?m.u32(rl+8u):0u,ptr(g)?m.u32(rl+1084u):0u,m.u32(RlineAllocatorWord),gamer,gi,gi?m.u32(gi):0u,gi?m.u32(gi+4u):0u,
        ptr(g)?m.u32(g+TaskQueueOffset):0u,ptr(g)?m.u32(g+TaskQueueOffset+4u):0u,ptr(g)?m.u32(g+TaskQueueOffset+8u):0u);
}
// The task's step / status words; returns a compact key for change detection.
inline std::uint64_t formatTask(Mem& m,char* out,int cap,unsigned t100ms) noexcept {
    const std::uint32_t netobj=m.u32(NetObjectWord),x=ptr(netobj)?m.u32(netobj+MainSession):0u,g=ptr(x)?x+SessionInContainer:0u;
    const std::uint32_t task=ptr(g)?m.u32(g+TaskQueueOffset):0u,sp=ptr(netobj)?m.u32(netobj+StatusPtrOffset):0u;
    const std::uint32_t step=ptr(task)?m.u32(task+100u):0xFFFFFFFFu,inner=ptr(task)?m.u32(task+104u):0xFFFFFFFFu,status=ptr(sp)?m.u32(sp):0xFFFFFFFFu;
    std::snprintf(out,std::size_t(cap),"RENDER_ONLINE_LOCAL l1 task t=%u.%u task=%08x vt=%08x step=%d inner=%d status=%d sess=%08x gamer=%d mode=%d slots=%d q=%u state=%u",
        t100ms/10,t100ms%10,task,ptr(task)?m.u32(task):0u,int(step),int(inner),int(status),ptr(task)?m.u32(task+84u):0u,ptr(task)?int(m.u32(task+112u)):-1,
        ptr(task)?int(m.u32(task+120u)):-1,ptr(task)?int(m.u32(task+124u)):-1,ptr(g)?m.u32(g+TaskQueueOffset+8u):0u,ptr(netobj)?m.u32(netobj+StateOffset):0u);
    return (std::uint64_t(task)<<32)^(std::uint64_t(step)<<24)^(std::uint64_t(inner)<<12)^status;
}

// One state line (L1 monitor): the state machine, its status, the network-game bytes, the local player's team and the player count.
inline void formatHostState(Mem& m,char* out,int cap,const char* tag) noexcept {
    const std::uint32_t netobj=m.u32(NetObjectWord),pm=m.u32(PlayerMgrWord);
    const std::uint32_t state=ptr(netobj)?m.u32(netobj+StateOffset):0u,sp=ptr(netobj)?m.u32(netobj+StatusPtrOffset):0u;
    const std::uint32_t status=ptr(sp)?m.u32(sp):0xFFFFFFFFu,lp=ptr(pm)?m.u32(pm+128u):0u;
    const std::uint32_t team=ptr(lp)?m.u32(lp+140u):0xFFFFFFFFu,players=ptr(pm)?m.u32(pm+PlayerCountOffset):0u;
    std::snprintf(out,std::size_t(cap),"RENDER_ONLINE_LOCAL l1 %s state=%u status=%d ae9=%d ada=%d aea=%d gip=%d local140=%d players=%u f61=%d auto=%d",
        tag,state,int(status),m.u8(AE9),m.u8(ADA),m.u8(AEA),m.u8(AEB),int(team),players,m.u8(F61),int(readAutoHost(m).predicted()));
}

// v1003 (lead l1-deadlock.md: Host's one allocation, alloc(732) through [s+0], walks a free list in sub_82374FC0): a read-only walk of
// the session allocator's 16 bins (heap+12+4*b; node: [node] == node or the game traps, [node+4] size, [node+20] next), at most 64
// nodes per bin. Logged before L0, after L0 and before Host. Flags: self-check failures, non-pointers, cycles (bounded revisit check).
inline void formatHeap(Mem& m,std::uint32_t al,char* out,int cap,const char* tag) noexcept {
    int k=std::snprintf(out,std::size_t(cap),"RENDER_ONLINE_LOCAL l1 heap %s al=%08x",tag,al);
    if(!ptr(al)){std::snprintf(out+k,std::size_t(cap-k)," not-a-pointer");return;}
    k+=std::snprintf(out+k,std::size_t(cap-k)," max=%u small=%d f8728=%d cs=%08x%08x bins",m.u32(al+216u),m.u8(al+263u),m.u8(al+8728u),
                     m.u32(al+8736u),m.u32(al+8740u));
    int bad=0;
    for(std::uint32_t b=0;b<16 && k<cap-24;++b) {
        std::uint32_t node=m.u32(al+12u+4u*b),seen[64];int n=0;const char* why=nullptr;std::uint32_t at=0;
        while(node && n<64) {
            if(!ptr(node)){why="ptr";at=node;break;}
            if(m.u32(node)!=node){why="self";at=node;break;}
            for(int i=0;i<n;++i)if(seen[i]==node){why="cycle";at=node;break;}
            if(why)break;
            seen[n++]=node;node=m.u32(node+20u);
        }
        if(n==64 && node && !why)why="cap";
        if(n || why)k+=std::snprintf(out+k,std::size_t(cap-k)," %u:%d",b,n);
        if(why && k<cap-24){k+=std::snprintf(out+k,std::size_t(cap-k),"!%s@%08x",why,at);++bad;}
    }
    if(k<cap-12)std::snprintf(out+k,std::size_t(cap-k)," bad=%d",bad);
}

// L0's preconditions, re-checked on the tick of the call. nullptr = all hold; else the first that fails (written into why).
inline const char* preOpen(Mem& m,char* why,int cap,bool spoofOn=false) noexcept {   // v1000: spoofOn (L1) accepts the signed-online byte
    const std::uint32_t netobj=m.u32(NetObjectWord),pm=m.u32(PlayerMgrWord);
    const std::uint32_t pedHolder=m.u32(PedWord),ped=ptr(pedHolder)?m.u32(pedHolder+4u):0u;
    const std::uint32_t idx=m.u32(RosIndexWord);
    if(!ptr(netobj)){std::snprintf(why,std::size_t(cap),"netobj=%08x",netobj);return why;}
    const std::uint32_t state=m.u32(netobj+StateOffset);
    if(state!=0){std::snprintf(why,std::size_t(cap),"state=%u",state);return why;}
    if(m.u8(AE9) || m.u8(ADA)){std::snprintf(why,std::size_t(cap),"ae9=%d ada=%d",m.u8(AE9),m.u8(ADA));return why;}
    if(!ptr(pm)){std::snprintf(why,std::size_t(cap),"pm=%08x",pm);return why;}
    if(m.u32(pm+128u)){std::snprintf(why,std::size_t(cap),"local=%08x",m.u32(pm+128u));return why;}
    if(!ped){std::snprintf(why,std::size_t(cap),"no-player-ped");return why;}
    if(idx>3){std::snprintf(why,std::size_t(cap),"gamer-index=%u",idx);return why;}
    if(m.u8(RosByteBase+idx)){std::snprintf(why,std::size_t(cap),"ros-creds=%d",m.u8(RosByteBase+idx));return why;}
    if(!spoofOn && m.u8(SignedOnlineByte)){std::snprintf(why,std::size_t(cap),"signed-online=%d",m.u8(SignedOnlineByte));return why;}
    return nullptr;
}
// L0's post-checks: the state OpenNetwork must leave. Returns the number of failed checks and writes every value into out.
inline int postOpen(Mem& m,char* out,int cap) noexcept {
    const std::uint32_t pm=m.u32(PlayerMgrWord);
    const std::uint32_t lp=ptr(pm)?m.u32(pm+128u):0u,mode=ptr(pm)?m.u32(pm+124u):0u,flags=ptr(pm)?m.u32(pm+764u):0u;
    const bool lpIn=ptr(pm) && lp>=pm+768u && lp<pm+8832u && (lp-(pm+768u))%448u==0;
    const std::uint32_t vt=lpIn?m.u32(lp):0u,w140=lpIn?m.u32(lp+140u):0u;const int b41=lpIn?m.u8(lp+41u):-1;
    const std::uint32_t af8=m.u32(AF8),afc=m.u32(AFC),b00=m.u32(B00);
    const int ae9=m.u8(AE9),ada=m.u8(ADA),aea=m.u8(AEA),aeb=m.u8(AEB);
    int bad=0;
    const int f764=ptr(pm)?m.u8(pm+764u):0;   // a byte: the game's lbz / stb r,764(pm) (flags shows the word for the log)
    bad+=ae9!=1;bad+=ada!=1;bad+=aea!=0;bad+=aeb!=0;bad+=mode!=7;bad+=!(f764&0x80);bad+=!lpIn;bad+=vt!=LocalPlayerVtable;
    bad+=w140!=10;bad+=b41!=255;bad+=!af8;bad+=!afc;bad+=!b00;
    std::snprintf(out,std::size_t(cap),"ae9=%d ada=%d aea=%d aeb=%d pm124=%u pm764=%08x local=%08x in_pool=%d vtable=%08x local140=%u "
        "local41=%d af8=%08x afc=%08x b00=%08x failed=%d",ae9,ada,aea,aeb,mode,flags,lp,int(lpIn),vt,w140,b41,af8,afc,b00,bad);
    return bad;
}
// E5: what the full close must leave. Returns the number of failed checks.
inline int postClose(Mem& m,char* out,int cap) noexcept {
    const std::uint32_t pm=m.u32(PlayerMgrWord),lp=ptr(pm)?m.u32(pm+128u):0u;
    const int ae9=m.u8(AE9),ada=m.u8(ADA);
    const int bad=(ae9!=0)+(ada!=0)+(lp!=0);
    std::snprintf(out,std::size_t(cap),"ae9=%d ada=%d local=%08x af8=%08x afc=%08x b00=%08x failed=%d",ae9,ada,lp,m.u32(AF8),m.u32(AFC),m.u32(B00),bad);
    return bad;
}

// The per-process sequence, ticked once per call of the state machine wrapper (main thread). open / close call the game's own
// sub_82FD9878 / sub_82FD4A08 through the wrapper (no arguments).
class Sequence {
public:
    enum class Phase : std::uint8_t {Off,E6Only,WaitStart,Open,HoldOpen,Close,HoldClosed,Reopen,HoldReopened,L1,Monitor,L1Close,HoldAfter,L3a,L3Wait,L3b,Done,Stopped};
    void configure(bool e6,bool l0,bool l1=false) noexcept {phase_=l0?Phase::WaitStart:e6?Phase::E6Only:Phase::Off;e6_=e6 || l0;l1_=l0 && l1;}
    std::uint32_t maxState() const noexcept {return maxState_;}
    void setStartDelay(std::uint32_t ms) noexcept {startDelay_=ms;}
    void setMonitorSeconds(unsigned s) noexcept {monitorSeconds_=s;}   // v1020
    // v1102 (round 5 stage C): Online mode with the route on: L1 does not host on its own schedule; maintransition's T18 ENTER / HOST hosts
    // through hostForGame() in the native's frame. The hook's thread id is recorded (the VM and the update-list hook share it, observed).
    void setDeferHost(bool on) noexcept {deferHost_=on;}
    void setHookTid(int tid) noexcept {hookTid_=tid;}
    bool awaitingGame() const noexcept {return deferHost_ && phase_==Phase::L1;}
    Phase phase() const noexcept {return phase_;}
    template<class OpenFn,class CloseFn>
    // v1001: NOT noexcept. A guest call made from here (L0 open / close, L1's ops) can raise the executor's guarded stop (StopSignal:
    // an unimplemented / rejected import, a guest trap, ...); it must unwind to the executor, which reports the structured reason.
    // t-l1-20261107-1102 (v1000): a stop inside Host hit a noexcept frame -> std::terminate (SIGABRT, no reason logged).
    void tick(Mem& m,std::uint64_t nowMs,std::uint32_t frame,OpenFn open,CloseFn close,LogFn log) {tick(m,nowMs,frame,open,close,log,nullptr);}
    template<class OpenFn,class CloseFn>
    void tick(Mem& m,std::uint64_t nowMs,std::uint32_t frame,OpenFn open,CloseFn close,LogFn log,L1Ops* ops) {
        if(phase_==Phase::Off || phase_==Phase::Done || phase_==Phase::Stopped){e6(m,nowMs,log);return;}
        if(!t0_)t0_=nowMs?nowMs:1;
        e6(m,nowMs,log);
        if(l1_)guard(m,log);   // v1000 (lead): the auto-host guard runs on every tick of an L1 run
        if(phase_==Phase::Stopped)return;
        char l[320],why[128];   // v1002: 128 (the session-allocator reason is 66 bytes)
        switch(phase_) {
        case Phase::E6Only:return;
        case Phase::WaitStart:
            if(::recomp::online_curtain::holdSession.load(std::memory_order_acquire))return;   // v1066: the first-visit creator runs
            if(nowMs-t0_<startDelay_)return;   // v1014: settable (the hook uses L1StartDelayMs with L1)
            mark("closed",frame,log);phase_=Phase::Open;
            if(l1_)heap(m,"before-l0",log);   // v1003
            return;   // (the baseline phase ran from t0)
        case Phase::Open: case Phase::Reopen: {
            const bool re=phase_==Phase::Reopen;
            if(const char* w=preOpen(m,why,sizeof why,l1_)){
                std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l0 %s refused precondition=%s (nothing called)",re?"reopen":"open",w);log(l);
                phase_=Phase::Stopped;return;}
            AutoHost before=readAutoHost(m);
            std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l0 %s before ae9=%d ada=%d local=%08x state=%u frame=%u",re?"reopen":"open",
                before.ae9,before.ada,before.local,before.state,frame);log(l);
            const std::uint32_t r=open();
            char post[256];const int bad=postOpen(m,post,sizeof post);
            std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l0 %s after ret=%u %s",re?"reopen":"open",r,post);log(l);
            if(bad){phase_=Phase::Stopped;return;}   // (left open as the game left it; the log says what differs)
            mark("open",frame,log);phase_=re?Phase::HoldReopened:l1_?Phase::L1:Phase::HoldOpen;phaseAt_=nowMs;
            if(l1_ && !re)heap(m,"after-l0",log);   // v1003
            return;
        }
        case Phase::HoldOpen:
            if(nowMs-phaseAt_>=HoldMs)phase_=Phase::Close;
            return;
        case Phase::Close: {
            std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL e5 close before ae9=%d ada=%d frame=%u",m.u8(AE9),m.u8(ADA),frame);log(l);
            close();
            char post[200];const int bad=postClose(m,post,sizeof post);
            std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL e5 close after %s",post);log(l);
            if(bad){phase_=Phase::Stopped;return;}
            mark("closed",frame,log);phase_=Phase::HoldClosed;phaseAt_=nowMs;return;
        }
        case Phase::HoldClosed:
            if(nowMs-phaseAt_>=HoldMs)phase_=Phase::Reopen;
            return;
        case Phase::L1: {
            if(deferHost_) {   // v1102: the game's T18 ENTER hosts (hostForGame); a legacy self-host only after L1DeferMaxMs, and never at T17 / T18
                const int mt=readGlobal(m,1344034u);
                if(nowMs-phaseAt_<L1DeferMaxMs || mt==17 || mt==18) {
                    if(!deferLogged_){deferLogged_=true;log("RENDER_ONLINE_LOCAL l1 waits for the game's NETWORK_SESSION_ENTER (maintransition T18); no own host in Online mode");}
                    return;
                }
                std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l1 no ENTER in %llu s, mt=%d: own host (legacy)",(unsigned long long)(L1DeferMaxMs/1000u),mt);log(l);
                deferHost_=false;
            }
            if(nowMs-phaseAt_<L1OpenHoldMs)return;
            if(l1Retries_ && nowMs<l1NextTry_)return;   // v1048: waiting out the game's own busy worker
            if(!ops){log("RENDER_ONLINE_LOCAL l1 refused precondition=no-guest-ops (nothing called)");phase_=Phase::Stopped;return;}
            if(!l1Retries_){char e8[400];formatE8(m,e8,sizeof e8);log(e8);}   // v1005: read only, before the preconditions (once)
            if(!l1Retries_){char e9[300];formatE9(m,e9,sizeof e9);log(e9);}   // v1007: HostOffline's gates, read only (once)
            if(const char* w=preHost(m,*ops,why,sizeof why)){
                // v1048 (t-online-20261107-1947, Online mode: no walk-out, so L1 met the game's own busy worker 10 s after L0 and stopped
                // for good): a worker-gate refusal (nothing written) is tried again every L1RetryMs, at most L1RetryMax times; every other
                // refusal stops as before.
                const bool worker=std::strncmp(w,"worker-gate",11)==0;
                if(worker && l1Retries_<L1RetryMax){
                    if(!l1Retries_ || l1Retries_%10==0){std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l1 waits precondition=%s (nothing written) try=%u of %u",w,l1Retries_+1,L1RetryMax);log(l);}
                    ++l1Retries_;l1NextTry_=nowMs+L1RetryMs;return;}
                std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l1 refused precondition=%s (nothing written)%s",w,worker?" retries exhausted":"");log(l);phase_=Phase::Stopped;return;}
            if(l1Retries_){std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l1 worker gate clear after %u retries",l1Retries_);log(l);}
            (void)hostOnce(m,*ops,log,nowMs,frame);   // v1102: factored (the game's ENTER uses the same steps through hostForGame)
            return;
        }
        case Phase::Monitor: {
            if(nowMs-phaseAt_<6000 && taskLines_<40) {   // v1007: the host task, every tick for 6 s, logged on change (at most 40 lines)
                char tl[300];const std::uint64_t key=formatTask(m,tl,sizeof tl,unsigned((nowMs-phaseAt_)/100));
                if(key!=lastTask_){lastTask_=key;++taskLines_;log(tl);}
            }
            if(nowMs<monitorNext_)return;
            monitorNext_+=1000;++monitorCount_;
            const std::uint32_t netobj=m.u32(NetObjectWord),st=ptr(netobj)?m.u32(netobj+StateOffset):0u;
            if(st>maxState_ && st<32)maxState_=st;
            char tag[16];std::snprintf(tag,sizeof tag,"t=%u",monitorCount_);formatHostState(m,l,sizeof l,tag);log(l);
            if(monitorCount_>=monitorSeconds_){
                std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l1 done max_state=%u gip=%d (%s)",maxState_,m.u8(AEB),
                              L1Leave?"then the clean leave":L1AbClose?"then the A/B close":"session left up; no in-run close");log(l);
                phase_=L1Leave?Phase::L3a:L1AbClose?Phase::L1Close:Phase::Done;}
            return;
        }
        // v1012 (lead after t-l1-20261107-1237, the local session at state 9 / gip 1): the in-run frame-time A/B. After the 60 s session
        // the game's own full close sub_82FD4A08 (as E5), then 30 s closed; the phase marks (closed / open / session / closed-after) cut the
        // renderer's own timing lines. Heat drift: one run, three phases in order (not alternating); read the session against both ends.
        case Phase::L1Close: {
            std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l1 ab close before ae9=%d ada=%d state=%u gip=%d frame=%u",m.u8(AE9),m.u8(ADA),
                          ptr(m.u32(NetObjectWord))?m.u32(m.u32(NetObjectWord)+StateOffset):0u,m.u8(AEB),frame);log(l);
            close();
            char post[200];const int bad=postClose(m,post,sizeof post);
            std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l1 ab close after %s gip=%d state=%u",post,m.u8(AEB),
                          ptr(m.u32(NetObjectWord))?m.u32(m.u32(NetObjectWord)+StateOffset):0u);log(l);
            (void)bad;
            mark("closed-after",frame,log);phase_=Phase::HoldAfter;phaseAt_=nowMs;monitorNext_=nowMs+5000;monitorCount_=0;return;
        }
        case Phase::HoldAfter: {
            if(nowMs<monitorNext_)return;
            monitorNext_+=5000;monitorCount_+=5;
            char tag[24];std::snprintf(tag,sizeof tag,"closed t=%u",monitorCount_);formatHostState(m,l,sizeof l,tag);log(l);
            if(leaveFlag_ || leaveLines_) {   // v1024: the leave-flag protocol's outcome
                std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l3 after t=%u g1344077=%d g1344104=%d g1344033=%d",monitorCount_,readGlobal(m,LeaveFlagGlobal),
                              readGlobal(m,BailGlobal),readGlobal(m,TransitionStateGlobal));log(l);
                if(monitorCount_>=30 && leaveFlag_) {
                    const std::uint32_t fa=globalAddr(m,LeaveFlagGlobal);
                    if(fa && ops && m.u32(fa)==1u){ops->write32(fa,0u);log("RENDER_ONLINE_LOCAL l3 leave-flag not consumed by the game: put back to 0");}
                    else log("RENDER_ONLINE_LOCAL l3 leave-flag consumed by the game (0)");
                    leaveFlag_=false;
                }
            }
            if(monitorCount_>=30){log("RENDER_ONLINE_LOCAL l1 ab done (phases: closed / open / session / closed-after)");phase_=Phase::Done;}
            return;
        }
        case Phase::L3a: {
            if(::recomp::online_curtain::armed.load(std::memory_order_acquire)) {   // v1043: cover the screen first (bounded wait)
                if(!curtainAsked_){curtainAsked_=true;curtainAt_=nowMs;::recomp::online_curtain::leaveAsked.store(true,std::memory_order_release);
                    log("RENDER_ONLINE_LOCAL l3 waits for the curtain");return;}
                const bool covered=::recomp::online_curtain::leaveCovered.load(std::memory_order_acquire);
                if(!covered && nowMs-curtainAt_<CurtainWaitMs)return;
                if(!curtainLogged_){curtainLogged_=true;std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l3 curtain covered=%d after %u ms",covered?1:0,unsigned(nowMs-curtainAt_));log(l);}
            }
            const std::uint32_t netobj=m.u32(NetObjectWord);
            if(!ptr(netobj) || !ops){log("RENDER_ONLINE_LOCAL l3 refused (no netobj / no guest ops); session left up");phase_=Phase::Done;return;}
            {   // v1024: the deliberate-leave flag (maintransition 0x4338d(0) protocol), header comment at LeaveFlagGlobal
                const std::uint32_t fa=globalAddr(m,LeaveFlagGlobal);leaveLines_=true;
                if(fa){const std::uint32_t b=m.u32(fa);if(b==0u){ops->write32(fa,1u);leaveFlag_=true;}
                    std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l3 leave-flag g1344077 before=%u after=%u addr=%08x",b,m.u32(fa),fa);log(l);}
                else log("RENDER_ONLINE_LOCAL l3 leave-flag unavailable (global block 5 not addressable; the bail alert is expected)");
            }
            const std::uint32_t st0=m.u32(netobj+StateOffset);const int voice=m.u8(netobj+VoiceByteOffset);
            const std::uint32_t r=ops->endSession(netobj);
            const std::uint32_t sp=m.u32(netobj+StatusPtrOffset);
            std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l3 leave sub_8305C420 ret=%u state %u -> %u status=%d voice1acd9=%d gip=%d frame=%u",r,st0,
                          m.u32(netobj+StateOffset),ptr(sp)?int(m.u32(sp)):-1,voice,m.u8(AEB),frame);log(l);
            mark("leaving",frame,log);phase_=Phase::L3Wait;phaseAt_=nowMs;lastState_=m.u32(netobj+StateOffset);return;
        }
        case Phase::L3Wait: {
            const std::uint32_t netobj=m.u32(NetObjectWord),st=ptr(netobj)?m.u32(netobj+StateOffset):0u;
            if(st!=lastState_){lastState_=st;char tag[40];std::snprintf(tag,sizeof tag,"leave state-change t=%u",unsigned((nowMs-phaseAt_)/100));
                formatHostState(m,l,sizeof l,tag);log(l);}
            if(st==0){phase_=Phase::L3b;return;}
            if(nowMs-phaseAt_>=L3WaitMs){formatHostState(m,l,sizeof l,"leave timeout (state not 0 in 20 s; nothing else called)");log(l);phase_=Phase::Done;}
            return;
        }
        case Phase::L3b: {
            const std::uint32_t netobj=m.u32(NetObjectWord);
            const int voice=m.u8(netobj+VoiceByteOffset);
            ops->netShutdown(netobj);
            const bool adaBefore=m.u8(ADA)!=0,guard=m.u32(netobj+StateOffset)==0 && !voice;
            if(adaBefore && guard)close();
            const std::uint32_t r=ops->netInit(netobj);
            std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l3 teardown netobj-shutdown ok close=%s netobj-init ret=%u state=%u ae9=%d ada=%d gip=%d voice1acd9=%d",
                          adaBefore?(guard?"called":"skipped-guard"):"not-needed (ADA 0)",r,m.u32(netobj+StateOffset),m.u8(AE9),m.u8(ADA),m.u8(AEB),voice);log(l);
            mark("closed-after",frame,log);phase_=Phase::HoldAfter;phaseAt_=nowMs;monitorNext_=nowMs+5000;monitorCount_=0;return;
        }
        case Phase::HoldReopened:
            if(nowMs-phaseAt_>=HoldMs){log("RENDER_ONLINE_LOCAL l0 done (opened, closed, re-opened; left open)");phase_=Phase::Done;}
            return;
        default:return;
        }
    }
public:
    // v1102 (round 5 stage C): maintransition's T18 NETWORK_SESSION_ENTER / HOST, in the native's frame. Refused (0, nothing written, each
    // reason logged once) unless, in order: L1 waits for the game (awaitingGame), the L0 open hold is over, the caller is on the update-list
    // hook's thread, CAN_ENTER's own gate sub_83039500 passes, and L1's preconditions hold. Then the same host steps as L1's own tick; a second
    // call after a success returns 0 (the phase is Monitor).
    std::uint32_t hostForGame(Mem& m,L1Ops& ops,LogFn log,std::uint64_t nowMs,std::uint32_t frame,std::uint32_t ip,int tid) {
        char why[128];const char* w=nullptr;
        if(!awaitingGame())w="not-awaiting";
        else if(nowMs-phaseAt_<L1OpenHoldMs)w="l0-hold";
        else if(tid!=hookTid_)w="thread";
        else if(!ops.canEnter())w="can-enter";
        else w=preHost(m,ops,why,sizeof why);
        if(w){refuse(w,ip,tid,log);return 0;}
        {char e8[400];formatE8(m,e8,sizeof e8);log(e8);}   // read only, once (the first success is the only host)
        {char e9[300];formatE9(m,e9,sizeof e9);log(e9);}
        char l[160];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l1 host by maintransition ENTER ip=%x",ip);log(l);
        return hostOnce(m,ops,log,nowMs,frame);
    }
private:
    std::uint32_t hostOnce(Mem& m,L1Ops& ops,LogFn log,std::uint64_t nowMs,std::uint32_t frame) {
        char l[320];
        formatHostState(m,l,sizeof l,"before");log(l);
        heap(m,"before-host",log);   // v1003 (after the preconditions, before any write)
        const std::uint32_t netobj=m.u32(NetObjectWord),cfg=netobj+CfgOffset,status=m.u32(netobj+StatusPtrOffset),main=m.u32(netobj+MainSession);
        ops.write32(netobj+ModeFlagOffset,L1Mode==0?1u:0u);ops.write32(ModeWord,L1Mode);
        ops.config(cfg,L1Slots,L1Priv);
        ops.write32(netobj+ClearOffset,0);
        ops.zeroAtomic(status);
        const std::uint32_t r=ops.host(main+SessionInContainer,cfg,L1Mode,L1Slots,L1Priv,L1HostFlags,status);   // v1005: the snSession X+8
        if(r==1){ops.write32(netobj+StateOffset,2);ops.write32(netobj+StampOffset,ops.now()|1u);}
        char tag[48];std::snprintf(tag,sizeof tag,"host ret=%u mode=%u slots=%u priv=%u flags=%u",r,L1Mode,L1Slots,L1Priv,L1HostFlags);
        formatHostState(m,l,sizeof l,tag);log(l);
        if(r!=1){phase_=Phase::Stopped;return r;}
        mark("session",frame,log);phase_=Phase::Monitor;phaseAt_=nowMs;monitorNext_=nowMs+1000;monitorCount_=0;maxState_=2;
        {char tl[300];lastTask_=formatTask(m,tl,sizeof tl,0);++taskLines_;log(tl);}   // v1007
        return r;
    }
    void refuse(const char* w,std::uint32_t ip,int tid,LogFn log) noexcept {
        std::uint32_t h=2166136261u;for(const char* p=w;*p && *p!=' ';++p)h=(h^std::uint8_t(*p))*16777619u;   // the reason's first word
        for(unsigned i=0;i<refusedN_;++i)if(refused_[i]==h)return;
        if(refusedN_<16)refused_[refusedN_++]=h;
        char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l1 host by maintransition refused=%s ip=%x tid=%d hook_tid=%d (nothing written)",w,ip,tid,hookTid_);log(l);
    }
    bool deferHost_=false,deferLogged_=false;int hookTid_=-1;std::uint32_t refused_[16]{};unsigned refusedN_=0;   // v1102
    // v1024: a script global through the VM's block table (the same table activities.h readGlobal uses); 0 / -1 = not addressable.
    static std::uint32_t globalAddr(Mem& m,std::uint32_t gl) noexcept {
        const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,base=m.u32(GlobalTable+4u*b),size=m.u32(GlobalSizes+4u*b);
        return (ptr(base) && w<size)?base+4u*w:0u;
    }
    static int readGlobal(Mem& m,std::uint32_t gl) noexcept {const std::uint32_t a=globalAddr(m,gl);return a?int(m.u32(a)):-1;}
    void e6(Mem& m,std::uint64_t nowMs,LogFn log) noexcept {
        if(!e6_ || e6Count_>=E6Seconds)return;
        if(!e6Next_)e6Next_=nowMs;
        if(nowMs<e6Next_)return;
        e6Next_+=1000;
        char l[320];formatAutoHost(readAutoHost(m),l,sizeof l,e6Count_++);log(l);
    }
    // v1000 (lead 10:4x: "abort L1 if predicted_autohost ever goes to 1"). Before Host: the run stops (no L0 / no L1 after it). After
    // Host the prediction keys off the main session L1 itself made active (sub_824F5588: s+2320 in {2,3}), so it is EXPECTED to go
    // to 1 there; nothing can be undone without a teardown (L3), so the first 1 after Host is logged once with every input and the
    // monitor goes on (each state line also carries auto=).
    void guard(Mem& m,LogFn log) noexcept {
        const AutoHost h=readAutoHost(m);
        if(!h.predicted())return;
        char l[320],a[300];formatAutoHost(h,a,sizeof a,0);
        const bool beforeHost=phase_==Phase::WaitStart || phase_==Phase::Open || phase_==Phase::L1;   // v1012: L1Close / HoldAfter are after Host
        if(beforeHost) {
            std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l1 aborted guard=predicted_autohost before-host phase=%d | %s",int(phase_),a+20);log(l);
            phase_=Phase::Stopped;return;
        }
        if(!guardLogged_){guardLogged_=true;std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l1 guard predicted_autohost=1 after-host t=%u | %s",monitorCount_,a+20);log(l);}
    }
    static void heap(Mem& m,const char* tag,LogFn log) noexcept {
        const std::uint32_t netobj=m.u32(NetObjectWord),s=ptr(netobj)?m.u32(netobj+MainSession):0u,al=ptr(s)?m.u32(s+SessionInContainer):0u;   // v1005: X+8
        char l[400];formatHeap(m,al,l,sizeof l,tag);log(l);
    }
    static void mark(const char* phase,std::uint32_t frame,LogFn log) noexcept {
        char l[96];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL l0 phase=%s frame=%u",phase,frame);log(l);
    }
    unsigned monitorSeconds_=L1MonitorSeconds;   // v1020
    bool curtainAsked_=false,curtainLogged_=false;std::uint64_t curtainAt_=0;   // v1043
    std::uint32_t l1Retries_=0;std::uint64_t l1NextTry_=0;   // v1048
    bool leaveFlag_=false,leaveLines_=false;     // v1024: L3 wrote the deliberate-leave flag (and still owns it)
    std::uint32_t startDelay_=StartDelayMs,lastState_=0;Phase phase_=Phase::Off;bool e6_=false,l1_=false,guardLogged_=false;std::uint64_t lastTask_=~0ull;unsigned taskLines_=0;std::uint64_t monitorNext_=0;unsigned monitorCount_=0;std::uint32_t maxState_=0;unsigned e6Count_=0;std::uint64_t e6Next_=0,t0_=0,phaseAt_=0;
};

}  // namespace recomp::online_l0
