#pragma once
// ammo_restore_diag.h (v1565, thor-ammo-restore lane 2026-11-12). DIAGNOSTIC, READ ONLY, DEFAULT OFF: files/private-trace/diag-ammo-restore
// (read once). Question: after an Online restart the pistol reads FULL (250) although the saved MP0_PISTOL_FM_AMMO_CURRENT is 236.
// The game's own spawn restore (maintransition category 3 0x49cec-0x49fc4, freemode twin 0x72101-0x723d9): a pistol-family weapon that
// is NOT held -> GIVE_WEAPON_TO_PED(ped, w, 0, 0, 1) and local11 = max saved FM_AMMO_CURRENT (stat read 0x4bb51 -> 0x7120 STAT_GET_INT
// 0x713c, read gate 0x4bb79-0x4bb8b: Global_19455 == 0 or Global_19456 == 0, else 0); then SET_PED_AMMO_BY_TYPE(ped,
// GET_PED_AMMO_TYPE_FROM_WEAPON(ped, PISTOL), local11) at 0x49fc4 only if local11 != 0. A weapon already held is left as it is.
// (311c52bb = joaat("SET_PED_AMMO_BY_TYPE"), 09337863 = joaat("GET_PED_AMMO_TYPE_FROM_WEAPON"), checked by the host test.)
// Three things are logged, each line only when its value changes (no per-frame flood), at most LineMax lines in all:
//  1. Observer: the SCRIPTS' own calls (the VM's native return) of the weapon natives below for the four pistol-family weapons
//     (SET_PED_AMMO_BY_TYPE and REMOVE_ALL_PED_WEAPONS always), with the script, its IP, the arguments that survive the call and the
//     result; keyed on (native, script, ip, weapon): a new key or a new (result, ammo argument) is a line.
//  2. Observer: the scripts' STAT_GET_INT of the five pistol stats (from the existing STAT_GET_INT wrapper, which sees the hash before
//     the call), with the value the script got.
//  3. Sampler (the Sandbox tick, our own read-only natives, ownCall set so (2) skips them): ped, model, which pistol-family weapons the
//     ped holds, the pistol's ammo, the five stats, Global_19455 / Global_19456 (the read gate) - one line per change.
// STAT_SET_INT is not hooked (its result overwrites the hash before any after-call hook); the sampler shows every change of the stored
// value instead. Nothing is written: no guest store, no native with a side effect.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#if defined(RECOMP_APP_PATH)
#include <unistd.h>
#endif

