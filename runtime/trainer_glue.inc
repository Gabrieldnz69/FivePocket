#include "online_save_guard.h" // v967 (wave-3 S6)
// trainer (2026-10-25): the library side of trainer.h, included at the end of indirect_call_resolver.cpp (it uses that file's
// PPCFuncMappings search and its native registration / probe paths). Shared by every edition: no Thor-only part.
//  - recomp_native_probe_register (every native the game registers) -> recomp_trainer_note_native: the handlers of the natives
//    the trainer calls (trainer.h UsedNatives), by hash.
//  - indirectCallSlow's probe path, after a probed native returned -> recomp_trainer_after_native: when it was PLAYER_PED_ID
//    called by the script VM (return address 0x8343A208, sub_83439990's native call), the trainer's tick runs once per game frame.
//    Both are plain calls (no new branch in the profiled resolver: its IR-PGO CFG hash, and so its profile, stay as they were).
//  - the exports the apps call (dlsym on the library they loaded): recomp_trainer_v1 / _state_v1 / _status_v1 / _list_v1.
#include "trainer.h"
#include <chrono>
#include <unistd.h>
#if defined(__ANDROID__)
inline int recompTidNow() noexcept {return int(::gettid());}   // v994 (log only)
#else
inline int recompTidNow() noexcept {return 0;}                 // (host tests: no gettid on macOS)
#endif
#include "autopilot.h"   // v842: the play bot's in-game autopilot (the game's own AI tasks), ticked at the same safe point
#include "phone_call.h"  // v852: the phone's call state (IS_PED_RINGTONE_PLAYING / IS_MOBILE_PHONE_CALL_ONGOING), polled at the same safe point
#include "sandbox_runtime.h" // Update 1 P4: Sandbox mode's tick (whole map, weapons, denylist check, sweep) at the same safe point
#include "online_world.h"   // Update 1 P8: Online World Level 1 (freemode character, places, world toggle); Sandbox mode only
#include "property_doors.h" // lane property-doors (PROPOSED): property icons + walk-in doors in Sandbox mode (trips via action 16)
#include "character_store.h" // OW L2 1a: the creator's record (FileStore, the Sandbox generation only; online_world.h has the creator)
#include "save_sandbox.h"    // OW L2 1a: filesRootFor, the Sandbox generation root (resolved once per process by the saves state)
#include "activities.h"      // OW L2 1c: action 19 Activity, list 8 (activities.h Launcher; Sandbox mode only)
#include "force_clock.h"     // v1076 TEST-ONLY: diag-force-clock (lamp-cone A/B at dawn)
#include "session_env.h"     // v1018: read-only clock / weather lines while the L1 run is armed (diag-online-l1)
#include "shop_probe.h"      // v1039: read-only shop_controller probe in the local session (diag-shop-probe)
#include "car_keep.h"        // v1036: the player's vehicle survives the local session's start (recreated networked)
#include "periodic_save.h"   // v1562: the local stand-in for the cloud periodic MP stats save (Online mode; kill switch diag-stats-periodic-off)
#include "online_mode_glue.h" // v1056: the controller's PedOps on the game (the swap gate)
#include "online_mode.h"      // v1050: the Online mode controller (step 1: the boot loading screen held until the session is up)
#include "fmi_probe.h"       // v1048: read-only fm_intro drop-off probe (diag-fmi-probe)
#include "property_unlock.h" // v1501: read-only Online property-unlock probe (online-doors REPORT.md)
#include "contact_missions.h" // v1508: read-only Online contact-mission probe (contact-missions REPORT.md)
#include "door_chain_probe.h" // v1510: read-only Online race -> garage cutscene chain probe (doors REPORT.md)
#include "char_probe_flag.h"   // v1082: the player ped for the creator probe
#include "online_arrival.h"  // v1046: Online mode arrival: first visit = the game's fm_intro, later = the last saved spot (arrival.json)
#include "online_curtain.h"  // v1043: the loading screen while the local session comes up / goes down (Online mode, diag-online-curtain)
#include "online_respawn.h"  // v1123: Online mode death -> the GTA Online respawn when freemode does not run (owner: "game freezes when character dies")
#include "sandbox_respawn.h"  // v1351: the same answer in Sandbox mode while our local session is up (main ended by the session's force cleanup)
#include "online_intro.h"    // PROPOSED action 20 OnlineIntro (lane upd1-spoof/intro-cutscene; Sandbox + diag-online-local only)
#include "sandbox_diag.h"    // PROPOSED (creator route): CurrentThread, to know which thread hosts the tick
#include "look_refresh.h"    // v1509 (shops subagent): the saved look takes the game's outfit stats in Online mode
#include "creator_route.h"   // PROPOSED (lane upd1-spoof/creator-route): action 21 DiscCreator (Sandbox + diag-online-local only)
#include "creator_story_digest.h" // PROPOSED (creator route): the story-save digest (read only) at a run's start and end
#include "mp_globals.h"        // v985: the MP global blocks (mp_registration & co.) for Sandbox + diag-online-local
#include "online_local_glue.h" // v968 (B2): the one diag-online-local latch for signin, session, intro (20) and creator (21)
#include "menu_draw.h"        // PROPOSED (lane upd1-creator-menu/shop-menu-look): the creator menu in the shops' own look (natives only)
#include "imenu_diag.h"       // DIAGNOSTIC (lane upd1-imenu/diag-run): the Interaction Menu E1-E5 run; Sandbox + diag-imenu only
#include "imenu_view_adapter.h" // v989 (lanes upd1-imenu model-rows + draw-glue, INTEGRATION option B): the Interaction Menu, Sandbox only
#include "imenu_back_gate.h"    // v989 (E3): Back decided in the pad read (the camera cycles on press offline)
#include "ammo_restore_diag.h" // v1565 (diagnostic, default off, diag-ammo-restore): the Online pistol ammo restore, read only
#include "touch_exports.h"    // v988 (ALLOCATION 14, android-qol): recomp_camera_turn_v1 / recomp_aim_assist_v1 (Story and Sandbox)
#include <cstdlib>
#if defined(__aarch64__)
#include <arm_acle.h>
#endif

namespace {
constexpr std::uint32_t VmNativeReturn=0x8343A208u;   // the script VM's native call (sub_83439990, bctrl at 0x8343A204)
constexpr std::uint32_t FrameCounter=0x83C5D10Cu;     // GET_FRAME_COUNT's word (sub_82D666E8: lis -31802, addi -12024, lwz +4)
constexpr std::uint32_t PlayerPedIdHash=0xfa92e226u;
void trainerLog(const char* line) {__android_log_print(ANDROID_LOG_INFO,"RecompRenderer","%s",line);}
// (Update 1 P7: the play-mode latch tells the trainer Sandbox from Story; SwitchCharacter is refused in Story mode)
recomp::trainer::Trainer& trainer() {static recomp::trainer::Trainer t(trainerLog,recomp::sandbox::active);return t;}
std::array<std::atomic<std::uint32_t>,std::size(recomp::trainer::UsedNatives)> trainerHandlers{};
std::array<std::atomic<std::uint32_t>,std::size(recomp::autopilot::UsedNatives)> autopilotHandlers{}; // v842
std::array<std::atomic<std::uint32_t>,std::size(recomp::phonecall::UsedNatives)> phoneCallHandlers{}; // v852
std::array<std::atomic<std::uint32_t>,std::size(recomp::sandbox::UsedNatives)> sandboxHandlers{}; // Update 1 P4
std::array<std::atomic<std::uint32_t>,std::size(recomp::online::UsedNatives)> onlineHandlers{}; // Update 1 P8
std::array<std::atomic<std::uint32_t>,std::size(recomp::creator::UsedNatives)> creatorHandlers{}; // OW L2 1a (the creator's natives)
std::array<std::atomic<std::uint32_t>,std::size(recomp::activities::UsedNatives)> activityHandlers{}; // OW L2 1c
std::array<std::atomic<std::uint32_t>,std::size(recomp::session_env::UsedNatives)> sessionEnvHandlers{}; // v1019
std::array<std::atomic<std::uint32_t>,std::size(recomp::car_keep::UsedNatives)> carKeepHandlers{}; // v1036
std::array<std::atomic<std::uint32_t>,std::size(recomp::periodic_save::UsedNatives)> periodicSaveHandlers{}; // v1562
#if defined(__ANDROID__)   // v1564: the stats clock word *(0x82000A38)+16 is in a special range: the guest scalar loader emulates it
inline std::uint32_t periodicClockWord(const std::uint8_t* base,std::uint32_t a) noexcept {return recomp::bootstrap::loadGuestScalar<std::uint32_t>(base,a);}
#else                     // host glue tests: plain memory
inline std::uint32_t periodicClockWord(const std::uint8_t* base,std::uint32_t a) noexcept {std::uint32_t v;std::memcpy(&v,base+a,4);return __builtin_bswap32(v);}
#endif
std::array<std::atomic<std::uint32_t>,std::size(recomp::online_curtain::UsedNatives)> curtainHandlers{}; // v1043
std::array<std::atomic<std::uint32_t>,std::size(recomp::online_respawn::UsedNatives)> respawnHandlers{}; // v1123
std::array<std::atomic<std::uint32_t>,std::size(recomp::online_arrival::UsedNatives)> arrivalHandlers{}; // v1046
std::array<std::atomic<std::uint32_t>,std::size(recomp::fmi_probe::UsedNatives)> fmiProbeHandlers{}; // v1048
std::array<std::atomic<std::uint32_t>,std::size(recomp::online_mode::UsedNatives)> onlineModeHandlers{}; // v1056
std::array<std::atomic<std::uint32_t>,std::size(recomp::shop_probe::UsedNatives)> shopProbeHandlers{}; // v1039
std::array<std::atomic<std::uint32_t>,std::size(recomp::facecam::UsedNatives)> faceCamHandlers{}; // v959 (the creator's face camera)
std::array<std::atomic<std::uint32_t>,std::size(recomp::intro::UsedNatives)> introHandlers{}; // PROPOSED action 20 (the intro player's natives)
std::array<std::atomic<std::uint32_t>,std::size(recomp::creatorroute::UsedNatives)> discCreatorHandlers{}; // PROPOSED action 21
std::array<std::atomic<std::uint32_t>,std::size(recomp::creatormenu::UsedNatives)> creatorMenuHandlers{}; // PROPOSED 17 4 (the creator menu)
std::array<std::atomic<std::uint32_t>,std::size(recomp::menudraw::UsedNatives)> menuDrawHandlers{}; // PROPOSED creator menu look
std::array<std::atomic<std::uint32_t>,std::size(recomp::imenudiag::UsedNatives)> imenuDiagHandlers{}; // DIAGNOSTIC imenu E1-E5
std::array<std::atomic<std::uint32_t>,std::size(recomp::propdoors::UsedNatives)> propDoorHandlers{}; // lane property-doors
std::array<std::atomic<std::uint32_t>,std::size(touchx::UsedNatives)> touchHandlers{}; // v988 touch exports
// v989: INVALIDATE_IDLE_CAM (registered: d75cdd75 -> sub_82D4C6E0, which calls sub_824458D8 + sub_82614E10). A touch turn ends the
// game's idle camera as right-stick input would (t-touch v988: after ~40 s without input the idle cam rendered, IS_GAMEPLAY_CAM_RENDERING
// was 0, the pitch turn was dropped and the next turn refused).
inline constexpr std::uint32_t InvalidateIdleCam=0xd75cdd75u;
std::atomic<std::uint32_t> invalidateIdleCamHandler{0};
touchx::TouchExports& touchX() noexcept {static touchx::TouchExports t;return t;}
std::array<std::atomic<std::uint32_t>,std::size(recomp::imenu::UsedNatives)> imenuHandlers{};        // v989 Interaction Menu model
std::array<std::atomic<std::uint32_t>,std::size(recomp::imenudraw::UsedNatives)> imenuDrawHandlers{}; // v989 Interaction Menu drawer   // v988 (any thread: the apps' exports)
recomp::autopilot::Autopilot& autopilot() {static recomp::autopilot::Autopilot a(trainerLog);return a;}
// v842: a native's recorded handler, from either list (0: not one of ours, or not registered yet)
std::uint32_t nativeHandler(std::uint32_t hash) noexcept {
    if(const auto* at=std::find(std::begin(recomp::trainer::UsedNatives),std::end(recomp::trainer::UsedNatives),hash);at!=std::end(recomp::trainer::UsedNatives))
        return trainerHandlers[std::size_t(at-std::begin(recomp::trainer::UsedNatives))].load(std::memory_order_acquire);
    if(const auto* at=std::find(std::begin(recomp::autopilot::UsedNatives),std::end(recomp::autopilot::UsedNatives),hash);at!=std::end(recomp::autopilot::UsedNatives))
        return autopilotHandlers[std::size_t(at-std::begin(recomp::autopilot::UsedNatives))].load(std::memory_order_acquire);
    if(const auto* at=std::find(std::begin(recomp::phonecall::UsedNatives),std::end(recomp::phonecall::UsedNatives),hash);at!=std::end(recomp::phonecall::UsedNatives))
        return phoneCallHandlers[std::size_t(at-std::begin(recomp::phonecall::UsedNatives))].load(std::memory_order_acquire); // v852
    if(const auto* at=std::find(std::begin(recomp::sandbox::UsedNatives),std::end(recomp::sandbox::UsedNatives),hash);at!=std::end(recomp::sandbox::UsedNatives))
        return sandboxHandlers[std::size_t(at-std::begin(recomp::sandbox::UsedNatives))].load(std::memory_order_acquire); // P4
    if(const auto* at=std::find(std::begin(recomp::online::UsedNatives),std::end(recomp::online::UsedNatives),hash);at!=std::end(recomp::online::UsedNatives))
        return onlineHandlers[std::size_t(at-std::begin(recomp::online::UsedNatives))].load(std::memory_order_acquire); // P8
    if(const auto* at=std::find(std::begin(recomp::creator::UsedNatives),std::end(recomp::creator::UsedNatives),hash);at!=std::end(recomp::creator::UsedNatives))
        return creatorHandlers[std::size_t(at-std::begin(recomp::creator::UsedNatives))].load(std::memory_order_acquire); // OW L2 1a
    if(const auto* at=std::find(std::begin(recomp::activities::UsedNatives),std::end(recomp::activities::UsedNatives),hash);at!=std::end(recomp::activities::UsedNatives))
        return activityHandlers[std::size_t(at-std::begin(recomp::activities::UsedNatives))].load(std::memory_order_acquire); // OW L2 1c
    if(const auto* at=std::find(std::begin(recomp::session_env::UsedNatives),std::end(recomp::session_env::UsedNatives),hash);at!=std::end(recomp::session_env::UsedNatives))
        return sessionEnvHandlers[std::size_t(at-std::begin(recomp::session_env::UsedNatives))].load(std::memory_order_acquire); // v1019
    if(const auto* at=std::find(std::begin(recomp::car_keep::UsedNatives),std::end(recomp::car_keep::UsedNatives),hash);at!=std::end(recomp::car_keep::UsedNatives))
        return carKeepHandlers[std::size_t(at-std::begin(recomp::car_keep::UsedNatives))].load(std::memory_order_acquire); // v1036
    if(const auto* at=std::find(std::begin(recomp::periodic_save::UsedNatives),std::end(recomp::periodic_save::UsedNatives),hash);at!=std::end(recomp::periodic_save::UsedNatives))
        return periodicSaveHandlers[std::size_t(at-std::begin(recomp::periodic_save::UsedNatives))].load(std::memory_order_acquire); // v1562
    if(const auto* at=std::find(std::begin(recomp::online_curtain::UsedNatives),std::end(recomp::online_curtain::UsedNatives),hash);at!=std::end(recomp::online_curtain::UsedNatives))
        return curtainHandlers[std::size_t(at-std::begin(recomp::online_curtain::UsedNatives))].load(std::memory_order_acquire); // v1043
    if(const auto* at=std::find(std::begin(recomp::online_respawn::UsedNatives),std::end(recomp::online_respawn::UsedNatives),hash);at!=std::end(recomp::online_respawn::UsedNatives))
        return respawnHandlers[std::size_t(at-std::begin(recomp::online_respawn::UsedNatives))].load(std::memory_order_acquire); // v1123
    if(const auto* at=std::find(std::begin(recomp::online_arrival::UsedNatives),std::end(recomp::online_arrival::UsedNatives),hash);at!=std::end(recomp::online_arrival::UsedNatives))
        return arrivalHandlers[std::size_t(at-std::begin(recomp::online_arrival::UsedNatives))].load(std::memory_order_acquire); // v1046
    if(const auto* at=std::find(std::begin(recomp::fmi_probe::UsedNatives),std::end(recomp::fmi_probe::UsedNatives),hash);at!=std::end(recomp::fmi_probe::UsedNatives))
        return fmiProbeHandlers[std::size_t(at-std::begin(recomp::fmi_probe::UsedNatives))].load(std::memory_order_acquire); // v1048
    if(const auto* at=std::find(std::begin(recomp::online_mode::UsedNatives),std::end(recomp::online_mode::UsedNatives),hash);at!=std::end(recomp::online_mode::UsedNatives))
        return onlineModeHandlers[std::size_t(at-std::begin(recomp::online_mode::UsedNatives))].load(std::memory_order_acquire); // v1056
    if(const auto* at=std::find(std::begin(recomp::shop_probe::UsedNatives),std::end(recomp::shop_probe::UsedNatives),hash);at!=std::end(recomp::shop_probe::UsedNatives))
        return shopProbeHandlers[std::size_t(at-std::begin(recomp::shop_probe::UsedNatives))].load(std::memory_order_acquire); // v1039
    if(const auto* at=std::find(std::begin(recomp::facecam::UsedNatives),std::end(recomp::facecam::UsedNatives),hash);at!=std::end(recomp::facecam::UsedNatives))
        return faceCamHandlers[std::size_t(at-std::begin(recomp::facecam::UsedNatives))].load(std::memory_order_acquire); // v959
    if(const auto* at=std::find(std::begin(recomp::intro::UsedNatives),std::end(recomp::intro::UsedNatives),hash);at!=std::end(recomp::intro::UsedNatives))
        return introHandlers[std::size_t(at-std::begin(recomp::intro::UsedNatives))].load(std::memory_order_acquire); // PROPOSED action 20
    if(const auto* at=std::find(std::begin(recomp::creatorroute::UsedNatives),std::end(recomp::creatorroute::UsedNatives),hash);at!=std::end(recomp::creatorroute::UsedNatives))
        return discCreatorHandlers[std::size_t(at-std::begin(recomp::creatorroute::UsedNatives))].load(std::memory_order_acquire); // PROPOSED 21
    if(const auto* at=std::find(std::begin(recomp::creatormenu::UsedNatives),std::end(recomp::creatormenu::UsedNatives),hash);at!=std::end(recomp::creatormenu::UsedNatives))
        return creatorMenuHandlers[std::size_t(at-std::begin(recomp::creatormenu::UsedNatives))].load(std::memory_order_acquire); // PROPOSED 17 4
    if(const auto* at=std::find(std::begin(recomp::menudraw::UsedNatives),std::end(recomp::menudraw::UsedNatives),hash);at!=std::end(recomp::menudraw::UsedNatives))
        return menuDrawHandlers[std::size_t(at-std::begin(recomp::menudraw::UsedNatives))].load(std::memory_order_acquire); // PROPOSED menu look
    if(const auto* at=std::find(std::begin(recomp::imenudiag::UsedNatives),std::end(recomp::imenudiag::UsedNatives),hash);at!=std::end(recomp::imenudiag::UsedNatives))
        return imenuDiagHandlers[std::size_t(at-std::begin(recomp::imenudiag::UsedNatives))].load(std::memory_order_acquire); // DIAGNOSTIC imenu
    if(const auto* at=std::find(std::begin(recomp::propdoors::UsedNatives),std::end(recomp::propdoors::UsedNatives),hash);at!=std::end(recomp::propdoors::UsedNatives))
        return propDoorHandlers[std::size_t(at-std::begin(recomp::propdoors::UsedNatives))].load(std::memory_order_acquire); // property-doors
    if(const auto* at=std::find(std::begin(touchx::UsedNatives),std::end(touchx::UsedNatives),hash);at!=std::end(touchx::UsedNatives))
        return touchHandlers[std::size_t(at-std::begin(touchx::UsedNatives))].load(std::memory_order_acquire); // v988 touch exports
    if(hash==InvalidateIdleCam)return invalidateIdleCamHandler.load(std::memory_order_acquire);   // v989
    if(const auto* at=std::find(std::begin(recomp::imenu::UsedNatives),std::end(recomp::imenu::UsedNatives),hash);at!=std::end(recomp::imenu::UsedNatives))
        return imenuHandlers[std::size_t(at-std::begin(recomp::imenu::UsedNatives))].load(std::memory_order_acquire); // v989 imenu model
    if(const auto* at=std::find(std::begin(recomp::imenudraw::UsedNatives),std::end(recomp::imenudraw::UsedNatives),hash);at!=std::end(recomp::imenudraw::UsedNatives))
        return imenuDrawHandlers[std::size_t(at-std::begin(recomp::imenudraw::UsedNatives))].load(std::memory_order_acquire); // v989 imenu draw
    return 0;
}
std::atomic<std::uint32_t> trainerLastFrame{0xFFFFFFFFu};
thread_local bool trainerInside=false;

std::uint32_t trainerBe(const std::uint8_t* base,std::uint32_t a) noexcept {std::uint32_t v;std::memcpy(&v,base+a,4);return __builtin_bswap32(v);}
void trainerPut(std::uint8_t* base,std::uint32_t a,std::uint32_t v) noexcept {v=__builtin_bswap32(v);std::memcpy(base+a,&v,4);}
// Update 1 P7: after a character switch, the PLAYER_PED_ID call that hosted the tick still holds the OLD ped as its result. The VM's
// call context is at r1+128 (sub_83439990 0x8343A1E4..0x8343A200: +0 result pointer, +4 argument count, +8 argument pointer, +12 0)
// and the script reads the result from that pointer after 0x8343A208. It is rewritten to the new ped only when it is plainly that
// call's result: no arguments, a plausible aligned pointer, and the word there is the old ped.
bool trainerFixPedResult(std::uint8_t* base,std::uint32_t r1,std::uint32_t oldPed,std::uint32_t newPed) noexcept {
    const std::uint32_t res=trainerBe(base,r1+128),argc=trainerBe(base,r1+132);
    if(argc!=0 || res<0x10000u || res>0xFFFF0000u || (res&3u) || trainerBe(base,res)!=oldPed)return false;
    trainerPut(base,res,newPed);
    return true;
}
PPCFunc* trainerHost(std::uint32_t guest) noexcept {
    const auto count=recomp::bootstrap::validatedMappingCount.load(std::memory_order_acquire);
    std::size_t first=0,last=count;
    while(first<last){const auto m=first+(last-first)/2;if(PPCFuncMappings[m].guest<guest)first=m+1;else last=m;}
    return first<count && PPCFuncMappings[first].guest==guest?PPCFuncMappings[first].host:nullptr;
}

// The natives through guest memory: one call context below the VM's stack pointer, laid out as the VM's own (sub_83439990 at
// r1+128: +0 result pointer, +4 argument count, +8 argument pointer, +12 = 0), then the arguments, the result and small scratch.
class GuestNatives final : public recomp::trainer::Natives {
public:
    static constexpr std::uint32_t Frame=0x800,Scratch=0x100,Args=0x20,Result=0x70,Heap=0x80,HeapEnd=0x600;
    GuestNatives(PPCContext& c,std::uint8_t* base,std::uint32_t r1) noexcept:c_(c),base_(base),r1_(r1),s_(r1+Scratch) {}
    bool call(std::uint32_t hash,const std::uint32_t* args,unsigned n,std::uint32_t* out) override {
        out[0]=out[1]=out[2]=0;
        if(n>12)return false;
        const std::uint32_t handler=nativeHandler(hash); // (v842: the trainer's and the autopilot's natives)
        PPCFunc* host=handler?trainerHost(handler):nullptr;
        if(!host)return false;
        std::memset(base_+s_,0,Heap);
        trainerPut(base_,s_+0,s_+Result);trainerPut(base_,s_+4,n);trainerPut(base_,s_+8,s_+Args);
        for(unsigned k=0;k<n;++k)trainerPut(base_,s_+Args+4*k,args[k]);
        c_.r3.u64=s_;c_.r1.u64=r1_;c_.lr=VmNativeReturn;
        host(c_,base_);
        for(unsigned k=0;k<3;++k)out[k]=trainerBe(base_,s_+Result+4*k);
        return true;
    }
    std::uint32_t text(const char* s) override {
        const std::size_t n=std::strlen(s)+1;
        if(n>HeapEnd-Heap-used_)return 0;
        const std::uint32_t a=s_+Heap+used_;std::memcpy(base_+a,s,n);used_=std::uint32_t((used_+n+3)&~std::size_t(3));return a;
    }
    std::uint32_t word(std::uint32_t v) override {
        if(used_+4>HeapEnd-Heap)return 0;
        const std::uint32_t a=s_+Heap+used_;trainerPut(base_,a,v);used_+=4;return a;
    }
    std::uint32_t load(std::uint32_t a) override {return trainerBe(base_,a);}
private:
    PPCContext& c_;std::uint8_t* base_;std::uint32_t r1_,s_,used_=0;
};

// The settings file read: true when it was read (the switch then follows it). missingIsOff: Sync's read - a missing file is "off".
bool trainerReadSettings(bool missingIsOff) noexcept {
    char text[512]{};std::size_t n=0;
    FILE* f=std::fopen(RECOMP_APP_PATH("/settings/trainer.txt"),"rb");
    // (Update 1 P4: forced on in Sandbox mode, in memory only; the settings file is never written)
    if(!f){if(missingIsOff)trainer().setEnabled(recomp::sandbox::active());return false;}
    n=std::fread(text,1,sizeof text-1,f);std::fclose(f);
    trainer().setEnabled(recomp::sandbox::active() || recomp::trainer::parseEnabled(text,n));
    return true;
}
// v853 (= the Fold's v719, 2026-11-05: trainer.txt "trainer=on", yet the trainer stayed off all session): the first read used to be
// latched whatever it found. The apps poll the state from their window's start, so that first read can come before the library has
// derived its app folder: the default path, no file, "off" for good. Now only a read that found the file is final; before that a
// state call retries at most every TrainerSettingsRetryMs.
constexpr long long TrainerSettingsRetryMs=1000;
#define RECOMP_TRAINER_SETTINGS_RETRY 1 // (glue_test.cpp runs its v719 check only with this fix)
std::atomic<bool> trainerSettingsRead{false};
void trainerSettingsOnce() noexcept {
    if(trainerSettingsRead.load(std::memory_order_acquire))return;
    static std::atomic<long long> nextTry{0};
    const long long now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    long long due=nextTry.load(std::memory_order_relaxed);
    if(now<due || !nextTry.compare_exchange_strong(due,now+TrainerSettingsRetryMs,std::memory_order_relaxed))return; // (one caller per try)
    if(trainerReadSettings(false))trainerSettingsRead.store(true,std::memory_order_release);
}
} // namespace

