#pragma once
// online_stats_store.h (v1089, trainer-mods; lead 2026-11-08 04:2x: "trace the cloud-file layer next (where STAT_SAVE writes; serve it from
// the Online root), so cash, RP and progress persist. Don't let defaults overwrite anything on a later visit.").
// The game's own MP stats save / load legs (traced 2026-11-08, notes trainer-mods-stats-save-trace.md): the stats task (sub_82959C10)
// LOADS through sub_82950A10 (rlCloud GET of "GTA5/saves/mpstats/<prefix><slot:04d>.save" into a grow buffer) and SAVES through
// sub_82950B38 (rlCloud POST of the serialised buffer). online_stats_cloud_hook.inc answers both from one file per slot here.
// Pure (host-testable): the file per slot in <Online generation>/online/, atomic writes, and the overwrite guard:
//  - a slot's file is written only when this process LOADED that slot from its file, or found NO file at its load, or never loaded it and
//    no file exists (a brand-new character's first save);
//  - never when a file exists that this process did not load (defaults would overwrite progress) or whose load failed;
//  - only under an Online generation root ("/online-mode/g<N>"): never a Story or Sandbox root.
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace recomp::online_stats_store {
constexpr int Slots=6;
constexpr std::size_t MaxBytes=1u<<20;   // 1 MiB; the game's buffers are far smaller

// Is this an Online generation root (".../online-mode/g<digits>")?
inline bool onlineRoot(const std::string& root) noexcept {
    const std::size_t k=root.rfind("/online-mode/g");
    if(k==std::string::npos)return false;
    const std::size_t d=k+14;
    if(d>=root.size())return false;
    for(std::size_t i=d;i<root.size();++i)if(root[i]<'0' || root[i]>'9')return false;
    return true;
}

enum class Load : std::uint8_t {Unknown,Loaded,Absent,Failed};

class Guard {
public:
    void loaded(int slot,Load how) noexcept {if(valid(slot))load_[std::size_t(slot)]=how;}
    Load state(int slot) const noexcept {return valid(slot)?load_[std::size_t(slot)]:Load::Failed;}
    // May this process write the slot's file now? fileExists = the file is there at this moment.
    bool allowSave(int slot,bool fileExists) const noexcept {
        if(!valid(slot))return false;
        switch(load_[std::size_t(slot)]) {
        case Load::Loaded: return true;
        case Load::Absent: return true;               // the load found none: this character's progress starts here
        case Load::Unknown: return !fileExists;       // never loaded: only a first save, never over a file
        case Load::Failed: return false;
        }
        return false;
    }
    static bool valid(int slot) noexcept {return slot>=0 && slot<Slots;}
private:
    std::array<Load,Slots> load_{};
};

class Files {
public:
    explicit Files(std::string generationRoot):root_(std::move(generationRoot)) {ok_=onlineRoot(root_);dir_=root_+"/online";}
    bool usable() const noexcept {return ok_;}
    std::string path(int slot) const {return dir_+"/mpstats-"+std::to_string(slot)+".save";}
    bool exists(int slot) const noexcept {
        if(!ok_ || !Guard::valid(slot))return false;
        struct stat st{};return ::lstat(path(slot).c_str(),&st)==0 && S_ISREG(st.st_mode);
    }
    // 1 read (out filled), 0 no file, -1 error / too big / not a regular file
    int read(int slot,std::vector<std::uint8_t>& out) const {
        out.clear();
        if(!ok_ || !Guard::valid(slot))return -1;
        const int fd=::open(path(slot).c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
        if(fd<0)return errno==ENOENT?0:-1;
        struct stat st{};
        if(::fstat(fd,&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<=0 || std::size_t(st.st_size)>MaxBytes){::close(fd);return -1;}
        out.resize(std::size_t(st.st_size));
        std::size_t k=0;
        while(k<out.size()){const ssize_t r=::read(fd,out.data()+k,out.size()-k);if(r<0){if(errno==EINTR)continue;::close(fd);out.clear();return -1;}if(r==0)break;k+=std::size_t(r);}
        ::close(fd);
        if(k!=out.size()){out.clear();return -1;}
        return 1;
    }
    // atomic: <dir>/.mpstats-<slot>.save.tmp, fsync, rename over the slot's file. The generation root must exist (the core made it).
    bool write(int slot,const std::uint8_t* data,std::size_t len) const {
        if(!ok_ || !Guard::valid(slot) || !data || len==0 || len>MaxBytes)return false;
        struct stat st{};
        if(::lstat(root_.c_str(),&st)!=0 || !S_ISDIR(st.st_mode))return false;
        if(::lstat(dir_.c_str(),&st)!=0){if(errno!=ENOENT || ::mkdir(dir_.c_str(),0700)!=0)return false;}
        else if(!S_ISDIR(st.st_mode))return false;
        const std::string tmp=dir_+"/.mpstats-"+std::to_string(slot)+".save.tmp";
        (void)::unlink(tmp.c_str());   // only our own temporary name
        const int fd=::open(tmp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0)return false;
        std::size_t k=0;bool good=true;
        while(k<len){const ssize_t w=::write(fd,data+k,len-k);if(w<0){if(errno==EINTR)continue;good=false;break;}k+=std::size_t(w);}
        if(good && ::fsync(fd)!=0)good=false;
        ::close(fd);
        if(!good || ::rename(tmp.c_str(),path(slot).c_str())!=0){(void)::unlink(tmp.c_str());return false;}
        return true;
    }
private:
    std::string root_,dir_;bool ok_=false;
};
}
