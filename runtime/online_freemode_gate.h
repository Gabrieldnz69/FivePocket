#pragma once
// online_freemode_gate.h (v1093, trainer-mods; lead 10:28, update-1/FREEMODE-GATE-2026-11-08.md): maintransition parks in GTA Online's
// CHARACTER SELECT (UI 4 = ACTIVATE_FRONTEND_MENU FE_MENU_VERSION_MP_CHARACTER_SELECT at 0x40288, T5 its idle state, every exit needs frontend
// input) because the profile has no active MP character: T3 0x67442 and the T4 chooser 0x6702e test 0x366c8(slot) =
// STAT_GET_BOOL(MP0_CHAR_ISACTIVE 0x79dcc136) with slot = STAT_GET_INT(MPPLY_LAST_MP_CHAR 0x2f2f120f); our local slots are blank.
// Step 1 (diagnostic-heavy): in Online mode, on a visit that HAS a saved character (the controller's savedSex() >= 0, latched at its first
// tick: never the first visit, whose route goes through the game's creator), the two reads are ANSWERED in memory:
// STAT_GET_BOOL(MP0_CHAR_ISACTIVE) -> 1 (out 1), STAT_GET_INT(MPPLY_LAST_MP_CHAR) -> 1 (out 0). Nothing is written to the stat store, so
// nothing can be saved; Story / Sandbox and every other stat: the original. files/private-trace/diag-fm-seed-off disables the answers
// (the logs stay). Every read of the three hashes is logged (online_freemode_gate_hook.inc). Pure: the decision, for the host test.
#include <atomic>
#include <cstdint>
namespace recomp::freemode_gate {
constexpr std::uint32_t Mp0CharIsActive=0x79DCC136u,Mp1CharIsActive=0x8819ED33u,MpplyLastMpChar=0x2F2F120Fu;
// -1 unknown (the controller has not ticked yet), 0 no saved character (a first visit), 1 a saved character (a later visit)
inline std::atomic<int> savedCharacter{-1};
enum class Answer : std::uint8_t {Original,Bool1,Int0};
inline Answer decide(bool onlineMode,bool seedOff,int saved,std::uint32_t hash,bool boolRead) noexcept {
    if(!onlineMode || seedOff || saved!=1)return Answer::Original;
    if(boolRead && hash==Mp0CharIsActive)return Answer::Bool1;
    if(!boolRead && hash==MpplyLastMpChar)return Answer::Int0;
    return Answer::Original;
}
// v1100 (lead, round 4 stage B1, update-1/FREEMODE-GATE-4-2026-11-08.md): T17's character lineup reads the character type packed 384
// (MP0_PSTAT_INT0 0x724BAE22 bit 0, 8 bits) through STAT_GET_MASKED_INT at 0x6877f; blank stats give 0 -> the "player model" route
// (GET_ENTITY_MODEL(PLAYER_PED_ID) = player_zero), whose MP parent head blend never finishes. The console persists 9 (both packed-384 writers
// in maintransition push 9: 0x40c43, 0x43435), which selects the freemode models (G115067.f7 / .f8). Answered in memory, Online later
// visits only, FILL BLANKS ONLY (a real nonzero byte wins, which also covers an mpstats file saved with another type):
//   bit 0  -> 9 when the original failed or read 0;
//   bit 16 -> the saved character's sex (0 male / 1 female, latched from the controller's savedSex()) when bit 0 was answered and the
//             original is blank (packed 386, gender).
// Every other hash / bit / width: the original. Nothing is written to the stat store.
constexpr std::uint32_t Mp0PstatInt0=0x724BAE22u,FreemodeCharType=9u;
inline std::atomic<int> savedSexSeed{-1};   // -1 unknown / no record, 0 male, 1 female (latched with savedCharacter)
struct Masked {bool answer;std::uint32_t value;};
inline Masked decideMasked(bool onlineMode,bool seedOff,int saved,int sex,std::uint32_t hash,std::uint32_t bit,std::uint32_t nbits,
                           bool origOk,std::uint32_t origByte,bool typeAnswered) noexcept {
    if(!onlineMode || seedOff || saved!=1 || hash!=Mp0PstatInt0 || nbits!=8u)return {false,0u};
    const bool blank=!origOk || origByte==0u;
    if(bit==0u)return blank?Masked{true,FreemodeCharType}:Masked{false,0u};
    if(bit==16u)return (blank && typeAnswered && (sex==0 || sex==1))?Masked{true,std::uint32_t(sex)}:Masked{false,0u};
    return {false,0u};
}
// v1105 (FREEMODE-GATE-6 C): a returning character's intro, from OUR arrival record. freemode 0x6df CALL 0x9fbe -> STAT_GET_BOOL
// (MP0_FM_INTRO_MISS_DONE) at 0x9f3d decides FM_intro; maintransition T27 / its spawn read MP0_FM_INTRO_CUT_DONE. fm_intro writes exactly these
// two at completion (enums 119 / 115), and our arrival.json intro=1 records that it ran. Answered in memory, fill blanks only (a real 1 wins),
// never persisted: Online, the route's seed answers on (seedOff false), no kill switch (diag-fm-intro-answer-off), a later visit (saved 1), the
// record intro==1, a bool read of MISS or CUT. TRIGTUT / RACETUT stay original (fm_intro never writes them).
constexpr std::uint32_t Mp0FmIntroMissDone=0x0B19ABF2u,Mp0FmIntroCutDone=0x791E04FAu,Mp0FmTrigtutDone=0x0C9DAB8Fu,Mp0FmRacetutDone=0x58862927u;
inline std::atomic<int> introRecord{-1};   // -1 unknown, 0 no record / intro 0, 1 intro 1 (latched by the controller's Off tick)
inline bool decideIntro(bool onlineMode,bool seedOff,bool answerOff,int saved,int intro,std::uint32_t hash,bool boolRead,bool origOk,std::uint32_t origOut) noexcept {
    if(!onlineMode || seedOff || answerOff || saved!=1 || intro!=1 || !boolRead)return false;
    if(hash!=Mp0FmIntroMissDone && hash!=Mp0FmIntroCutDone)return false;
    return !origOk || origOut==0u;
}
inline bool watched(std::uint32_t hash) noexcept {return hash==Mp0CharIsActive || hash==Mp1CharIsActive || hash==MpplyLastMpChar
    || hash==Mp0FmIntroMissDone || hash==Mp0FmIntroCutDone || hash==Mp0FmTrigtutDone || hash==Mp0FmRacetutDone;}   // v1105: + the four intro stats
}
