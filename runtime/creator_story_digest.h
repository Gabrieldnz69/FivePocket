#pragma once
// creator_story_digest.h (Update 1, lane upd1-spoof/creator-route, 2026-11-07; PROPOSED, used by trainer_glue.inc's DiscCreatorGlue).
// The disc creator route's story-save check (creator_route.h Ops::storyUnchanged): a digest of the STORY saves this process would
// use, taken when a run is requested and again when it ends. Read only: open(O_RDONLY | O_NOFOLLOW), opendir, lstat. Nothing written.
//
// Which folder: the saves state's root is save_sandbox::filesRootFor(RECOMP_SAVES_DIR or the files directory). In Sandbox mode that
// is <story base>/sandbox-mode/g<N> (save_sandbox.h sandboxModeRoot), and <story base> is the files root, or the save-sandbox root
// (<files>/saves-sandbox/<name>) when diag-save-sandbox is on, as every device check with "copy upd1-dc-..." is. The story saves are
// <story base>/saves. storyBaseOf() strips the "/sandbox-mode/g<N>" suffix exactly; any other shape gives "" (the glue then refuses
// to start: "story-root-unresolved").
//
// The digest: every regular file below <story base>/saves, recursively (depth <= 32, as the sandbox copy allows), sorted, in
// save_sandbox's own TreeSummary / finishDigest format ("<sha256>  ./<relative path>\n" lines, then SHA-256 of the lines). A tree
// with no change since the generation was copied therefore gives the same digest as that generation's SANDBOX_READY saves_sha256.
// Directories ("dir") and anything else (symlink, device: "other:<mode>") go into a second hash (shape), so a change of either is
// seen too without changing the files digest.
// An absent <story base>/saves is the state "absent" (a fresh install); an unreadable one is "unreadable" (never equal to itself
// across a change: the glue treats it as a refusal at the start).
#include "save_sandbox.h"
#include <cerrno>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>
#include <sys/stat.h>

namespace recomp::creatorroute::story {
namespace ss=recomp::bootstrap::save_sandbox;

// "<base>/sandbox-mode/g<N>" -> "<base>"; anything else -> "".
inline std::string storyBaseOf(const std::string& generationRoot) {
    for(const std::string_view mid:{std::string_view(ss::SandboxModeParent),std::string_view(ss::OnlineModeParent)}) {   // v1046: + "/online-mode"
        const std::size_t at=generationRoot.rfind(std::string(mid)+"/");
        if(at==std::string::npos || at==0)continue;
        const std::string_view gen=std::string_view(generationRoot).substr(at+mid.size()+1);
        if(ss::detail::generationNumber(gen)==0)continue;
        return generationRoot.substr(0,at);
    }
    return {};
}

struct Digest {
    std::string state;               // "ok", "absent", "unresolved", "unreadable"
    std::string sha;                 // regular files only, save_sandbox finishDigest format (= SANDBOX_READY saves_sha256 if unchanged)
    std::string shape;               // SHA-256 of the "dir" / "other:<mode>" lines ("" when there are none)
    std::uint64_t files=0,bytes=0;
    std::string text() const {return state=="ok"?"sha256="+sha+" files="+std::to_string(files)+" bytes="+std::to_string(bytes)+(shape.empty()?"":" shape="+shape.substr(0,16)):state;}
    bool operator==(const Digest& o) const {return state==o.state && sha==o.sha && shape==o.shape && files==o.files && bytes==o.bytes;}
    bool usable() const {return state=="ok" || state=="absent";}
};

inline bool walk(const std::string& dir,const std::string& rel,int depth,ss::TreeSummary& sum) {
    if(depth>ss::MaximumCopyDepth)return false;
    std::vector<std::string> names;std::string err;
    if(!ss::detail::sortedNames(dir,names,err))return false;
    for(const auto& name:names) {
        const std::string path=dir+"/"+name,r=rel+"/"+name;
        struct stat st{};
        if(::lstat(path.c_str(),&st)!=0)return false;
        if(S_ISDIR(st.st_mode)){sum.entries.emplace_back(r,"dir");if(!walk(path,r,depth+1,sum))return false;}
        else if(S_ISREG(st.st_mode)) {
            std::uint64_t size=0;std::string h;
            if(!ss::detail::hashFile(path,size,h,err))return false;
            sum.files+=1;sum.bytes+=size;sum.entries.emplace_back(r,h);
        } else sum.entries.emplace_back(r,"other:"+std::to_string(unsigned(st.st_mode)));
    }
    return true;
}
// The digest of <storyBase>/saves.
inline Digest digestOf(const std::string& storyBase) {
    Digest d;
    if(storyBase.empty() || storyBase.front()!='/'){d.state="unresolved";return d;}
    const std::string dir=storyBase+"/saves";
    struct stat st{};
    if(::lstat(dir.c_str(),&st)!=0){d.state=errno==ENOENT?"absent":"unreadable";return d;}
    if(!S_ISDIR(st.st_mode)){d.state="unreadable";return d;}
    ss::TreeSummary sum;
    if(!walk(dir,".",0,sum)){d.state="unreadable";return d;}
    // regular files: exactly save_sandbox's finishDigest (the copy's own format); directories / other entries: a separate hash
    ss::TreeSummary files;files.entries.reserve(sum.entries.size());
    std::string extra;
    for(const auto& e:sum.entries){if(e.second=="dir" || e.second.rfind("other:",0)==0)extra+=e.second+"  "+e.first+"\n";else files.entries.push_back(e);}
    ss::detail::finishDigest(files);
    d.sha=files.digest;
    if(!extra.empty()){ss::Sha256 sha;sha.update(reinterpret_cast<const std::uint8_t*>(extra.data()),extra.size());d.shape=sha.hex();}
    d.state="ok";d.files=unsigned(sum.files);d.bytes=sum.bytes;
    return d;
}
} // namespace recomp::creatorroute::story