namespace recomp::ammo_diag {
constexpr std::uint32_t joaatLower(const char* s) noexcept {
    std::uint32_t h=0;for(;*s;++s){h+=std::uint8_t(*s>='A' && *s<='Z'?*s+32:*s);h+=h<<10;h^=h>>6;}h+=h<<3;h^=h>>11;h+=h<<15;return h;
}
namespace n {
constexpr std::uint32_t HasPedGotWeapon=joaatLower("HAS_PED_GOT_WEAPON"),GiveWeaponToPed=joaatLower("GIVE_WEAPON_TO_PED"),
    AddAmmoToPed=joaatLower("ADD_AMMO_TO_PED"),GetAmmoInPedWeapon=joaatLower("GET_AMMO_IN_PED_WEAPON"),SetPedAmmo=joaatLower("SET_PED_AMMO"),
    SetPedAmmoByType=joaatLower("SET_PED_AMMO_BY_TYPE"),GetPedAmmoTypeFromWeapon=joaatLower("GET_PED_AMMO_TYPE_FROM_WEAPON"),
    RemoveWeaponFromPed=joaatLower("REMOVE_WEAPON_FROM_PED"),RemoveAllPedWeapons=joaatLower("REMOVE_ALL_PED_WEAPONS"),
    StatGetInt=joaatLower("STAT_GET_INT"),PlayerPedId=joaatLower("PLAYER_PED_ID"),GetEntityModel=joaatLower("GET_ENTITY_MODEL");
}
// the pistol family maintransition 0x49cf2..0x49fae walks: WEAPON_PISTOL, WEAPON_COMBATPISTOL, WEAPON_APPISTOL, WEAPON_PISTOL50
inline constexpr std::uint32_t Family[4]{joaatLower("weapon_pistol"),joaatLower("weapon_combatpistol"),joaatLower("weapon_appistol"),
                                        joaatLower("weapon_pistol50")};
// MP0_PISTOL_FM_AMMO_CURRENT and its family stats (the lead's decrypted copies: all four = 236 after Ammu-Nation A), MP1's pistol stat
inline constexpr std::uint32_t Stats[5]{0xA4DCE5A3u,0x7F4EBA70u,0x8EC50F63u,0x364E2BC1u,0x2E5A7395u};
inline constexpr std::uint32_t GateGlobals[2]{19455u,19456u};   // GLOBAL_U16_LOAD 75 255 / 76 0 (0x68c34 / 0x0258c), big-endian operand
// The weapon natives whose probe stays on while the switch is on (indirect_call_resolver.cpp probeNeeded).
inline constexpr std::uint32_t Probed[]{n::HasPedGotWeapon,n::GiveWeaponToPed,n::AddAmmoToPed,n::GetAmmoInPedWeapon,n::SetPedAmmo,
    n::SetPedAmmoByType,n::GetPedAmmoTypeFromWeapon,n::RemoveWeaponFromPed,n::RemoveAllPedWeapons};
constexpr unsigned LineMax=600,KeyMax=512;

#if defined(RECOMP_APP_PATH)
inline bool on() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-ammo-restore"),F_OK)==0;return o;}
#else
inline std::atomic<bool> testOn{false};   // host tests
inline bool on() noexcept {return testOn.load(std::memory_order_relaxed);}
#endif
inline bool probedHash(std::uint32_t h) noexcept {for(const std::uint32_t p:Probed)if(p==h)return true;return false;}
inline bool probed(std::uint32_t h) noexcept {return probedHash(h) && on();}
inline bool family(std::uint32_t w) noexcept {for(const std::uint32_t f:Family)if(f==w)return true;return false;}
inline bool isStat(std::uint32_t h) noexcept {for(const std::uint32_t s:Stats)if(s==h)return true;return false;}
inline thread_local bool ownCall=false;   // set by the sampler around its own reads (the STAT_GET_INT wrapper then logs nothing)

inline const char* scriptName(std::uint32_t h) noexcept {
    switch(h){case 0x5FF2841Fu:return "maintransition";case 0xC875557Du:return "freemode";default:break;}
    if(h==joaatLower("gunclub_shop"))return "gunclub_shop";
    return "?";
}
inline const char* nativeName(std::uint32_t h) noexcept {
    switch(h){
    case n::HasPedGotWeapon:return "HAS_PED_GOT_WEAPON";case n::GiveWeaponToPed:return "GIVE_WEAPON_TO_PED";case n::AddAmmoToPed:return "ADD_AMMO_TO_PED";
    case n::GetAmmoInPedWeapon:return "GET_AMMO_IN_PED_WEAPON";case n::SetPedAmmo:return "SET_PED_AMMO";case n::SetPedAmmoByType:return "SET_PED_AMMO_BY_TYPE";
    case n::GetPedAmmoTypeFromWeapon:return "GET_PED_AMMO_TYPE_FROM_WEAPON";case n::RemoveWeaponFromPed:return "REMOVE_WEAPON_FROM_PED";
    case n::RemoveAllPedWeapons:return "REMOVE_ALL_PED_WEAPONS";case n::StatGetInt:return "STAT_GET_INT";default:return "?";}
}

// The lines budget and the (key -> last value) table, shared by every thread that reports (script threads run concurrently).
class Ledger {
public:
    // true: a new key or a changed value (the caller logs); false: seen with this value, the table full, or the budget spent
    bool fresh(std::uint32_t key,std::uint64_t value) noexcept {
        while(busy_.test_and_set(std::memory_order_acquire)){}
        bool log=false;
        if(lines_<LineMax) {
            unsigned i=0;for(;i<n_ && keys_[i]!=key;++i){}
            if(i<n_){if(values_[i]!=value){values_[i]=value;log=true;}}
            else if(n_<KeyMax){keys_[n_]=key;values_[n_]=value;++n_;log=true;}
            if(log)++lines_;
        }
        busy_.clear(std::memory_order_release);
        return log;
    }
    // the budget alone (sample lines: each one is a change already)
    bool spend() noexcept {
        while(busy_.test_and_set(std::memory_order_acquire)){}
        const bool ok=lines_<LineMax;if(ok)++lines_;
        busy_.clear(std::memory_order_release);
        return ok;
    }
    unsigned lines() const noexcept {return lines_;}
private:
    std::atomic_flag busy_=ATOMIC_FLAG_INIT;std::uint32_t keys_[KeyMax]{};std::uint64_t values_[KeyMax]{};unsigned n_=0,lines_=0;
};
inline Ledger& ledger() noexcept {static Ledger l;return l;}
inline std::uint32_t mix(std::initializer_list<std::uint32_t> v) noexcept {std::uint32_t h=2166136261u;for(const std::uint32_t x:v)h=(h^x)*16777619u;return h;}

// (1) One script call of a probed weapon native, after it returned. a = the argument words as they are after the call (a result is
// written over argument 0, so a[0] is the ped only for natives without a result), r = the result word (0xFFFFFFFF: none). Fills line
// and returns true when it is to be logged.
inline bool weaponCall(std::uint32_t native,std::uint32_t script,std::uint32_t ip,const std::uint32_t a[5],std::uint32_t r,std::uint32_t frame,
                       char* line,std::size_t size) noexcept {
    if(!probedHash(native))return false;
    const bool always=native==n::SetPedAmmoByType || native==n::RemoveAllPedWeapons;
    const std::uint32_t weapon=always?0u:a[1];
    if(!always && !family(weapon))return false;
    // the value that matters per native: the result, the ammo argument (GIVE / ADD / SET_PED_AMMO: a[2]; BY_TYPE: type a[1], amount a[2])
    const std::uint64_t value=(std::uint64_t(r)<<32)|(native==n::SetPedAmmoByType?(a[1]*2654435761u)^a[2]:a[2]);
    if(!ledger().fresh(mix({native,script,ip,weapon}),value))return false;
    switch(native){
    case n::GiveWeaponToPed:std::snprintf(line,size,"RENDER_AMMO_DIAG call %s(ped=%08x weapon=%08x ammo=%d p3=%d p4=%d) script=%s(%08x) ip=%x frame=%u",
        nativeName(native),a[0],a[1],int(a[2]),int(a[3]),int(a[4]),scriptName(script),script,ip,frame);break;
    case n::AddAmmoToPed:case n::SetPedAmmo:std::snprintf(line,size,"RENDER_AMMO_DIAG call %s(ped=%08x weapon=%08x ammo=%d) script=%s(%08x) ip=%x frame=%u",
        nativeName(native),a[0],a[1],int(a[2]),scriptName(script),script,ip,frame);break;
    case n::SetPedAmmoByType:std::snprintf(line,size,"RENDER_AMMO_DIAG call %s(ped=%08x type=%08x amount=%d) script=%s(%08x) ip=%x frame=%u",
        nativeName(native),a[0],a[1],int(a[2]),scriptName(script),script,ip,frame);break;
    case n::RemoveWeaponFromPed:std::snprintf(line,size,"RENDER_AMMO_DIAG call %s(ped=%08x weapon=%08x) script=%s(%08x) ip=%x frame=%u",
        nativeName(native),a[0],a[1],scriptName(script),script,ip,frame);break;
    case n::RemoveAllPedWeapons:std::snprintf(line,size,"RENDER_AMMO_DIAG call %s(ped=%08x p1=%d) script=%s(%08x) ip=%x frame=%u",
        nativeName(native),a[0],int(a[1]),scriptName(script),script,ip,frame);break;
    default:std::snprintf(line,size,"RENDER_AMMO_DIAG call %s(weapon=%08x) -> %d script=%s(%08x) ip=%x frame=%u",   // HAS / GET_AMMO / AMMO_TYPE
        nativeName(native),a[1],int(r),scriptName(script),script,ip,frame);break;
    }
    return true;
}
// (2) One script STAT_GET_INT (hash read before the call) and what it got.
inline bool statRead(std::uint32_t hash,std::uint32_t script,std::uint32_t ip,std::uint32_t r,std::uint32_t out,char* line,std::size_t size) noexcept {
    if(!isStat(hash))return false;
    if(!ledger().fresh(mix({n::StatGetInt,script,ip,hash}),(std::uint64_t(r)<<32)|out))return false;
    std::snprintf(line,size,"RENDER_AMMO_DIAG call STAT_GET_INT(hash=%08x) -> %d out=%d script=%s(%08x) ip=%x",hash,int(r),int(out),
                  scriptName(script),script,ip);
    return true;
}

// (3) The sampler: our own read-only natives once per Sandbox tick; one line when anything but the frame changed. Natives is
// trainer.h's interface (call / word / load); templated so this header needs no trainer.h.
struct Sample {
    std::uint32_t ped=0,model=0,has=0,stats[5]{},statOk=0,gate[2]{};std::int32_t ammo=-1;
    bool operator==(const Sample& o) const noexcept {
        if(ped!=o.ped || model!=o.model || has!=o.has || ammo!=o.ammo || statOk!=o.statOk || gate[0]!=o.gate[0] || gate[1]!=o.gate[1])return false;
        for(int i=0;i<5;++i)if(stats[i]!=o.stats[i])return false;
        return true;
    }
};
// a script global's word through the VM's global block table (online_freemode_gate_hook.inc globalWord): 0xFFFFFFFF = not readable
template<class Nat> std::uint32_t globalWord(Nat& nat,std::uint32_t gl) noexcept {
    const std::uint32_t b=gl>>18,w=gl&0x3FFFFu,bs=nat.load(0x83C5CE48u+4u*b),sz=nat.load(0x83C5CF50u+4u*b);
    return (bs>=0x10000u && !(bs&3u) && w<sz)?nat.load(bs+4u*w):0xFFFFFFFFu;
}
class Sampler {
public:
    template<class Nat,class Log> void tick(Nat& nat,std::uint32_t frame,bool onlineCharacter,Log log) noexcept {
        Sample s;std::uint32_t out[3]{};
        ownCall=true;
        if(nat.call(n::PlayerPedId,nullptr,0,out))s.ped=out[0];
        if(s.ped) {
            if(nat.call(n::GetEntityModel,&s.ped,1,out))s.model=out[0];
            for(unsigned k=0;k<4;++k){const std::uint32_t a[3]{s.ped,Family[k],0};if(nat.call(n::HasPedGotWeapon,a,3,out) && out[0])s.has|=1u<<k;}
            if(s.has&1u){const std::uint32_t a[2]{s.ped,Family[0]};if(nat.call(n::GetAmmoInPedWeapon,a,2,out))s.ammo=std::int32_t(out[0]);}
        }
        for(unsigned k=0;k<5;++k) {
            const std::uint32_t slot=nat.word(0xFFFFFFFFu);if(!slot)continue;
            const std::uint32_t a[3]{Stats[k],slot,0xFFFFFFFFu};
            if(nat.call(n::StatGetInt,a,3,out) && out[0]){s.statOk|=1u<<k;s.stats[k]=nat.load(slot);}
        }
        ownCall=false;
        for(unsigned k=0;k<2;++k)s.gate[k]=globalWord(nat,GateGlobals[k]);
        if(seen_ && s==last_)return;
        seen_=true;last_=s;
        if(!ledger().spend())return;   // the shared line budget
        char l[300];
        std::snprintf(l,sizeof l,"RENDER_AMMO_DIAG sample frame=%u ped=%08x model=%08x online_char=%d has=%u%u%u%u pistol_ammo=%d stats=%d,%d,%d,%d,%d"
                      " stat_ok=%x g19455=%d g19456=%d",frame,s.ped,s.model,onlineCharacter?1:0,s.has&1u,(s.has>>1)&1u,(s.has>>2)&1u,(s.has>>3)&1u,
                      int(s.ammo),int(s.stats[0]),int(s.stats[1]),int(s.stats[2]),int(s.stats[3]),int(s.stats[4]),s.statOk,int(s.gate[0]),int(s.gate[1]));
        log(l);
    }
private:
    Sample last_{};bool seen_=false;
};
inline Sampler& sampler() noexcept {static Sampler s;return s;}
} // namespace recomp::ammo_diag
