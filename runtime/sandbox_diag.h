#pragma once
// sandbox_diag.h (v953, Update 1 W1-5, diagnostic attempt A; SB E1 + review E-R1): LOG ONLY. Who starts which script, through which
// path. Pure: no guest memory, no platform calls (sandbox_launch.inc reads the guest and calls in; sandbox_diag_test.cpp tests this).
//
// The thread start sub_82A9B020 has three callers (review V-6; return addresses in the v855 lineage's code):
//   0x82AAC618 sub_82AAC528, the start-script implementation (START_NEW_SCRIPT's function pointer at 0x8386AA50, and a code path)
//   0x825A7718 sub_825A76B8 (program from an object field +28, the id stored at +36)
//   0x829A0CD8 sub_829A0C68 (a state check +76 == 1, a name lookup, then the start)
// All three pass the script name as a C string in r7. sub_82AAC528's own callers:
//   0x8343B928 START_NEW_SCRIPT (sub_8343B8E0), 0x8343BA14 START_NEW_SCRIPT_WITH_ARGS (sub_8343B9C8), 0x83053560 sub_83053420.
// The parent = the script running when the start happens: the current-thread global u32 0x83C5CA24 (sub_825453F8), name at +104.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace recomp::sandboxdiag {

enum Caller : int {StartScript=0,ObjectStarter=1,LookupStarter=2,OtherCaller=3,CallerCount=4};
enum Via : int {ViaNone=0,StartNewScript=1,StartNewScriptWithArgs=2,CodePath=3,ViaOther=4};
constexpr std::uint32_t CurrentThread=0x83C5CA24u,ThreadName=104;
inline Caller callerOf(std::uint32_t lr) noexcept {
    return lr==0x82AAC618u?StartScript:lr==0x825A7718u?ObjectStarter:lr==0x829A0CD8u?LookupStarter:OtherCaller;
}
inline Via viaOf(std::uint32_t lr) noexcept {
    return lr==0?ViaNone:lr==0x8343B928u?StartNewScript:lr==0x8343BA14u?StartNewScriptWithArgs:lr==0x83053560u?CodePath:ViaOther;
}
inline const char* callerName(Caller c) noexcept {
    static const char* const n[]={"start_script","sub_825A76B8","sub_829A0C68","other"};return n[c];
}
inline const char* viaName(Via v) noexcept {
    static const char* const n[]={"-","START_NEW_SCRIPT","START_NEW_SCRIPT_WITH_ARGS","sub_83053420","other"};return n[v];
}
// A guest C string: at most 63 printable bytes and a terminator within 64, else "?" (nothing of the guest is trusted).
template<class Read> void name(Read read,std::uint32_t at,char (&out)[64]) noexcept {
    out[0]='?';out[1]=0;
    if(at<0x10000u || at>0xFFFF0000u)return;
    char b[64];
    for(int i=0;i<64;++i) {
        const auto c=static_cast<unsigned char>(read(at+std::uint32_t(i)));
        if(!c){if(!i)return;b[i]=0;std::memcpy(out,b,std::size_t(i)+1);return;}
        if(c<32 || c>126)return;
        b[i]=char(c);
    }
}

// One record per distinct (name, caller, via, parent); the first sighting is logged, later ones are counted for the summary.
class Recorder {
public:
    using LogFn=void(*)(const char*);
    static constexpr std::size_t Slots=1024;
    explicit Recorder(LogFn log) noexcept:log_(log) {}
    void hook(const char* fn) noexcept {char l[96];std::snprintf(l,sizeof l,"RENDER_SANDBOX hook=1 fn=%s mode=log-only",fn);if(log_)log_(l);}
    // A start that returned `id` (0 = the start failed).
    void launch(const char* script,std::uint32_t lr,std::uint32_t outerLr,const char* parent,std::uint32_t id) noexcept {
        const Caller c=callerOf(lr);const Via v=c==StartScript?viaOf(outerLr):ViaNone;
        std::lock_guard l(m_);
        ++total_;++byCaller_[c];if(!id)++failed_;
        const std::uint32_t k=key(script,c,v,parent);
        std::size_t at=k%Slots;
        for(std::size_t probe=0;probe<Slots;++probe,at=(at+1)%Slots) {
            if(slots_[at].key==k && slots_[at].used){++slots_[at].count;return;}
            if(!slots_[at].used)break;
        }
        if(slots_[at].used){++dropped_;return;}   // (table full: counted, not logged)
        slots_[at]={k,1,true};++distinct_;
        char line[320];
        std::snprintf(line,sizeof line,"RENDER_SANDBOX launch name=%s caller=%s lr=%08x via=%s parent=%s id=%u",script,callerName(c),lr,
                      viaName(v),parent,id);
        if(log_)log_(line);
    }
    void summary() noexcept {
        char line[200];
        {std::lock_guard l(m_);
        std::snprintf(line,sizeof line,"RENDER_SANDBOX summary launches=%llu distinct=%u failed=%llu start_script=%llu sub_825A76B8=%llu "
                      "sub_829A0C68=%llu other=%llu unlogged=%llu",total_,distinct_,failed_,byCaller_[0],byCaller_[1],byCaller_[2],byCaller_[3],
                      dropped_);}
        if(log_)log_(line);
    }
    unsigned long long total() const noexcept {std::lock_guard l(m_);return total_;}
private:
    static std::uint32_t key(const char* s,Caller c,Via v,const char* p) noexcept {
        std::uint32_t h=2166136261u;
        const auto mix=[&](unsigned char x){h^=x;h*=16777619u;};
        for(;*s;++s)mix(static_cast<unsigned char>(*s));
        mix(0xFF);mix(static_cast<unsigned char>(c));mix(static_cast<unsigned char>(v));
        for(;*p;++p)mix(static_cast<unsigned char>(*p));
        return h;
    }
    struct Slot {std::uint32_t key=0;unsigned count=0;bool used=false;};
    LogFn log_;mutable std::mutex m_;std::array<Slot,Slots> slots_{};
    unsigned long long total_=0,failed_=0,dropped_=0;std::array<unsigned long long,CallerCount> byCaller_{};unsigned distinct_=0;
};

} // namespace recomp::sandboxdiag
