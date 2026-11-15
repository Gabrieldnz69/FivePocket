#pragma once
// creator_route.h (Update 1, lane upd1-spoof/creator-route, 2026-11-06; PROPOSED trainer action 21 DiscCreator, Sandbox only).
// Owner decision 2026-11-06 ("just remove the script or spoof it"): run THE DISC'S OWN character creator inside the offline Sandbox,
// then put the created character into Sandbox free roam. Purely local: no socket, no service, nothing authenticated. Every step is
// gated on recomp::sandbox::active() AND files/private-trace/diag-online-local (the glue's Ops::sandbox / Ops::devSwitch); Story mode
// never reaches this file's code (the trainer answers ErrArgs, the glue never constructs a Route).
//
// What the creator IS (decoded from the player's own disc; lane NOTES.md section 2 has every address):
//  - not a script of its own: UI state 5 (handler 0x38c97) of `maintransition` (started by selector with START_NEW_SCRIPT(name, 8032),
//    no arguments). maintransition has two state words: G1344033 (UI, dispatch 0xfad) and G1344034 (transition, dispatch 0x42356);
//    its main loop runs the watchdog 0x67c95, then the transition handler, then the UI handler, then WAIT 0 (the only WAIT).
//  - the creator is a frontend menu (ACTIVATE_FRONTEND_MENU(joaat FE_MENU_VERSION_MP_CHARACTER_CREATION)), with 4 family peds.
//  - the game enters it from the character select (UI 4) on an empty slot (0x40bf7..0x40c50): G1332502.6[slot] = 1, 0x3bcfc(slot)
//    (G59751 = G1344064 = slot), UI 5, 0x5459(9) (G1344063 = 9), 0x40d3e(9, slot) (packed stat 384 = 9, G1338472[slot] = 9),
//    transition 22 (NETWORK_SESSION_ENTER). This route does the same writes EXCEPT the transition: it pins the transition word to 0,
//    whose SWITCH case is an empty jump (0x42515), so the creator's own session step (22) is never taken. That is NOT "no session
//    native ever runs": the creator's quit 0x3b238 sets UI 4 + transition 61 (0x432e5 reaches NETWORK_SESSION_HOST_SINGLE_PLAYER /
//    LEAVE_SINGLE_PLAYER / SESSION_END), and transition 5 (0x66cee, set by the watchdog and the HUD_CHNGCHARCON path) reaches
//    NETWORK_SESSION_END / NETWORK_LEAVE_TRANSITION. A transition set in one slice runs at the start of the NEXT slice, so the route
//    sends TERMINATE_THREAD on the tick it sees any non-commit end (Route::beginTeardown), before that slice, unless maintransition's
//    own native call hosts the tick (then deferred). A watchdog trip sets transition 5 at the top of a slice and the same slice runs
//    it: that one cannot be prevented (NOTES R3).
//  - its commit (0x3b382, sub-state 3) writes the character into the multiplayer stats of the slot (ints / floats through the tables
//    G1363889 / G1371618 that maintransition's own init 0x68c3a fills with literal hashes; packed 8-bit values through
//    GET_PACKED_INT_STAT_KEY + ca160bcc + STAT_SET_MASKED_INT), then UI 6, then G1345827 = 1, G1345828 = 0, SET_FRONTEND_ACTIVE(0).
//    mp_menuped (the pause-menu ped) rebuilds the character from exactly these stats (0x3190c), which is the consumer this route's
//    capture mirrors.
//
// The walk (Route::tick, one step per Sandbox tick, idle = no native call):
//   Idle -> [request(Start): preflight, no side effect on refusal] -> Bootstrap (the game's own offline-continue pair: STAT_DELETE_SLOT
//   0..5, STAT_RESET_ALL_ONLINE_CHARACTER_STATS 0..7, maintransition 0x295f; slot 0 loaded, not failed) -> Load (REQUEST_SCRIPT) ->
//   Start (sub_82AAC528("maintransition", 0, 0, 8032)) -> WaitEntry (the entry ran: its table init wrote the known hash AND it set a
//   transition) -> Pin (the character-select writes above, transition 0) -> Creator (watch: committed / user quit / watchdog / thread
//   gone / timeout / cancel / Sandbox left) -> Teardown (TERMINATE_THREAD on the same tick the end is seen; nothing while the tick is
//   hosted by maintransition itself; then restore every global this route wrote, restore the player: fade in, frontend off, script
//   cams off, control on, unfrozen, visible, collision; a thread that never ends: the player is still restored, the globals are
//   left, Failed with saves blocked) -> Capture (the committed stats, a few reads per
//   tick; stats are global, not the thread's) -> Save (character.json = the route-B Look; creator.json = the raw creator values) ->
//   Apply (trainer 15 <gender>, then 17 0) -> Done. Any failure after the start goes through Teardown. A changed story save (Ops::
//   storyUnchanged false) ends Failed with saves blocked and no new run.
//
// Guest writes: ONLY the script globals listed in Globals below, only after the addressing check passed (the table word that
// maintransition's own init writes reads back as the disc's literal hash), only on a tick NOT hosted by maintransition's own thread
// (Ops::hostIs(id)), and every one is restored from the snapshot at Teardown. Stats: MP stats of the Sandbox session only (in memory;
// STAT_SAVE is cloud-gated, level2b-spike NOTES 2.4). Files: the Sandbox generation only (Ops::saveLook / saveRaw, character_store.h).
#include "trainer.h"
#include "character_creator.h"
#include "creator_hair_table.h"   // v1083
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

// (the formatting helpers below pass a format parameter through: every caller gives a literal)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
#pragma GCC diagnostic ignored "-Wformat-nonliteral"

