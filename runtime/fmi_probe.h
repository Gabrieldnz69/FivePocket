#pragma once
// fmi_probe.h (v1048, trainer-mods; lead 2026-11-07: what fm_intro waits for at the drop-off, the seam to Lamar's tutorial race). Read only,
// developer switch files/private-trace/diag-fmi-probe, local session only. Decoded from the disc's fm_intro (intro-cutscene lane work/):
//  - client record static 1733[participant] (5 words): f0 stage (0 init, 2 running, 5 -> 3 -> 6 end), f2 sub-stage (-2, -1, 0 = the intro
//    proper 0xe32, 1/2 = nothing, 3 = 0xa33: GIVE_WEAPON pistol + flags bit 2 = the end, 99), f3 flags. Server static 1561: stage 0 -> 2 -> 6
//    (6 once any client set flags bit 2).
//  - after the drop-off, 0xe32 waits for IS_SCREEN_FADED_IN, then asks for Lamar's phone call "FM_ICALL" through 0x212e every frame until
//    it is accepted. 0x212e refuses while G1349396 f2 is set or 0x40a1 says so (0x40f3), or while G1348646 f1 is set (0x40e5); the request
//    itself goes into the comms block G1349208 (f40 = request in use, read by 0x400d), which some controller (freemode / social_controller
//    include the same library; neither runs in our session) takes and plays.
//  - the end path also reads the packed stat-bit cache G80929 + 1015 (element n at +1016+n): bit 6 of n = 28..38 (0x17a9) and bit 5 of n =
//    33 (0xaa3(1130)).
// Logged every EveryFrames while the session is up, only when something changed (and once at the start): the two gates, G1349208 words
// 0..Comms-1, the stat bits, G19455, and IS_MOBILE_PHONE_CALL_ONGOING / IS_SCRIPTED_CONVERSATION_ONGOING / the number of threads running
// social_controller and freemode. fm_intro's own statics are not readable here (no thread accessor yet): unknown, stated in each line.
#include "trainer.h"
#include "local_session_flag.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace recomp::fmi_probe {
namespace n {
using recomp::trainer::joaat;
constexpr std::uint32_t CallOngoing=joaat("IS_MOBILE_PHONE_CALL_ONGOING"),ConvOngoing=joaat("IS_SCRIPTED_CONVERSATION_ONGOING"),
    Threads=joaat("GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH"),NetTime=joaat("GET_NETWORK_TIME");   // v1078: + NetTime
}
inline constexpr std::uint32_t UsedNatives[]{n::CallOngoing,n::ConvOngoing,n::Threads,n::NetTime};
constexpr std::uint32_t EveryFrames=90,Comms=1349208u,GateA=1349396u,GateB=1348646u,StatCache=80929u+1015u,G19455=19455u;
constexpr int CommsWords=48;
// v1078 (lead 01:10, the P2 seam; fm_intro decode 2026-11-08): the FM_ICALL request 0x212e refuses at its entry when 0x40f3 says so:
// G1349396 f2, OR 0x410c = 0x40a1 = 0x3c24: the phone state G10433 f1 (G10434) == 0 or 1 (v1048's comment named 0x40a1 but logged only
// G1349396 f2; G10434 was never read). Inside, 0x404c refuses again on 0x40a1, on comms f40 == 3 (0x1ccc), on 0x1cfe(0) (cellphone_flashhand
// running, or G10434 > 3), and, unless bit 6 of the request flags, on the cooldown IS_TIME_LESS_THAN(GET_NETWORK_TIME, G1362596) (0x408d).
// fm_intro disables the phone in sub-stage -2 (0x479c -> 0x7ab8(1): G10434 = 1) and re-enables it (0x7ab8(0): 1 -> 3) from 0x77e8 / 0xc8ea /
// 0xf98f. cellphone_controller writes 3 at its start. The phone line logs G10434, the cooldown, comms words 48..54 and the phone scripts.
constexpr std::uint32_t PhoneState=10433u+1u,Cooldown=1362596u;
using LogFn=void(*)(const char*);
class Probe {
public:
    explicit Probe(LogFn log) noexcept:log_(log) {}
    void tick(recomp::trainer::Natives& nat,std::uint32_t frame,bool armed) noexcept {
        if(!armed || !log_ || !::recomp::local_session::active())return;
        if(started_ && frame-last_<EveryFrames)return;
        last_=frame;
        int v[6+CommsWords+12]{};int k=0;
        v[k++]=readGlobal(nat,GateA+2u);v[k++]=readGlobal(nat,GateB+1u);v[k++]=readGlobal(nat,G19455);
        v[k++]=one(nat,n::CallOngoing,{});v[k++]=one(nat,n::ConvOngoing,{});
        v[k++]=one(nat,n::Threads,{recomp::trainer::joaat("social_controller")})*100+one(nat,n::Threads,{recomp::trainer::joaat("freemode")});
        for(int i=0;i<CommsWords;++i)v[k++]=readGlobal(nat,Comms+std::uint32_t(i));
        for(int s=28;s<=39;++s)v[k++]=readGlobal(nat,StatCache+1u+std::uint32_t(s));
        // v1078: the phone line's values (the network time itself is not compared: only whether the cooldown is still running)
        const int netNow=one(nat,n::NetTime,{}),cool=readGlobal(nat,Cooldown);
        int ph[4+7]{readGlobal(nat,PhoneState),int(std::int32_t(std::uint32_t(netNow)-std::uint32_t(cool))<0),
                    one(nat,n::Threads,{recomp::trainer::joaat("cellphone_controller")})*100+one(nat,n::Threads,{recomp::trainer::joaat("cellphone_flashhand")}),
                    one(nat,n::Threads,{recomp::trainer::joaat("dialogue_handler")})};
        for(int i=0;i<7;++i)ph[4+i]=readGlobal(nat,Comms+48u+std::uint32_t(i));
        if(started_ && std::memcmp(v,prev_,sizeof v)==0 && std::memcmp(ph,prevPh_,sizeof ph)==0)return;
        started_=true;std::memcpy(prev_,v,sizeof v);std::memcpy(prevPh_,ph,sizeof ph);
        char l[700];int p=std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL fmi-probe frame=%u gate1349396.f2=%d gate1348646.f1=%d g19455=%d call=%d conv=%d threads(social*100+freemode)=%d statics=unread comms",
                                        frame,v[0],v[1],v[2],v[3],v[4],v[5]);
        for(int i=0;i<CommsWords && p>0 && p<int(sizeof l)-14;++i)p+=std::snprintf(l+p,sizeof l-std::size_t(p)," %d",v[6+i]);
        log_(l);
        p=std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL fmi-probe frame=%u statbits n28..39",frame);
        for(int i=0;i<12 && p>0 && p<int(sizeof l)-14;++i)p+=std::snprintf(l+p,sizeof l-std::size_t(p)," %08x",unsigned(v[6+CommsWords+i]));
        log_(l);
        p=std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL fmi-probe frame=%u phone g10434=%d cooldown_running=%d (net=%d g1362596=%d) threads(cellphone_controller*100+flashhand)=%d dialogue_handler=%d comms48..54",
                        frame,ph[0],ph[1],netNow,cool,ph[2],ph[3]);
        for(int i=0;i<7 && p>0 && p<int(sizeof l)-14;++i)p+=std::snprintf(l+p,sizeof l-std::size_t(p)," %d",ph[4+i]);
        log_(l);
    }
private:
    static int one(recomp::trainer::Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};return nat.call(h,a.begin(),unsigned(a.size()),out)?int(out[0]):-1;}
    static int readGlobal(recomp::trainer::Natives& nat,std::uint32_t gl) noexcept {   // the VM's block table (as shop_probe.h / activities.h)
        const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,base=nat.load(0x83C5CE48u+4u*b),size=nat.load(0x83C5CF50u+4u*b);
        return (base && !(base&3u) && w<size)?int(nat.load(base+4u*w)):-1;
    }
    LogFn log_;bool started_=false;std::uint32_t last_=0;int prev_[6+CommsWords+12]{};int prevPh_[11]{};
};
}
