#pragma once
// online_corona_probe.h (v1117, trainer-mods; read only). OBSERVED t-onlroute-ugc-2106 (v1116): after the solo transition answers fmmc_launcher
// read NETWORK_IS_TRANSITION_HOST -> 1 and then called no other transition native; the job intro camera held on Lamar's group. fmmc's corona state
// is G1573860[PLAYER_ID].f165 (setter 0x1279b, getter 0x79d78; the main switch 0x1736b has states 0..27: 4 = matchmaking (QUICKMATCH 0x177e6),
// 27 = 0x18ca3). This probe logs that word for players 0 and 1 on change, every 15 frames, memory only (the VM's block table, as the transition
// probe): it names the state the corona waits in. Pure parts for the host test.
// v1131 (read only; v1130's race run: state 6 held with the 195/25 event raised): state 6's host branch (fmmc 0x17c49..0x17c56) needs
// G1341588.f62 (main_persistent 0x1a67b on event 195 sub-code 25) and IS_BIT_SET(G1573860[PLAYER_ID].f121, 0) (freemode's tutorial race trigger
// 0x611fe: SET at 0x61e71 within 3 m of the trigger, 0x61472; CLEAR at 0x61d7c). Logged with the rest: f62, f63, f121 of players 0 and 1.
// v1118 (t-onlroute-ugc-2130, v1117: states 0 -> 4 -> 6, then 6 for good): state 6 (0x17b21) reads G1595823.f3 (0x71231) and .f4 (0x6e039), and
// every participant's G1573860[p].f15.f20 bit 14 (0x6e0ac / 0x6e047); 0x7123f reads G1341588 bit 0. Those words are logged too (on change).
#include <cstdint>
#include <cstdio>
namespace recomp::corona_probe {
constexpr std::uint32_t StateArray=1573860u,EntryWords=257u,StateOffset=165u,Every=15u;
constexpr std::uint32_t Broadcast=1595823u,Flags=1341588u,ReadyOffset=15u+20u;
constexpr std::uint32_t TrigOffset=121u;   // v1131: G1573860[p].f121 (bit 0 = at the tutorial race trigger)   // G1595823.f0.., G1341588, G1573860[p].f15.f20
// the global index of G1573860[player].f165 (an array global: its size word first)
constexpr std::uint32_t stateIndex(std::uint32_t player) noexcept {return StateArray+1u+player*EntryWords+StateOffset;}
class Probe {
public:
    template<class N,class Log> void tick(N& nat,std::uint32_t frame,Log log) noexcept {
        if(frame-last_<Every && started_)return;
        last_=frame;
        const int a=glob(nat,stateIndex(0)),b=glob(nat,stateIndex(1));
        int w[16];for(unsigned i=0;i<9u;++i)w[i]=glob(nat,Broadcast+i);w[9]=glob(nat,Flags);w[10]=glob(nat,StateArray+1u+ReadyOffset);w[11]=glob(nat,StateArray+1u+EntryWords+ReadyOffset);
        w[12]=glob(nat,Flags+62u);w[13]=glob(nat,Flags+63u);w[14]=glob(nat,StateArray+1u+TrigOffset);w[15]=glob(nat,StateArray+1u+EntryWords+TrigOffset);   // v1131
        bool same=started_ && a==p0_ && b==p1_;for(unsigned i=0;i<16u && same;++i)same=w[i]==w_[i];
        if(same)return;
        started_=true;p0_=a;p1_=b;for(unsigned i=0;i<16u;++i)w_[i]=w[i];
        char l[400];std::snprintf(l,sizeof l,"RENDER_ONLINE_CORONA state p0=%d p1=%d frame=%u (fmmc G1573860[p].f165) g1595823=%d,%d,%d,%d,%d,%d,%d,%d,%d g1341588=%08x ready20=%08x,%08x f62=%d f63=%d f121=%08x,%08x",
            a,b,frame,w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7],w[8],unsigned(w[9]),unsigned(w[10]),unsigned(w[11]),w[12],w[13],unsigned(w[14]),unsigned(w[15]));log(l);
    }
private:
    template<class N> static int glob(N& nat,std::uint32_t gl) noexcept {
        const std::uint32_t blk=gl>>18,w=gl&0x3FFFFu,base=nat.load(0x83C5CE48u+4u*blk),size=nat.load(0x83C5CF50u+4u*blk);
        return (base && !(base&3u) && w<size)?int(nat.load(base+4u*w)):-1;
    }
    bool started_=false;std::uint32_t last_=0;int p0_=-2,p1_=-2,w_[16]{};
};
}