namespace recomp::creatorroute {
using trainer::Natives;
using trainer::joaat;
using trainer::asFloat;

// ------------------------------------------------------------------------------------------------ natives (all registered on 0.0.0.9)
// Argument / result counts from maintransition's own NATIVE instructions (b1 = args<<2 | returns); registration checked against
// natives/registrations.json by tests/check_route.py.
namespace n {
constexpr std::uint32_t ThreadsWithHash=joaat("GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH"), // 1 -> 1
    RequestScript=joaat("REQUEST_SCRIPT"),HasScriptLoaded=joaat("HAS_SCRIPT_LOADED"),                     // 1 -> 0, 1 -> 1
    ScriptNoLongerNeeded=joaat("SET_SCRIPT_AS_NO_LONGER_NEEDED"),TerminateThread=joaat("TERMINATE_THREAD"), // 1 -> 0, 1 -> 0
    NetworkGame=joaat("NETWORK_IS_GAME_IN_PROGRESS"),SignedIn=joaat("NETWORK_IS_SIGNED_IN"),               // 0 -> 1
    SignedOnline=joaat("NETWORK_IS_SIGNED_ONLINE"),OnlinePrivileges=joaat("NETWORK_HAVE_ONLINE_PRIVILEGES"), // 0 -> 1
    MissionFlag=joaat("GET_MISSION_FLAG"),PauseMenuActive=joaat("IS_PAUSE_MENU_ACTIVE"),                   // 0 -> 1
    StatDeleteSlot=joaat("STAT_DELETE_SLOT"),StatResetAllOnline=0x343b27e2u,                               // 1 -> 1, 1 -> 0
    StatSlotIsLoaded=joaat("STAT_SLOT_IS_LOADED"),StatCloudSlotLoadFailed=joaat("STAT_CLOUD_SLOT_LOAD_FAILED"), // 1 -> 1
    StatGetInt=joaat("STAT_GET_INT"),StatGetFloat=joaat("STAT_GET_FLOAT"),                                 // 3 -> 1 (hash, &out, -1)
    StatGetMaskedInt=joaat("STAT_GET_MASKED_INT"),StatSetMaskedInt=joaat("STAT_SET_MASKED_INT"),           // 5 -> 1
    PackedKey=joaat("GET_PACKED_INT_STAT_KEY"),PackedWord=0xca160bccu,                                     // 4 -> 1, 1 -> 1
    SetFrontendActive=joaat("SET_FRONTEND_ACTIVE"),RenderScriptCams=joaat("RENDER_SCRIPT_CAMS"),           // 1 -> 0, 5 -> 0
    SetPlayerControl=joaat("SET_PLAYER_CONTROL"),FreezeEntity=joaat("FREEZE_ENTITY_POSITION"),             // 3 -> 0, 2 -> 0
    SetEntityVisible=joaat("SET_ENTITY_VISIBLE"),SetEntityCollision=joaat("SET_ENTITY_COLLISION"),         // 2 -> 0, 3 -> 0
    DoScreenFadeIn=joaat("DO_SCREEN_FADE_IN"),IsScreenFadedIn=joaat("IS_SCREEN_FADED_IN"),                 // 1 -> 0, 0 -> 1
    PlayerId=joaat("PLAYER_ID"),PlayerPedId=joaat("PLAYER_PED_ID"),                                       // 0 -> 1
    SwitchInProgress=joaat("IS_PLAYER_SWITCH_IN_PROGRESS"),ControlOn=joaat("IS_PLAYER_CONTROL_ON"),      // v990: 0 -> 1, 1 -> 1
    // v992: maintransition's own switch stop 0x68090 (registered: 82d5c598 / 82d77a50 / 82d84dc8 / 82dd1228 / 82db2460 / 82d9ff20)
    EntityExists=joaat("DOES_ENTITY_EXIST"),MissionEntityQ=0x2632e124u /* 1 -> 1, unnamed: 0x68090 skips the next call when it is 1 */,
    SetMissionEntity=joaat("SET_ENTITY_AS_MISSION_ENTITY"),DeletePed=joaat("DELETE_PED"),SetPlayerInvincible=joaat("SET_PLAYER_INVINCIBLE"),
    StopPlayerSwitch=joaat("STOP_PLAYER_SWITCH"),
    // v993 (lead-0607 switch-descent.md): maintransition's own descent 0x449fa (registered: 82db6718 / 82ddd8e0 / 82db65f0 / 82db6598 /
    // 82db24b8 / 82d5df00)
    IsMultiFirstDone=joaat("IS_SWITCH_TO_MULTI_FIRSTPART_FINISHED"),MultiSecondPart=joaat("SWITCH_TO_MULTI_SECONDPART"),
    SwitchState=joaat("GET_PLAYER_SWITCH_STATE"),SwitchType=joaat("GET_PLAYER_SWITCH_TYPE"),GetInvincible=joaat("GET_PLAYER_INVINCIBLE"),
    SwitchMpFlag=0x932fdb81u /* 1 -> 0, unnamed: 0x51b sets 1, the descent 0x44d11 sets 0 */,
    EntityModel=joaat("GET_ENTITY_MODEL"),HeadBlendFinished=joaat("HAS_PED_HEAD_BLEND_FINISHED");   // v995 (82d8a300 / 82dd3c50)
}
inline constexpr std::uint32_t UsedNatives[]{n::ThreadsWithHash,n::RequestScript,n::HasScriptLoaded,n::ScriptNoLongerNeeded,
    n::TerminateThread,n::NetworkGame,n::SignedIn,n::SignedOnline,n::OnlinePrivileges,n::MissionFlag,n::PauseMenuActive,
    n::StatDeleteSlot,n::StatResetAllOnline,n::StatSlotIsLoaded,n::StatCloudSlotLoadFailed,n::StatGetInt,n::StatGetFloat,
    n::StatGetMaskedInt,n::StatSetMaskedInt,n::PackedKey,n::PackedWord,n::SetFrontendActive,n::RenderScriptCams,n::SetPlayerControl,
    n::FreezeEntity,n::SetEntityVisible,n::SetEntityCollision,n::DoScreenFadeIn,n::IsScreenFadedIn,n::PlayerId,n::PlayerPedId,n::SwitchInProgress,n::ControlOn,
    n::EntityExists,n::MissionEntityQ,n::SetMissionEntity,n::DeletePed,n::SetPlayerInvincible,n::StopPlayerSwitch,
    n::IsMultiFirstDone,n::MultiSecondPart,n::SwitchState,n::SwitchType,n::GetInvincible,n::SwitchMpFlag,n::EntityModel,n::HeadBlendFinished};
// This route never starts a session or touches the cloud / UGC / save natives (the creator's own session step is pinned away).
constexpr bool forbidden(std::uint32_t h) noexcept {
    return h==joaat("NETWORK_SESSION_ENTER") || h==joaat("NETWORK_SESSION_HOST") || h==joaat("NETWORK_SESSION_HOST_SINGLE_PLAYER")
        || h==joaat("STAT_SAVE") || h==joaat("STAT_LOAD") || h==joaat("NETWORK_IS_CLOUD_AVAILABLE") || h==joaat("SET_SAVE_MENU_ACTIVE");
}
constexpr bool noForbidden() noexcept {for(auto h:UsedNatives)if(forbidden(h))return false;return true;}
static_assert(noForbidden(),"the creator route must not call session / cloud / save natives");

// ------------------------------------------------------------------------------------------------ the script and its globals
constexpr std::uint32_t ScriptHash=0x5ff2841fu;      // joaat("maintransition") (selector's "MainTransition"; joaat is case-blind)
constexpr const char* ScriptName="maintransition";
constexpr std::uint32_t ScriptStack=8032;            // selector 0x454d: START_NEW_SCRIPT("MainTransition", 8032), no arguments
static_assert(joaat("maintransition")==ScriptHash);
// The VM's global-block table (r21 in the GLOBAL_U24 handlers loc_8343A7C4 / loc_8343A7F4: block = g >> 18, word = block + 4 * (g &
// 0x3FFFF), a null block = the VM's error path). The table's address is the VM's r4 argument (lester-blip lane, OBSERVED in
// ppc_recomp.18 / 295: lis r9,-31802; addi r4,r9,-12728).
constexpr std::uint32_t GlobalsBlockTable=0x83C5CE48u;
constexpr std::uint32_t BlockOwnerTable=0x83C5CA30u,BlockSizeTable=0x83C5CF50u;   // v989: sub_83438C20's owner / size tables
// The owning program's globals signature (header +0x0C) per block; 0 = not decoded (only non-null + size checked): 0 startup,
// 1 tunables_registration, 3 ugc_global_registration, 5 mp_registration, 6 mp_fm_registration (lead-0607 block5.md V5 / section 0).
inline constexpr std::uint32_t BlockOwner[8]{0xfdf69e36u,0x3f644fe5u,0u,0xe0099fb4u,0u,0xc74193eau,0x5334a1beu,0u};
// maintransition reads blocks 0, 1, 3, 4, 5, 6, 7 (dis_mt_all.txt counts 632/10/278/61/10110/124/2); a null one kills its thread at
// loc_8343AE2C, so the preflight refuses unless each is placed.
inline constexpr std::uint32_t BlocksUsed[]{1,3,4,6,7};
namespace g {
constexpr std::uint32_t UiState=1344033,Transition=1344034,CreatorDone=1345827,CreatorDone2=1345828,LastMpChar=1338478,
    EntryMode=19455,                 // maintransition 0x243 SWITCH 0x68c2f() = G19455: case 0 = no FORCE_CLEANUP(2) (default branch has it)
    PendingMode=19456,               // v989: the mode to enter next (lead-0607 entrymode.md); startup / selector write 999
    ModeMirror=1332466,              // v989: the setter 0x144a writes it with G19455 (map: 0/1/2 as is, -1 and 999 -> -1)
    G1345772At9=1345772+1+9,
    SwitchPed=1345771,
    SwitchMpState=1332481+17,        // v993: G1332498, the switch-to-multi state word (0x51b: 1 then 2; descent 0x449fa: 4, then 0)               // v992: G1345771, the other ped of maintransition's player switch (0x449fa START_PLAYER_SWITCH, 0x68090 DELETE_PED)         // v989 log only: maintransition's T2 0x36f3d(9) sets G1345772[9] (array word = base + 1 + index)
    NewSlotBase=1332509,             // G1332502.6[slot]  (0x40c27: GLOBAL_U24 1332502, IOFFSET_U8 6, ARRAY_U8_STORE 1)
    SlotUsedBase=1332399,            // G1332398[slot]    (0x3b80c, set by the commit)
    CurrentSlotSp=59751,             // 0x350ba: GLOBAL_U16_STORE 59751
    ActiveSlot=1344064,CharType=1344063,
    CharTypeCacheBase=1338473,       // G1338472[slot]    (0x40d66)
    IntTableHash1017Slot0=1363891+6*1017;   // G1363889[1017][0] (count word, then [5, h0..h4] per enum)
constexpr std::uint32_t Hash1017Slot0=0x46baf40fu;   // maintransition 0x68c3a's literal for (1017, 0): the addressing check
}
constexpr std::uint32_t StoryMode=999;   // startup 0x1a5: PUSH_S16 999; GLOBAL_U16_STORE 19455
constexpr std::uint32_t CreatorUi=5,AfterCommitUi=6,CharSelectUi=4,IdleTransition=0,CreatorCharType=9;
constexpr bool watchdogUi(std::uint32_t ui) noexcept {return ui==13 || ui==14 || ui==15 || ui==25;}   // 0x67c95's sign-in / privilege exits

// ------------------------------------------------------------------------------------------------ the creator's stats (commit 0x3b382)
// Hashes for slot 0 (MP0_*), from maintransition's own init 0x68c3a (tools/stat_tables.py); names where the joaat of a public
// name matches (OBSERVED), else the script's enum number. A float's table is G1371618, an int's G1363889.
struct StatHash {int en;std::uint32_t slot0;const char* name;};
inline constexpr StatHash FloatStats[]{
    {87,0x7b6c7663u,"overlay0-opacity"},{88,0xbc5b7956u,"overlay1-opacity"},{89,0xa1bb6b29u,"overlay2-opacity"},
    {90,0x07098e05u,"overlay3-opacity"},{91,0xdb388d66u,"overlay4-opacity"},{92,0x707b19d4u,"overlay5-opacity"},
    {93,0x2e8a8a42u,"overlay6-opacity"},{94,0x60fea628u,"MP0_HEADBLEND_GEOM_BLEND"},{95,0x06e2b393u,"MP0_HEADBLEND_TEX_BLEND"},
    {97,0xf8f52825u,"MP0_HEADBLEND_DOM"}};
inline constexpr StatHash IntStats[]{
    {1017,0x46baf40fu,"lifestyle0"},{1018,0xd44e43f0u,"lifestyle1"},{1019,0x3aab1289u,"lifestyle2"},{1020,0xdeace3b6u,"lifestyle3"},
    {1021,0xa6ed05a9u,"lifestyle4"},{1022,0x2de644d6u,"lifestyle5"},{1023,0x1f65e838u,"lifestyle6"},
    {1040,0xf6d51d33u,"enum1040"},{1041,0x2a4d94a5u,"enum1041"}};
// Packed (8-bit) values: index p in 384..456 -> key GET_PACKED_INT_STAT_KEY(p - 384, 0, 1, slot), bit offset
// ((p - 384) - ca160bcc(p - 384) * 8) * 8, 8 bits (0x35a5e / 0x68668). A negative value is stored as 255.
constexpr int PackedFirst=384,PackedEnd=457;
namespace pk {
// v998: 387+c holds the TEXTURE and 399+c the DRAWABLE (0x35b87 -> 0x3616f fills 399+c from GET_PED_DRAWABLE_VARIATION 29850fe2 and
// 387+c from GET_PED_TEXTURE_VARIATION c0a8590a; 411+c from GET_PED_PALETTE_VARIATION ef1bc082). v988..v997 read them swapped.
// Enum385 is the creator's hair item (0x3abef), the index into the game's freemode hair table (H_FMF_ / H_FMM_ d_t).
constexpr int CharType=384,Enum385=385,Gender=386,TextureBase=387,DrawableBase=399,PaletteBase=411,PropBase=423,PropTextureBase=432,
    HeadB=441,HeadA=442,OverlayIndexBase=447;
}
constexpr int Components=12,PropAnchors=9,Overlays=7,Lifestyle=7;
constexpr int bitOffset(int p,std::uint32_t word) noexcept {return ((p-PackedFirst)-int(word)*8)*8;}

// ------------------------------------------------------------------------------------------------ the captured result
struct DiscResult {
    int gender=-1;                                   // packed 386 (0 when the creator's struct+18 == 0, 0x39289)
    int charType=-1;                                 // packed 384 (9 = this route's / the select's freemode type)
    int headA=-1,headB=-1;                           // packed 442 / 441 (mp_menuped 0x319d9 / 0x31a74: the two parent heads;
                                                     // 442 = the parent built with model index 0 = mp_m_freemode_01, 441 = index 1 = female)
    int geomBlend=0,texBlend=0,dominance=0;          // floats 94 / 95 / 97 in permille
    std::array<int,Overlays> overlayIndex{},overlayOpacity{};   // packed 447+k / float 87+k (0x39297(struct, k))
    std::array<int,Components> drawable{},texture{},palette{};  // packed 399+c / 387+c / 411+c (0x35694; see pk)
    std::array<int,PropAnchors> prop{},propTexture{};           // packed 423+k / 432+k; 255 = none
    int enum385=0;
    std::array<int,Lifestyle> lifestyle{};
    int enum1040=0,enum1041=0;
};
inline int permille(float f) noexcept {
    if(!std::isfinite(f))return -1;
    const long v=std::lround(double(f)*1000.0);
    return v<0||v>1000?-1:int(v);
}

// One read of the capture plan.
enum class Kind : std::uint8_t {Packed,Int,Float};
struct Read {Kind kind;int en;};
// The plan, in a fixed order (Capture::apply puts each value in its field).
inline constexpr int PlanSize=2+2+Overlays+3*Components+2*PropAnchors+1 + 9 + 10;
inline std::array<Read,PlanSize> plan() noexcept {
    std::array<Read,PlanSize> p{};int k=0;
    p[k++]={Kind::Packed,pk::Gender};p[k++]={Kind::Packed,pk::CharType};p[k++]={Kind::Packed,pk::HeadA};p[k++]={Kind::Packed,pk::HeadB};
    for(int i=0;i<Overlays;++i)p[k++]={Kind::Packed,pk::OverlayIndexBase+i};
    for(int c=0;c<Components;++c){p[k++]={Kind::Packed,pk::DrawableBase+c};p[k++]={Kind::Packed,pk::TextureBase+c};p[k++]={Kind::Packed,pk::PaletteBase+c};}
    for(int a=0;a<PropAnchors;++a){p[k++]={Kind::Packed,pk::PropBase+a};p[k++]={Kind::Packed,pk::PropTextureBase+a};}
    p[k++]={Kind::Packed,pk::Enum385};
    for(const auto& s:IntStats)p[k++]={Kind::Int,s.en};
    for(const auto& s:FloatStats)p[k++]={Kind::Float,s.en};
    return p;
}
inline std::uint32_t hashOf(Kind k,int en) noexcept {
    if(k==Kind::Int){for(const auto& s:IntStats)if(s.en==en)return s.slot0;}
    else if(k==Kind::Float){for(const auto& s:FloatStats)if(s.en==en)return s.slot0;}
    return 0;
}
inline void store(DiscResult& r,const Read& rd,std::uint32_t raw) noexcept {
    const int v=int(raw);
    if(rd.kind==Kind::Packed) {
        const int p=rd.en,b=v&0xFF;
        if(p==pk::Gender)r.gender=b;else if(p==pk::CharType)r.charType=b;else if(p==pk::HeadA)r.headA=b;else if(p==pk::HeadB)r.headB=b;
        else if(p==pk::Enum385)r.enum385=b;
        else if(p>=pk::OverlayIndexBase && p<pk::OverlayIndexBase+Overlays)r.overlayIndex[std::size_t(p-pk::OverlayIndexBase)]=b;
        else if(p>=pk::DrawableBase && p<pk::DrawableBase+Components)r.drawable[std::size_t(p-pk::DrawableBase)]=b;
        else if(p>=pk::TextureBase && p<pk::TextureBase+Components)r.texture[std::size_t(p-pk::TextureBase)]=b;
        else if(p>=pk::PaletteBase && p<pk::PaletteBase+Components)r.palette[std::size_t(p-pk::PaletteBase)]=b;
        else if(p>=pk::PropBase && p<pk::PropTextureBase)r.prop[std::size_t(p-pk::PropBase)]=b;
        else if(p>=pk::PropTextureBase && p<pk::PropTextureBase+PropAnchors)r.propTexture[std::size_t(p-pk::PropTextureBase)]=b;
    } else if(rd.kind==Kind::Int) {
        if(rd.en>=1017 && rd.en<=1023)r.lifestyle[std::size_t(rd.en-1017)]=v;else if(rd.en==1040)r.enum1040=v;else if(rd.en==1041)r.enum1041=v;
    } else {
        const int m=permille(asFloat(raw));
        if(rd.en>=87 && rd.en<=93)r.overlayOpacity[std::size_t(rd.en-87)]=m;else if(rd.en==94)r.geomBlend=m;else if(rd.en==95)r.texBlend=m;
        else if(rd.en==97)r.dominance=m;
    }
}
// One stat read through the game's own natives (the same calls the disc's 0x68668 / 0x7120 / 0x35616 make). False: unavailable.
inline bool readOne(Natives& nat,const Read& rd,int slot,std::uint32_t& out) noexcept {
    std::uint32_t r[3]{};
    const std::uint32_t cell=nat.word(0);
    if(!cell)return false;
    if(rd.kind==Kind::Packed) {
        const std::uint32_t i=std::uint32_t(rd.en-PackedFirst);
        std::uint32_t a[4]{i,0,1,std::uint32_t(slot)};
        if(!nat.call(n::PackedKey,a,4,r))return false;
        const std::uint32_t key=r[0];
        if(!nat.call(n::PackedWord,&i,1,r))return false;
        const int off=bitOffset(rd.en,r[0]);
        if(off<0 || off>56)return false;
        std::uint32_t b[5]{key,cell,std::uint32_t(off),8,0xFFFFFFFFu};
        if(!nat.call(n::StatGetMaskedInt,b,5,r) || !r[0])return false;
    } else {
        const std::uint32_t h=hashOf(rd.kind,rd.en);
        if(!h || slot!=0)return false;   // the embedded hashes are slot 0's (this route always creates slot 0)
        std::uint32_t a[3]{h,cell,0xFFFFFFFFu};
        if(!nat.call(rd.kind==Kind::Int?n::StatGetInt:n::StatGetFloat,a,3,r) || !r[0])return false;
    }
    out=nat.load(cell);
    return true;
}

// The gender index -> model: mp_menuped 0x31c23(9, g) = G115067 +7 (g == 0) / +8 (g != 0), which startup 0x350bf / 0x350ca fills
// with the literals 0x705e61f2 = joaat("mp_m_freemode_01") and 0x9c9effd8 = joaat("mp_f_freemode_01") (OBSERVED).
inline std::uint32_t modelOf(int gender) noexcept {return gender==0?creator::MaleModel:gender==1?creator::FemaleModel:0u;}

// The route-B record (character_creator.h Look). APPROXIMATION, stated in NOTES.md 4.3: the disc builds the online head with
// SET_PED_BLEND_FROM_PARENTS(child, parentA, parentB, dominance, -1.0) over two hidden parent peds, each blended (h, h+1) by
// GEOM_BLEND (mp_menuped 0x3190c); route B has no parent peds, so v1 blends the two parent heads directly by the dominance.
struct Mapped {creator::Look look;int propsDropped=0;bool ok=false;const char* why="";int eyebrowsDefaulted=-1;};   // v1106: the eyebrow index whose opacity was defaulted
constexpr int EyebrowSlot=2;   // v1106: head overlay 2 = the eyebrows
inline Mapped toLook(const DiscResult& r,std::uint32_t model) noexcept {
    Mapped m;
    if(!creator::isFreemode(model)){m.why="model-unknown";return m;}
    creator::Look l;l.model=model;
    const auto head=[](int h){return h>=0 && h<=creator::MaxHeadId;};
    if(!head(r.headA) || !head(r.headB)){m.why="head-out-of-range";return m;}
    if(r.dominance<0 || r.geomBlend<0){m.why="blend-out-of-range";return m;}
    l.shapeFirst=r.headA;l.shapeSecond=r.headB;l.shapeMix=r.dominance;l.skinExplicit=false;
    for(int k=0;k<Overlays;++k) {
        const int i=r.overlayIndex[std::size_t(k)],o=r.overlayOpacity[std::size_t(k)];
        // v1106 INTERIM (owner: no eyebrows on the created character): the eyebrows (overlay 2) with a valid index but opacity stat 89 read 0
        // take full opacity - the creator's preview applied them at 1.000 (t-onlcre104: SET_PED_HEAD_OVERLAY slot=2 index=11 opacity=1.000)
        // and the default look uses Permille; same suspected cause as the hair (the creator's stat writes not read back).
        if(k==EyebrowSlot && i>=0 && i!=255 && o<=0){l.overlay[std::size_t(k)]={i,creator::Permille};m.eyebrowsDefaulted=i;continue;}
        if(i==255 || i<0 || o<=0)l.overlay[std::size_t(k)]={0,0};else l.overlay[std::size_t(k)]={i,o};
    }
    for(int c=0;c<Components;++c) {
        const int d=r.drawable[std::size_t(c)],t=r.texture[std::size_t(c)],p=r.palette[std::size_t(c)];
        l.part[std::size_t(c)]={d==255?0:d,t==255?0:t,(p>=0 && p<=3)?p:0};
    }
    for(int a=0;a<PropAnchors;++a) {
        const int d=r.prop[std::size_t(a)],t=r.propTexture[std::size_t(a)];
        int slot=-1;for(int s=0;s<creator::PropSlots;++s)if(int(creator::PropAnchor[std::size_t(s)])==a)slot=s;
        if(slot<0){if(d!=255)++m.propsDropped;continue;}
        l.prop[std::size_t(slot)]=d==255?creator::Prop{-1,0}:creator::Prop{d,t==255?0:t};
    }
    if(!creator::inRange(l)){m.why="look-out-of-range";return m;}
    m.look=l;m.ok=true;return m;
}

// The raw record (<generation>/online/creator.json): every captured value, so an exact parents-blend apply can come later without
// running the creator again. Integers only.
constexpr const char* RawFormat="fivepocket-disc-creator";
constexpr int RawVersion=1,RawMax=2048;
inline int serializeRaw(const DiscResult& r,int slot,std::uint32_t model,char* out,int cap) noexcept {
    if(!out || cap<=0)return -1;
    int k=0;bool ok=true;
    const auto put=[&](const char* f,auto... a){if(!ok)return;const int w=std::snprintf(out+k,std::size_t(cap-k),f,a...);if(w<0 || w>=cap-k){ok=false;return;}k+=w;};
    const auto arr=[&](const char* key,const int* v,int nv,bool last=false){put("\"%s\":[",key);for(int i=0;i<nv;++i)put(i?",%d":"%d",v[i]);put(last?"]}\n":"],\n");};
    put("{\"format\":\"%s\",\"version\":%d,\"slot\":%d,\"model\":\"%s\",\"gender\":%d,\"char_type\":%d,\n",RawFormat,RawVersion,slot,
        model==creator::FemaleModel?"female":model==creator::MaleModel?"male":"unknown",r.gender,r.charType);
    put("\"heads\":[%d,%d],\"geom_blend\":%d,\"tex_blend\":%d,\"dominance\":%d,\"enum385\":%d,\"enum1040\":%d,\"enum1041\":%d,\n",
        r.headA,r.headB,r.geomBlend,r.texBlend,r.dominance,r.enum385,r.enum1040,r.enum1041);
    arr("overlay_index",r.overlayIndex.data(),Overlays);arr("overlay_opacity",r.overlayOpacity.data(),Overlays);
    arr("drawable",r.drawable.data(),Components);arr("texture",r.texture.data(),Components);arr("palette",r.palette.data(),Components);
    arr("prop",r.prop.data(),PropAnchors);arr("prop_texture",r.propTexture.data(),PropAnchors);arr("lifestyle",r.lifestyle.data(),Lifestyle,true);
    return ok && k<=RawMax?k:-1;
}

// ------------------------------------------------------------------------------------------------ the glue's side
struct Ops {
    virtual ~Ops()=default;
    virtual bool sandbox()=0;                       // recomp::sandbox::active() (the play-mode latch)
    virtual bool devSwitch()=0;                     // files/private-trace/diag-online-local (read once per process)
    virtual bool storyUnchanged()=0;                // the story-save snapshot taken at the Sandbox start is still what is on disk
    virtual void holdSaves(bool on)=0;              // the Sandbox core's save-script hold (sandbox_mode.h; bit 4 of the state word)
    virtual bool hostIs(std::uint32_t threadId)=0;  // the tick is hosted by a native call of this thread (then: no global write)
    virtual bool storeWord(std::uint32_t addr,std::uint32_t value)=0;   // one big-endian guest word (the glue checks the range)
    virtual std::uint32_t start(std::uint32_t nameAddr,std::uint32_t stack)=0;   // sub_82AAC528(name, 0, 0, stack) -> id (0 failed)
    virtual bool saveLook(const creator::Look& l)=0;                // character.json (FileStore, the Sandbox generation only)
    virtual bool saveRaw(const char* text,int len)=0;               // creator.json (FileStore "creator.json", same guard)
    virtual bool requestTrainer(int action,int a0)=0;               // queue a trainer request (15 / 17); true = accepted
    // v1066 (Online mode's first visit, online_mode.h Creator state): keep the screen dark after the creator (no fade-in in the player's
    // restore or the descent's cleanup, no wait for it) and end right after character.json / creator.json are saved: the OnlineMode
    // controller applies the saved character itself under its curtain, so no Story character is ever shown. Sandbox: false (unchanged).
    virtual bool keepDark() {return false;}
};
using LogFn=void(*)(const char*);

constexpr std::uint32_t DescendFirstFrames=300,DescendSecondFrames=900;   // v993: 10 s for FIRSTPART, 30 s after SECONDPART (UNCONFIRMED bounds)
constexpr std::uint32_t ApplyFrames=2700,ApplyLookFrames=10;
constexpr std::uint32_t LookSettleFrames=300,LookWaitFrames=1800;   // v995: 10 s settle after 15, 60 s at most for the model + blend   // v990: 90 s at 30 fps for the switch camera to end; a look every 10 frames
constexpr std::uint32_t LoadFrames=600,EntryFrames=120,CreatorFrames=54000,TerminateFrames=300,ReissueFrames=30,FadeFrames=120,
    MonitorThreadFrames=30;
constexpr int ReadsPerTick=8,StartOp=0,CancelOp=1,OpCount=2;
constexpr std::uint32_t FadeInMs=500;

enum class St : std::uint8_t {Idle,Bootstrap,Load,Start,WaitEntry,Pin,Creator,Teardown,Descend,Restore,Capture,Save,Apply,Done,Failed};
inline const char* stName(St s) noexcept {
    static const char* const k[]={"idle","bootstrap","load","start","wait-entry","pin","creator","teardown","descend","restore","capture",
        "save","apply","done","failed"};   // v993: one name per state (v990..v992 lacked "apply": Failed read past the table)
    static_assert(sizeof k/sizeof k[0]==int(St::Failed)+1);
    return k[int(s)];
}

class Route {
public:
    Route(Ops& ops,LogFn log) noexcept:ops_(ops),log_(log) {}
    St state() const noexcept {return st_;}
    bool active() const noexcept {return st_!=St::Idle && st_!=St::Done && st_!=St::Failed;}
    std::uint32_t threadId() const noexcept {return id_;}   // v989: maintransition's thread id once started (0 before)
    bool savesBlocked() const noexcept {return blocked_;}
    const DiscResult& result() const noexcept {return res_;}
    std::uint32_t thread() const noexcept {return id_;}

