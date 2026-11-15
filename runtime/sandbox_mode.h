#pragma once
// sandbox_mode.h (Update 1 P4, Sandbox core runtime; design docs/fable/UPDATE-1-SANDBOX-DESIGN.md §2-3, decisions of the lead
// 2026-11-06 in local-analysis/update-1/denylist/PROPOSAL.md). Pure: no guest memory, no platform calls. The glue
// (sandbox_launch.inc: the filter at sub_82A9B020; trainer_glue.inc: the tick at the trainer's safe point) supplies the guest.
//
// Active only when the play mode is latched to Sandbox (play_mode.h, set by the launcher before the game thread starts). In Story
// mode nothing here runs: the filter calls the original and returns, the tick is never called.
//
// 1. Hold off the story: the thread start sub_82A9B020 (all three callers; the script name in r7) asks LaunchPolicy::decide.
//    A held name is STARTED THEN ENDED (P1): the original runs, then the new thread is killed (sub_83439408, TERMINATE_THREAD's
//    worker) before it runs one instruction, and the caller gets the id. Returning 0 instead would block a calling script forever
//    (DISCOVERIES 2026-11-06 SB E2). A held name started again within RelaunchFrames by sub_82AAC528's path gets the previous (dead)
//    id without a new start (churn limit; never for sub_825A76B8, which looks the thread up by id and writes through it).
// 2. The sweep (at most once per SweepFrames): every running thread whose name is held is terminated (scripts started before the
//    mode took effect, or through a path the filter does not see).
// 3. The tick (every game frame): SET_MINIMAP_HIDE_FOW(TRUE) (the whole map; the exploration mask itself is never written); after a
//    new player ped (the first one, and every character switch) once the player is playing: the trainer's GiveWeapons; the
//    denylist presence check (DOES_SCRIPT_EXIST on each held name, CheckPerFrame per frame) logged once.
//    (v1565, thor-ammo-restore) NOT for an online (freemode) character: its weapons are the game's own Online inventory, which
//    maintransition's spawn restore (category 3, 0x49cec-0x49fc4; freemode twin 0x72101-0x723d9) rebuilds from the character's
//    stats: a pistol-family weapon NOT held is given with 0 ammo, then SET_PED_AMMO_BY_TYPE(ped, type, saved MPx_*_FM_AMMO_CURRENT)
//    restores the saved ammo; a weapon already held is left as it is (0x49d1d -> 0x49d24-0x49d44). Our grant gave the online ped
//    36 weapons at full ammo 3.5 s after the Reveal (every Online run, e.g. t-onlammu-B-20261112-0504), so the saved ammo never came
//    back and freemode's writer then copied the full pool into the stats. An online character is the trainer's bit 20 (passed in
//    as online.character) or, for the game's own freemode route before the adoption, a ped whose model is mp_m/mp_f_freemode_01.
// The trainer is forced on in Sandbox (the glue; the settings file is not written).
// 4. (v955, review dependencies 3-4) While an online (freemode) character is the player (trainer state bit 20, passed in by the glue):
//    the save scripts (SaveScripts) are held like denylist names and ended by the sweep at once, so no save is ever written with a
//    freemode model; and a HELD D-pad DOWN (the wheel) is dropped (sandbox_pad.h; taps pass, never in the pause menu). Both end
//    with bit 20. autosave_controller and savegame_bed are started on demand (never seen in E3's free roam; savegame_bed by its
//    entity brain), so a held one is simply started again later.
// 5. (wave 3, lane lester-blip) The story's contact / mission-trigger icons: with the triggerers held, the static blips they activate
//    never show (the owner's Michael save lost Lester's icon). sandbox_contact_blips.h reads the flow's trigger table (read only)
//    and draws the same icon itself while a story character is the player; removed for an online character. Never a guest write.
#include "sandbox_contact_blips.h"
#include "sandbox_denylist.h"
#include "online_leave_flag.h"   // v1085
#include "sandbox_pad.h"
#include "trainer.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace recomp::sandbox {
using recomp::trainer::Natives;
using recomp::trainer::joaat;

