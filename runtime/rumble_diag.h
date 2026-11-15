#pragma once
// rumble_diag.h (v953, Update 1 W1-5, diagnostic attempt A; FT E-H1 retry rate + E-H3 motor values): LOG ONLY. Counts the game's
// vibration requests per call path of the XInput wrapper sub_8363DA18 (r4 = &XINPUT_VIBRATION: u16 left, u16 right, big-endian), the
// import's result, and a histogram of (left, right). Pure (rumble_diag.inc reads the guest; rumble_diag_test.cpp tests this).
// Paths (return addresses, v855 lineage; review C7):
//   0x832311D0 sub_83230E68 first site: the pad update's non-zero speeds; +48 (dirty) cleared only on success -> retried each update
//   0x83231204 sub_83230E68 second site: the stop / zero send; +48 cleared unconditionally
//   0x83226378 sub_832262D8 (called from three places): its own send, same retry shape (+48 = 1 on failure)
#include <array>
#include <cstdint>
#include <cstdio>
#include <mutex>

namespace recomp::rumblediag {

enum Path : int {UpdateSpeeds=0,UpdateStop=1,OtherSender=2,Unknown=3,PathCount=4};
inline Path pathOf(std::uint32_t lr) noexcept {
    return lr==0x832311D0u?UpdateSpeeds:lr==0x83231204u?UpdateStop:lr==0x83226378u?OtherSender:Unknown;
}
inline const char* pathName(Path p) noexcept {static const char* const n[]={"update","stop","sub_832262D8","other"};return n[p];}
constexpr int Bins=4;   // per motor: 0, 1..0x3FFF, 0x4000..0xBFFF, 0xC000..0xFFFF (zero, light, medium, strong)
inline int bin(std::uint16_t v) noexcept {return v==0?0:v<0x4000?1:v<0xC000?2:3;}

class Counter {
public:
    using LogFn=void(*)(const char*);
    explicit Counter(LogFn log) noexcept:log_(log) {}
    // One wrapper call: the values sent (as the game set them, before the wrapper's old-kernel swap) and the import's result.
    void call(std::uint32_t lr,std::uint16_t left,std::uint16_t right,std::uint32_t result) noexcept {
        const Path p=pathOf(lr);
        std::lock_guard l(m_);
        auto& c=paths_[p];
        ++c.calls;
        if(left||right)++c.nonzero;
        if(!c.seen || c.left!=left || c.right!=right)++c.changes;
        c.seen=true;c.left=left;c.right=right;
        if(result)++c.failed;
        ++hist_[std::size_t(bin(left)*Bins+bin(right))];
    }
    // The window's line (FT: one per ~600 presents; the glue calls it every 20 s while calls arrive), then the window restarts.
    void flush(const char* why) noexcept {
        char line[512];int n=0;
        {std::lock_guard l(m_);
        n=std::snprintf(line,sizeof line,"RENDER_DIAG_RUMBLE window=%s",why);
        for(int p=0;p<PathCount;++p) {
            const auto& c=paths_[std::size_t(p)];
            if(!c.calls)continue;
            n+=std::snprintf(line+n,sizeof line-std::size_t(n)," %s=calls:%u,nonzero:%u,changes:%u,failed:%u",pathName(Path(p)),c.calls,c.nonzero,
                             c.changes,c.failed);
        }
        n+=std::snprintf(line+n,sizeof line-std::size_t(n)," hist(left,right)=");
        for(int i=0;i<Bins*Bins;++i)if(hist_[std::size_t(i)])n+=std::snprintf(line+n,sizeof line-std::size_t(n),"%d%d:%u;",i/Bins,i%Bins,hist_[std::size_t(i)]);
        for(auto& c:paths_){c.calls=c.nonzero=c.changes=c.failed=0;}   // (the last values stay: a change is against the previous call)
        hist_.fill(0);}
        if(log_)log_(line);
    }
private:
    struct PathCount_ {unsigned calls=0,nonzero=0,changes=0,failed=0;bool seen=false;std::uint16_t left=0,right=0;};
    LogFn log_;std::mutex m_;std::array<PathCount_,PathCount> paths_{};std::array<unsigned,Bins*Bins> hist_{};
};

} // namespace recomp::rumblediag