    static bool validArgument(int a0) noexcept {return a0>=0 && a0<OpCount;}
    // Cancel from ANY thread (the trainer's request(), the app's API thread or the one-shot reader), outside the trainer's gated queue:
    // the creator is a frontend menu, so the trainer gate reports pause-menu while it is up and would drop a queued cancel. One atomic
    // store; the Sandbox tick consumes it (Route::tick). A start clears a stale flag.
    void requestCancel() noexcept {cancel_.store(true,std::memory_order_release);}
    // The trainer's request (script thread). 1 ok, 2 fail (detail written). Start: preflight only (no side effect on a refusal).
    int request(Natives& nat,int a0,char* detail,int cap) noexcept {
        if(a0==CancelOp) {
            if(!active())return fail(detail,cap,"not-running");
            if(st_==St::Teardown || st_==St::Descend || st_==St::Restore || st_==St::Capture || st_==St::Save || st_==St::Apply)return fail(detail,cap,"finishing");
            requestCancel();
            return ok(detail,cap,"cancelling");
        }
        if(a0!=StartOp)return fail(detail,cap,"bad-op");
        if(blocked_)return fail(detail,cap,"saves-blocked");
        if(active())return fail(detail,cap,"busy");
        const char* why=preflight(nat);
        if(why){char l[128];std::snprintf(l,sizeof l,"RENDER_ONLINE creator refused reason=%s",why);log_(l);return fail(detail,cap,why);}
        st_=St::Bootstrap;frame0_=0;id_=0;captured_=0;res_=DiscResult{};reason_[0]=0;committed_=false;hostedDefers_=0;firstTerm_=0;
        cancel_.store(false,std::memory_order_release);
        if(!snapshot(nat)){st_=St::Idle;return fail(detail,cap,"snapshot");}   // reads only: the values from before the run
        {   // v993: kept OUT of snap_ (the teardown's generic restore would clear the word before Descend reads it: switch-descent.md V2.1)
            const std::uint32_t pl=call1(nat,n::PlayerId,{});inv0_=call1(nat,n::GetInvincible,{pl})&0xFF;mp0_=0;(void)getG(nat,g::SwitchMpState,mp0_);
            char l[128];std::snprintf(l,sizeof l,"RENDER_ONLINE creator switch-pre in=%u state=%d type=%d mp=%u inv=%u",call1(nat,n::SwitchInProgress,{})&0xFF,
                int(call1(nat,n::SwitchState,{})),int(call1(nat,n::SwitchType,{})),mp0_,inv0_);log_(l);
        }
        ops_.holdSaves(true);
        return ok(detail,cap,"started slot=%d",slot_);
    }
    // The Sandbox tick (script thread, once per game frame). Idle / Done / Failed: no native call.
    void tick(Natives& nat,std::uint32_t frame) noexcept {
        if(!active())return;
        if(!frame0_)frame0_=frame?frame:1;
        if(!ops_.sandbox() && st_<St::Teardown){beginTeardown(nat,frame,"left-sandbox");return;}
        if(cancel_.exchange(false,std::memory_order_acq_rel)) {
            if(st_<St::Teardown){beginTeardown(nat,frame,"cancelled");return;}
            char l[80];std::snprintf(l,sizeof l,"RENDER_ONLINE creator cancel ignored state=%s",stName(st_));log_(l);
        }
        switch(st_) {
        case St::Bootstrap: bootstrap(nat,frame);break;
        case St::Load: load(nat,frame);break;
        case St::Start: startScript(nat,frame);break;
        case St::WaitEntry: waitEntry(nat,frame);break;
        case St::Pin: pin(nat,frame);break;
        case St::Creator: monitor(nat,frame);break;
        case St::Teardown: teardown(nat,frame);break;
        case St::Descend: descend(nat,frame);break;
        case St::Restore: restore(nat,frame);break;
        case St::Capture: capture(nat,frame);break;
        case St::Save: save(nat,frame);break;
        case St::Apply: apply(nat,frame);break;
        default: break;
        }
    }

private:
    // -------------------------------------------------------------------------- preflight (reads only)
    std::uint32_t call1(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a,bool* okp=nullptr) noexcept {
        std::uint32_t out[3]{};const bool r=nat.call(h,a.begin(),unsigned(a.size()),out);if(okp)*okp=r;return r?out[0]:0u;
    }
    void call0(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
        std::uint32_t out[3]{};(void)nat.call(h,a.begin(),unsigned(a.size()),out);
    }
    // v986: the whole mapped guest range. v985 on the Thor: block 5 (mp_registration, allocated by its program load) existed but this
    // test, written against the virtual-heap blocks seen before (0x4/0x7...), refused a block outside 0x40000000..0x9FFFFFFF
    // (the 360's physical ranges 0xA0000000+ are valid guest memory). Still: non-null, >= 0x40000000, word-aligned.
    // v989 (lead's block5.md, verified): the game's own proof of a placed block instead of an address window. sub_83438C20 (the
    // program placement) writes table[b] (0x83C5CE48), size[b] = the word count (0x83C5CF50) and owner[b] = the program's globals
    // signature (0x83C5CA30). A block is accepted when its pointer is non-null and aligned, its owner is the expected program where
    // that is decoded, and the global's word lies inside its size. The VM itself only tests for null (loc_8343A7C4).
    static bool aligned(std::uint32_t a) noexcept {return a && !(a&3u);}
    std::uint32_t block(Natives& nat,std::uint32_t gl) noexcept {return nat.load(GlobalsBlockTable+4u*(gl>>18));}
    bool blockOk(Natives& nat,std::uint32_t b,std::uint32_t word) noexcept {
        if(b>=std::size(BlockOwner))return false;
        if(!aligned(nat.load(GlobalsBlockTable+4u*b)))return false;
        if(BlockOwner[b] && nat.load(BlockOwnerTable+4u*b)!=BlockOwner[b])return false;
        return word<nat.load(BlockSizeTable+4u*b);
    }
    std::uint32_t addr(Natives& nat,std::uint32_t gl) noexcept {
        const std::uint32_t b=gl>>18,w=gl&0x3FFFFu;
        return blockOk(nat,b,w)?block(nat,gl)+4u*w:0u;
    }
    bool getG(Natives& nat,std::uint32_t gl,std::uint32_t& v) noexcept {const std::uint32_t a=addr(nat,gl);if(!a)return false;v=nat.load(a);return true;}
    const char* preflight(Natives& nat) noexcept {
        static char why[96];
        if(!ops_.sandbox())return "story-mode";
        if(!ops_.devSwitch())return "dev-switch-off";
        bool okc=false;
        if(call1(nat,n::NetworkGame,{},&okc) || !okc)return okc?"network-game":"native-unavailable";
        const std::uint32_t si=call1(nat,n::SignedIn,{}),so=call1(nat,n::SignedOnline,{}),pv=call1(nat,n::OnlinePrivileges,{});
        if(!(si&0xFF) || !(so&0xFF) || !(pv&0xFF)){   // (answered by the signin-spoof lane; this route only reads them)
            std::snprintf(why,sizeof why,"needs-signin-spoof signed_in=%u online=%u priv=%u",si&0xFF,so&0xFF,pv&0xFF);return why;}
        if(call1(nat,n::MissionFlag,{}))return "on-mission";
        if(call1(nat,n::PauseMenuActive,{})&0xFF)return "pause-menu";
        if(call1(nat,n::ThreadsWithHash,{ScriptHash},&okc) || !okc)return okc?"already-running":"native-unavailable";
        std::uint32_t ui=0,tr=0,mode=0,last=0,table=0;
        if(!getG(nat,g::UiState,ui) || !getG(nat,g::Transition,tr) || !getG(nat,g::LastMpChar,last))return "globals-block-5";
        if(!getG(nat,g::EntryMode,mode))return "globals-block-0";
        // G19455 = the current game mode (every script's accessor; startup writes 999, maintransition 0x6623c writes 0 = freemode).
        // The entry SWITCH needs 0: 999 (story, what the Sandbox reads) takes the default branch with FORCE_CLEANUP(2), the game's own
        // story -> online cleanup. v987: 999 is pinned to 0 just before the start (pinMode_), restored at the teardown with the rest.
        // v989 (lead-0607 entrymode.md V3): both the current and the pending mode must read 999 (Story / Sandbox); the start tick then
        // does exactly what the game's setter 0x144a(0) does (G19455 = 0, G1332466 = 0; pending stays 999) and Pin puts all three back.
        std::uint32_t pending=0,mirror=0;
        if(!getG(nat,g::PendingMode,pending) || !getG(nat,g::ModeMirror,mirror))return "globals-block-0";
        if(mode!=StoryMode || pending!=StoryMode){std::snprintf(why,sizeof why,"entry-mode=%u pending=%u",mode,pending);return why;}
        pinMode_=true;
        // v993: a Story switch in progress would be stopped by the entry's re-entry guard (0x003e1 / 0x0049a); a non-zero switch-to-multi
        // word is a leak from an earlier run (the Sandbox steady state is 0)
        if(call1(nat,n::SwitchInProgress,{})&0xFF)return "switch-in-progress";
        {std::uint32_t mp=0;if(getG(nat,g::SwitchMpState,mp) && mp!=0){std::snprintf(why,sizeof why,"switch-mp-state=%u",mp);return why;}}
        for(std::uint32_t b:BlocksUsed)if(!blockOk(nat,b,0)){std::snprintf(why,sizeof why,"globals-block-%u",b);return why;}
        if(ui!=0 || tr!=0){std::snprintf(why,sizeof why,"transition-globals-dirty ui=%u transition=%u",ui,tr);return why;}
        if(last!=0){std::snprintf(why,sizeof why,"last-mp-char=%u",last);return why;}            // the entry copies it as the slot
        if(getG(nat,g::IntTableHash1017Slot0,table) && table!=0 && table!=g::Hash1017Slot0)return "globals-addressing";
        verified_=table==g::Hash1017Slot0;   // (ingamehud / freemode_init fill the same table: often already proved here)
        slot_=0;
        return nullptr;
    }
    // -------------------------------------------------------------------------- steps
    void bootstrap(Natives& nat,std::uint32_t) noexcept {
        for(std::uint32_t s=0;s<=5;++s)(void)call1(nat,n::StatDeleteSlot,{s});            // maintransition 0x295f, as is
        for(std::uint32_t c=0;c<=7;++c)call0(nat,n::StatResetAllOnline,{c});
        const std::uint32_t loaded=call1(nat,n::StatSlotIsLoaded,{0})&0xFF,failed=call1(nat,n::StatCloudSlotLoadFailed,{0})&0xFF;
        char l[96];std::snprintf(l,sizeof l,"RENDER_ONLINE creator mpslot loaded=%u failed=%u",loaded,failed);log_(l);
        if(!loaded || failed){finishFailed("mpslot",false);return;}   // nothing written to guest memory yet: no teardown needed
        const std::uint32_t name=nat.text(ScriptName);
        if(!name){finishFailed("no-scratch",false);return;}
        call0(nat,n::RequestScript,{name});
        st_=St::Load;stepAt_=frame0_;
    }
    void load(Natives& nat,std::uint32_t frame) noexcept {
        const std::uint32_t name=nat.text(ScriptName);
        if(name && (call1(nat,n::HasScriptLoaded,{name})&0xFF)){st_=St::Start;return;}
        if(name)call0(nat,n::RequestScript,{name});
        if(frame-stepAt_>LoadFrames)finishFailed("load-timeout",false);
    }
    void startScript(Natives& nat,std::uint32_t frame) noexcept {
        const std::uint32_t name=nat.text(ScriptName);
        bool pinned=false;
        if(pinMode_ && name){
            modeWritten_=true;   // (from the first store on: whatever took is put back)
            pinned=put(nat,g::EntryMode,0) && put(nat,g::ModeMirror,0);   // = 0x144a(0); G19456 stays 999
            char m[96];std::snprintf(m,sizeof m,"RENDER_ONLINE creator game-mode pin %u->0 ok=%d",StoryMode,pinned?1:0);log_(m);
            if(!pinned){(void)restoreMode(nat);finishFailed("mode-pin",false);return;}
        }
        id_=name?ops_.start(name,ScriptStack):0;
        if(pinned && !id_)(void)restoreMode(nat);   // no thread: put back at once
        if(name)call0(nat,n::ScriptNoLongerNeeded,{name});
        char l[96];std::snprintf(l,sizeof l,"RENDER_ONLINE creator start id=%u slot=%d stack=%u",id_,slot_,ScriptStack);log_(l);
        if(!id_){finishFailed("start-failed",false);return;}
        st_=St::WaitEntry;stepAt_=frame;
    }
    bool alive(Natives& nat) noexcept {bool okc=false;const std::uint32_t k=call1(nat,n::ThreadsWithHash,{ScriptHash},&okc);return okc && k>0;}
    void waitEntry(Natives& nat,std::uint32_t frame) noexcept {
        if(!alive(nat)){beginTeardown(nat,frame,"thread-ended-at-entry");return;}
        std::uint32_t tr=0,table=0,last=0;
        if(getG(nat,g::Transition,tr) && tr!=0 && getG(nat,g::IntTableHash1017Slot0,table) && table==g::Hash1017Slot0) {
            entryTransition_=tr;verified_=true;
            if(!getG(nat,g::LastMpChar,last)){beginTeardown(nat,frame,"globals-block-5");return;}
            // (the entry copied G1338478 into its slot static before its main loop could change G1338478; preflight saw 0)
            st_=St::Pin;
            pin(nat,frame);
            return;
        }
        if(frame-stepAt_>EntryFrames)beginTeardown(nat,frame,table && table!=g::Hash1017Slot0?"globals-addressing":"entry-timeout");
    }
    // The snapshot of every global this route writes or the entry changes (taken at the request, reads only). Restored at the
    // teardown only when the addressing was proved (else nothing is written at all).
    struct Saved {std::uint32_t gl,value;};
    static constexpr int SnapMax=16;
    std::array<Saved,SnapMax> snap_{};int snapN_=0;bool haveSnap_=false,verified_=false,pinMode_=false,modeWritten_=false;
    std::uint32_t snapOf(std::uint32_t gl) const noexcept {for(int i=0;i<snapN_;++i)if(snap_[std::size_t(i)].gl==gl)return snap_[std::size_t(i)].value;return StoryMode;}
    // The three mode words back from the snapshot (Pin, teardown, a failed start). true when every store took.
    bool restoreMode(Natives& nat) noexcept {
        const bool a=put(nat,g::EntryMode,snapOf(g::EntryMode)),b=put(nat,g::PendingMode,snapOf(g::PendingMode)),
            c=put(nat,g::ModeMirror,snapOf(g::ModeMirror));   // (all three tried, none skipped by an earlier failure)
        const bool ok=a && b && c;
        if(ok)modeWritten_=false;
        return ok;
    }
    bool snapshot(Natives& nat) noexcept {
        const std::uint32_t list[]{g::UiState,g::Transition,g::CreatorDone,g::CreatorDone2,g::NewSlotBase+std::uint32_t(slot_),
            g::SlotUsedBase+std::uint32_t(slot_),g::CurrentSlotSp,g::ActiveSlot,g::CharType,g::CharTypeCacheBase+std::uint32_t(slot_),
            g::LastMpChar,g::EntryMode,g::PendingMode,g::ModeMirror};
        snapN_=0;
        for(std::uint32_t gl:list){std::uint32_t v=0;if(!getG(nat,gl,v))return false;snap_[std::size_t(snapN_++)]={gl,v};}
        haveSnap_=true;return true;
    }
    bool put(Natives& nat,std::uint32_t gl,std::uint32_t v) noexcept {const std::uint32_t a=addr(nat,gl);return a && ops_.storeWord(a,v);}
    void pin(Natives& nat,std::uint32_t frame) noexcept {
        if(ops_.hostIs(id_)){if(frame-stepAt_>EntryFrames)beginTeardown(nat,frame,"pin-hosted");return;}   // never write mid-slice of it
        const std::uint32_t s=std::uint32_t(slot_);
        // v989: the entry has run its SWITCH: the mode words go back now (the creator UI 5 does not read them; UI 6 then stays inert),
        // so the Sandbox's story scripts see Freemode for the entry slice only (lead-0607 entrymode.md R2 / V3 step 4)
        std::uint32_t uiBefore=0,pendingBefore=0,f9=0;(void)getG(nat,g::UiState,uiBefore);(void)getG(nat,g::PendingMode,pendingBefore);
        const bool f9ok=getG(nat,g::G1345772At9,f9);
        const bool modeBack=!modeWritten_ || restoreMode(nat);
        bool w=modeBack && put(nat,g::NewSlotBase+s,1)&&put(nat,g::CurrentSlotSp,s)&&put(nat,g::ActiveSlot,s)&&put(nat,g::CharType,CreatorCharType)
            &&put(nat,g::CharTypeCacheBase+s,CreatorCharType)&&put(nat,g::CreatorDone,0)&&put(nat,g::CreatorDone2,0)
            &&put(nat,g::Transition,IdleTransition)&&put(nat,g::UiState,CreatorUi);
        // 0x40d3e(9, slot): packed 384 = 9 for the slot (index 0: offset 0, 8 bits, save flag 1 as the disc passes it)
        if(w) {
            std::uint32_t r[3]{};std::uint32_t a[4]{0,0,1,s};
            w=nat.call(n::PackedKey,a,4,r);
            if(w){std::uint32_t b[5]{r[0],CreatorCharType,0,8,1};w=nat.call(n::StatSetMaskedInt,b,5,r);}
        }
        if(!w){beginTeardown(nat,frame,"pin-write");return;}
        char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE creator pinned slot=%d entry_transition=%u pin=late ui_before=%u pending_before=%u g1345772_9=%d mode_back=%d",
            slot_,entryTransition_,uiBefore,pendingBefore,f9ok?int(f9):-1,int(modeBack));log_(l);
        st_=St::Creator;stepAt_=frame;
    }
    void monitor(Natives& nat,std::uint32_t frame) noexcept {
        std::uint32_t done=0,ui=0,tr=0;
        if(!getG(nat,g::CreatorDone,done) || !getG(nat,g::UiState,ui) || !getG(nat,g::Transition,tr)){beginTeardown(nat,frame,"globals-block-5");return;}
        if(done==1 && (ui==AfterCommitUi || ui==CreatorUi)) {
            committed_=true;
            char l[96];std::snprintf(l,sizeof l,"RENDER_ONLINE creator committed frames=%u ui=%u",frame-stepAt_,ui);log_(l);
            beginTeardown(nat,frame,"committed");return;
        }
        if(ui==CharSelectUi){beginTeardown(nat,frame,"user-quit");return;}
        if(watchdogUi(ui)){char r[40];std::snprintf(r,sizeof r,"watchdog-ui=%u",ui);beginTeardown(nat,frame,r);return;}
        if(ui!=CreatorUi){char r[40];std::snprintf(r,sizeof r,"ui=%u",ui);beginTeardown(nat,frame,r);return;}
        if(tr!=IdleTransition){char r[40];std::snprintf(r,sizeof r,"transition=%u",tr);beginTeardown(nat,frame,r);return;}   // never fight it
        if((frame-stepAt_)%MonitorThreadFrames==0 && !alive(nat)){beginTeardown(nat,frame,"thread-ended");return;}
        if(frame-stepAt_>CreatorFrames)beginTeardown(nat,frame,"timeout");
    }
    // The end was seen on this tick: TERMINATE_THREAD now, before maintransition's next slice can run a transition this slice set
    // (UI 4 + 61 on quit, 5 on HUD_CHNGCHARCON, any transition=N / ui=N), unless maintransition's own native call hosts this tick
    // (never terminate the thread whose call we are inside: teardown() sends it on the first tick hosted elsewhere).
    void beginTeardown(Natives& nat,std::uint32_t frame,const char* why) noexcept {
        std::snprintf(reason_,sizeof reason_,"%s",why);
        st_=St::Teardown;tdStart_=frame?frame:1;lastTerm_=0;globalsRestored_=false;
        if(id_ && !ops_.hostIs(id_) && alive(nat))terminate(nat,frame);   // (alive: maintransition was not running before our start)
    }
    void terminate(Natives& nat,std::uint32_t frame) noexcept {
        call0(nat,n::TerminateThread,{id_});lastTerm_=frame?frame:1;
        if(!firstTerm_)firstTerm_=lastTerm_;
    }
    void teardown(Natives& nat,std::uint32_t frame) noexcept {
        if(!tdStart_)tdStart_=frame?frame:1;
        const bool late=frame-tdStart_>TerminateFrames;
        if(id_ && ops_.hostIs(id_)) {   // inside the dying script's own native call: no terminate, no global write, no native yet
            ++hostedDefers_;
            if(late)timeoutEnd(nat,"hosted-timeout");
            return;
        }
        if(id_ && alive(nat)) {
            if(!lastTerm_ || frame-lastTerm_>=ReissueFrames)terminate(nat,frame);
            if(late)timeoutEnd(nat,"timeout");
            return;
        }
        {   // v993: the switch as the thread left it, before any restore
            std::uint32_t mp=0;(void)getG(nat,g::SwitchMpState,mp);
            char l[144];std::snprintf(l,sizeof l,"RENDER_ONLINE creator switch-td in=%u state=%d type=%d first_done=%u mp=%u",
                call1(nat,n::SwitchInProgress,{})&0xFF,int(call1(nat,n::SwitchState,{})),int(call1(nat,n::SwitchType,{})),call1(nat,n::IsMultiFirstDone,{})&0xFF,mp);log_(l);
        }
        const bool restoreGlobals=haveSnap_ && verified_ && id_!=0;
        if(restoreGlobals)for(int i=0;i<snapN_;++i)(void)put(nat,snap_[std::size_t(i)].gl,snap_[std::size_t(i)].value);   // (thread gone)
        if(modeWritten_)(void)restoreMode(nat);   // v989: written under the block proof, put back whether or not the entry proved the table
        globalsRestored_=restoreGlobals;
        logTeardown("ok");
        restorePlayer(nat);
        st_=St::Descend;descendAt_=frame;secondAt_=0;secondSent_=false;   // v993
    }
    // v993 (lead-0607 switch-descent.md 4.3 + V2.1): maintransition's own descent 0x449fa(0,1,0,1), one decision per tick, driven by the
    // engine (in progress && FIRSTPART finished -> SECONDPART); the word G1332498 only for the 1 -> 2 step and the log. Commit and cancel alike.
    void descend(Natives& nat,std::uint32_t frame) noexcept {
        std::uint32_t mp=0;(void)getG(nat,g::SwitchMpState,mp);
        const bool sw=(call1(nat,n::SwitchInProgress,{})&0xFF)!=0;
        if(!secondSent_ && !sw && mp==0){descendDone(nat,"none",frame,false);return;}           // 0x44a74: nothing of the game's up there
        if(call1(nat,n::PauseMenuActive,{})&0xFF){call0(nat,n::SetFrontendActive,{0});return;}   // 0x44a12-0x44a20
        if(mp==1){(void)put(nat,g::SwitchMpState,2);return;}                                // 0x44a80-0x44a8e
        if(!secondSent_) {
            if(!sw){descendDone(nat,"lost",frame,true);return;}
            if(call1(nat,n::IsMultiFirstDone,{})&0xFF) {
                (void)put(nat,g::SwitchMpState,4);                                           // 0x44c02-0x44c03, in this order
                call0(nat,n::MultiSecondPart,{call1(nat,n::PlayerPedId,{})});                 // 0x44c07-0x44c0b
                secondSent_=true;secondAt_=frame;
                char l[96];std::snprintf(l,sizeof l,"RENDER_ONLINE creator descend secondpart state=%d type=%d",int(call1(nat,n::SwitchState,{})),int(call1(nat,n::SwitchType,{})));log_(l);
                return;
            }
            if(frame-descendAt_>DescendFirstFrames)descendAbort(nat,"first-timeout",frame);
            return;
        }
        if(!sw){descendDone(nat,"secondpart",frame,true);return;}                                // 0x44c7d
        if((frame-secondAt_)%15u==0){char l[64];std::snprintf(l,sizeof l,"RENDER_ONLINE creator descend wait state=%d",int(call1(nat,n::SwitchState,{})));log_(l);}
        if(frame-secondAt_>DescendSecondFrames)descendAbort(nat,"second-timeout",frame);
    }
    // The bound hit: the game's own abort (0x68090's natives, a harmless superset with cleanup's 932fdb81(0)), then the cleanup.
    void descendAbort(Natives& nat,const char* why,std::uint32_t frame) noexcept {
        stopSwitch(nat,call1(nat,n::PlayerId,{}));   // (v992's: switch ped deleted when it exists, invincible off, STOP_PLAYER_SWITCH)
        descendDone(nat,why,frame,true);
    }
    // cleanup (0x44d03-0x44d16), then the route's hand-back; "none" changes nothing
    void descendDone(Natives& nat,const char* via,std::uint32_t frame,bool clean) noexcept {
        const std::uint32_t pl=call1(nat,n::PlayerId,{});
        if(clean) {
            call0(nat,n::SetPlayerInvincible,{pl,inv0_});        // the original passes 0 (G1344102 == 0); inv0_ keeps a God mode
            call0(nat,n::SwitchMpFlag,{0});
            (void)put(nat,g::SwitchMpState,mp0_);
            if(!(call1(nat,n::ControlOn,{pl})&0xFF))call0(nat,n::SetPlayerControl,{pl,1,0});
            if(!ops_.keepDark() && !(call1(nat,n::IsScreenFadedIn,{})&0xFF))call0(nat,n::DoScreenFadeIn,{FadeInMs});   // v1066: kept dark in Online mode
        }
        char l[160];std::snprintf(l,sizeof l,"RENDER_ONLINE creator descend done via=%s frames=%u state_end=%d in=%u inv=%u",via,frame-descendAt_,
            int(call1(nat,n::SwitchState,{})),call1(nat,n::SwitchInProgress,{})&0xFF,call1(nat,n::GetInvincible,{pl})&0xFF);log_(l);
        st_=St::Restore;stepAt_=frame;
    }
    // The thread would not end (or kept hosting the tick): the player is given back anyway, the globals are LEFT (the thread may still
    // run; never fight it), Failed with saves blocked and no new run in this process. The script may take the frontend back.
    void timeoutEnd(Natives& nat,const char* term) noexcept {
        // v989: G19455 = 0 must not stay behind (the Sandbox's story scripts read it): put back on a tick the thread does not host
        if(modeWritten_){if(id_ && ops_.hostIs(id_))log_("RENDER_ONLINE creator mode-words-left (hosted tick)");else (void)restoreMode(nat);}
        logTeardown(term);
        restorePlayer(nat);
        finishFailed("terminate-timeout",true);
    }
    // The player back (entry: SET_PLAYER_CONTROL off; state 2 / commit: freeze, invincible, fade out; creator: frontend, cams).
    // v992 (t-creatorgo v990: the sky camera never came down after the commit or the cancel; freemode normally starts the descent):
    // maintransition's own switch stop 0x68090, in its order: while a switch is in progress, the switch ped G1345771 made a mission
    // entity (when 2632e124 says it is not yet) and DELETE_PED(&G1345771); SET_PLAYER_INVINCIBLE(PLAYER_ID, 0) (the trainer's God mode,
    // if on, re-applies it every tick); STOP_PLAYER_SWITCH(). Its two script-internal calls (0x42c7a, 0x884) are not replicated.
    void stopSwitch(Natives& nat,std::uint32_t player) noexcept {
        if(!(call1(nat,n::SwitchInProgress,{})&0xFF))return;
        std::uint32_t sped=0;const bool have=getG(nat,g::SwitchPed,sped);int deleted=-1;
        if(have && sped && (call1(nat,n::EntityExists,{sped})&0xFF)) {
            if(!(call1(nat,n::MissionEntityQ,{sped})&0xFF))call0(nat,n::SetMissionEntity,{sped,0,0});
            const std::uint32_t slot=nat.word(sped);
            if(slot){call0(nat,n::DeletePed,{slot});const std::uint32_t left=nat.load(slot);deleted=left==0;(void)put(nat,g::SwitchPed,left);}
        }
        call0(nat,n::SetPlayerInvincible,{player,0});
        call0(nat,n::StopPlayerSwitch,{});
        const bool still=(call1(nat,n::SwitchInProgress,{})&0xFF)!=0;
        char l[128];std::snprintf(l,sizeof l,"RENDER_ONLINE creator switch-stop switch_ped=%08x deleted=%d still_in_progress=%d",sped,deleted,int(still));log_(l);
    }
    void restorePlayer(Natives& nat) noexcept {
        const std::uint32_t player=call1(nat,n::PlayerId,{}),ped=call1(nat,n::PlayerPedId,{});
        (void)player;   // (v993: the switch is brought down by St::Descend after this, not stopped here)
        call0(nat,n::SetFrontendActive,{0});
        call0(nat,n::RenderScriptCams,{0,0,1000,0,1});           // maintransition 0x6892f's literal pattern
        call0(nat,n::SetPlayerControl,{player,1,0});
        if(ped){call0(nat,n::FreezeEntity,{ped,0});call0(nat,n::SetEntityVisible,{ped,1});call0(nat,n::SetEntityCollision,{ped,1,0});}
        if(!ops_.keepDark())call0(nat,n::DoScreenFadeIn,{FadeInMs});   // v1066: kept dark in Online mode (the controller reveals)
    }
    void logTeardown(const char* term) noexcept {
        char lag[16];if(firstTerm_)std::snprintf(lag,sizeof lag,"%u",firstTerm_-tdStart_);else std::snprintf(lag,sizeof lag,"none");
        char l[192];std::snprintf(l,sizeof l,"RENDER_ONLINE creator teardown reason=%s terminate=%s globals=%s term_lag=%s hosted_defers=%u",
            reason_,term,globalsRestored_?"restored":"untouched",lag,hostedDefers_);log_(l);
    }
    void restore(Natives& nat,std::uint32_t frame) noexcept {
        if(!ops_.keepDark() && !(call1(nat,n::IsScreenFadedIn,{})&0xFF) && frame-stepAt_<=FadeFrames)return;   // v1066: no wait when kept dark
        if(!committed_){finishDone(false);return;}
        st_=St::Capture;captured_=0;
    }
    void capture(Natives& nat,std::uint32_t) noexcept {
        static const auto P=plan();
        for(int k=0;k<ReadsPerTick && captured_<PlanSize;++k,++captured_) {
            std::uint32_t v=0;
            if(!readOne(nat,P[std::size_t(captured_)],slot_,v)){
                char r[48];std::snprintf(r,sizeof r,"stat-read en=%d",P[std::size_t(captured_)].en);finishFailedAfter(r);return;}
            store(res_,P[std::size_t(captured_)],v);
        }
        if(captured_>=PlanSize)st_=St::Save;
    }
    void save(Natives& nat,std::uint32_t) noexcept {
        const std::uint32_t model=modelOf(res_.gender);
        {   // v1104 INTERIM FALLBACK (lead 2026-11-08 ~14:20, owner "fix the bugs I pointed out": integrate the v1083 draft; the reset of
            // component 2 at its origin is still open, and the creator PREVIEW stays bald). v1083 (lead 2026-11-08: the created character was bald): the creator applies its hair item through the catalogue (0x5abf ->
            // 0xd985 -> 0x16869 / 0x1ec62), then a second call in the same frame reset component 2 to 0, so the capture of the ped read 0.
            // The item itself (packed 385) is kept: its catalogue entry is the hair the creator chose and first applied.
            int d=0,t=0;
            if(res_.drawable[2]==0 && ::recomp::creator_hair::lookup(model==creator::FemaleModel,res_.enum385,d,t) && d!=0) {
                char l[160];std::snprintf(l,sizeof l,"RENDER_ONLINE creator hair from the catalogue item=%d -> d=%d t=%d (captured d=%d t=%d)",
                                          res_.enum385,d,t,res_.drawable[2],res_.texture[2]);log_(l);
                res_.drawable[2]=d;res_.texture[2]=t;
            }
        }
        const Mapped m=toLook(res_,model);
        if(m.eyebrowsDefaulted>=0){char e[140];std::snprintf(e,sizeof e,"RENDER_ONLINE creator eyebrows index=%d: opacity read 0 -> %d (v1106 interim)",m.eyebrowsDefaulted,creator::Permille);log_(e);}
        {   // v997: every captured component (drawable.texture.palette, as the commit read them from the cloned ped) and the props; the
            // look's mapping forces a palette outside 0..3 to 0 (toLook), so a hair colour carried in the palette would show here
            char l[256];int k=std::snprintf(l,sizeof l,"RENDER_ONLINE creator captured parts");
            for(int c=0;c<Components && k<int(sizeof l)-16;++c)k+=std::snprintf(l+k,sizeof l-std::size_t(k)," %d.%d.%d",res_.drawable[std::size_t(c)],res_.texture[std::size_t(c)],res_.palette[std::size_t(c)]);
            log_(l);
            std::snprintf(l,sizeof l,"RENDER_ONLINE creator hair item=%d d=%d t=%d",res_.enum385,res_.drawable[2],res_.texture[2]);
            log_(l);
            k=std::snprintf(l,sizeof l,"RENDER_ONLINE creator captured props");
            for(int a=0;a<PropAnchors && k<int(sizeof l)-12;++a)k+=std::snprintf(l+k,sizeof l-std::size_t(k)," %d.%d",res_.prop[std::size_t(a)],res_.propTexture[std::size_t(a)]);
            log_(l);
        }
        char raw[RawMax];const int k=serializeRaw(res_,slot_,model,raw,sizeof raw);
        const bool rawOk=k>0 && ops_.saveRaw(raw,k);
        const bool lookOk=m.ok && ops_.saveLook(m.look);
        char l[176];std::snprintf(l,sizeof l,"RENDER_ONLINE creator captured model=%s heads=%d,%d dom=%d props_dropped=%d look=%s raw=%s",
            model==creator::FemaleModel?"female":model==creator::MaleModel?"male":"unknown",res_.headA,res_.headB,res_.dominance,
            m.propsDropped,lookOk?"saved":m.ok?"save-failed":m.why,rawOk?"saved":"save-failed");
        log_(l);
        if(!lookOk){finishFailedAfter(m.ok?"look-save-failed":m.why);return;}
        if(ops_.keepDark()){log_("RENDER_ONLINE creator saved: the OnlineMode controller applies the character (kept dark)");finishDone(false);return;}   // v1066
        // v990: the apply waits for the world: after the teardown the game's own switch camera (the sky cam back to the player) runs for
        // ~40 s, and the trainer drops 15 / 17 queued during it (t-creatorgo v989: blocked=character-switch). Requested once no switch is
        // in progress, player control is on and the screen is faded in; ApplyFrames at most, then done without the apply (logged).
        applyFemale_=model==creator::FemaleModel;st_=St::Apply;applyWait_=0;applyPhase_=0;   // (stepAt_ = the first Apply frame)
    }
    void apply(Natives& nat,std::uint32_t frame) noexcept {
        if(!applyWait_)stepAt_=frame;
        if(++applyWait_%ApplyLookFrames!=1)return;   // a look every ApplyLookFrames frames (4 natives)
        const std::uint32_t pl=call1(nat,n::PlayerId,{});
        const bool sw=(call1(nat,n::SwitchInProgress,{})&0xFF)!=0,ctl=(call1(nat,n::ControlOn,{pl})&0xFF)!=0,in=(call1(nat,n::IsScreenFadedIn,{})&0xFF)!=0;
        if(applyPhase_==1){applyLook(nat,frame,sw);return;}   // v995
        if(!sw && ctl && in) {
            char l[96];std::snprintf(l,sizeof l,"RENDER_ONLINE creator apply ready waited_frames=%u",frame-stepAt_);log_(l);
            // v995 (t-creatorgo v993: GPU fence stuck ~1 s after 17 started, the open C2 renderer hang on a head-blend apply; 15's own
            // default blend in the same seconds did not hang): 15 now, 17 only once the model is on, the head blend reports finished
            // and LookSettleFrames passed (the falsifier: does a settled 17 still hang?)
            if(!ops_.requestTrainer(trainer::OnlineCharacter,applyFemale_?1:0)){finishDone(false);return;}
            applyPhase_=1;lookAt_=frame;log_("RENDER_ONLINE creator apply character requested (look waits for the settle)");
            return;
        }
        if(frame-stepAt_>ApplyFrames) {
            char l[112];std::snprintf(l,sizeof l,"RENDER_ONLINE creator apply-timeout switch=%d control=%d faded_in=%d",int(sw),int(ctl),int(in));log_(l);
            finishDone(false);
        }
    }
    void applyLook(Natives& nat,std::uint32_t frame,bool sw) noexcept {
        const std::uint32_t ped=call1(nat,n::PlayerPedId,{}),model=ped?call1(nat,n::EntityModel,{ped}):0u;
        const std::uint32_t want=applyFemale_?creator::FemaleModel:creator::MaleModel;
        const bool on=model==want,blend=on && (call1(nat,n::HeadBlendFinished,{ped})&0xFF)!=0,settled=frame-lookAt_>=LookSettleFrames;
        if(on && blend && settled && !sw) {
            char l[128];std::snprintf(l,sizeof l,"RENDER_ONLINE creator apply look requested waited_frames=%u model=%08x blend_finished=1",frame-lookAt_,model);log_(l);
            finishDone(ops_.requestTrainer(trainer::CharacterLook,0));return;
        }
        if(frame-lookAt_>LookWaitFrames) {
            char l[128];std::snprintf(l,sizeof l,"RENDER_ONLINE creator apply look-timeout model_on=%d blend_finished=%d switch=%d",int(on),int(blend),int(sw));log_(l);
            finishDone(false);
        }
    }
    void finishDone(bool applied) noexcept {
        ops_.holdSaves(false);
        if(!ops_.storyUnchanged()){blocked_=true;st_=St::Failed;log_("RENDER_ONLINE creator PROTECT story-save-changed saves=blocked");return;}
        char l[160];std::snprintf(l,sizeof l,"RENDER_ONLINE creator done reason=%s back=sandbox-freeroam applied=%s",reason_,
            committed_?(applied?"requested":"not-requested"):"none");log_(l);
        st_=St::Done;
    }
    void finishFailedAfter(const char* why) noexcept {   // after the teardown: the world is back, only the result is missing
        std::snprintf(reason_,sizeof reason_,"%s",why);
        ops_.holdSaves(false);
        if(!ops_.storyUnchanged()){blocked_=true;log_("RENDER_ONLINE creator PROTECT story-save-changed saves=blocked");}
        char l[128];std::snprintf(l,sizeof l,"RENDER_ONLINE creator failed reason=%s back=sandbox-freeroam",why);log_(l);
        st_=St::Failed;
    }
    void finishFailed(const char* why,bool block) noexcept {
        std::snprintf(reason_,sizeof reason_,"%s",why);
        ops_.holdSaves(false);
        if(block || !ops_.storyUnchanged())blocked_=true;
        char l[128];std::snprintf(l,sizeof l,"RENDER_ONLINE creator failed reason=%s%s",why,blocked_?" saves=blocked":"");log_(l);
        st_=St::Failed;
    }
    template<class... A> static int ok(char* d,int cap,const char* f,A... a) noexcept {if(d && cap>0)std::snprintf(d,std::size_t(cap),f,a...);return 1;}
    template<class... A> static int fail(char* d,int cap,const char* f,A... a) noexcept {if(d && cap>0)std::snprintf(d,std::size_t(cap),f,a...);return 2;}

    Ops& ops_;LogFn log_;
    St st_=St::Idle;
    std::uint32_t applyWait_=0;bool applyFemale_=false;   // v990
    int applyPhase_=0;std::uint32_t lookAt_=0;   // v995
    std::uint32_t inv0_=0,mp0_=0,descendAt_=0,secondAt_=0;bool secondSent_=false;   // v993
    std::uint32_t frame0_=0,stepAt_=0,tdStart_=0,lastTerm_=0,firstTerm_=0,hostedDefers_=0,id_=0,entryTransition_=0;
    std::atomic<bool> cancel_{false};   // requestCancel (any thread) -> tick (script thread)
    int slot_=0,captured_=0;
    bool blocked_=false,committed_=false,globalsRestored_=false;
    DiscResult res_{};
    char reason_[64]{};
};

} // namespace recomp::creatorroute
#pragma GCC diagnostic pop