namespace n {
// argument use read from the handlers (DISCOVERIES 2026-11-06 P4 entry): HIDE_FOW 1 BOOL (stb 0x83895DB0); the iterator reset none;
// GET_NEXT_THREAD_ID none -> id (0 = end); GET_NAME_OF_SCRIPT_WITH_THIS_ID 1 id -> char* (a 64-byte static copy); TERMINATE_THREAD 1 id
// (-> sub_83439408); DOES_SCRIPT_EXIST 1 char* -> BOOL; PLAYER_ID / PLAYER_PED_ID / IS_PLAYER_PLAYING / IS_SCREEN_FADED_OUT as trainer.h.
constexpr std::uint32_t SetMinimapHideFow=joaat("SET_MINIMAP_HIDE_FOW"),ThreadIteratorReset=joaat("SCRIPT_THREAD_ITERATOR_RESET"),
    ThreadIteratorNext=joaat("SCRIPT_THREAD_ITERATOR_GET_NEXT_THREAD_ID"),NameOfThread=joaat("GET_NAME_OF_SCRIPT_WITH_THIS_ID"),
    TerminateThread=joaat("TERMINATE_THREAD"),DoesScriptExist=joaat("DOES_SCRIPT_EXIST");
}
inline constexpr std::uint32_t UsedNatives[]{n::SetMinimapHideFow,n::ThreadIteratorReset,n::ThreadIteratorNext,n::NameOfThread,
    n::TerminateThread,n::DoesScriptExist,
    // wave 3 (contact icons, sandbox_contact_blips.h): the glue records handlers only for hashes listed here
    contact::n::AddBlipForCoord,contact::n::SetBlipSprite,contact::n::SetBlipColour,contact::n::SetBlipPriority,contact::n::SetBlipDisplay,
    contact::n::SetBlipCategory,contact::n::SetBlipScale,contact::n::SetBlipShortRange,contact::n::SetBlipHighDetail,
    contact::n::SetBlipCreator,contact::n::ShowTickOnBlip,contact::n::SetBlipAlpha,contact::n::SetBlipHiddenOnLegend,contact::n::RemoveBlip,
    contact::n::DoesBlipExist,contact::n::ThisScriptName,contact::n::CanRemoveAnyBlip,contact::n::GetEntityModel,contact::n::GetWantedLevel};

// A held name? joaat lower-cases; names are at most 63 bytes (the glue's reader bounds them).
inline bool held(const char* name,bool holdFlow) noexcept {
    if(!name || !*name)return false;
    const std::uint32_t h=joaat(name);
    if(h==FlowController.hash)return holdFlow;
    const auto it=std::lower_bound(Denylist.begin(),Denylist.end(),h,[](const Held& e,std::uint32_t v){return e.hash<v;});
    return it!=Denylist.end() && it->hash==h;
}

// v955: held only while an online character is the player (inventory: these three names; save_anywhere is a developer script).
inline constexpr std::array<std::uint32_t,3> SaveScripts{joaat("autosave_controller"),joaat("savegame_bed"),joaat("save_anywhere")};
inline bool saveScript(const char* name) noexcept {
    if(!name || !*name)return false;
    const std::uint32_t h=joaat(name);
    return std::find(SaveScripts.begin(),SaveScripts.end(),h)!=SaveScripts.end();
}

constexpr std::uint32_t RelaunchFrames=60,SweepFrames=30,CheckPerFrame=16,SweepMaxThreads=512;
using LogFn=void(*)(const char*);