extern "C" void recomp_trainer_note_native(std::uint32_t hash,std::uint32_t handler) noexcept {
    const auto* at=std::find(std::begin(recomp::trainer::UsedNatives),std::end(recomp::trainer::UsedNatives),hash);
    if(at!=std::end(recomp::trainer::UsedNatives))
        trainerHandlers[std::size_t(at-std::begin(recomp::trainer::UsedNatives))].store(handler,std::memory_order_release);
    const auto* ap=std::find(std::begin(recomp::autopilot::UsedNatives),std::end(recomp::autopilot::UsedNatives),hash);
    if(ap!=std::end(recomp::autopilot::UsedNatives))
        autopilotHandlers[std::size_t(ap-std::begin(recomp::autopilot::UsedNatives))].store(handler,std::memory_order_release);
    const auto* pc=std::find(std::begin(recomp::phonecall::UsedNatives),std::end(recomp::phonecall::UsedNatives),hash); // v852
    if(pc!=std::end(recomp::phonecall::UsedNatives))
        phoneCallHandlers[std::size_t(pc-std::begin(recomp::phonecall::UsedNatives))].store(handler,std::memory_order_release);
    const auto* sb=std::find(std::begin(recomp::sandbox::UsedNatives),std::end(recomp::sandbox::UsedNatives),hash); // P4
    if(sb!=std::end(recomp::sandbox::UsedNatives))
        sandboxHandlers[std::size_t(sb-std::begin(recomp::sandbox::UsedNatives))].store(handler,std::memory_order_release);
    const auto* ow=std::find(std::begin(recomp::online::UsedNatives),std::end(recomp::online::UsedNatives),hash); // P8
    if(ow!=std::end(recomp::online::UsedNatives))
        onlineHandlers[std::size_t(ow-std::begin(recomp::online::UsedNatives))].store(handler,std::memory_order_release);
    const auto* cc=std::find(std::begin(recomp::creator::UsedNatives),std::end(recomp::creator::UsedNatives),hash); // OW L2 1a
    if(cc!=std::end(recomp::creator::UsedNatives))
        creatorHandlers[std::size_t(cc-std::begin(recomp::creator::UsedNatives))].store(handler,std::memory_order_release);
    const auto* fa=std::find(std::begin(recomp::activities::UsedNatives),std::end(recomp::activities::UsedNatives),hash); // OW L2 1c
    if(fa!=std::end(recomp::activities::UsedNatives))
        activityHandlers[std::size_t(fa-std::begin(recomp::activities::UsedNatives))].store(handler,std::memory_order_release);
    const auto* se=std::find(std::begin(recomp::session_env::UsedNatives),std::end(recomp::session_env::UsedNatives),hash); // v1019
    if(se!=std::end(recomp::session_env::UsedNatives))
        sessionEnvHandlers[std::size_t(se-std::begin(recomp::session_env::UsedNatives))].store(handler,std::memory_order_release);
    const auto* ck=std::find(std::begin(recomp::car_keep::UsedNatives),std::end(recomp::car_keep::UsedNatives),hash); // v1036
    if(ck!=std::end(recomp::car_keep::UsedNatives))
        carKeepHandlers[std::size_t(ck-std::begin(recomp::car_keep::UsedNatives))].store(handler,std::memory_order_release);
    const auto* ps=std::find(std::begin(recomp::periodic_save::UsedNatives),std::end(recomp::periodic_save::UsedNatives),hash); // v1562
    if(ps!=std::end(recomp::periodic_save::UsedNatives))
        periodicSaveHandlers[std::size_t(ps-std::begin(recomp::periodic_save::UsedNatives))].store(handler,std::memory_order_release);
    const auto* cu=std::find(std::begin(recomp::online_curtain::UsedNatives),std::end(recomp::online_curtain::UsedNatives),hash); // v1043
    if(cu!=std::end(recomp::online_curtain::UsedNatives))
        curtainHandlers[std::size_t(cu-std::begin(recomp::online_curtain::UsedNatives))].store(handler,std::memory_order_release);
    const auto* rs=std::find(std::begin(recomp::online_respawn::UsedNatives),std::end(recomp::online_respawn::UsedNatives),hash); // v1123
    if(rs!=std::end(recomp::online_respawn::UsedNatives))
        respawnHandlers[std::size_t(rs-std::begin(recomp::online_respawn::UsedNatives))].store(handler,std::memory_order_release);
    const auto* oa=std::find(std::begin(recomp::online_arrival::UsedNatives),std::end(recomp::online_arrival::UsedNatives),hash); // v1046
    if(oa!=std::end(recomp::online_arrival::UsedNatives))
        arrivalHandlers[std::size_t(oa-std::begin(recomp::online_arrival::UsedNatives))].store(handler,std::memory_order_release);
    const auto* fp=std::find(std::begin(recomp::fmi_probe::UsedNatives),std::end(recomp::fmi_probe::UsedNatives),hash); // v1048
    if(fp!=std::end(recomp::fmi_probe::UsedNatives))
        fmiProbeHandlers[std::size_t(fp-std::begin(recomp::fmi_probe::UsedNatives))].store(handler,std::memory_order_release);
    const auto* om=std::find(std::begin(recomp::online_mode::UsedNatives),std::end(recomp::online_mode::UsedNatives),hash); // v1056
    if(om!=std::end(recomp::online_mode::UsedNatives))
        onlineModeHandlers[std::size_t(om-std::begin(recomp::online_mode::UsedNatives))].store(handler,std::memory_order_release);
    const auto* sp=std::find(std::begin(recomp::shop_probe::UsedNatives),std::end(recomp::shop_probe::UsedNatives),hash); // v1039
    if(sp!=std::end(recomp::shop_probe::UsedNatives))
        shopProbeHandlers[std::size_t(sp-std::begin(recomp::shop_probe::UsedNatives))].store(handler,std::memory_order_release);
    const auto* fc=std::find(std::begin(recomp::facecam::UsedNatives),std::end(recomp::facecam::UsedNatives),hash); // v959
    if(fc!=std::end(recomp::facecam::UsedNatives))
        faceCamHandlers[std::size_t(fc-std::begin(recomp::facecam::UsedNatives))].store(handler,std::memory_order_release);
    const auto* oi=std::find(std::begin(recomp::intro::UsedNatives),std::end(recomp::intro::UsedNatives),hash); // PROPOSED action 20
    if(oi!=std::end(recomp::intro::UsedNatives))
        introHandlers[std::size_t(oi-std::begin(recomp::intro::UsedNatives))].store(handler,std::memory_order_release);
    const auto* dc=std::find(std::begin(recomp::creatorroute::UsedNatives),std::end(recomp::creatorroute::UsedNatives),hash); // PROPOSED 21
    if(dc!=std::end(recomp::creatorroute::UsedNatives))
        discCreatorHandlers[std::size_t(dc-std::begin(recomp::creatorroute::UsedNatives))].store(handler,std::memory_order_release);
    const auto* cm=std::find(std::begin(recomp::creatormenu::UsedNatives),std::end(recomp::creatormenu::UsedNatives),hash); // PROPOSED 17 4
    if(cm!=std::end(recomp::creatormenu::UsedNatives))
        creatorMenuHandlers[std::size_t(cm-std::begin(recomp::creatormenu::UsedNatives))].store(handler,std::memory_order_release);
    const auto* md=std::find(std::begin(recomp::menudraw::UsedNatives),std::end(recomp::menudraw::UsedNatives),hash); // PROPOSED menu look
    if(md!=std::end(recomp::menudraw::UsedNatives))
        menuDrawHandlers[std::size_t(md-std::begin(recomp::menudraw::UsedNatives))].store(handler,std::memory_order_release);
    const auto* im=std::find(std::begin(recomp::imenudiag::UsedNatives),std::end(recomp::imenudiag::UsedNatives),hash); // DIAGNOSTIC imenu
    if(im!=std::end(recomp::imenudiag::UsedNatives))
        imenuDiagHandlers[std::size_t(im-std::begin(recomp::imenudiag::UsedNatives))].store(handler,std::memory_order_release);
    const auto* pd=std::find(std::begin(recomp::propdoors::UsedNatives),std::end(recomp::propdoors::UsedNatives),hash); // property-doors
    if(pd!=std::end(recomp::propdoors::UsedNatives))
        propDoorHandlers[std::size_t(pd-std::begin(recomp::propdoors::UsedNatives))].store(handler,std::memory_order_release);
    const auto* tx=std::find(std::begin(touchx::UsedNatives),std::end(touchx::UsedNatives),hash); // v988 touch exports
    if(tx!=std::end(touchx::UsedNatives))
        touchHandlers[std::size_t(tx-std::begin(touchx::UsedNatives))].store(handler,std::memory_order_release);
    if(hash==InvalidateIdleCam)invalidateIdleCamHandler.store(handler,std::memory_order_release);   // v989
    const auto* imm=std::find(std::begin(recomp::imenu::UsedNatives),std::end(recomp::imenu::UsedNatives),hash); // v989 imenu model
    if(imm!=std::end(recomp::imenu::UsedNatives))
        imenuHandlers[std::size_t(imm-std::begin(recomp::imenu::UsedNatives))].store(handler,std::memory_order_release);
    const auto* imd=std::find(std::begin(recomp::imenudraw::UsedNatives),std::end(recomp::imenudraw::UsedNatives),hash); // v989 imenu draw
    if(imd!=std::end(recomp::imenudraw::UsedNatives))
        imenuDrawHandlers[std::size_t(imd-std::begin(recomp::imenudraw::UsedNatives))].store(handler,std::memory_order_release);
}
namespace {
// v852: the call poll's off switch (files/private-trace/diag-phone-call-off), looked for every SwitchFrames game frames; one
// RENDER_PHONE_CALL line per change of the call bits (and of the switch).
std::atomic<bool> phoneCallOff{false};
std::atomic<std::uint32_t> phoneCallLastFrame{0xFFFFFFFFu}; // (the poll's own once-per-frame word)
std::atomic<std::uint32_t> storyReadLastFrame{0xFFFFFFFFu}; // v1088: the story-character read's own once-per-StoryCharFrames word
void phoneCallSwitch(std::uint32_t frame) noexcept {
    static std::uint32_t nextAt=0;static bool first=true;
    if(!first && frame-nextAt>0x80000000u)return;   // (before nextAt; wraps)
    nextAt=frame+recomp::phonecall::SwitchFrames;
    const bool off=::access(RECOMP_APP_PATH("/private-trace/diag-phone-call-off"),F_OK)==0;
    if(first || off!=phoneCallOff.load(std::memory_order_relaxed)) {
        phoneCallOff.store(off,std::memory_order_relaxed);
        if(off)recomp::phonecall::setCall(false,false);
        char msg[160];std::snprintf(msg,sizeof msg,"RENDER_PHONE_CALL poll=%u ring_native=%u call_native=%u",unsigned(!off),
            unsigned(nativeHandler(recomp::phonecall::IsPedRingtonePlaying)!=0),unsigned(nativeHandler(recomp::phonecall::IsMobilePhoneCallOngoing)!=0));
        trainerLog(msg);
    }
    first=false;
}
// (v953) the rumble counters' timer flush, published by rumble_diag.inc at its first call (null in builds without it)
extern "C" std::atomic<void(*)()> recomp_diag_rumble_tick_fn;
std::atomic<void(*)()> recomp_diag_rumble_tick_fn{nullptr};
// v989: the Interaction Menu (ALLOCATION: recomp_imenu_v1, state bit 23, settings/quick-menu.txt style=fivepocket|imenu, default
// fivepocket). Model = lane model-rows (rows, pages, actions, frame rules); drawing = lane draw-glue's Drawer through its ModelFrame
// (INTEGRATION option B). Back: the pad gate (imenu_back_gate.h) withholds it from the game in the imenu style and hands taps back.
recomp::imenugate::BackGate& imenuGate() noexcept {static recomp::imenugate::BackGate g;return g;}
std::atomic<bool> imenuStyleOn{false};       // style=imenu (the file at start, or recomp_imenu_v1(3|4) at run time)
std::atomic<bool> imenuStyleSet{false};      // an explicit recomp_imenu_v1(3|4) wins over the file read
recomp::imenu::Model& imenuModel() noexcept {
    static recomp::imenu::Model& m=[]()->recomp::imenu::Model& {
        static recomp::imenu::Model x(trainerLog);
        recomp::imenu::Lists l{};
        l.places={int(recomp::online::PlaceList.size()),[](int i)->const char*{return recomp::online::PlaceList[std::size_t(i)].label;}};
        l.activities={int(recomp::activities::Catalog.size()),[](int i)->const char*{return recomp::activities::Catalog[std::size_t(i)].label;}};
        x.setLists(l);x.setBackSource(&imenuGate());   // (Quick GPS: no GpsSource yet, its rows show grey)
        return x;}();
    return m;
}
recomp::imenudraw::ModelFrame& imenuFrame() noexcept {static recomp::imenudraw::ModelFrame f;return f;}
std::atomic<bool> imenuOpenNow{false};       // state bit 23 (any thread reads it)
std::atomic<bool> imenuDiagLines{false};     // diag-imenu-style: the camera-mode and Back lines of the device check
void imenuStyleOnce(std::uint32_t frame) noexcept {   // settings/quick-menu.txt, read like menu.txt (retries while the folder is unknown)
    static int tries=0;static bool done=false;
    if(done || frame%60u)return;
    if(imenuStyleSet.load(std::memory_order_acquire)){done=true;return;}
    // developer only (the public apps never write diag-*): files/private-trace/diag-imenu-style = the imenu style for this process,
    // without writing the app's settings file; it also turns on the RENDER_IMENU cam / back lines the device check reads
    if(::access(RECOMP_APP_PATH("/private-trace/diag-imenu-style"),F_OK)==0) {
        done=true;imenuStyleOn.store(true,std::memory_order_release);imenuDiagLines.store(true,std::memory_order_relaxed);
        trainerLog("RENDER_IMENU style=imenu source=diag");return;
    }
    FILE* f=std::fopen(RECOMP_APP_PATH("/settings/quick-menu.txt"),"rb");
    if(!f){if(++tries>=30)done=true;return;}
    char text[256]{};const std::size_t n=std::fread(text,1,sizeof text-1,f);std::fclose(f);text[n]=0;done=true;
    const bool on=std::strstr(text,"style=imenu")!=nullptr;
    if(!imenuStyleSet.load(std::memory_order_acquire))imenuStyleOn.store(on,std::memory_order_release);
    char l[96];std::snprintf(l,sizeof l,"RENDER_IMENU style=%s source=file",on?"imenu":"fivepocket");trainerLog(l);
}
// The pad read's hook (guest_import_registry.cpp XamInputGetState, user 0): one acquire load while the gate is off.
void imenuPadFilter(std::uint32_t user,std::uint16_t& buttons) noexcept {
    if(user!=0)return;
    const std::uint64_t now=std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    imenuGate().filter(buttons,now);
}
void imenuPadFilterC(std::uint32_t user,std::uint16_t* buttons) noexcept {if(buttons)imenuPadFilter(user,*buttons);}
// v988: the app's saved aim choice (settings/menu.txt, the apps' file), read once. The app folder can be derived after the first frames: a
// missing file is looked for again every 60 frames, at most 30 times.
// v1172 (android-qol / lead): the apps write only non-default words (delete-when-default), and the Fold's default is Strong, so NO
// "aim-assist=" word (or no file at all) is the Strong preference too ("aim-assist=game" = the game's own mode). This is a preference
// only: since v1171 Strong is applied only after a touch input in this process, so a pad player / the Thor (no touch calls) keeps the
// game's own targeting mode.
void touchSettingsOnce(std::uint32_t frame) noexcept {
    static int tries=0;static bool done=false;
    if(done || frame%60u)return;
    FILE* f=std::fopen(RECOMP_APP_PATH("/settings/menu.txt"),"rb");
    if(!f){if(++tries>=30){done=true;touchX().loadSettings(nullptr);trainerLog("RENDER_TOUCHX settings aim=strong (no menu.txt: the default; applied on touch only)");}return;}
    char text[1024]{};const std::size_t n=std::fread(text,1,sizeof text-1,f);std::fclose(f);text[n]=0;done=true;
    touchX().loadSettings(text);
    char l[112];std::snprintf(l,sizeof l,"RENDER_TOUCHX settings aim=%s%s (applied on touch only)",touchX().aim(-1)?"strong":"game",std::strstr(text,"aim-assist=")?"":" (no word: the default)");trainerLog(l);
}
// v988 (developer only; the public apps never write diag-*): files/private-trace/diag-touchx, looked for every SwitchFrames frames.
// Its first line calls an export as the apps would: "turn Y P" (recomp_camera_turn_v1) / "aim M" (recomp_aim_assist_v1) / "read" (the
// relative heading / pitch and the aim state, logged at the next touch tick). The file is removed once a line is read.
std::atomic<bool> touchReadWanted{false};
void touchDiagSwitch(std::uint32_t frame) noexcept {
    static std::uint32_t nextAt=0;static bool first=true;
    if(!first && frame-nextAt>0x80000000u)return;
    nextAt=frame+recomp::phonecall::SwitchFrames;first=false;
    constexpr const char* Path=RECOMP_APP_PATH("/private-trace/diag-touchx");
    FILE* f=std::fopen(Path,"r");
    if(!f)return;
    char line[64]{};const bool got=std::fgets(line,sizeof line,f)!=nullptr;std::fclose(f);
    if(!got || !line[0])return;   // (still being written)
    ::unlink(Path);
    float y=0.f,p=0.f;int m=0,r=-9;char msg[128];
    if(std::sscanf(line,"turn %f %f",&y,&p)==2){r=touchX().turn(y,p);std::snprintf(msg,sizeof msg,"RENDER_TOUCHX diag turn yaw=%.1f pitch=%.1f result=%d frame=%u",double(y),double(p),r,frame);}
    else if(std::sscanf(line,"aim %d",&m)==1){r=touchX().aim(m);std::snprintf(msg,sizeof msg,"RENDER_TOUCHX diag aim mode=%d result=%d frame=%u",m,r,frame);}
    else if(std::strncmp(line,"read",4)==0){touchReadWanted.store(true,std::memory_order_release);std::snprintf(msg,sizeof msg,"RENDER_TOUCHX diag read frame=%u",frame);}
    else if(std::sscanf(line,"reticle %d",&m)==1){touchX().reticleArm(m);std::snprintf(msg,sizeof msg,"RENDER_TOUCHX diag reticle ticks=%d frame=%u",m,frame);}   // red reticle: N ticks of transition logs without the app
    else std::snprintf(msg,sizeof msg,"RENDER_TOUCHX diag bad-line frame=%u",frame);
    trainerLog(msg);
}
// v953 (Update 1 W1-5, diagnostic attempt A): files/private-trace/diag-trainer-probe, looked for every SwitchFrames game frames on the
// same once-per-frame path (so it works with the trainer off): when present it is removed and ONE Probe request (action 14) is queued.
void trainerProbeSwitch(std::uint32_t frame) noexcept {
    static std::uint32_t nextAt=0;static bool first=true;
    if(!first && frame-nextAt>0x80000000u)return;   // (before nextAt; wraps)
    nextAt=frame+recomp::phonecall::SwitchFrames;first=false;
    if(auto* tick=recomp_diag_rumble_tick_fn.load(std::memory_order_acquire))tick(); // (v953: the rumble counters' timer flush)
    constexpr const char* Path=RECOMP_APP_PATH("/private-trace/diag-trainer-probe");
    FILE* f=std::fopen(Path,"r");
    if(!f)return;
    // OW L2 1b (D-NET1): a first byte '1' asks for the net probe (Probe a0 = 1); anything else (v953's empty file) the model probe
    const int byte0=std::fgetc(f);std::fclose(f);
    ::unlink(Path);
    const int mode=byte0=='1'?recomp::trainer::ProbeNet:byte0=='2'?recomp::trainer::ProbeSession:recomp::trainer::ProbeModels;   // v960: '2' = D-NET2
    const int seq=trainer().request(recomp::trainer::Probe,mode,0,0);
    char msg[112];std::snprintf(msg,sizeof msg,"RENDER_TRAINER probe switch seen frame=%u mode=%d request=%d",frame,mode,seq);trainerLog(msg);
}
// v1032 DIAGNOSTIC (bugfix-stability, G2: the trailers3.xnv re-read loop; test-only, the public apps never write diag-*):
// files/private-trace/diag-trainer-spawn holds ONE vehicle model name (lower-case letters/digits, <= 23 chars), looked for every
// SwitchFrames game frames in Story or Sandbox mode. When present it is read and removed, the trainer is switched on IN MEMORY
// for this process (files/settings/trainer.txt is not touched; the app's next Sync sets it back from that file), and ONE
// SpawnVehicle request by hash (a1 = 0: joaat of the name) is queued; the trainer's own job requests the model, waits up to
// ModelWaitFramesMax and creates it at the player. One line says what was asked; the trainer's job logs the result.
bool parseSpawnModelLine(const char* line,char* out,std::size_t cap) noexcept {
    std::size_t n=0;const char* p=line;while(*p==' ')++p;
    for(;*p && *p!='\n' && *p!='\r' && *p!=' ';++p) {
        const char c=*p;
        if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_'))return false;
        if(n+1>=cap)return false;
        out[n++]=c;
    }
    out[n]=0;
    return n>0;
}
void trainerSpawnSwitch(std::uint32_t frame) noexcept {
    static std::uint32_t nextAt=0;static bool first=true;
    if(!first && frame-nextAt>0x80000000u)return;   // (before nextAt; wraps)
    nextAt=frame+recomp::phonecall::SwitchFrames;first=false;
    constexpr const char* Path=RECOMP_APP_PATH("/private-trace/diag-trainer-spawn");
    FILE* f=std::fopen(Path,"r");
    if(!f)return;
    char line[32]{};const bool got=std::fgets(line,sizeof line,f)!=nullptr;
    std::fclose(f);
    if(!got || !line[0])return;   // (empty: still being written)
    ::unlink(Path);
    char model[24]{};
    int seq=-100;std::uint32_t hash=0;
    if(parseSpawnModelLine(line,model,sizeof model)) {
        hash=recomp::trainer::joaat(model);
        if(!trainer().enabled())trainer().setEnabled(true);   // in memory only (logged by the trainer: RENDER_TRAINER enabled=1)
        seq=trainer().request(recomp::trainer::SpawnVehicle,static_cast<int>(hash),0,0);
    }
    char msg[128];std::snprintf(msg,sizeof msg,"RENDER_TRAINER spawn switch seen frame=%u model=%s hash=%08x request=%d",frame,model[0]?model:"-",hash,seq);
    trainerLog(msg);
}
// Update 1 P7 (experiment E5, developer only; the public apps never write diag-*): files/private-trace/diag-trainer-switch-character,
// looked for every SwitchFrames game frames and only in Sandbox mode (Story mode never even looks). Its first line "C [M]" (character
// 0..2, method 0 change-ped / 1 set-model, default 0) queues ONE SwitchCharacter request; the file is removed once a line is read (an
// empty file is left for the next look: it may still be being written).
bool parseSwitchCharacterLine(const char* line,int& character,int& method) noexcept {
    character=-1;method=0;
    const char* p=line;while(*p==' ')++p;
    if(*p<'0' || *p>'2')return false;
    character=*p++-'0';
    while(*p==' ')++p;
    if(*p=='0' || *p=='1'){method=*p++-'0';while(*p==' ')++p;}
    return *p==0 || *p=='\n' || *p=='\r';
}
void trainerSwitchCharacterSwitch(std::uint32_t frame) noexcept {
    if(!recomp::sandbox::active())return;
    static std::uint32_t nextAt=0;static bool first=true;
    if(!first && frame-nextAt>0x80000000u)return;   // (before nextAt; wraps)
    nextAt=frame+recomp::phonecall::SwitchFrames;first=false;
    constexpr const char* Path=RECOMP_APP_PATH("/private-trace/diag-trainer-switch-character");
    FILE* f=std::fopen(Path,"r");
    if(!f)return;
    char line[16]{};const bool got=std::fgets(line,sizeof line,f)!=nullptr;
    std::fclose(f);
    if(!got || !line[0])return;   // (empty: still being written; the next look reads it, as autopilotPoll does)
    ::unlink(Path);
    int character=-1,method=0;
    const int seq=parseSwitchCharacterLine(line,character,method)?trainer().request(recomp::trainer::SwitchCharacter,character,method,0):-100;
    char msg[112];std::snprintf(msg,sizeof msg,"RENDER_TRAINER switch-character switch seen frame=%u character=%d method=%d request=%d",frame,character,method,seq);
    trainerLog(msg);
}
template<class N> void phoneCallTick(N& natives) {
    if(phoneCallOff.load(std::memory_order_relaxed))return;
    const unsigned before=recomp::phonecall::state()&(recomp::phonecall::Ringing|recomp::phonecall::InCall);
    const unsigned now=recomp::phonecall::poll(natives);
    if(now!=before){char msg[96];std::snprintf(msg,sizeof msg,"RENDER_PHONE_CALL ring=%u call=%u up=%u",unsigned((now&recomp::phonecall::Ringing)!=0),
        unsigned((now&recomp::phonecall::InCall)!=0),unsigned((recomp::phonecall::state()&recomp::phonecall::Up)!=0));trainerLog(msg);}
}
} // namespace
namespace {
// v842: the bot's command file, files/private-trace/diag-autopilot (one line, written atomically by the bot: tmp + mv), looked for at
// most every 100 ms on the script thread; read, removed and handed to the autopilot. Nothing else reads it.
void autopilotPoll() noexcept {
    static std::atomic<long long> next{0};
    const long long now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(now<next.load(std::memory_order_relaxed))return;
    next.store(now+100,std::memory_order_relaxed);
    constexpr const char* Path=RECOMP_APP_PATH("/private-trace/diag-autopilot");
    FILE* f=std::fopen(Path,"r");
    if(!f)return;
    char line[256]{};const bool got=std::fgets(line,sizeof line,f)!=nullptr;
    std::fclose(f);
    if(!got || !line[0])return;   // (empty: being written; the next poll reads it)
    ::unlink(Path);
    recomp::autopilot::Command c;
    if(recomp::autopilot::parse(line,c))autopilot().submit(c);
    else {char msg[300];std::snprintf(msg,sizeof msg,"RENDER_AUTOPILOT rejected line=%.200s",line);trainerLog(msg);}
}
} // namespace
// v842: the owner's own input (the physical pad or the app's touch), from the gamepad callback: the autopilot lets go at once.
extern "C" void recomp_autopilot_note_human() noexcept {autopilot().noteHuman();}
namespace {
// Update 1 P4: the Sandbox tick (sandbox_mode.h); its weapons go through the trainer's own GiveWeapons (logged, gated).
int sandboxWeapons() {return trainer().request(recomp::trainer::GiveWeapons,0,0,0);}
recomp::sandbox::Tick& sandboxTick() {static recomp::sandbox::Tick tk(recomp::sandbox::policy(),recomp::sandbox::log,sandboxWeapons);return tk;}
// Update 1 P8: Online World Level 1. The trainer gets the online actions (15, and 16 if accepted) only in Sandbox mode. The instance
// priority mode (developer-only until E4 shows an effect) is wanted while sandbox_flags bit 0 (recomp_online_set_flags) is set or the
// developer switch files/private-trace/diag-sandbox-online-world exists (looked for at the first Sandbox tick, then every
// SwitchFrames game frames, so E4 can run A/B/A/B in one run; written only by test tools).
recomp::online::Actions& onlineActions() {static recomp::online::Actions a;return a;}
recomp::online::World& onlineWorld() {static recomp::online::World w;return w;}
std::atomic<bool> onlineFlagBit{false},onlineDevSwitch{false};
void onlineWorldWanted() noexcept {onlineWorld().setWanted(onlineFlagBit.load(std::memory_order_relaxed) || onlineDevSwitch.load(std::memory_order_relaxed));}
void onlineReadWorldSwitch(const char* why,bool always) noexcept {
    const bool dev=::access(RECOMP_APP_PATH("/private-trace/diag-sandbox-online-world"),F_OK)==0;
    if(dev==onlineDevSwitch.load(std::memory_order_relaxed) && !always)return;
    onlineDevSwitch.store(dev,std::memory_order_relaxed);onlineWorldWanted();
    char l[96];std::snprintf(l,sizeof l,"RENDER_ONLINE %s world_wanted=%d",why,onlineWorld().wanted()?1:0);trainerLog(l);
}
// E4: the mode word logged once at about 60 s and 180 s after the first Sandbox tick (on the SwitchFrames cadence) while the mode is
// wanted or applied. The clock is a pointer so the host test can drive it.
double onlineSteadySeconds() noexcept {return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
double (*onlineClock)() noexcept=onlineSteadySeconds;
double onlineStartedAt=0;unsigned onlineWordLogs=0;
// OW L2 1c: the activity launcher (action 19) and its start. The start is the game's start-script implementation sub_82AAC528 (r3
// name, r4 argument block, r5 argument bytes, r6 stack -> r3 thread id, 0 = failed), reached exactly as the START_NEW_SCRIPT handlers
// reach it: through the function mapping of its guest address (the pointer at 0x8386AA50 holds 0x82AAC528; ppc_func_mapping.cpp maps it
// to the symbol sandbox_launch.inc hooks), so the Sandbox filter sub_82A9B020 still decides. Not the natives: their handler writes the
// CALLING thread (+12, bytes 48..63; activities.h header). lr = TrainerStartLr, a sentinel that is no guest code address (only data on
// this path: the recompiled function saves it and returns by C++ return); sandbox_launch's record prints it as via=other.
constexpr std::uint32_t StartScriptGuest=0x82AAC528u,TrainerStartLr=0x7F7F0017u;
struct GuestStarter final : recomp::activities::Starter {
    PPCContext& c;std::uint8_t* base;std::uint32_t r1;
    GuestStarter(PPCContext& cc,std::uint8_t* b,std::uint32_t frameBase) noexcept:c(cc),base(b),r1(frameBase) {}
    std::uint32_t start(std::uint32_t name,std::uint32_t block,std::uint32_t bytes,std::uint32_t stack) override {
        PPCFunc* host=trainerHost(StartScriptGuest);
        if(!host)return 0;
        const PPCContext saved=c;   // (the tick restores the whole context too; this call leaves it as it found it)
        // r1 = the GuestNatives frame base: the callee's frame (stwu r1,-144 and deeper) grows down, away from the scratch above it
        c.r1.u64=r1;c.r3.u64=name;c.r4.u64=block;c.r5.u64=bytes;c.r6.u64=stack;c.lr=TrainerStartLr;
        host(c,base);
        const std::uint32_t id=c.r3.u32;
        c=saved;
        return id;
    }    // v1017: the activity launcher's fmflag hold (activities.h holdFmFlag / releaseFmFlag): the same range as DiscCreatorGlue::storeWord;
    // the address is always the VM's block table entry + 4 * the listed global (Launcher::globalAddr), never a value from elsewhere.
    bool storeWord(std::uint32_t a,std::uint32_t v) override {
        if(!base || a<0x40000000u || a>=0xF0000000u || (a&3u))return false;
        trainerPut(base,a,v);return true;
    }
};
recomp::activities::Activities& activities() {static recomp::activities::Activities a(trainerLog,recomp::sandbox::active);return a;}
// lane property-doors (PROPOSED): the property icons and walk-in doors (property_doors.h), Sandbox mode only. The trips are the
// trainer's own action 16 (in: a0 = PlaceList index; out: a0 = propdoors::exitArg), so they share its gate, queue and job; a trip's
// result is read back from the trainer's status line ("seq=N ok|fail|dropped ..."): 0 not yet, 1 ok, 2 failed / dropped / lost.
int propDoorsRequest(int a0) {return trainer().request(recomp::trainer::OnlinePlace,a0,0,0);}
int propDoorsPoll(int seq) {
    char s[128]{};const int at=trainer().status(s,sizeof s);
    if(at<seq)return 0;
    if(at>seq)return 2;   // a later request finished first: this one's line is gone (counted as failed; the doors stay safe)
    int got=0;char kind[16]{};
    if(std::sscanf(s,"seq=%d %15s",&got,kind)!=2 || got!=seq)return 2;
    return std::strcmp(kind,"ok")==0?1:2;
}
recomp::propdoors::Doors& propDoors() {static recomp::propdoors::Doors d(trainerLog,{propDoorsRequest,propDoorsRequest,propDoorsPoll});return d;}
bool propDoorsUsed=false;   // ticked at least once (Sandbox): the Sandbox-off guard removes the icons
// OW L2 1a: the creator's record lives in the Sandbox generation, <base>/sandbox-mode/g<N>/online/character.json. The root is what the
// saves state was made with (guest_import_registry.cpp savesStateFor: filesRootFor(RECOMP_SAVES_DIR or the files directory), resolved
// and cached once per process; the same key here gets the same answer). The store refuses any root that is not a Sandbox generation.
std::string onlineCreatorRoot() {
    const char* savesBase=std::getenv("RECOMP_SAVES_DIR");
    return recomp::bootstrap::save_sandbox::filesRootFor(savesBase?savesBase:RECOMP_APP_PATH(""));
}
// v959: the face camera's natives are all registered (their handler recorded and mapped), else the camera is skipped (logged once).
bool faceCamNativeRegistered(std::uint32_t hash) noexcept {const std::uint32_t h=nativeHandler(hash);return h && trainerHost(h);}
bool onlineReady=false;   // v959: the online actions were handed over (first Sandbox tick); the Sandbox-off guard below reads it
// PROPOSED (lane upd1-spoof/creator-route, action 21): the disc creator route's glue. Constructed only in Sandbox mode with
// files/private-trace/diag-online-local present (the signin-spoof lane's switch; read once per process). The story-save check is
// creator_story_digest.h's digest of <story base>/saves (recursive, read only; <story base> = the saves state's Sandbox generation
// root without "/sandbox-mode/g<N>", i.e. the save-sandbox root when diag-save-sandbox is on), taken when a run is REQUESTED (not at
// the hand-over: no hash on the first Sandbox tick) and again when it ends. Both are on the script thread; the time is logged.
bool discCreatorSwitch() noexcept {return ::recomp::online_local_glue::enabled();}   // v968 B2: the one latch (Sandbox-only read)
recomp::creatorroute::story::Digest storySavesDigest(const char* when) noexcept {
    const auto t0=std::chrono::steady_clock::now();
    const recomp::creatorroute::story::Digest d=recomp::creatorroute::story::digestOf(recomp::creatorroute::story::storyBaseOf(onlineCreatorRoot()));
    const long long ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-t0).count();
    char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE creator story-digest when=%s %s ms=%lld",when,d.text().c_str(),ms);trainerLog(l);
    return d;
}
// The id of the thread whose native call hosts this tick: 0 = none (no current thread), Unknown = a current-thread word that is not a
// plausible guest pointer (then hostIs answers true for every id: the route defers, never writes or terminates on a guess).
constexpr std::uint32_t DiscCreatorHostUnknown=0xFFFFFFFFu;
std::uint32_t discCreatorHostThread=0;   // set per tick below, only while a run is active
std::atomic<std::uint32_t> discCreatorLastFrame{0xFFFFFFFFu};   // v989: the route's own once-per-frame claim
constexpr std::uint32_t DiscCreatorHostedWaitFrames=30;
struct DiscCreatorGlue final : recomp::creatorroute::Ops, recomp::trainer::DiscCreatorActions {
    PPCContext* c=nullptr;std::uint8_t* base=nullptr;std::uint32_t r1=0;   // set on each Sandbox tick (script thread)
    recomp::creatorroute::story::Digest storyAtStart;
    recomp::creatorroute::Route route{*this,trainerLog};
    recomp::creator::FileStore look{onlineCreatorRoot()},raw{onlineCreatorRoot(),"creator.json"};
    bool sandbox() override {return recomp::sandbox::active();}
    bool devSwitch() override {return discCreatorSwitch();}
    bool storyUnchanged() override {return storyAtStart.usable() && storySavesDigest("end")==storyAtStart;}
    // Sandbox saves go to the generation only (save_sandbox; the story root is never opened for writing in Sandbox) and the v955 core's
    // autosave hold is on for the whole session; nothing extra to hold here. NEEDS DEVICE CHECK C-DC1: no autosave line during a run.
    void holdSaves(bool) override {}
    bool hostIs(std::uint32_t id) override {return id && (discCreatorHostThread==id || discCreatorHostThread==DiscCreatorHostUnknown);}
    bool storeWord(std::uint32_t a,std::uint32_t v) override {
        // v988: the same range as creator_route's plausible() since v986 (the Sandbox's global blocks are at 0xACC4xxxx on the Thor:
        // t-creator v987 refused "game-mode pin ok=0" under the old < 0xA0000000 bound). The address is always the VM's own block
        // table entry + 4 * a listed global (creator_route.h addr()), never a value from elsewhere.
        if(!base || a<0x40000000u || a>=0xF0000000u || (a&3u))return false;
        trainerPut(base,a,v);return true;
    }
    std::uint32_t start(std::uint32_t name,std::uint32_t stack) override {
        if(!c || !base)return 0;
        GuestStarter st(*c,base,r1);return st.start(name,0,0,stack);
    }
    bool saveLook(const recomp::creator::Look& l) override {
        char buf[recomp::creator::RecordMax];const int k=recomp::creator::serialize(l,buf,sizeof buf);
        return k>0 && look.usable() && look.write(buf,k);
    }
    bool saveRaw(const char* t,int n) override {return raw.usable() && raw.write(t,n);}
    bool requestTrainer(int action,int a0) override {return trainer().request(action,a0,0,0)>0;}
    bool keepDark() override {return ::recomp::bootstrap::play_mode::online();}   // v1066: Online mode's first visit (the controller reveals)
    bool validArgument(int a0) const noexcept override {return recomp::creatorroute::Route::validArgument(a0);}
    int request(recomp::trainer::Natives& nat,int a0,char* d,int cap) noexcept override {
        if(a0==recomp::creatorroute::StartOp && !route.active() && !route.savesBlocked()) {   // the baseline, once per requested run
            storyAtStart=storySavesDigest("start");
            if(!storyAtStart.usable()){if(d && cap>0)std::snprintf(d,std::size_t(cap),"story-root-%s",storyAtStart.state.c_str());return 2;}
        }
        return route.request(nat,a0,d,cap);
    }
    void cancel() noexcept override {route.requestCancel();}   // any thread (trainer.h: never queued)
};
DiscCreatorGlue* discCreatorGlue() noexcept {
    if(!recomp::sandbox::active() || !discCreatorSwitch())return nullptr;   // Story mode, or no switch: never constructed
    static DiscCreatorGlue g;return &g;
}
void onlineFirstSandboxTick() noexcept {
    if(!recomp::sandbox::active())return;   // Story mode never gets the online actions
    static bool done=false;
    if(done)return;
    done=true;
    static recomp::creator::FileStore store(onlineCreatorRoot());   // OW L2 1a (CC NOTES 4.4: the 3 glue lines)
    if(store.usable())onlineActions().setCreatorStore(&store);
    // v1509 (shops subagent): in Online mode the saved look first takes what the game's own outfit stats changed since the creator's
    // capture (look_refresh.h: the shops' purchases survive a restart and a death). files/private-trace/diag-look-refresh-off = v1183.
    if(::recomp::bootstrap::play_mode::online()) {
        static recomp::creator::FileStore raw(onlineCreatorRoot(),"creator.json");
        static recomp::look_refresh::GameRefresher refresher(&raw,
            []{static const bool off=::access(RECOMP_APP_PATH("/private-trace/diag-look-refresh-off"),F_OK)==0;return !off && ::recomp::bootstrap::play_mode::online();},
            trainerLog);
        if(store.usable() && raw.usable())onlineActions().setLookRefresher(&refresher);
        {char l[120];std::snprintf(l,sizeof l,"RENDER_ONLINE look refresh %s",store.usable() && raw.usable()?"on (the game's outfit stats over the record)":"off (no store)");trainerLog(l);}
    }
    {char l[96];std::snprintf(l,sizeof l,"RENDER_ONLINE look store=%s",store.usable()?"sandbox-generation":"none");trainerLog(l);}
    trainer().setActivities(&activities());   // OW L2 1c (Sandbox only)
    onlineActions().setPlaceLog(trainerLog);   // wave2 mp-interiors: one RENDER_ONLINE place line per trip
    onlineActions().setCameraLog(trainerLog);  // v959: RENDER_ONLINE look camera=on|off reason=...
    onlineActions().setCameraNativeCheck(faceCamNativeRegistered);   // v959: no camera at all unless every native it calls is registered
    onlineActions().setMenuLog(trainerLog);                         // PROPOSED 17 4: RENDER_ONLINE menu ... lines
    onlineActions().setMenuNativeCheck(faceCamNativeRegistered);    // PROPOSED 17 4: the menu refuses to open unless all its natives are
    onlineReady=true;
    if(DiscCreatorGlue* dc=discCreatorGlue())trainer().setDiscCreator(dc);   // PROPOSED action 21 (diag-online-local only)
    trainer().setOnline(&onlineActions());
    // PROPOSED action 20 (lane upd1-spoof/intro-cutscene): the intro player only with the developer switch diag-online-local (ALLOCATION
    // section 4: Sandbox only; Story never reaches this function). Read once here; without the file action 20 answers ErrArgs.
    if(::recomp::online_local_glue::enabled()) {   // v968 B2: the one latch (was its own access(); the same file, Sandbox only)
        static recomp::intro::Actions intro(trainerLog);
        trainer().setIntro(&intro);
        trainerLog("RENDER_ONLINE intro ready switch=diag-online-local");
    }
    onlineStartedAt=onlineClock();
    onlineReadWorldSwitch("ready",true);
}
// The world toggle and the character reconciliation, every Sandbox tick. Nothing wanted, nothing applied and no freemode model on:
// no native call and no guest load at all.
void onlineTick(recomp::trainer::Natives& nat) noexcept {
    auto& w=onlineWorld();
    if(w.wanted() || w.applied())w.tick(nat,trainerLog);   // one guest load per tick while wanted; a native call only on a change
    const auto& ch=onlineActions().character();
    if(ch.active() && !ch.busy()) { // a freemode model is on: the game may have replaced it (one PLAYER_PED_ID; GET_ENTITY_MODEL per new ped)
        std::uint32_t out[3]{};
        const std::uint32_t ped=nat.call(PlayerPedIdHash,nullptr,0,out)?out[0]:0u;
        onlineActions().reconcile(nat,ped,trainerLog);
    }
}
// Developer switch for device experiments E3 / E4 before any app offers the actions (like v953's diag-trainer-probe): the file
// files/private-trace/diag-trainer-online holds "ACTION A0" (15 or 16 only). Looked for every SwitchFrames game frames in Sandbox mode,
// after the Sandbox block has forced the trainer on, and queued as ONE trainer request; removed once the trainer took it or refused its
// words (ErrArgs), kept for the next look otherwise (ErrOff / ErrFull). The same cadence reads the world switch and the E4 word lines.
// Written only by test tools; the public app never writes diag-* files.
void onlineSwitch(recomp::trainer::Natives& nat,std::uint32_t frame) noexcept {
    if(!recomp::sandbox::active())return;
    static std::uint32_t nextAt=0;static bool first=true;
    if(!first && frame-nextAt>0x80000000u)return;   // (before nextAt; wraps)
    nextAt=frame+recomp::phonecall::SwitchFrames;first=false;
    onlineReadWorldSwitch("switch",false);
    activities().setUngatedSwitch(::access(RECOMP_APP_PATH("/private-trace/diag-activity-ungated"),F_OK)==0); // OW L2 1c (FA-D2 only)
    activities().setEndCondSwitch(::access(RECOMP_APP_PATH("/private-trace/diag-activity-endcond-off"),F_OK)!=0); // v1350: default ON (off switch)
    recomp::intro::visibilityExperiment.store(::access(RECOMP_APP_PATH("/private-trace/diag-intro-vis"),F_OK)==0,std::memory_order_relaxed);   // v1014
    auto& w=onlineWorld();
    if(onlineWordLogs<2 && (w.wanted() || w.applied()) && onlineClock()-onlineStartedAt>=(onlineWordLogs?180.0:60.0)) {
        char l[96];std::snprintf(l,sizeof l,"RENDER_ONLINE instance_priority_word=%u at=%us wanted=%d resets=%u",nat.load(recomp::online::ModeWord),
                                 onlineWordLogs?180u:60u,w.wanted()?1:0,w.resets());trainerLog(l);
        ++onlineWordLogs;
    }
    constexpr const char* Path=RECOMP_APP_PATH("/private-trace/diag-trainer-online");
    FILE* f=std::fopen(Path,"r");
    if(!f)return;
    char line[64]{};const bool got=std::fgets(line,sizeof line,f)!=nullptr;std::fclose(f);
    int action=-1,a0=-1,a1=0;
    float tx=0,ty=0,tz=0;   // v1020 TEST ONLY: "tp X Y Z" = one trip through the trainer's teleport job (trainer.h TestTeleportIndex)
    const bool exactTp=got && std::sscanf(line,"tpz %f %f %f",&tx,&ty,&tz)==3;   // v1029: exact height, no ground probe
    if(exactTp || (got && std::sscanf(line,"tp %f %f %f",&tx,&ty,&tz)==3)) {
        const bool ok=std::isfinite(tx) && std::isfinite(ty) && std::isfinite(tz) && std::fabs(tx)<=20000.f && std::fabs(ty)<=20000.f;
        if(ok)recomp::trainer::armTestTeleport(tx,ty,tz,exactTp);
        const int seq=ok?trainer().request(recomp::trainer::TeleportLandmark,recomp::trainer::TestTeleportIndex,0,0):recomp::trainer::ErrArgs;
        if(seq>=0 || seq==recomp::trainer::ErrArgs){::unlink(Path);if(seq<0)recomp::trainer::testTeleportArmed.store(false,std::memory_order_release);}
        char msg[128];std::snprintf(msg,sizeof msg,"RENDER_ONLINE switch tp%s=%.1f,%.1f,%.1f request=%d",exactTp?"z":"",double(tx),double(ty),double(tz),seq);trainerLog(msg);
        return;
    }
    if(!got || std::sscanf(line,"%d %d %d",&action,&a0,&a1)<2)return;   // (empty: being written; the next look reads it)
    // OW L2: 17 / 18 (online()) and 19 Activity ("19 INDEX OP") too; a1 is used by 19 only
    // v968 (spoof plan C2 + D1): 20 OnlineIntro ("20 A0 A1") and 21 DiscCreator ("21 0" start / "21 1" cancel) too; a1 for 19 and 20
    // v996 (t-nextweapon v989: every "22" line was refused ErrArgs here, so the device check never reached the trainer): also 22
    // NextWeapon ("22 0" next / "22 1" previous). The general actions (4, 5, ...) stay refused (online_glue_test pins 5 -> ErrArgs).
    const bool ours=recomp::trainer::Trainer::online(action) || action==recomp::trainer::Activity
                    || action==recomp::trainer::OnlineIntro || action==recomp::trainer::DiscCreator || action==recomp::trainer::NextWeapon
                    || action==recomp::trainer::AddCash;   // v1153: "23 N" = AddCash N dollars (test tools; the app sends it through recomp_trainer_v1)
    const bool withA1=action==recomp::trainer::Activity || action==recomp::trainer::OnlineIntro;
    const int seq=ours?trainer().request(action,a0,withA1?a1:0,0):recomp::trainer::ErrArgs;
    if(seq>=0 || seq==recomp::trainer::ErrArgs)::unlink(Path);
    char extra[24]="";if(withA1)std::snprintf(extra,sizeof extra," a1=%d",a1);
    char msg[128];std::snprintf(msg,sizeof msg,"RENDER_ONLINE switch action=%d a0=%d request=%d%s",action,a0,seq,extra);
    trainerLog(msg);   // (v955's line unchanged for 15 / 16 / 17 / 18; " a1=N" only for 19, where it is the start / stop word)
}
} // namespace
namespace {
// DIAGNOSTIC (lane upd1-imenu/diag-run, plan item 4: E1-E5 + the nested-text guard + the frame-cost A/B). Developer files, written only by
// the device script (the public apps never write diag-*), looked for at most every 100 ms on the script thread and only in Sandbox mode:
//  - files/private-trace/diag-imenu: ONE command line (imenu_diag.h Diag::command), read and removed (an empty file is left: still being
//    written). The first accepted command activates the diagnostic and logs E0 (every native registered AND mapped).
//  - files/private-trace/diag-imenu-ab: present = the menu forced open (tools/iter/ab_toggle.sh flips it), absent = closed.
// Never activated: one steady-clock read per Sandbox tick and one fopen + access every 100 ms; no native call, no guest load.
struct GuestWords final : recomp::imenudiag::Words {
    std::uint8_t* base;
    explicit GuestWords(std::uint8_t* b) noexcept:base(b) {}
    std::uint32_t load(std::uint32_t a) override {return trainerBe(base,a);}
    bool store(std::uint32_t a,std::uint32_t v) override {   // the text style / GFX-align block only (imenu_diag.h storeAllowed)
        if(!recomp::imenudiag::storeAllowed(a))return false;
        trainerPut(base,a,v);return true;
    }
};
std::uint64_t imenuNowUs() {return std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
recomp::imenudiag::Diag& imenuDiag() {static recomp::imenudiag::Diag d(trainerLog);return d;}
bool imenuNativeUsable(std::uint32_t hash) noexcept {const std::uint32_t h=nativeHandler(hash);return h && trainerHost(h);}
void imenuPoll(std::uint32_t frame) noexcept {
    static long long next=0;static bool reported=false;
    const long long now=(long long)(imenuNowUs()/1000);
    if(now<next)return;
    next=now+100;
    constexpr const char* Path=RECOMP_APP_PATH("/private-trace/diag-imenu");
    if(FILE* f=std::fopen(Path,"r")) {
        char line[64]{};const bool got=std::fgets(line,sizeof line,f)!=nullptr;std::fclose(f);
        if(got && line[0]){::unlink(Path);imenuDiag().command(line,frame);}
    }
    imenuDiag().setAb(::access(RECOMP_APP_PATH("/private-trace/diag-imenu-ab"),F_OK)==0,frame);
    if(imenuDiag().active() && !reported){reported=true;recomp::imenudiag::reportNatives(imenuNativeUsable,trainerLog);}
}
} // namespace
// Update 1 P8: sandbox_flags (ALLOCATION.md §3: the start extra; bit 0 = instance priority mode). The play-mode code calls this when
// the launcher passes the flags; in Story mode the bit is ignored (the tick below runs only in Sandbox mode). Not an export.
extern "C" void recomp_online_set_flags(unsigned flags) noexcept {onlineFlagBit.store((flags&1u)!=0,std::memory_order_relaxed);onlineWorldWanted();}
// Update 1 P8: the Sandbox core calls this with 1 once its autosave hold on recomp_trainer_state_v1 bit 20 is in place; until then
// trainer action 15 answers "fail no-autosave-hold" for the freemode models. Not an export.
extern "C" void recomp_online_set_autosave_hold(unsigned on) noexcept {onlineActions().setAutosaveHold(on!=0);}
// v1119 (diagnostic; files/private-trace/diag-script-trace, read once; with diag-v273-probe-all every script native reaches this hook): the
// natives one script calls - fmmc_launcher (a3fb8a5e) unless the file holds another script hash in hex - each new (ip, native) once (v1120; v1119 keyed the result too),
// at most ScriptTraceMax lines. The VM's thread context is r23 (+4 script hash, +12 the native's ip); the call context is the VM's r1+128
// (sub_83439990: addi r3,r1,128; bctrl), its word 0 the result pointer. Read only.
namespace script_trace {
constexpr unsigned ScriptTraceMax=2000,SeenMax=2400;   // v1120: v1119's 600 filled in fmmc's first 20 frames
// v1128 (diagnostic; t-onldeathroute126: on the route the engine's respawn search never became active after a death): files/private-trace/
// diag-script-trace-death (with diag-v273-probe-all) traces freemode (c875557d, or the hex the file holds) and only from the player's death on
// (armed by the Online glue when NETWORK_IS_GAME_IN_PROGRESS and IS_PLAYER_DEAD(PLAYER_ID)), so the cap holds the death path, not the start-up.
inline bool deathMode() noexcept {static const bool d=::access(RECOMP_APP_PATH("/private-trace/diag-script-trace-death"),F_OK)==0;return d;}
// v1139: files/private-trace/diag-script-trace-hold traces freemode from the first Select hold (control 0 disabled-pressed, the v1138 probe)
inline bool holdMode() noexcept {static const bool h=::access(RECOMP_APP_PATH("/private-trace/diag-script-trace-hold"),F_OK)==0;return h;}
inline std::atomic<bool> armed{false};
inline std::uint32_t target() noexcept {
    static const std::uint32_t t=[]{std::uint32_t v=0;
        if(holdMode())return 0xC875557Du;   // v1139
        if(FILE* f=std::fopen(RECOMP_APP_PATH("/private-trace/diag-script-trace-death"),"r")){char b[16]{};
            const std::size_t n=std::fread(b,1,sizeof b-1,f);std::fclose(f);b[n]=0;v=std::uint32_t(std::strtoul(b,nullptr,16));if(!v)v=0xC875557Du;return v;}
        if(FILE* f=std::fopen(RECOMP_APP_PATH("/private-trace/diag-script-trace"),"r")){char b[16]{};
        const std::size_t n=std::fread(b,1,sizeof b-1,f);std::fclose(f);b[n]=0;v=std::uint32_t(std::strtoul(b,nullptr,16));if(!v)v=0xA3FB8A5Eu;}return v;}();
    return t;
}
inline std::uint32_t seen[SeenMax];inline unsigned nSeen=0;inline std::atomic<unsigned> lines{0};inline std::atomic_flag busy=ATOMIC_FLAG_INIT;
inline void note(std::uint32_t hash,PPCContext& c,std::uint8_t* base) noexcept {
    const std::uint32_t want=target();if(!want)return;
    if((deathMode() || holdMode()) && !armed.load(std::memory_order_relaxed))return;   // v1128 / v1139
    const std::uint32_t t=c.r23.u32;if(t<0x40000000u)return;
    if(trainerBe(base,t+4u)!=want)return;
    const std::uint32_t ip=trainerBe(base,t+12u),cxt=c.r1.u32+128u,rp=trainerBe(base,cxt),ret=(rp>=0x10000u && !(rp&3u))?trainerBe(base,rp):0xFFFFFFFFu;
    std::uint32_t k=2166136261u;for(std::uint32_t v:{ip,hash})k=(k^v)*16777619u;   // v1120: (ip, native) - the first result is logged
    while(busy.test_and_set(std::memory_order_acquire)){}
    bool fresh=true;for(unsigned i=0;i<nSeen;++i)if(seen[i]==k){fresh=false;break;}
    if(fresh && nSeen<SeenMax)seen[nSeen++]=k;
    busy.clear(std::memory_order_release);
    if(!fresh)return;
    if(lines.fetch_add(1,std::memory_order_relaxed)>=ScriptTraceMax)return;
    char l[120];std::snprintf(l,sizeof l,"RENDER_SCRIPT_TRACE %08x ip=%x native=%08x -> %d frame=%u",want,ip,hash,int(ret),trainerBe(base,FrameCounter));trainerLog(l);
}
}
// v1180/v1182 (diagnostic, READ-ONLY, files/private-trace/diag-photoq): the Snapmatic stuck spinner (t-onlcam v1170: GET_STATUS_OF_SORTED_LIST_OPERATION
// stays 1). Chain (recompiled code): the type-1 request on the save-operation queue 0x839CDB70 (16 x 156 B; head +2500, count +2504) ->
// sub_8294F6A0 starts the photo op 0x837C72CC (status +4) via sub_8294B3D0 onto the worker queue 0x839C9D50 (16 pointers; head +68, count +72),
// state word 0x839CED18; the worker sub_82244150 runs the head's vtable +4. Logged once a second (game frames; 240 lines): both queues, the
// worker queue's head object + its vtable + status, the photo op status, the state word.
namespace photoq {
inline bool on() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-photoq"),F_OK)==0;return o;}
inline void tick(std::uint8_t* base) noexcept {
    static std::uint32_t last=0;static unsigned lines=0;if(lines>=240)return;
    const std::uint32_t frame=trainerBe(base,FrameCounter);if(frame-last<30)return;last=frame;++lines;
    constexpr std::uint32_t SaveQ=0x839CDB70u,WorkQ=0x839C9D50u,PhotoOp=0x837C72CCu,State=0x839CED18u;
    const std::uint32_t sHead=trainerBe(base,SaveQ+2500),sCount=trainerBe(base,SaveQ+2504);
    const std::uint32_t sIdx=(sHead+1)&15u,sType=trainerBe(base,SaveQ+sIdx*156u);
    const std::uint32_t wHead=trainerBe(base,WorkQ+68),wCount=trainerBe(base,WorkQ+72);
    const std::uint32_t wIdx=(wHead+1)&15u,wObj=trainerBe(base,WorkQ+wIdx*4u);
    const bool ok=wObj>=0x40000000u && wObj<0xF0000000u && !(wObj&3u);
    // v1182: + the photo op's substate (+8: 0 start, 1 waiting on the manager, 2 done, 3 failed) and the photo manager (*0x839C9F58): +5400 busy
    // (1) / done (2), +5392 the rlCloud netStatus (1 pending, 2 failed, 3 succeeded), +5396 its code, +5388 / +5376 the counts, +5404 the flag
    const std::uint32_t mgr=trainerBe(base,0x839C9F58u);const bool mok=mgr>=0x40000000u && mgr<0xF0000000u && !(mgr&3u);
    char l[300];std::snprintf(l,sizeof l,"RENDER_PHOTOQ frame=%u saveq count=%u head=%u type=%u workq count=%u head=%u obj=%08x vt=%08x st=%d photo st=%d sub=%d state=%u mgr=%08x busy=%d net=%d code=%d n=%d/%d flag=%u",
        frame,sCount,sHead,sType,wCount,wHead,wObj,ok?trainerBe(base,wObj):0u,ok?int(trainerBe(base,wObj+4)):-9,int(trainerBe(base,PhotoOp+4)),int(trainerBe(base,PhotoOp+8)),trainerBe(base,State),
        mgr,mok?int(trainerBe(base,mgr+5400u)):-9,mok?int(trainerBe(base,mgr+5392u)):-9,mok?int(trainerBe(base,mgr+5396u)):-9,mok?int(trainerBe(base,mgr+5388u)):-9,mok?int(trainerBe(base,mgr+5376u)):-9,
        mok?unsigned(base[mgr+5404u]):9u);
    trainerLog(l);
}
}
// v1565 (diagnostic, default off; ammo_restore_diag.h): one script call of a probed weapon native, after it returned. The VM's call context
// at r1+128 (+0 result pointer, +4 argument count, +8 argument pointer), the script / IP at r23 +4 / +12, as script_trace. Read only.
namespace ammo_diag_glue {
inline void call(std::uint32_t hash,PPCContext& c,std::uint8_t* base) noexcept {
    const std::uint32_t t=c.r23.u32;if(t<0x40000000u)return;
    const std::uint32_t cxt=c.r1.u32+128u,rp=trainerBe(base,cxt),argc=trainerBe(base,cxt+4u),ap=trainerBe(base,cxt+8u);
    if(ap<0x10000u || (ap&3u))return;
    std::uint32_t a[5]{};for(unsigned k=0;k<5u && k<argc;++k)a[k]=trainerBe(base,ap+4u*k);
    const std::uint32_t r=(rp>=0x10000u && !(rp&3u))?trainerBe(base,rp):0xFFFFFFFFu;
    char l[300];
    if(recomp::ammo_diag::weaponCall(hash,trainerBe(base,t+4u),trainerBe(base,t+12u),a,r,trainerBe(base,FrameCounter),l,sizeof l))trainerLog(l);
}
}
extern "C" [[gnu::noinline]] void recomp_trainer_after_native(std::uint32_t hash,std::uint64_t callerLr,PPCContext& c,std::uint8_t* base) noexcept {
    recomp::periodic_save::noteNative(hash);   // v1562: a stat write marks the MP stats dirty (one compare loop, probed natives only)
    if(callerLr==VmNativeReturn && recomp::ammo_diag::probedHash(hash) && recomp::ammo_diag::on())ammo_diag_glue::call(hash,c,base);   // v1565 (diagnostic)
    if(photoq::on())photoq::tick(base);   // v1180/v1182 (diagnostic, file-gated)
    if(callerLr==VmNativeReturn && script_trace::target())script_trace::note(hash,c,base);   // v1119 (diagnostic, file-gated)
    if(hash!=PlayerPedIdHash || callerLr!=VmNativeReturn || trainerInside)return;
    auto& t=trainer();auto& ap=autopilot();
    autopilotPoll(); // v842
    const std::uint32_t r1=c.r1.u32;
    if(r1<0x10000u+GuestNatives::Frame || (r1&3u))return;
    const std::uint32_t frame=trainerBe(base,FrameCounter);
    // v852: the trainer / autopilot tick keeps its own once-per-frame rule exactly as before (claimed only while one of them wants a
    // tick); the call poll has its own (every game frame, whatever the trainer does).
    const bool sandboxOn=recomp::sandbox::active(); // (Update 1 P4: the play-mode latch; never changes in a process)
    const bool tickFrame=(sandboxOn || t.wanted() || ap.wanted() || touchX().wanted() || touchX().reticleWanted() || touchReadWanted.load(std::memory_order_relaxed)) && trainerLastFrame.exchange(frame,std::memory_order_relaxed)!=frame;   // v988: + a pending touch turn / aim
    const bool phoneFrame=phoneCallLastFrame.exchange(frame,std::memory_order_relaxed)!=frame;
    const bool storyFrame=recomp::trainer::storyReadDue(frame,storyReadLastFrame);   // v1088: independent of tickFrame (idle Story)
    // v989: the disc creator route has its own once-per-frame claim, taken only on a PLAYER_PED_ID call that maintransition's own
    // thread does not host (t-creator v988: 301 hosted defers at Pin, then hosted-timeout). The hosting thread's id is read for any
    // aligned pointer in the mapped range (v988 and before: only below 0xA0000000, else "unknown" = hosted for every id; script
    // threads are heap objects like the global blocks at 0xAC..). After DiscCreatorHostedWaitFrames frames with only hosted calls
    // the route ticks anyway (it then defers by itself, as before).
    bool creatorFrame=false;DiscCreatorGlue* creatorGlue=nullptr;
    if(sandboxOn && (creatorGlue=discCreatorGlue())!=nullptr && creatorGlue->route.active() && discCreatorLastFrame.load(std::memory_order_relaxed)!=frame) {
        const std::uint32_t cur=trainerBe(base,recomp::sandboxdiag::CurrentThread);
        const std::uint32_t host=!cur?0u:(cur>=0x40000000u && cur<0xF0000000u && !(cur&3u))?trainerBe(base,cur+4):DiscCreatorHostUnknown;
        const std::uint32_t routeThread=creatorGlue->route.threadId();
        const bool byRoute=host==DiscCreatorHostUnknown || (routeThread && host==routeThread);
        static std::uint32_t lastTick=0,loggedFor=0;
        if(routeThread && loggedFor!=routeThread){loggedFor=routeThread;
            char l[128];std::snprintf(l,sizeof l,"RENDER_ONLINE creator host-check route_thread=%u host=%u cur=%08x frame=%u",routeThread,host,cur,frame);trainerLog(l);}
        const bool waited=frame-lastTick>=DiscCreatorHostedWaitFrames;
        if(!byRoute || waited) {
            creatorFrame=discCreatorLastFrame.exchange(frame,std::memory_order_relaxed)!=frame;
            if(creatorFrame){lastTick=frame;discCreatorHostThread=host;
                static unsigned waits=0;
                if(byRoute && ++waits<=5){char l[112];std::snprintf(l,sizeof l,"RENDER_ONLINE creator host-wait hosted=1 host=%u frame=%u",host,frame);trainerLog(l);}}
        }
    }
    if(!creatorGlue || !creatorFrame)creatorGlue=creatorFrame?creatorGlue:nullptr;
    if(!tickFrame && !phoneFrame && !creatorFrame && !storyFrame)return;
    if(phoneFrame && ::recomp::online_local_glue::enabled()) {   // v994 (lead): the script VM's thread once, beside the L0 hook's tid
        static bool vmTidLogged=false;
        if(!vmTidLogged){vmTidLogged=true;char l[80];std::snprintf(l,sizeof l,"RENDER_ONLINE_LOCAL script-vm tid=%d frame=%u",recompTidNow(),frame);trainerLog(l);}
    }
    if(phoneFrame){phoneCallSwitch(frame);trainerProbeSwitch(frame);trainerSpawnSwitch(frame);trainerSwitchCharacterSwitch(frame);touchSettingsOnce(frame);touchDiagSwitch(frame);imenuStyleOnce(frame);} // (v953: the probe switch; P7: E5's)
    trainerInside=true;
    const PPCContext saved=c;   // the VM continues with exactly the registers it had
#if defined(__aarch64__)
    std::uint64_t fpcr;asm volatile("mrs %0, fpcr":"=r"(fpcr));
#endif
    {
        // v959: the face camera never outlives Sandbox mode. The play-mode latch never changes in a process (sandbox_runtime.h), so this
        // is a guard: it costs one bool load per frame and calls nothing unless the camera is on with the latch no longer Sandbox.
        if(onlineReady && !sandboxOn && onlineActions().camera().on()){GuestNatives own(c,base,(r1-GuestNatives::Frame)&~0xFu);onlineActions().endCamera(own,"sandbox-off");}
        if(onlineReady && !sandboxOn && onlineActions().placeFadeActive()){GuestNatives own(c,base,(r1-GuestNatives::Frame)&~0xFu);onlineActions().endPlaceFade(own,"sandbox-off");}   // v976: never leave the screen faded
        if(onlineReady && !sandboxOn && onlineActions().menu().isOpen()){GuestNatives own(c,base,(r1-GuestNatives::Frame)&~0xFu);onlineActions().endMenu(own,"sandbox-off");}   // PROPOSED 17 4 (same guard)
        // lane property-doors: likewise the icons never outlive Sandbox mode (one bool load per frame otherwise)
        if(propDoorsUsed && !sandboxOn){GuestNatives own(c,base,(r1-GuestNatives::Frame)&~0xFu);propDoors().clear(own,"sandbox-off");propDoorsUsed=false;recomp::propdoors::chooserOpen.store(false,std::memory_order_release);}
        if(tickFrame && sandboxOn) { // P4: first, so a weapons request it queues is served by this frame's trainer tick
            if(!t.enabled())t.setEnabled(true);   // the trainer is forced on in Sandbox (in memory)
            static bool coreReady=false;          // v955: the Sandbox core's side of P8, once: the launcher's sandbox_flags, then the
            if(!coreReady) {                      // autosave hold on bit 20 (sandbox_mode.h 4.) is in place from this tick on
                coreReady=true;
                const unsigned flags=recomp::bootstrap::play_mode::sandboxFlags.load(std::memory_order_acquire);
                recomp_online_set_flags(flags);recomp_online_set_autosave_hold(1);
                char l[96];std::snprintf(l,sizeof l,"RENDER_SANDBOX online-ready sandbox_flags=%u autosave_hold=1",flags);recomp::sandbox::log(l);
            }
            onlineFirstSandboxTick();             // P8: the online actions and the world switch (Sandbox only)
            GuestNatives own(c,base,(r1-GuestNatives::Frame)&~0xFu);
            if(recomp::ammo_diag::on())recomp::ammo_diag::sampler().tick(own,frame,onlineActions().character().active(),trainerLog);   // v1565 (diagnostic, read only)
            // v957 (WAVE2-REPORT 2.1 item 4): the save hold is on while a model change loads / applies, not only from the frame after it
            sandboxTick().tick(own,frame,{onlineActions().character().active() || onlineActions().character().busy(),(recomp::phonecall::state()&recomp::phonecall::Up)!=0,onlineActions().menu().isOpen()}); // v955: bit 20 + phone; v975 (G1): the creator menu
            onlineTick(own);onlineSwitch(own,frame); // P8: after the trainer is on, so a waiting one-shot request is not refused
            GuestStarter st(c,base,(r1-GuestNatives::Frame)&~0xFu);
            GuestNatives act(c,base,(r1-GuestNatives::Frame)&~0xFu);
            activities().tick(act,st,frame);     // OW L2 1c: idle = no native call; a pending start loads / starts; a running one polls
            { static recomp::session_env::Logger env(trainerLog);static const bool carNet=::access(RECOMP_APP_PATH("/private-trace/diag-car-net"),F_OK)==0;
              env.setCarNet(carNet);env.setOnline(::recomp::bootstrap::play_mode::online());env.tick(act,frame,::recomp::online_local_glue::l1Enabled(),activities().launcher().running()==recomp::activities::InternalFmIntro); }
            { // v1076 TEST-ONLY (bugfix-stability; force_clock.h): files/private-trace/diag-force-clock "HH MM" holds the online clock there
              // (NETWORK_OVERRIDE_CLOCK_TIME every 30 frames), so the lamp-cone A/B sees the lamps lit at the fm_intro shots. Absent: one fopen
              // per 300 frames, nothing else.
              static ::recomp::force_clock::Holder forced;
              const bool apply=forced.tick(frame,[] {
                  static char text[32];text[0]=0;
                  if(FILE* f=std::fopen(RECOMP_APP_PATH("/private-trace/diag-force-clock"),"r")){const std::size_t n=std::fread(text,1,sizeof text-1,f);text[n]=0;std::fclose(f);}
                  return (const char*)text;});
              if(apply) {
                  const auto t=forced.time();const std::uint32_t a[3]{std::uint32_t(t.h),std::uint32_t(t.m),0u};std::uint32_t out[3]{};
                  const bool ok=act.call(::recomp::session_env::n::OverrideClock,a,3,out);
                  if(forced.applied()==1 || (forced.applied()&(forced.applied()-1))==0){char d[96];std::snprintf(d,sizeof d,
                      "RENDER_FORCE_CLOCK applied=%u time=%02d:%02d ok=%d frame=%u",forced.applied(),t.h,t.m,ok?1:0,frame);trainerLog(d);}
              }
            }   // v1053: fm_intro owns the clock   // v1018 (read only); v1034 diag-car-net
            static recomp::car_keep::Keeper keep(trainerLog);keep.tick(act,frame);   // v1036: local session only; idle = one atomic load
            { static recomp::periodic_save::Saver periodicSaver(trainerLog);   // v1562: Online mode only; real work every 30 frames
              periodicSaver.tick(act,frame,::recomp::bootstrap::play_mode::online(),[base]() noexcept -> std::uint32_t {   // v1564: the stats clock
                  const std::uint32_t co=trainerBe(base,recomp::periodic_save::ClockPtr);   // *(0x82000A38): a normal word
                  return co>=0x10000u?periodicClockWord(base,co+16u):0u;}); }   // +16: a special range (emulated)
            {   // v1351 (sandbox_respawn.h): Sandbox + our local session -> online_respawn's answer (its gates: network game, no freemode, dead)
                static const bool sbRespawnOn=::access(RECOMP_APP_PATH("/private-trace/diag-sandbox-respawn-off"),F_OK)!=0;   // v1155: default ON
                if(::recomp::sandbox_respawn::wanted(::recomp::bootstrap::play_mode::online(),::recomp::local_session::active(),sbRespawnOn)) {
                    static recomp::online_respawn::Respawn sbRespawn(trainerLog);sbRespawn.tick(act,frame,false);}
            }
            if(::recomp::bootstrap::play_mode::online()) {   // v1050: the Online mode controller (Story / Sandbox: this one branch)
                static recomp::online_mode::Controller onlineCtl(trainerLog);std::uint32_t gipOut[3]{};
                { static recomp::online_respawn::Respawn respawn(trainerLog);static const bool respawnOff=::access(RECOMP_APP_PATH("/private-trace/diag-online-respawn-off"),F_OK)==0;
                  respawn.tick(act,frame,respawnOff); }   // v1123: the GTA Online death answer when freemode does not run
                if(script_trace::deathMode() && !script_trace::armed.load(std::memory_order_relaxed) && frame%15u==0u) {   // v1128 (diagnostic)
                    namespace rn=recomp::online_respawn::n;std::uint32_t o[3]{};
                    if(act.call(rn::Gip,nullptr,0,o) && o[0]==1u && act.call(rn::PlayerId,nullptr,0,o)) {
                        const std::uint32_t pid=o[0];std::uint32_t d[3]{};
                        if(act.call(rn::PlayerDead,&pid,1,d) && d[0]==1u){script_trace::armed.store(true,std::memory_order_relaxed);
                            char l[120];std::snprintf(l,sizeof l,"RENDER_SCRIPT_TRACE armed at the player's death frame=%u target=%08x",frame,script_trace::target());trainerLog(l);}
                    }
                }
                const bool gip=act.call(recomp::trainer::joaat("NETWORK_IS_GAME_IN_PROGRESS"),nullptr,0,gipOut) && gipOut[0]==1u;
                const std::uint64_t ms=std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                static recomp::creator::FileStore lookStore(onlineCreatorRoot());   // v1056: the saved character.json (sex + look), read only
                ::recomp::online_mode::guestBase=base;   // v1082: CloudsDown writes G1332498 as maintransition does
                recomp::online_mode::GamePedOps pedOps(act,&lookStore,   // v1058: driven here, not through the trainer's gate (maintransition's switch)
                    [](recomp::trainer::Natives& nat,int a,int a0,bool start,std::uint32_t player,std::uint32_t ped,std::uint32_t fr,char* d,int cap){
                        return start?onlineActions().begin(nat,a,a0,player,ped,fr,d,cap):onlineActions().step(nat,player,ped,fr,d,cap);},frame,
                    [](recomp::trainer::Natives& nat,char* d,int cap){   // v1066: the first-visit creator, started directly (not the trainer's gate)
                        DiscCreatorGlue* dc=discCreatorGlue();
                        if(!dc){if(d && cap>0)std::snprintf(d,std::size_t(cap),"no-route");return 2;}
                        return dc->request(nat,recomp::creatorroute::StartOp,d,cap);},
                    [](){DiscCreatorGlue* dc=discCreatorGlue();if(!dc)return 0;using recomp::creatorroute::St;const St s=dc->route.state();
                        return s==St::Idle?0:s==St::Creator?2:s==St::Done?3:s==St::Failed?4:1;},
                    [](recomp::trainer::Natives& nat,std::uint32_t ped,char* d,int cap){   // v1105 (FREEMODE-GATE-6 B3): the game's freemode player adopted
                        return onlineActions().adoptGameCharacter(nat,ped,d,cap,trainerLog);});
                static recomp::creator::FileStore introStore(onlineCreatorRoot(),"arrival.json");pedOps.setArrivalStore(&introStore);   // v1105 (FREEMODE-GATE-6 C4): read only
                onlineCtl.tick(frame,ms,true,gip,keep.busy(),pedOps);   // v1056: PedSwap before any reveal
                ::recomp::gpu::lower_backdrop::creatorScreen.store(onlineCtl.creatorRunning() || ::recomp::gpu::lower_backdrop::chooserUi(pedOps.uiState()),std::memory_order_relaxed);   // v1109: + the MP character select
                ::recomp::gpu::lower_backdrop::onlineMode.store(true,std::memory_order_relaxed);   // v1106: the lower spinner follows the game's   // v1103: the creator's backdrop on the lower screen
                // v1071 (the MainTransition race: v1063 launched it at 21:53, not at 23:11; same library and launch arguments): selector reaches
                // its go-online check (0x42d4, behind the boot latch static 158) only while G13121 (G13109 + 12) == 0 and G14 == 0 (0x41ce..0x41e7,
                // else it jumps to 0x42fb); the boot card handler's first call (where v1055-v1070 set the byte) came 16-17 s after selector
                // started, sometimes after that window closed. Now the byte is set from the controller's first ticks (once decided, not on a first
                // visit), before selector starts, and re-asserted for 60 s if anything clears it; G14 / G13121 are logged with it.
                if(::recomp::online_mode::goOnlineDecided.load(std::memory_order_acquire) && !::recomp::online_mode::skipGoOnline.load(std::memory_order_acquire)) {
                    static std::uint32_t firstFrame=frame;static unsigned sets=0;
                    if(frame-firstFrame<1800u && !::recomp::online_mode::choiceFrozen.load(std::memory_order_acquire)) {   // v1075: not after the freeze
                        const std::uint32_t w=act.load(0x83B13AECu);
                        if((w>>24)==0u) {
                            trainerPut(base,0x83B13AECu,(w&0x00FFFFFFu)|0x01000000u);++sets;
                            if(sets<=3 || sets%30==0) {
                                auto glob=[&](std::uint32_t gl)->int{const std::uint32_t b=gl>>18,wd=gl&0x3FFFFu,bs=act.load(0x83C5CE48u+4u*b),sz=act.load(0x83C5CF50u+4u*b);
                                    return (bs && !(bs&3u) && wd<sz)?int(act.load(bs+4u*wd)):-1;};
                                char l[180];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE go-online byte set early #%u frame=%u (selector window: g14=%d g13121=%d)",sets,frame-firstFrame,glob(14u),glob(13121u));
                                trainerLog(l);
                            }
                        }
                    }
                }
                // v1125: the route's first visit, after the creator saved the character (online_mode.h goOnlineAfterCreator): the later visit's byte
                // write above, until selector has started maintransition (G1351512 changed, selector 0x4551) or maintransition's own words move
                // (G1344033 / G1344034 non-zero); at most AfterCreatorFrames, then one line and nothing more (l1's 90 s own host stays the net).
                if(::recomp::online_mode::goOnlineAfterCreator.load(std::memory_order_acquire)) {
                    constexpr std::uint32_t AfterCreatorFrames=1800u;
                    static std::uint32_t acFirst=0;static int acBase=-2;static unsigned acSets=0;static bool acDone=false;
                    if(!acDone) {
                        auto glob=[&](std::uint32_t gl)->int{const std::uint32_t b=gl>>18,wd=gl&0x3FFFFu,bs=act.load(0x83C5CE48u+4u*b),sz=act.load(0x83C5CF50u+4u*b);
                            return (bs && !(bs&3u) && wd<sz)?int(act.load(bs+4u*wd)):-1;};
                        const int id=glob(1351512u),ui=glob(1344033u),mt=glob(1344034u);
                        if(!acFirst){acFirst=frame?frame:1u;acBase=id;}
                        if((acBase>=0 && id>=0 && id!=acBase) || ui>0 || mt>0) {
                            acDone=true;char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE after-creator: selector started maintransition (g1351512 %d -> %d, ui=%d mt=%d) after %u frames, %u byte sets",
                                acBase,id,ui,mt,frame-acFirst,acSets);trainerLog(l);
                        } else if(frame-acFirst>=AfterCreatorFrames) {
                            acDone=true;char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE after-creator: no maintransition in %u frames (g1351512=%d ui=%d mt=%d g14=%d g13121=%d); byte left as is",
                                AfterCreatorFrames,id,ui,mt,glob(14u),glob(13121u));trainerLog(l);
                        } else {
                            const std::uint32_t w=act.load(0x83B13AECu);
                            if((w>>24)==0u) {
                                trainerPut(base,0x83B13AECu,(w&0x00FFFFFFu)|0x01000000u);++acSets;
                                if(acSets<=3 || acSets%30==0){char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE after-creator: go-online byte set #%u frame=%u (selector window: g14=%d g13121=%d g1351512=%d)",
                                    acSets,frame-acFirst,glob(14u),glob(13121u),id);trainerLog(l);}
                            }
                        }
                    }
                }
                // v1126 (read only; t-onldeathroute124: on the route freemode never resurrected the dead player in 50 s): freemode's respawn polls
                // NETWORK_QUERY_RESPAWN_RESULTS (freemode 0x120e6; handler sub_82DA9CC0 -> sub_82D98DB0): complete byte 0x8388C624 ? (ok byte
                // 0x8388C625 ? 1 : 0) : (active byte 0x8388C5DB ? 2 : 3); freemode waits on 2 and 3. The search starts in sub_8256F198, completes in
                // sub_825747D8 (run by sub_82243D50 while active, gated by byte 0x83785DCE). These five values, logged on change (at most 60 lines):
                // never active = the start failed / never asked; active and never complete = the update never finishes.
                {
                    static std::uint64_t lastKey=~0ull;static unsigned lines=0;
                    auto byteAt=[&](std::uint32_t a)->unsigned{return (act.load(a&~3u)>>(8u*(3u-(a&3u))))&0xFFu;};
                    const unsigned active=byteAt(0x8388C5DBu),complete=byteAt(0x8388C624u),okb=byteAt(0x8388C625u),gate=byteAt(0x83785DCEu);
                    const std::uint32_t res=act.load(0x8388C4BCu);
                    const std::uint64_t key=(std::uint64_t(active)<<56)|(std::uint64_t(complete)<<48)|(std::uint64_t(okb)<<40)|(std::uint64_t(gate)<<32)|res;
                    if(key!=lastKey && lines<60u){lastKey=key;++lines;char l[200];
                        std::snprintf(l,sizeof l,"RENDER_ONLINE_RESPAWN_SEARCH frame=%u active=%u complete=%u ok=%u result=%u gate5dce=%u (query would answer %d)",
                            frame,active,complete,okb,unsigned(res),gate,complete?(okb?1:0):(active?2:3));trainerLog(l);}
                }
                // v1138 (read only; t-onlrace-post-20261109-1000: a 1.2 s Select hold after the race opened no Interaction Menu): freemode's opener
                // (fm 0xcb2e5-0xcb375: IS_DISABLED_CONTROL_PRESSED(0, 0) 350 ms, words f4 == 0 && f5 == -1 && f6 == -1 && f12 == 0, gate 0xcb3ed -> f6 = 0)
                // and am_pi_menu (opens on f6 >= 0): the words G1339003 + 4/5/6/10/12 and control 0 (pressed / disabled-pressed), every 3 frames,
                // logged on change, at most 80 lines. Online mode on the route only.
                if(::recomp::fm_route::routeOn() && frame%3u==0u) {
                    static int last[7]{-9,-9,-9,-9,-9,-9,-9};static unsigned lines=0;
                    auto glob=[&](std::uint32_t gl)->int{const std::uint32_t b=gl>>18,wd=gl&0x3FFFFu,bs=act.load(0x83C5CE48u+4u*b),sz=act.load(0x83C5CF50u+4u*b);
                        return (bs && !(bs&3u) && wd<sz)?int(act.load(bs+4u*wd)):-1;};
                    const std::uint32_t pc[2]{0u,0u};std::uint32_t o1[3]{},o2[3]{};
                    const int dcp=act.call(recomp::activities::n::DisabledControlPressed,pc,2,o1)?int(o1[0]&0xFFu):-1;
                    const int cp=act.call(recomp::activities::n::ControlPressed,pc,2,o2)?int(o2[0]&0xFFu):-1;
                    const int v[7]{glob(1339007u),glob(1339008u),glob(1339009u),glob(1339013u),glob(1339015u),dcp,cp};
                    if(dcp==1 && script_trace::holdMode() && !script_trace::armed.load(std::memory_order_relaxed)){script_trace::armed.store(true,std::memory_order_relaxed);
                        char l[120];std::snprintf(l,sizeof l,"RENDER_SCRIPT_TRACE armed at the first Select hold frame=%u target=%08x",frame,script_trace::target());trainerLog(l);}   // v1139
                    if(std::memcmp(v,last,sizeof v)!=0 && lines<80u){std::memcpy(last,v,sizeof v);++lines;char l[200];
                        std::snprintf(l,sizeof l,"RENDER_ONLINE_PIM frame=%u f4=%d f5=%d f6=%d f10=%d f12=%d control0 disabled_pressed=%d pressed=%d",frame,v[0],v[1],v[2],v[3],v[4],v[5],v[6]);trainerLog(l);}
                }
                // v1141 (read only; owner bug on v1137: in the clothes shop and at Ammu-Nation the browse press moves the camera but no shop menu draws):
                // the shop menus' draw (clothes_shop_mp 0x329df) leaves unless the menu library's check 0x35950 passes: G1339892 != 0 passes at once;
                // else it refuses on !IS_SCREEN_FADED_IN, (G1355845 bit 8 && G1344034 != 65), IS_PAUSE_MENU_ACTIVE, IS_PLAYER_SWITCH_IN_PROGRESS,
                // IS_COMMERCE_STORE_OPEN (handler sub_82DAA440 = byte 0x83895DF3, set by sub_825F9C08, cleared by sub_825EEE40), G60102, G19453
                // (menu G13241 f6212) or IS_WARNING_MESSAGE_ACTIVE. The per-frame routine 0x31bbd also reads IS_SYSTEM_UI_BEING_DISPLAYED. Every 10 frames
                // in Online mode, logged on change with the derived verdict, at most 120 lines.
                if(::recomp::bootstrap::play_mode::online() && frame%10u==0u) {
                    static int last[12]{-9,-9,-9,-9,-9,-9,-9,-9,-9,-9,-9,-9};static unsigned lines=0;
                    auto glob=[&](std::uint32_t gl)->int{const std::uint32_t b=gl>>18,wd=gl&0x3FFFFu,bs=act.load(0x83C5CE48u+4u*b),sz=act.load(0x83C5CF50u+4u*b);
                        return (bs && !(bs&3u) && wd<sz)?int(act.load(bs+4u*wd)):-1;};
                    auto nat=[&](const char* name)->int{std::uint32_t o[3]{};return act.call(recomp::trainer::joaat(name),nullptr,0,o)?int(o[0]&0xFFu):-1;};
                    const unsigned storeByte=(act.load(0x83895DF3u&~3u)>>(8u*(3u-(0x83895DF3u&3u))))&0xFFu;
                    const int phone=glob(1355845u);
                    const int v[12]{glob(1339892u),nat("IS_SCREEN_FADED_IN"),phone<0?-1:((phone>>8)&1),glob(1344034u),nat("IS_PAUSE_MENU_ACTIVE"),
                        nat("IS_PLAYER_SWITCH_IN_PROGRESS"),nat("IS_COMMERCE_STORE_OPEN"),int(storeByte),glob(60102u),glob(19453u),nat("IS_WARNING_MESSAGE_ACTIVE"),
                        nat("IS_SYSTEM_UI_BEING_DISPLAYED")};
                    if(std::memcmp(v,last,sizeof v)!=0 && lines<120u){std::memcpy(last,v,sizeof v);++lines;
                        const bool pass=v[0]!=0 || !(v[1]!=1 || (v[2]==1 && v[3]!=65) || v[4]==1 || v[5]==1 || v[6]==1 || v[8]!=0 || v[9]!=0 || v[10]==1);
                        char l[300];std::snprintf(l,sizeof l,"RENDER_ONLINE_SHOPMENU frame=%u verdict=%s override=%d faded_in=%d phone_bit8=%d g1344034=%d pause=%d switch=%d"
                            " store_open=%d store_byte=%d g60102=%d menu6212=%d warning=%d system_ui=%d",frame,pass?"draw":"REFUSE",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],
                            v[8],v[9],v[10],v[11]);trainerLog(l);}
                    // the menu library's 6 shared slots (G13241 f4470 = G17711: count, then the owners' joaat(script name + MP instance id)); a full table
                    // or a key that changes between the register (0x517a7) and the draw (0x329df) also leaves the draw. Logged on change with
                    // NETWORK_IS_GAME_IN_PROGRESS (part of the key), at most 60 lines.
                    static int lastSlots[8]{-9,-9,-9,-9,-9,-9,-9,-9};static unsigned slotLines=0;
                    const int s[8]{glob(17711u),glob(17712u),glob(17713u),glob(17714u),glob(17715u),glob(17716u),glob(17717u),nat("NETWORK_IS_GAME_IN_PROGRESS")};
                    if(std::memcmp(s,lastSlots,sizeof s)!=0 && slotLines<60u){std::memcpy(lastSlots,s,sizeof s);++slotLines;char l[220];
                        std::snprintf(l,sizeof l,"RENDER_ONLINE_SHOPMENU_SLOTS frame=%u count=%d slots=%08x %08x %08x %08x %08x %08x game_in_progress=%d",frame,s[0],
                            unsigned(s[1]),unsigned(s[2]),unsigned(s[3]),unsigned(s[4]),unsigned(s[5]),unsigned(s[6]),s[7]);trainerLog(l);}
                }
                // v1141 (read only; owner correction: at "Press D-pad right to browse" the press does NOTHING): the shops' prompts are context
                // intentions (G30491: 6 x 18 words, f0 active, f1 id, f2 priority, f3 triggered) granted by context_controller (joaat 0x75B7261C): it
                // takes the press only after IS_CONTROL_RELEASED(2, 49) then IS_CONTROL_JUST_PRESSED(2, 49), and only while its block check 0x68b is
                // clear: G59079, !IS_PLAYER_PLAYING, G10433.f1 > 3, IS_CUTSCENE_PLAYING, G30606, IS_HUD_COMPONENT_ACTIVE(19), G13109.f4 (&& f104 == 4),
                // IS_PLAYER_SWITCH_IN_PROGRESS, phone bit 8. Control (2, 49) every frame and the rest every 10 frames, logged on change, at most 150 lines.
                if(::recomp::bootstrap::play_mode::online()) {
                    static int lastCtl[3]{-9,-9,-9},lastCtx[16];static bool ctxInit=false;static unsigned ctxLines=0;
                    if(!ctxInit){for(int& x:lastCtx)x=-9;ctxInit=true;}
                    auto glob=[&](std::uint32_t gl)->int{const std::uint32_t b=gl>>18,wd=gl&0x3FFFFu,bs=act.load(0x83C5CE48u+4u*b),sz=act.load(0x83C5CF50u+4u*b);
                        return (bs && !(bs&3u) && wd<sz)?int(act.load(bs+4u*wd)):-1;};
                    auto ctl=[&](const char* name)->int{const std::uint32_t a2[2]{2u,49u};std::uint32_t o[3]{};
                        return act.call(recomp::trainer::joaat(name),a2,2,o)?int(o[0]&0xFFu):-1;};
                    const int c[3]{ctl("IS_CONTROL_PRESSED"),ctl("IS_CONTROL_RELEASED"),ctl("IS_DISABLED_CONTROL_PRESSED")};
                    if(std::memcmp(c,lastCtl,sizeof c)!=0 && ctxLines<150u){std::memcpy(lastCtl,c,sizeof c);++ctxLines;char l[160];
                        std::snprintf(l,sizeof l,"RENDER_ONLINE_CONTEXT control(2,49) frame=%u pressed=%d released=%d disabled_pressed=%d",frame,c[0],c[1],c[2]);trainerLog(l);}
                    if(frame%10u==0u) {
                        auto nat1=[&](const char* name,std::uint32_t a)->int{const std::uint32_t a1[1]{a};std::uint32_t o[3]{};
                            return act.call(recomp::trainer::joaat(name),a1,1,o)?int(o[0]):-1;};
                        std::uint32_t pio[3]{};const std::uint32_t pidx=act.call(recomp::trainer::joaat("GET_PLAYER_INDEX"),nullptr,0,pio)?pio[0]:0u;
                        std::uint32_t cso[3]{};const int cut=act.call(recomp::trainer::joaat("IS_CUTSCENE_PLAYING"),nullptr,0,cso)?int(cso[0]&0xFFu):-1;
                        const int phone=glob(1355845u);
                        int act6=0,trig=0;for(unsigned i=0;i<6u;++i){if(glob(30492u+18u*i)>0)act6|=1<<i;if(glob(30492u+18u*i+3u)>0)trig|=1<<i;}
                        const int x[16]{nat1("GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH",0x75B7261Cu),glob(30609u),act6,trig,glob(59079u),
                            nat1("IS_PLAYER_PLAYING",pidx)&0xFF,glob(10434u),cut,glob(30606u),nat1("IS_HUD_COMPONENT_ACTIVE",19u)&0xFF,glob(13113u),glob(13213u),
                            [&]{std::uint32_t o[3]{};return act.call(recomp::trainer::joaat("IS_PLAYER_SWITCH_IN_PROGRESS"),nullptr,0,o)?int(o[0]&0xFFu):-1;}(),phone<0?-1:((phone>>8)&1),glob(30600u),glob(30601u)};
                        if(std::memcmp(x,lastCtx,sizeof x)!=0 && ctxLines<150u){std::memcpy(lastCtx,x,sizeof x);++ctxLines;
                            const bool blocked=x[4]!=0 || x[5]!=1 || x[6]>3 || x[7]==1 || x[8]!=0 || x[9]==1 || x[10]!=0 || x[12]==1 || x[13]==1;
                            char l[360];std::snprintf(l,sizeof l,"RENDER_ONLINE_CONTEXT frame=%u controller_threads=%d shown=%d active_mask=%02x triggered_mask=%02x gate=%s"
                                " g59079=%d playing=%d g10433f1=%d cutscene=%d g30606=%d wheel=%d g13109f4=%d g13109f104=%d switch=%d phone_bit8=%d g30600=%d g30601=%d",
                                frame,x[0],x[1],unsigned(x[2]),unsigned(x[3]),blocked?"BLOCKED":"clear",x[4],x[5],x[6],x[7],x[8],x[9],x[10],x[11],x[12],x[13],x[14],x[15]);trainerLog(l);}
                    }
                }
                // v1144 (read only; t-onlshopstage143: clothes_shop_mp launched at the glasses stand but never reached its stand states): the shops' run
                // gate 0x59d93 (clothes_shop_mp; gunclub_shop carries the same globals) passes only when G80929.f15 and .f16 are set, in MP (G59842) the
                // player's bit in G1338500 (G1338499.f1) is set, and G80877 is 10..13 (0x51cc4; startup 0x352b4 sets 13). Every 10 frames in Online mode,
                // logged on change with the verdict, at most 60 lines.
                if(::recomp::bootstrap::play_mode::online() && frame%10u==0u) {
                    static int lastGate[6]{-9,-9,-9,-9,-9,-9};static unsigned gateLines=0;
                    auto glob=[&](std::uint32_t gl)->int{const std::uint32_t b=gl>>18,wd=gl&0x3FFFFu,bs=act.load(0x83C5CE48u+4u*b),sz=act.load(0x83C5CF50u+4u*b);
                        return (bs && !(bs&3u) && wd<sz)?int(act.load(bs+4u*wd)):-1;};
                    std::uint32_t po[3]{};const int pid=act.call(recomp::trainer::joaat("PLAYER_ID"),nullptr,0,po)?int(po[0]):-1;
                    const int g[6]{glob(80877u),glob(80944u),glob(80945u),glob(59842u),glob(1338500u),pid};
                    if(std::memcmp(g,lastGate,sizeof g)!=0 && gateLines<60u){std::memcpy(lastGate,g,sizeof g);++gateLines;
                        const bool bit=g[5]>=0 && g[5]<32 && ((unsigned(g[4])>>unsigned(g[5]))&1u);
                        const bool pass=g[1]!=0 && g[2]!=0 && (g[3]==0 || bit) && g[0]>=10 && g[0]<=13;
                        char l[240];std::snprintf(l,sizeof l,"RENDER_ONLINE_SHOPGATE frame=%u verdict=%s g80877=%d (needs 10..13) f15=%d f16=%d g59842=%d g1338500=%08x player=%d bit=%d",
                            frame,pass?"open":"CLOSED",g[0],g[1],g[2],g[3],unsigned(g[4]),g[5],bit?1:0);trainerLog(l);}
                }
                // v1145 (read only; t-onlshopgate144: the run gate is open, yet the clothes shop never leaves state 0's substate 1 = its open bit
                // G80929.f1015[shop] bit 0 is clear): shop_controller opens a shop (0x4048 -> "Opening shop") only when 0x4112 passes; in MP it closes
                // "on mission" when static 82 (0x54a3: G1573860[p].f8.f1 bit 0, or G1573860[p].f0 != -1) and static 83 (0x5457: G1332465, G1344092 or
                // stat enum 119 MP0_FM_INTRO_MISS_DONE) are both set. Every 10 frames in Online mode, on change, at most 60 lines: f0, f8.f1, G1332465,
                // G1344092, G19455 (valid MP mode == 0), and the open-bit mask of shops 0..43 (G80929.f1015[i] bit 0).
                if(::recomp::bootstrap::play_mode::online() && frame%10u==0u) {
                    static int lastOpen[8]{-9,-9,-9,-9,-9,-9,-9,-9};static unsigned openLines=0;
                    auto glob=[&](std::uint32_t gl)->int{const std::uint32_t b=gl>>18,wd=gl&0x3FFFFu,bs=act.load(0x83C5CE48u+4u*b),sz=act.load(0x83C5CF50u+4u*b);
                        return (bs && !(bs&3u) && wd<sz)?int(act.load(bs+4u*wd)):-1;};
                    std::uint32_t po[3]{};const int pid=act.call(recomp::trainer::joaat("PLAYER_ID"),nullptr,0,po)?int(po[0]):-1;
                    const std::uint32_t pe=(pid>=0 && pid<32)?1573860u+1u+257u*std::uint32_t(pid):0u;
                    std::uint32_t lo=0,hi=0;for(unsigned i=0;i<44u;++i){const int w=glob(80929u+1015u+1u+i);if(w>0 && (w&1)){if(i<32u)lo|=1u<<i;else hi|=1u<<(i-32u);}}
                    const int o[8]{pe?glob(pe):-2,pe?glob(pe+8u+1u):-2,glob(1332465u),glob(1344092u),glob(19455u),pid,int(lo),int(hi)};
                    if(std::memcmp(o,lastOpen,sizeof o)!=0 && openLines<60u){std::memcpy(lastOpen,o,sizeof o);++openLines;
                        const bool onMission=(o[0]!=-1 || (o[1]>0 && (o[1]&1)));
                        char l[280];std::snprintf(l,sizeof l,"RENDER_ONLINE_SHOPOPEN frame=%u player=%d mission_f0=%d f8f1=%08x on_mission=%d g1332465=%d g1344092=%d g19455=%d"
                            " open_mask=%08x,%03x (G80929.f1015[i] bit 0, shops 0..43)",frame,o[5],o[0],unsigned(o[1]),onMission?1:0,o[2],o[3],o[4],unsigned(o[6]),unsigned(o[7]));trainerLog(l);}
                }
                static recomp::online_mode::TransitionProbe transition;transition.tick(act,frame,trainerLog);   // v1064: maintransition's states, logged on change
                if(::recomp::fm_route::routeOn()){static recomp::corona_probe::Probe corona;corona.tick(act,frame,trainerLog);}   // v1117: fmmc's corona state (read only)
                static recomp::online_mode::GateProbe gates;gates.tick(act,frame,trainerLog);   // v1093: the freemode gate's globals (FREEMODE-GATE-2026-11-08.md)
                static recomp::online_mode::GateProbe2 gates2;gates2.tick(act,frame,trainerLog);   // v1096: the offline-UGC / T18 globals (FREEMODE-GATE-2-2026-11-08.md)
                static recomp::online_mode::GateProbe3 gates3;gates3.tick(act,frame,trainerLog);   // v1098: T17's lineup + the T18 inputs (FREEMODE-GATE-3 A1)
                static recomp::online_mode::GateProbe4 gates4;gates4.tick(act,frame,trainerLog);   // v1102: T21-T26 (FREEMODE-GATE-5 F)
                if(frame%15u==0u && (::recomp::online_mode::skipGoOnline.load(std::memory_order_acquire) || ::recomp::local_session::active())) {   // v1082: the player ped for the creator probe (char_probe_flag.h); natives only on the creator visit or in the session (v1067)
                    std::uint32_t o[3]{};const std::uint32_t pp=act.call(::recomp::online_mode::gn::PlayerPed,nullptr,0,o)?o[0]:0u;
                    std::uint32_t m[3]{};const std::uint32_t a1[1]{pp};const std::uint32_t mm=pp && act.call(::recomp::online_mode::gn::Model,a1,1,m)?m[0]:0u;
                    ::recomp::char_probe::playerPed.store(pp,std::memory_order_relaxed);::recomp::char_probe::playerModel.store(mm,std::memory_order_relaxed);
                }
            }
            { const bool onlineMode=::recomp::bootstrap::play_mode::online(),l1On=::recomp::online_local_glue::l1Enabled();
              static const bool arrivalSw=::access(RECOMP_APP_PATH("/private-trace/diag-online-arrival"),F_OK)==0;   // v1046
              static recomp::creator::FileStore arrivalStore(onlineCreatorRoot(),"arrival.json");
              static recomp::online_arrival::Arrival arrival(trainerLog,arrivalStore,[](recomp::trainer::Natives& nat){   // the action 19 a0 = 101 start
                  char d[96];return activities().request(nat,recomp::activities::FmIntroIndex,recomp::activities::Activities::Start,d,int(sizeof d))==1;});
              const bool pedReady=!onlineMode || ::recomp::online_mode::pedConfirmed.load(std::memory_order_acquire);   // v1056: never before the swap in Online mode
              {   // v1105 (FREEMODE-GATE-6 D): on the freemode route the game's own fm_intro / placement (read only while it matters)
                  using AS=recomp::online_arrival::Arrival::State;
                  const bool fmRouteNow=onlineMode && ::recomp::fm_route::routeOn() && ::recomp::mt_adopt::entered.load(std::memory_order_acquire);
                  bool gameIntro=false;
                  if(fmRouteNow && (arrival.state()==AS::Idle || arrival.state()==AS::WaitSession || arrival.state()==AS::GameIntro)){
                      std::uint32_t out[3]{};const std::uint32_t a1[1]{0xEFE7C380u};   // joaat("fm_intro")
                      gameIntro=act.call(recomp::trainer::joaat("GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH"),a1,1,out) && int(out[0])>0;}
                  arrival.tick(act,frame,(onlineMode || arrivalSw) && l1On,keep.busy(),pedReady,fmRouteNow,gameIntro);
              }
              ::recomp::online_mode::arrivalPhase.store(arrival.phase(),std::memory_order_release);   // v1080: the controller's release waits for it
              static recomp::online_curtain::Curtain curtain(trainerLog);   // v1043: armed = Online mode or diag-online-curtain, with the L1 path on
              static const bool curtainSw=::access(RECOMP_APP_PATH("/private-trace/diag-online-curtain"),F_OK)==0;
              {   // v1105 (FREEMODE-GATE-6 E): the route inputs (the release flags, freemode's fm_intro thread)
                  recomp::online_curtain::RouteIn rt;
                  rt.fmRoute=onlineMode && ::recomp::fm_route::routeOn() && ::recomp::mt_adopt::entered.load(std::memory_order_acquire);
                  rt.routeSwitch=onlineMode && ::recomp::fm_route::routeOn();   // v1166 PROPOSAL (online-spinner): no own Entry spinner on the route
                  if(rt.fmRoute){rt.releaseRequested=::recomp::online_mode::releaseRequested.load(std::memory_order_acquire);rt.released=::recomp::online_mode::released.load(std::memory_order_acquire);
                      std::uint32_t out[3]{};const std::uint32_t a1[1]{0xEFE7C380u};rt.gameIntro=act.call(recomp::trainer::joaat("GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH"),a1,1,out) && int(out[0])>0;
                      rt.nowMs=std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
                  curtain.tick(act,frame,(onlineMode || curtainSw) && l1On,onlineMode,keep.busy() || arrival.holding(),pedReady,rt); } }   // v1056: no reveal while a Story model   // v1046: + the arrival's hold
            { static recomp::shop_probe::Probe shops(trainerLog);static const bool on=::access(RECOMP_APP_PATH("/private-trace/diag-shop-probe"),F_OK)==0;
              shops.tick(act,frame,on); }   // v1039: read only, developer switch
            { static recomp::fmi_probe::Probe fmi(trainerLog);static const bool on=::access(RECOMP_APP_PATH("/private-trace/diag-fmi-probe"),F_OK)==0;
              fmi.tick(act,frame,on); }   // v1048: read only, developer switch
            { static recomp::propunlock::Probe pu(trainerLog);   // v1501: read only, Online mode; off with diag-propunlock-probe-off
              static const bool off=::access(RECOMP_APP_PATH("/private-trace/diag-propunlock-probe-off"),F_OK)==0;
              static const bool test=::access(RECOMP_APP_PATH("/private-trace/diag-unlock60-test"),F_OK)==0;   // (logged only; the answer is the STAT hook's)
              pu.tick(act,frame,::recomp::bootstrap::play_mode::online() && !off,test); }
            { static recomp::contactmissions::Probe cm(trainerLog);   // v1508: read only, Online mode; off with diag-contact-probe-off
              static const bool off=::access(RECOMP_APP_PATH("/private-trace/diag-contact-probe-off"),F_OK)==0;
              cm.tick(act,frame,::recomp::bootstrap::play_mode::online() && !off); }
            { static recomp::doorchain::Probe dc(trainerLog);   // v1510: read only, Online mode; off with diag-doorchain-probe-off
              static const bool off=::access(RECOMP_APP_PATH("/private-trace/diag-doorchain-probe-off"),F_OK)==0;
              dc.tick(act,frame,::recomp::bootstrap::play_mode::online() && !off); }
            imenuPoll(frame);                    // DIAGNOSTIC imenu (Sandbox only): never activated = no native call, no guest load
            { // v989: the Interaction Menu. Style fivepocket and nothing open / running: one Back load in the model, no native.
                auto& im=imenuModel();   // v1027: the replica yields the Select hold to the game's own menu while that runs (state bit 24)
                const bool styleOn=imenuStyleOn.load(std::memory_order_acquire) && !activities().realMenuAvailable();
                imenuGate().setEnabled(styleOn);im.setOpener(styleOn);
                const std::uint32_t ts=t.state();
                recomp::imenu::Context ictx{};ictx.sandbox=true;ictx.freemodeRunning=false;   // (offline: freemode never runs in the Sandbox)
                ictx.phoneUp=(recomp::phonecall::state()&recomp::phonecall::Up)!=0 || recomp::propdoors::chooserOpen.load(std::memory_order_acquire)
                    || onlineActions().menu().isOpen();   // (the doors chooser and the creator menu share the D-pad / A / B like the phone)
                ictx.onlineCharacter=(ts&recomp::trainer::StateOnlineCharacter)!=0;ictx.invincible=(ts&1u)!=0;
                ictx.gender=-1;
                if(ictx.onlineCharacter && (im.open() || imenuGate().rawDown())) {   // (the model only reads gender on open frames)
                    GuestNatives gm(c,base,(r1-GuestNatives::Frame)&~0xFu);std::uint32_t o[3]{};
                    const std::uint32_t ped=gm.call(recomp::trainer::n::PlayerPedId,nullptr,0,o)?o[0]:0u;
                    if(ped){const std::uint32_t a[1]{ped};if(gm.call(recomp::trainer::n::GetEntityModel,a,1,o))
                        ictx.gender=o[0]==recomp::online::MaleModel?0:o[0]==recomp::online::FemaleModel?1:-1;}
                }
                static recomp::imenu::TrainerRequests ireq(trainer());
                GuestNatives in(c,base,(r1-GuestNatives::Frame)&~0xFu);
                const bool drawNow=im.tick(in,ictx,ireq);
                imenuFrame().after(in,im,drawNow);
                imenuOpenNow.store(im.open(),std::memory_order_relaxed);
                imenuGate().setMenuClosed(!im.open() && !im.cooling());
                if(imenuDiagLines.load(std::memory_order_relaxed)) {   // device check only: Back decisions and the camera mode as they change
                    static unsigned taps=0,holds=0;static int cam=-2;
                    const unsigned tp=imenuGate().taps(),hd=imenuGate().holds();
                    if(tp!=taps || hd!=holds){char l[112];std::snprintf(l,sizeof l,"RENDER_IMENU back taps=%u holds=%u open=%d frame=%u",tp,hd,int(im.open()),frame);trainerLog(l);taps=tp;holds=hd;}
                    if(frame%5u==0){GuestNatives cm(c,base,(r1-GuestNatives::Frame)&~0xFu);std::uint32_t o[3]{};
                        if(cm.call(recomp::imenudiag::n::CamMode,nullptr,0,o) && int(o[0])!=cam){
                            char l[96];std::snprintf(l,sizeof l,"RENDER_IMENU cam mode=%d was=%d open=%d frame=%u",int(o[0]),cam,int(im.open()),frame);trainerLog(l);cam=int(o[0]);}}
                }
            }
            if(::recomp::online_local_glue::enabled()) {   // v985: the MP global blocks (mp_globals.h), once, before MP scripts read them
                static recomp::mpglobals::Setup mpg;
                if(mpg.state()!=recomp::mpglobals::Setup::St::Done && mpg.state()!=recomp::mpglobals::Setup::St::Failed) {
                    GuestNatives mn(c,base,(r1-GuestNatives::Frame)&~0xFu);GuestStarter ms(c,base,(r1-GuestNatives::Frame)&~0xFu);
                    (void)mpg.tick(mn,frame,&ms,[](void* ctx,std::uint32_t name,std::uint32_t stack)->std::uint32_t{
                        return static_cast<GuestStarter*>(ctx)->start(name,0,0,stack);},trainerLog);
                }
            }
            // (v989: the v981 ROS-local record write is dropped. sub_83061D70 returns 0 on every path and never reaches a session request,
            //  and NETWORK_SESSION_HOST stops earlier at sub_82543A90 (hard false): the record was not on any path. lead-0607 creds.md.)
            if(imenuDiag().active()){GuestNatives im(c,base,(r1-GuestNatives::Frame)&~0xFu);GuestWords gw(base);imenuDiag().tick(im,gw,frame,imenuNowUs);}
            // v1501 (online-doors REPORT.md 0.5): our door layer is Sandbox only. In Online mode the game's own am_mp_property_ext / appinternet
            // own the doors (an unowned character gets nothing at a door, as on the console): no icons, no chooser, no trip.
            if(onlineReady && !::recomp::bootstrap::play_mode::online()) { // lane property-doors: icons + doors once the online actions (action 16) are handed over; a look every 10 frames
                GuestNatives pd(c,base,(r1-GuestNatives::Frame)&~0xFu);
                propDoors().tick(pd,frame,(t.state()&recomp::trainer::StateGameplay)!=0 && !onlineActions().menu().isOpen());propDoorsUsed=true;   // v975 (G2): an open creator menu = a closed gate for the doors
                recomp::propdoors::chooserOpen.store(propDoors().choosing(),std::memory_order_release);   // v975 (G2)
            }
        }
        if(creatorFrame && creatorGlue) { // PROPOSED action 21 (v989: its own once-per-frame claim, see the hook's top)
            DiscCreatorGlue* dc=creatorGlue;
            dc->c=&c;dc->base=base;dc->r1=(r1-GuestNatives::Frame)&~0xFu;
            GuestNatives dn(c,base,(r1-GuestNatives::Frame)&~0xFu);
            dc->route.tick(dn,frame);
        }
        GuestNatives natives(c,base,(r1-GuestNatives::Frame)&~0xFu);
        if(tickFrame && t.wanted())t.tick(natives,frame);
        if(storyFrame) {   // v1086: state bits 27-28 (the story character from the game); v1088: its own cadence, not behind tickFrame
            std::uint32_t o[3]{};const std::uint32_t ped=natives.call(recomp::trainer::n::PlayerPedId,nullptr,0,o)?o[0]:0u;
            std::uint32_t m[3]{};const std::uint32_t a1[1]{ped};
            const bool got=ped && ped!=0xFFFFFFFFu && natives.call(recomp::trainer::n::GetEntityModel,a1,1,m);
            recomp::trainer::storyCharacter.store(got?recomp::trainer::storyCharacterOf(m[0]):0u,std::memory_order_relaxed);
        }
        std::uint32_t oldPed=0,newPed=0; // P7: a switch just happened (only ever true in Sandbox mode)
        if(t.takePedChange(oldPed,newPed)) {
            const bool fixed=trainerFixPedResult(base,r1,oldPed,newPed);
            char msg[112];std::snprintf(msg,sizeof msg,"RENDER_TRAINER switch ped=%08x old=%08x vm_result=%s",newPed,oldPed,fixed?"rewritten":"left");
            trainerLog(msg);
        }
        if(tickFrame && ap.wanted()){GuestNatives own(c,base,(r1-GuestNatives::Frame)&~0xFu);ap.tick(own,frame);} // v842 (its own scratch)
        if(tickFrame && touchX().wanted()) { // v988: a touch turn / aim change (unused: one relaxed load); the trainer's gate when it runs
            GuestNatives tn(c,base,(r1-GuestNatives::Frame)&~0xFu);
            {std::uint32_t o[3]{};(void)tn.call(InvalidateIdleCam,nullptr,0,o);}   // v989: a touch turn ends the idle camera (as stick input does)
            // v1158 (android-qol v1401, Fold run-v1401-11100013): StateGameplay is refreshed only by the trainer's own tick, which runs only while
            // t.wanted(); with the trainer on and idle the bit stayed 0 and every recomp_camera_turn_v1 was refused. Idle: touchX's own checks decide.
            touchX().tick(tn,!t.enabled() || !t.wanted() || (t.state()&recomp::trainer::StateGameplay)!=0);
            {static unsigned seenSettles=0;const unsigned ns=touchX().pitchSettles();   // v1175: a swipe's pitch result (the Fold check reads it), 64 lines
             if(ns!=seenSettles && ns<=64){seenSettles=ns;char l[128];std::snprintf(l,sizeof l,"RENDER_TOUCHX pitch settle target=%.1f get=%.1f diff=%.1f ref=%.1f n=%u",
                 double(touchX().settledTarget()),double(touchX().settledPitch()),double(touchX().settledPitch()-touchX().settledTarget()),double(touchX().pitchRefDiff()),ns);trainerLog(l);}}
        }
        if(tickFrame && touchX().reticleWanted()) { // red reticle (recomp_aim_assist_v1(-3), STATUS C4): the HUD reticle's words; natives only while shown
            GuestNatives rn(c,base,(r1-GuestNatives::Frame)&~0xFu);
            const bool gate=(!t.enabled() || !t.wanted() || (t.state()&recomp::trainer::StateGameplay)!=0) && (recomp::phonecall::state()&1u)==0u;   // + phone up
            static std::uint32_t reticleLines=0;
            if(touchX().reticleTick(rn,gate,touchx::reticle::nowMs()) && reticleLines<400u) {   // transitions only, capped per process
                ++reticleLines;char l[128];std::snprintf(l,sizeof l,"RENDER_TOUCHX reticle bits=%d style=%d flash=%d visible=%d gate=%d frame=%u",
                    touchX().reticleBits(),int(touchX().reticleStyle()),int(touchX().reticleFlash()),int(touchX().reticleVisible()),int(gate),frame);
                trainerLog(l);
            }
        }
        if(tickFrame && touchReadWanted.exchange(false,std::memory_order_acq_rel)) { // v988 diag "read": the values the device check compares
            GuestNatives tr(c,base,(r1-GuestNatives::Frame)&~0xFu);std::uint32_t o[3]{};float h=0.f,p=0.f;int aim=-1;
            if(tr.call(touchx::n::GetRelHeading,nullptr,0,o))h=touchx::fval(o[0]);
            if(tr.call(touchx::n::GetRelPitch,nullptr,0,o))p=touchx::fval(o[0]);
            if(tr.call(touchx::n::GetAimState,nullptr,0,o))aim=int(o[0]);
            char l[160];std::snprintf(l,sizeof l,"RENDER_TOUCHX read heading=%.1f pitch=%.1f aim_state=%d strong=%d applied=%u",double(h),double(p),aim,
                int(touchX().strongApplied()),touchX().applied());trainerLog(l);
        }
        if(phoneFrame){GuestNatives own(c,base,(r1-GuestNatives::Frame)&~0xFu);phoneCallTick(own);} // v852
    }
