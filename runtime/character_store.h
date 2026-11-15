#pragma once
// character_store.h (Update 1, Online World character creator): the one file the creator writes, in the Sandbox save root only.
// <generation>/online/character.json where <generation> is what save_sandbox::filesRootFor() gives in play mode Sandbox:
// <base>/sandbox-mode/g<N>. Anything else (the story root, a test sandbox without sandbox-mode, FailedRoot under /dev/null, a path with
// "..") is refused: the store then reads nothing and writes nothing. Writes go to a temporary file in the same directory (O_EXCL,
// O_NOFOLLOW), fsync, rename; reads refuse symlinks. POSIX; no game data, no network.
#include "character_creator.h"
#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace recomp::creator {

// "<anything>/sandbox-mode/g<N>" (v1046: or "<anything>/online-mode/g<N>", play mode 3 = Online, save_sandbox.h OnlineModeParent: the
// Online mode's own generation keeps its own character) with N in 1..999999, no leading zero, no "..", no trailing slash, absolute.
inline bool isSandboxGenerationRoot(std::string_view root) noexcept {
    if(root.empty() || root.front()!='/' || root.back()=='/')return false;
    if(root.find("/../")!=std::string_view::npos || root.find("/./")!=std::string_view::npos || root.find("//")!=std::string_view::npos)return false;
    const std::size_t slash=root.rfind('/');
    const std::string_view gen=root.substr(slash+1),parent=root.substr(0,slash);
    if(gen.size()<2 || gen.size()>7 || gen[0]!='g' || gen[1]<'1' || gen[1]>'9')return false;
    for(std::size_t i=1;i<gen.size();++i)if(gen[i]<'0' || gen[i]>'9')return false;
    const auto endsWith=[&](std::string_view mode){return parent.size()>mode.size() && parent.substr(parent.size()-mode.size())==mode;};
    if(!endsWith("/sandbox-mode") && !endsWith("/online-mode"))return false;   // v1046: + Online mode
    if(root.rfind("/dev/null",0)==0)return false;
    return true;
}

class FileStore final : public Store {
public:
    // PROPOSED (lane upd1-spoof/creator-route): the file name may be another plain "<name>.json" of the same folder (creator.json, the
    // disc creator's raw values); anything with a slash, a leading dot or another extension is refused like a bad root.
    explicit FileStore(std::string generationRoot,const char* fileName="character.json") : root_(std::move(generationRoot)) {
        ok_=isSandboxGenerationRoot(root_) && plainJsonName(fileName);
        if(ok_){dir_=root_+"/online";name_=fileName;path_=dir_+"/"+name_;}
    }
    static bool plainJsonName(const char* f) noexcept {
        if(!f)return false;
        const std::string_view n(f);
        if(n.size()<6 || n.size()>40 || n.front()=='.' || n.substr(n.size()-5)!=".json")return false;
        for(char c:n)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return false;
        return n.find("..")==std::string_view::npos;
    }
    bool usable() const noexcept {return ok_;}
    const std::string& path() const noexcept {return path_;}
    int read(char* buf,int cap) noexcept override {
        if(!ok_ || !buf || cap<=0)return -1;
        const int fd=::open(path_.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
        if(fd<0)return errno==ENOENT?0:-1;
        int k=0;
        while(k<cap){const ssize_t r=::read(fd,buf+k,std::size_t(cap-k));if(r<0){if(errno==EINTR)continue;::close(fd);return -1;}if(r==0)break;k+=int(r);}
        ::close(fd);
        return k;
    }
    bool write(const char* buf,int len) noexcept override {
        if(!ok_ || !buf || len<=0 || len>RecordMax)return false;
        struct stat st{};
        if(::lstat(root_.c_str(),&st)!=0 || !S_ISDIR(st.st_mode))return false;          // the generation must already exist (the core made it)
        if(::lstat(dir_.c_str(),&st)!=0){if(errno!=ENOENT || ::mkdir(dir_.c_str(),0700)!=0)return false;}
        else if(!S_ISDIR(st.st_mode))return false;                                         // a symlink or a file is refused
        const std::string tmp=dir_+"/."+name_+".tmp";
        (void)::unlink(tmp.c_str());
        const int fd=::open(tmp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0)return false;
        int k=0;bool good=true;
        while(k<len){const ssize_t w=::write(fd,buf+k,std::size_t(len-k));if(w<0){if(errno==EINTR)continue;good=false;break;}k+=int(w);}
        if(good && ::fsync(fd)!=0)good=false;
        ::close(fd);
        if(!good || ::rename(tmp.c_str(),path_.c_str())!=0){(void)::unlink(tmp.c_str());return false;}
        return true;
    }
private:
    std::string root_,dir_,name_,path_;bool ok_=false;
};

} // namespace recomp::creator