class LaunchPolicy {
public:
    enum class Decision:std::uint8_t {Start,StartThenEnd,Reuse};
    explicit LaunchPolicy(LogFn log) noexcept:log_(log) {}
    void setHoldFlow(bool on) noexcept {holdFlow_.store(on,std::memory_order_relaxed);}
    bool holdFlow() const noexcept {return holdFlow_.load(std::memory_order_relaxed);}
    void setHoldSaves(bool on) noexcept {holdSaves_.store(on,std::memory_order_relaxed);}   // v955: the online character's save hold
    bool holdSaves() const noexcept {return holdSaves_.load(std::memory_order_relaxed);}
    bool heldNow(const char* name) const noexcept {return held(name,holdFlow()) || (holdSaves() && saveScript(name))
        || (::recomp::online_leave::hold.load(std::memory_order_acquire) && ::recomp::online_leave::storyInit(name))   // v1085: never the Story init after the reveal
        || (::recomp::online_leave::endTransition.load(std::memory_order_acquire) && ::recomp::online_leave::transitionScript(name));}   // v1086 INTERIM
    // reusable: the caller is sub_82AAC528's start (it only stores the id); reuseId receives the previous id for Reuse.
    Decision decide(const char* name,std::uint32_t frame,bool reusable,std::uint32_t& reuseId) noexcept {
        if(!heldNow(name))return Decision::Start;
        const std::uint32_t h=joaat(name);
        std::lock_guard l(m_);
        for(const auto& r:recent_)
            if(r.used && r.hash==h && reusable && r.id && frame-r.frame<RelaunchFrames){reuseId=r.id;++reused_;return Decision::Reuse;}
        return Decision::StartThenEnd;
    }
    // After the original started a held script (id 0 = the start failed: nothing to end) and the glue ended it.
    void ended(const char* name,std::uint32_t frame,std::uint32_t id,std::uint32_t lr) noexcept {
        const std::uint32_t h=joaat(name);
        unsigned count=0;
        {std::lock_guard l(m_);
        Recent* slot=&recent_[0];
        for(auto& r:recent_){if(r.used && r.hash==h){slot=&r;break;} if(!r.used || r.frame<slot->frame)slot=&r;}
        if(!(slot->used && slot->hash==h))*slot=Recent{h,frame,id,0,true};
        slot->frame=frame;slot->id=id;count=++slot->count;++held_;}
        if(log_ && (count<=3 || (count&(count-1))==0)) {
            char line[200];std::snprintf(line,sizeof line,"RENDER_SANDBOX hold name=%s policy=start-end id=%u lr=%08x count=%u",name,id,lr,count);
            log_(line);
        }
    }
    void reusedLog(const char* name,std::uint32_t id) noexcept {
        unsigned r;{std::lock_guard l(m_);r=reused_;}
        if(log_ && (r<=3 || (r&(r-1))==0)){char line[160];std::snprintf(line,sizeof line,"RENDER_SANDBOX hold name=%s policy=reuse id=%u reused=%u",name,id,r);log_(line);}
    }
    unsigned heldCount() const noexcept {std::lock_guard l(m_);return held_;}
    unsigned reusedCount() const noexcept {std::lock_guard l(m_);return reused_;}
private:
    struct Recent {std::uint32_t hash=0,frame=0,id=0;unsigned count=0;bool used=false;};
    LogFn log_;std::atomic<bool> holdFlow_{false},holdSaves_{false};mutable std::mutex m_;std::array<Recent,64> recent_{};unsigned held_=0,reused_=0;
};

// A guest C string read through Natives::load (big-endian words), at most 63 bytes; "" when unreadable or not printable.
inline void readName(Natives& nat,std::uint32_t at,char (&out)[64]) noexcept {
    out[0]=0;
    if(at<0x10000u || at>0xFFFF0000u)return;
    for(std::uint32_t i=0;i<63;++i) {
        const std::uint32_t a=at+i,w=nat.load(a&~3u);
        const char c=char((w>>(24-8*(a&3u)))&0xFFu);
        if(!c){out[i]=0;return;}
        if(c<32 || c>126){out[0]=0;return;}
        out[i]=c;
    }
    out[0]=0;   // (no terminator within 63)
}