#if defined(__aarch64__)
    asm volatile("msr fpcr, %0"::"r"(fpcr));
#endif
    c=saved;
    trainerInside=false;
}
// A saved game was committed (guest_save_store.h): logged when trainer toggles are on.
extern "C" void recomp_trainer_note_save(const char* name) noexcept {trainer().noteSave(name);}

// ---- the API (trainer.h: the action, toggle and list words; README: local-analysis/trainer-mods/README.md)
extern "C" __attribute__((visibility("default"),used)) const char recomp_trainer_marker[]="recomp-trainer-v1";
extern "C" __attribute__((visibility("default"))) int recomp_trainer_v1(int action,int a0,int a1,int a2) {
    trainerSettingsOnce();
    if(action==recomp::trainer::Sync){if(trainerReadSettings(true))trainerSettingsRead.store(true,std::memory_order_release);return trainer().enabled()?1:0;}
    return trainer().request(action,a0,a1,a2);
}
extern "C" __attribute__((visibility("default"))) unsigned recomp_trainer_state_v1(void) {   // v989: + bit 23 StateImenuOpen
    trainerSettingsOnce();return trainer().state()|recomp::trainer::storyBits(recomp::trainer::storyCharacter.load(std::memory_order_relaxed))   // v1086: bits 27-28
        |(imenuOpenNow.load(std::memory_order_relaxed)?(1u<<23):0u)
        |(activities().realMenuAvailable()?(1u<<24):0u)|(activities().realMenuOpen()?(1u<<25):0u);}   // v1027: 24 real menu available, 25 open
