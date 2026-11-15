#pragma once
// online_ugc_offline.h (v1110, trainer-mods; lead: "Lamar's race: no corona showed and nothing launched"). The freemode route takes the game's
// own OFFLINE UGC path (v1096: NETWORK_IS_CLOUD_AVAILABLE 0 at maintransition 0x65ccc): UGC_SET_QUERY_DATA_FROM_OFFLINE(1) and
// UGC_LOAD_OFFLINE_QUERY(1) = sub_82FCF2E0(ugc manager, 1), which opens "common:/data/ugc/UGC_rstar.ugc" (name table 0x8383E8A4[1] = "rstar"),
// but UGC_GET_CONTENT_NUM read 0 in every route run: no Rockstar jobs -> no corona at the trigger.
// OBSERVED (code): this exe's parser reads [count][header 23,128,64,64,64] then per record id[23] int path[128] name[64] (len,desc) (len,data)
// u64 uid[64] uname[64] (little-endian ints, byte-swapped by the stream). OBSERVED (file, common.rpf data/ugc/ugc_rstar.ugc, 305564 bytes):
// [version=1][count=366][the same header] then per record id[23] int name[64] (len,desc) (len,data) u64 uid[64] uname[64] npaths paths[npaths][128]
// - a newer layout: the exe reads version 1 as the count, then 366 != 23 fails its header check and the list stays empty. Verified on the host
// over all 366 records (ends exactly at the last byte; every path holds its id; every data JSON parses). Its race "Mall Or Nothing" starts at
// -210.3399,307.0503,95.9465 = freemode's trigger.
// v1110: parse the v1 layout into exactly the records the exe builds (offsets fixed by its getters: ID +0, the int +24, path +28 (the getter's
// offline branch returns a static buffer, so unused), NAME +156, DESCRIPTION +220 (atString), data +248 (atString), u64 +280, USER_ID +288,
// user name +352; 416 bytes). Anything that is not a complete v1 list -> the original parser runs. Pure: the parse, for the host test.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
namespace recomp::ugc_offline {
constexpr std::uint32_t Version=1,IdLen=23,PathLen=128,NameLen=64,UserLen=64,UnameLen=64;
constexpr std::uint32_t MaxCount=4096,MaxText=1u<<20,MaxPaths=8;
constexpr std::uint32_t RecordSize=416,OffId=0,OffInt=24,OffPath=28,OffName=156,OffDesc=220,OffData=248,OffU64=280,OffUid=288,OffUname=352;
constexpr std::uint32_t RstarKind=1;   // UGC_LOAD_OFFLINE_QUERY's argument for "rstar" (the only list on the disc)
constexpr char RstarPath[]="common:/data/ugc/UGC_rstar.ugc";
struct Rec {
    const std::uint8_t* id;std::int32_t a;const std::uint8_t* name;const std::uint8_t* desc;std::uint32_t descLen;
    const std::uint8_t* data;std::uint32_t dataLen;std::uint64_t u64;const std::uint8_t* uid;const std::uint8_t* uname;std::uint32_t npaths;
    const std::uint8_t* path0;
};
struct Reader {
    const std::uint8_t* p;std::size_t n,o=0;bool bad=false;
    bool has(std::size_t k) const noexcept {return !bad && k<=n && o<=n-k;}
    std::uint32_t u32() noexcept {if(!has(4)){bad=true;return 0;}std::uint32_t v=std::uint32_t(p[o])|std::uint32_t(p[o+1])<<8|std::uint32_t(p[o+2])<<16|std::uint32_t(p[o+3])<<24;o+=4;return v;}
    std::uint64_t u64() noexcept {const std::uint64_t lo=u32(),hi=u32();return lo|hi<<32;}
    const std::uint8_t* take(std::size_t k) noexcept {if(!has(k)){bad=true;return nullptr;}const std::uint8_t* r=p+o;o+=k;return r;}
};
// The v1 header check (the exe's own five sizes after version and count).
inline bool isV1(const std::uint8_t* p,std::size_t n) noexcept {
    Reader r{p,n};const std::uint32_t v=r.u32(),c=r.u32();
    const std::uint32_t h[5]={r.u32(),r.u32(),r.u32(),r.u32(),r.u32()};
    return !r.bad && v==Version && c>=1 && c<=MaxCount && h[0]==IdLen && h[1]==PathLen && h[2]==NameLen && h[3]==UserLen && h[4]==UnameLen;
}
// Parse a complete v1 list; false (out cleared, why set) unless every record is in bounds, each text ends in its NUL and the list ends exactly
// at the last byte.
inline bool parseV1(const std::uint8_t* p,std::size_t n,std::vector<Rec>& out,const char*& why) {
    out.clear();why="";
    if(!isV1(p,n)){why="not the v1 layout (version 1 + header 23,128,64,64,64)";return false;}
    Reader r{p,n};r.u32();const std::uint32_t count=r.u32();for(int i=0;i<5;++i)r.u32();
    out.reserve(count);
    for(std::uint32_t k=0;k<count;++k) {
        Rec e{};e.id=r.take(IdLen);e.a=std::int32_t(r.u32());e.name=r.take(NameLen);
        e.descLen=r.u32();if(e.descLen==0 || e.descLen>MaxText){why="description length out of range";out.clear();return false;}
        e.desc=r.take(e.descLen);
        e.dataLen=r.u32();if(e.dataLen==0 || e.dataLen>MaxText){why="data length out of range";out.clear();return false;}
        e.data=r.take(e.dataLen);e.u64=r.u64();e.uid=r.take(UserLen);e.uname=r.take(UnameLen);
        e.npaths=r.u32();if(e.npaths==0 || e.npaths>MaxPaths){why="path count out of range";out.clear();return false;}
        e.path0=r.take(PathLen);for(std::uint32_t q=1;q<e.npaths;++q)r.take(PathLen);
        if(r.bad){why="a record runs past the end";out.clear();return false;}
        if(e.desc[e.descLen-1]!=0 || e.data[e.dataLen-1]!=0){why="a text without its NUL";out.clear();return false;}
        if(std::memchr(e.id,0,IdLen)==nullptr){why="an id without its NUL";out.clear();return false;}
        out.push_back(e);
    }
    if(r.o!=n){why="bytes after the last record";out.clear();return false;}
    return true;
}
// The 416-byte record the exe's parser builds, minus the two atStrings (+220 / +248, assigned by the game's own string code): big-endian ints.
inline void fixedFields(const Rec& e,std::uint8_t* rec) noexcept {
    std::memset(rec,0,RecordSize);
    std::memcpy(rec+OffId,e.id,IdLen);
    const auto be32=[&](std::uint32_t off,std::uint32_t v){rec[off]=std::uint8_t(v>>24);rec[off+1]=std::uint8_t(v>>16);rec[off+2]=std::uint8_t(v>>8);rec[off+3]=std::uint8_t(v);};
    be32(OffInt,std::uint32_t(e.a));
    std::memcpy(rec+OffPath,e.path0,PathLen);std::memcpy(rec+OffName,e.name,NameLen);
    be32(OffU64,std::uint32_t(e.u64>>32));be32(OffU64+4,std::uint32_t(e.u64));
    std::memcpy(rec+OffUid,e.uid,UserLen);std::memcpy(rec+OffUname,e.uname,UnameLen);
}
// v1111: the per-job files. fmmc_launcher's DATAFILE_LOAD_OFFLINE_UGC(content id) -> sub_82D83A40 opens "common:/data/ugc/<id>.ugc", but the
// disc names every job file <id>_00.ugc (all 366 listed ids have one, none has <id>.ugc; t-onlroute-ugc-1755: "9GaLrcjnckqeHepNrQ8vUQ.ugc -> 0",
// then fmmc ended the tutorial session). On a miss the hook retries <name>_00. Only for a plain content-id-like name (1..40 of [A-Za-z0-9_-],
// not already ending in _00).
constexpr char JobSuffix[]="_00";constexpr std::size_t JobNameMax=40;
inline bool jobNameFallback(const char* name,char* out,std::size_t cap) noexcept {
    if(!name || !out)return false;
    std::size_t n=0;for(;name[n];++n){const char c=name[n];
        if(n>=JobNameMax || !((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;}
    if(n==0 || n+sizeof JobSuffix>cap)return false;
    if(n>=3 && std::memcmp(name+n-3,JobSuffix,3)==0)return false;
    std::memcpy(out,name,n);std::memcpy(out+n,JobSuffix,sizeof JobSuffix);return true;
}
}