// v955: what the glue knows about the online character (trainer state bit 20) and the phone (recomp_phone_state_v1 bit 0).
struct OnlineState {bool character=false,phoneUp=false,menuOpen=false;};   // v975 (G1): menuOpen = the in-game creator menu (17 4)
// v1565: the two freemode player models (online_mode.h MaleModel / FemaleModel; joaat of the model names).
inline constexpr std::uint32_t FreemodeMale=joaat("mp_m_freemode_01"),FreemodeFemale=joaat("mp_f_freemode_01");
inline constexpr bool freemodeModel(std::uint32_t m) noexcept {return m==FreemodeMale || m==FreemodeFemale;}

class Tick {
public:
    using RequestWeapons=int(*)();   // the glue: trainer().request(GiveWeapons, ...); >= 0 accepted
    Tick(LaunchPolicy& policy,LogFn log,RequestWeapons weapons) noexcept:policy_(policy),log_(log),weapons_(weapons),contact_(log) {}
    using Online=OnlineState;
    // Every game frame at the trainer's safe point, Sandbox mode only.
    void tick(Natives& nat,std::uint32_t frame,Online online={}) noexcept {
        call0(nat,n::SetMinimapHideFow,{1});
        if(online.character!=policy_.holdSaves()) {   // v955: the save hold follows bit 20; turning it on sweeps at once
            policy_.setHoldSaves(online.character);
            logf("RENDER_SANDBOX online-character=%d saves=%s wheel=%s",online.character?1:0,online.character?"held":"released",
                 online.character?"blocked":"free");
            if(online.character)sweptOnce_=false;
        }
        // v957 (WAVE2 §2.1-2): no pause read here (the tick does not run during the pause); the pad reader checks the pause byte live.
        pad::wheelBlocked.store(online.character && !online.phoneUp && !online.menuOpen,std::memory_order_relaxed);   // v975 (G1): the menu repeats held DOWN
        bool ok=false;
        const std::uint32_t player=call1(nat,trainer::n::PlayerId,{},&ok);
        const std::uint32_t ped=call1(nat,trainer::n::PlayerPedId,{});
        const bool playing=ok && ped && call1(nat,trainer::n::IsPlayerPlaying,{player}) && !call1(nat,trainer::n::IsScreenFadedOut,{});
        if(playing && ped!=weaponsPed_ && ped!=onlinePed_) {
            // v1565: an online character's weapons are the game's (its stats, restored at spawn); once per ped, never re-asked
            std::uint32_t model=0;
            if(!online.character){std::uint32_t out[3]{};if(nat.call(contact::n::GetEntityModel,&ped,1,out))model=out[0];}
            if(online.character || freemodeModel(model)) {
                onlinePed_=ped;
                logf("RENDER_SANDBOX weapons ped=%08x skipped online-character=%d model=%08x (the game restores its weapons from its stats)",
                     ped,online.character?1:0,model);
            } else {
                const int seq=weapons_?weapons_():-1;
                if(seq>=0)weaponsPed_=ped;
                logf("RENDER_SANDBOX weapons ped=%08x request=%d",ped,seq);
            }
        }
        contact_.tick(nat,frame,playing,online.character,player,ped);   // wave 3: the contact icons (once per contact::RefreshFrames)
        if(playing && checked_<Denylist.size()+1)check(nat);
        if(!sweptOnce_ || frame-lastSweep_>=SweepFrames){sweep(nat);lastSweep_=frame;sweptOnce_=true;}
    }
    unsigned kills() const noexcept {return kills_.load(std::memory_order_relaxed);}
    unsigned missing() const noexcept {return missing_.load(std::memory_order_relaxed);}
    bool denylistChecked() const noexcept {return checkedDone_.load(std::memory_order_relaxed);}
    unsigned weaponGrants() const noexcept {return weaponsPed_?1u:0u;}
    unsigned contactIcons() const noexcept {return contact_.count();}
    const contact::ContactBlips& contactBlips() const noexcept {return contact_;}
private:
    static std::uint32_t call1(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a,bool* ok=nullptr) noexcept {
        std::uint32_t out[3]{};const bool r=nat.call(h,a.begin(),unsigned(a.size()),out);if(ok)*ok=r;return r?out[0]:0u;
    }
    static void call0(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};(void)nat.call(h,a.begin(),unsigned(a.size()),out);
    }
    template<class... A> void logf(const char* f,A... a) const noexcept {if(!log_)return;char l[320];std::snprintf(l,sizeof l,f,a...);log_(l);}
    void check(Natives& nat) noexcept {
        // the held names this disc has (SB 2.3c); also catches a title-update variant
        for(std::uint32_t k=0;k<CheckPerFrame && checked_<Denylist.size();++k,++checked_) {
            const std::uint32_t s=nat.text(Denylist[checked_].name);
            if(s && call1(nat,n::DoesScriptExist,{s}))++present_;
            else {++missingLocal_;if(missingNames_[0]==0 || std::strlen(missingNames_)<sizeof missingNames_-70){
                std::strncat(missingNames_,Denylist[checked_].name,63);std::strncat(missingNames_," ",1);}}
        }
        if(checked_==Denylist.size()) {
            ++checked_;missing_.store(missingLocal_,std::memory_order_relaxed);checkedDone_.store(true,std::memory_order_relaxed);
            logf("RENDER_SANDBOX denylist present=%u missing=%u held=%zu flow=%s names=%s",present_,missingLocal_,Denylist.size(),
                 policy_.holdFlow()?"held":"kept",missingNames_[0]?missingNames_:"-");
        }
    }
    void sweep(Natives& nat) noexcept {
        call0(nat,n::ThreadIteratorReset,{});
        for(std::uint32_t k=0;k<SweepMaxThreads;++k) {
            const std::uint32_t id=call1(nat,n::ThreadIteratorNext,{});
            if(!id)break;
            char name[64];readName(nat,call1(nat,n::NameOfThread,{id}),name);
            if(!policy_.heldNow(name))continue;
            call0(nat,n::TerminateThread,{id});
            const unsigned k2=kills_.fetch_add(1,std::memory_order_relaxed)+1;
            logf("RENDER_SANDBOX sweep kill name=%s id=%u kills=%u",name,id,k2);
        }
    }
    LaunchPolicy& policy_;LogFn log_;RequestWeapons weapons_;
    // v1565: onlinePed_ = the online ped the weapons were not given to (asked once per ped)
    std::uint32_t weaponsPed_=0,onlinePed_=0,lastSweep_=0;bool sweptOnce_=false;std::size_t checked_=0;unsigned present_=0,missingLocal_=0;
    char missingNames_[320]{};std::atomic<unsigned> kills_{0},missing_{0};std::atomic<bool> checkedDone_{false};
    contact::ContactBlips contact_;
};

// recomp_sandbox_state_v1's word: bit 0 Sandbox active, bit 1 denylist checked, bit 2 flow_controller held, bit 3 weapons granted;
// bit 4 save scripts held (an online character is the player, v955), bit 5 character wheel blocked now (v955);
// bit 6 at least one contact icon of the Sandbox's own is on the map (wave 3, sandbox_contact_blips.h);
// bits 8-15 held launches, 16-23 sweep kills, 24-31 held names missing from this disc (each saturating at 255).
inline std::uint32_t stateWord(bool active,const LaunchPolicy& p,const Tick& t) noexcept {
    const auto sat=[](unsigned v){return std::min(v,255u);};
    return (active?1u:0u)|(t.denylistChecked()?2u:0u)|(p.holdFlow()?4u:0u)|(t.weaponGrants()?8u:0u)|(p.holdSaves()?16u:0u)|
           (pad::wheelBlocked.load(std::memory_order_relaxed)?32u:0u)|(t.contactIcons()?64u:0u)|sat(p.heldCount())<<8|
           sat(t.kills())<<16|sat(t.missing())<<24;
}

} // namespace recomp::sandbox
