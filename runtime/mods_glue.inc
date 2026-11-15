// script-mods (2026-10-25): the library side of script_mods.h, included in trace_runner.cpp after NativeReadOnlyFile. The file
// bindings (bindFileMetadataQuery / bindReadOnlyFiles) pass every opened guest file through recomp_mods::view; with the setting
// off (default) or no mods found, view returns the original object and nothing else happens.
#include "script_mods.h"
#include <dirent.h>
#include <algorithm>
#include <climits>

namespace recomp_mods {
constexpr std::uint32_t ArchiveKey=0x83BD4C98u;   // the RPF table key (sub_831A86B8's r3 for sub_831A6AB0, 32 bytes)
constexpr unsigned MaxFiles=512,MaxDepth=12;
void log(const char* line) {__android_log_print(ANDROID_LOG_INFO,"RecompRenderer","%s",line);}
recomp::mods::Layer& layer() {static recomp::mods::Layer l(log);return l;}
std::atomic<bool> enabled{false};
std::atomic<unsigned> found{0};

void scan(int dir,const std::string& prefix,std::map<std::string,recomp::mods::File>& out,unsigned depth,unsigned& refused,unsigned& budget) {
    DIR* d=::fdopendir(dir);
    if(!d){::close(dir);return;}
    while(const auto* e=::readdir(d)) {
        const std::string name=e->d_name;
        if(name.empty() || name[0]=='.')continue;          // ".", "..", hidden files (and the app's unfinished imports, ".import-*")
        std::string lower=name;for(auto& c:lower)c=char(std::tolower(static_cast<unsigned char>(c)));
        struct stat st{};
        if(::fstatat(::dirfd(d),name.c_str(),&st,AT_SYMLINK_NOFOLLOW))continue;
        const std::string key=prefix.empty()?lower:prefix+"/"+lower;
        if(S_ISDIR(st.st_mode)) {
            if(depth>=MaxDepth){++refused;continue;}
            const int sub=::openat(::dirfd(d),name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            if(sub>=0)scan(sub,key,out,depth+1,refused,budget);
        } else if(S_ISREG(st.st_mode)) {
            if(!budget || prefix.empty() || st.st_size<=0 || std::uint64_t(st.st_size)>(8ull<<30)){++refused;continue;}
            if(out.count(key))continue;                    // (the same path in two spellings of one folder: the first stays)
            const int fd=::openat(::dirfd(d),name.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
            if(fd<0){++refused;continue;}
            out.emplace(key,std::make_shared<NativeReadOnlyFile>(fd,std::uint64_t(st.st_size)));--budget;
        }
    }
    ::closedir(d);
}
// What setup found (written once there, before any guest thread runs; read by the status exports).
recomp::mods::Catalog catalog;
std::atomic<bool> safe{false};
std::atomic<bool> ready{false};   // catalog is complete (set last in setup; the status exports read nothing of it before)
unsigned refusedTop=0;
// Once, before the bindings: files/settings/mods.txt, then files/mods/ (packs in name order, the loose files first).
void setup() {
    char text[4096]{};std::size_t n=0;   // (4 KiB: the app keeps mods.txt under that; a longer file is read up to there)
    if(FILE* f=std::fopen(RECOMP_APP_PATH("/settings/mods.txt"),"rb")){n=std::fread(text,1,sizeof text-1,f);std::fclose(f);}
    const auto config=recomp::mods::parseConfig(text,n);
    ready.store(false,std::memory_order_release);
    const bool on=config.scripts && !config.safe;
    enabled.store(on,std::memory_order_relaxed);safe.store(config.scripts && config.safe,std::memory_order_relaxed);
    catalog=recomp::mods::Catalog{};refusedTop=0;found.store(0,std::memory_order_relaxed);
    if(!on){log(config.scripts?"RENDER_MODS enabled=0 safe=1 (this start with every mod off)":"RENDER_MODS enabled=0");layer().setMods({});ready.store(true,std::memory_order_release);return;}
    unsigned refused=0,budget=MaxFiles;
    const int root=::open(RECOMP_APP_PATH("/mods"),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    std::vector<std::string> folders;
    if(root>=0) {
        if(DIR* d=::fdopendir(::dup(root))) {
            while(const auto* e=::readdir(d)) {
                const std::string name=e->d_name;if(name.empty() || name[0]=='.')continue;
                struct stat st{};if(::fstatat(root,name.c_str(),&st,AT_SYMLINK_NOFOLLOW))continue;
                if(S_ISDIR(st.st_mode))folders.push_back(name);else if(S_ISREG(st.st_mode))++refused;   // a file at the top has no device folder
            }
            ::closedir(d);
        }
        std::sort(folders.begin(),folders.end());
    }
    const auto lowerOf=[](std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;};
    // the loose files (files/mods/<device>/...) first, as one pack
    std::map<std::string,recomp::mods::File> loose;bool anyLoose=false;recomp::mods::Pack looseStat;looseStat.legacy=true;looseStat.name="Loose files";
    for(const auto& f:folders) {
        if(!recomp::mods::deviceName(lowerOf(f)))continue;
        anyLoose=true;
        const int sub=::openat(root,f.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(sub>=0)scan(sub,lowerOf(f),loose,1,looseStat.refused,budget);
    }
    if(anyLoose)catalog.add(looseStat,loose);
    for(const auto& f:folders) {
        if(recomp::mods::deviceName(lowerOf(f)))continue;
        recomp::mods::Pack p;p.name=f;
        p.on=std::find(config.disabled.begin(),config.disabled.end(),f)==config.disabled.end();
        std::map<std::string,recomp::mods::File> files;
        if(p.on){const int sub=::openat(root,f.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(sub>=0)scan(sub,"",files,0,p.refused,budget);}
        catalog.add(p,files);   // (a switched-off pack is listed, its folder is not read)
    }
    if(root>=0)::close(root);
    refusedTop=refused;
    unsigned refusedAll=refused;for(const auto& p:catalog.packs)refusedAll+=p.refused;
    char line[400];std::snprintf(line,sizeof line,"RENDER_MODS enabled=1 files=%zu refused=%u (files/mods/<device>/<path>)",catalog.mods.size(),refusedAll);log(line);
    for(const auto& p:catalog.packs){std::snprintf(line,sizeof line,"RENDER_MODS pack %s on=%d files=%u conflicts=%u refused=%u",
        p.legacy?"*":recomp::mods::encodeName(p.name).c_str(),p.on?1:0,p.files,p.conflicts,p.refused);log(line);}
    for(const auto& c:catalog.conflicts){std::snprintf(line,sizeof line,"RENDER_MODS conflict %s: kept from %s",c.key.c_str(),
        catalog.packs[c.winner].legacy?"*":recomp::mods::encodeName(catalog.packs[c.winner].name).c_str());log(line);}
    for(const auto& [key,file]:catalog.mods){std::snprintf(line,sizeof line,"RENDER_MODS found %s bytes=%llu",key.c_str(),
        static_cast<unsigned long long>(file->size()));log(line);}
    found.store(unsigned(catalog.mods.size()),std::memory_order_relaxed);
    layer().setMods(catalog.mods);
    ready.store(true,std::memory_order_release);
}
// The file the guest gets: the original unless a mod covers it (the archive key is read from guest memory on the first such file).
recomp::mods::File view(std::string_view path,const recomp::mods::File& original,const std::uint8_t* base) {
    if(!layer().active() || !layer().covers(path))return original;
    static std::once_flag keyOnce;
    std::call_once(keyOnce,[&]{if(base)layer().setKey(base+ArchiveKey);});
    return layer().view(path,original);
}
} // namespace recomp_mods

// The app's status line: the number of mods applied; out = "enabled=.. found=.. applied=.. skipped=.." then one line per mod.
extern "C" __attribute__((visibility("default"),used)) const char recomp_mods_marker[]="recomp-script-mods-v1";
extern "C" __attribute__((visibility("default"))) int recomp_mods_status_v1(char* out,int cap) {
    const auto results=recomp_mods::layer().results();
    int applied=0,skipped=0;for(const auto& r:results)(r.ok?applied:skipped)+=1;
    if(out && cap>0) {
        std::string s="enabled="+std::to_string(recomp_mods::enabled.load()?1:0)+" found="+std::to_string(recomp_mods::found.load())+
            " applied="+std::to_string(applied)+" skipped="+std::to_string(skipped)+"\n";
        for(const auto& r:results)s+=(r.ok?"applied ":"skipped ")+r.key+": "+r.detail+"\n";
        std::snprintf(out,std::size_t(cap),"%s",s.c_str());
    }
    return applied;
}
// Update 1 (Mods page): the v2 status, one line per pack and per outcome (script_mods.h statusV2 has the format). Returns the length of
// the whole text (without the NUL), so a caller whose buffer was too small sees it; out gets as much as fits. Only the app's menus call
// it (never per frame); it builds the text from the outcomes so far.
extern "C" __attribute__((visibility("default"),used)) int recomp_mods_status_v2(char* out,int cap) {
    static const recomp::mods::Catalog none;
    const bool ready=recomp_mods::ready.load(std::memory_order_acquire);
    const auto s=recomp::mods::statusV2(ready && recomp_mods::enabled.load(),ready && recomp_mods::safe.load(),ready?recomp_mods::catalog:none,
        recomp_mods::layer().results(),ready?recomp_mods::refusedTop:0u);
    if(out && cap>0)std::snprintf(out,std::size_t(cap),"%s",s.c_str());
    return int(std::min<std::size_t>(s.size(),std::size_t(INT_MAX)));
}