// v852: the phone's state for the apps' touch layouts (phone_call.h): bit 0 phone up, bit 1 ringing, bit 2 in a call.
extern "C" __attribute__((visibility("default"))) unsigned recomp_phone_state_v1(void) {return recomp::phonecall::state();}
extern "C" __attribute__((visibility("default"))) int recomp_trainer_status_v1(char* out,int cap) {return trainer().status(out,cap);}
extern "C" __attribute__((visibility("default"))) int recomp_trainer_list_v1(int list,int index,char* out,int cap) {
    if(list==recomp::trainer::Activities)return recomp::activities::listEntry(index,out,cap);   // OW L2 1c: list 8
    return recomp::trainer::listEntry(list,index,out,cap);
}
// Update 1 P4: Sandbox state for the apps (sandbox_mode.h stateWord: bit 0 active, 1 denylist checked, 2 flow_controller held, 3 weapons
// granted; bits 8-15 held launches, 16-23 sweep kills, 24-31 held names missing from this disc). 0 in Story mode.
// v989 (ALLOCATION): 0 state, 1 open, 2 close, 3 style FivePocket, 4 style Interaction Menu. Returns the state bits (bit 0 open, bit 1
// the imenu style, bit 4 cool-down) or -2 for a bad op; -1 in Story mode (never active). The open is served on the next Sandbox tick.
// v1027 (lead routing decision): op 5 = open the game's own Interaction Menu (am_pi_menu, the local session only; the app's side-menu
// row for touch players), -3 when it is not available; bits 5 / 6 = the real menu available / open (state bits 24 / 25 too).
extern "C" __attribute__((visibility("default"))) int recomp_imenu_v1(int op) {
    if(op<0 || op>5)return -2;
    if(!recomp::sandbox::active())return -1;
    if(op==5 && !activities().requestRealMenuOpen())return -3;
    if(op==3 || op==4){imenuStyleOn.store(op==4,std::memory_order_release);imenuStyleSet.store(true,std::memory_order_release);
        char l[80];std::snprintf(l,sizeof l,"RENDER_IMENU style=%s source=api",op==4?"imenu":"fivepocket");trainerLog(l);}
    if(op==1)imenuModel().requestOpen();
    if(op==2)imenuModel().requestClose();
    return int((imenuOpenNow.load(std::memory_order_relaxed)?1u:0u)|(imenuStyleOn.load(std::memory_order_relaxed)?2u:0u)
               |(activities().realMenuAvailable()?32u:0u)|(activities().realMenuOpen()?64u:0u));
}
extern "C" __attribute__((visibility("default"))) int recomp_camera_turn_v1(float yawDeg,float pitchDeg) {return touchX().turn(yawDeg,pitchDeg);}   // v988 (ALLOCATION 14)
extern "C" __attribute__((visibility("default"))) int recomp_aim_assist_v1(int mode) {return touchX().aim(mode);}   // v988 (ALLOCATION 14); -3 red reticle (C4)
extern "C" __attribute__((visibility("default"))) unsigned recomp_sandbox_state_v1(void) {
    if(!recomp::sandbox::active())return 0;
    return recomp::sandbox::stateWord(true,recomp::sandbox::policy(),sandboxTick());
}
// v967 (wave-3 S6, online_save_guard.h): the save store asks the Sandbox hold through this probe (set once, at load).
[[maybe_unused]] static const bool recompSaveHoldProbeSet=(::recomp::sandbox::saveHoldProbe.store(&recomp_sandbox_state_v1,std::memory_order_release),true);
// v989 (E3): the pad read's hook (declared in guest_import_registry.cpp's XamInputGetState), set to the imenu Back gate's filter.
extern "C" std::atomic<void(*)(std::uint32_t,std::uint16_t*)> recomp_pad_filter_fn;
std::atomic<void(*)(std::uint32_t,std::uint16_t*)> recomp_pad_filter_fn{&imenuPadFilterC};
