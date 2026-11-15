#pragma once
// online_mode.h (v1050, trainer-mods; ONLINE-MODE-SPEC.md sections 2 and 7: one OnlineMode controller, decided once by play mode 3, never
// a Story character on screen, independent of where the Story save left the player).
// v1056 (lead: t-onlctl-20261107-2029 FAILS acceptance A: the world was revealed with Michael standing behind the Vinewood sign, then
// fm_intro's rising camera showed him from above): NOTHING is revealed until the player is the online character. The PedSwap state asks the
// trainer for the freemode character (action 15, male / female from the saved character.json) and the saved look (action 17 0 when a record
// exists), and confirms only when PLAYER_PED_ID's model is mp_m / mp_f_freemode_01, no swap is running, the head blend reports finished
// and the look's settle frames passed. Only then pedConfirmed is set; the loading-screen release, the curtain's reveals and the arrival's
// fm_intro start all wait for it. A player still in a vehicle (the swap is on foot only) is taken out first, behind the hold.
// Failure (no session, no swap within the limits) never reveals: the hold and the curtain stay, Failed is logged (the leave to the
// launcher, state bit 26, is still to be built).
// v1058 (t-onlctl4-20261107-2108: every queued 15 0 was dropped "blocked=character-switch": maintransition's own sky switch runs while we
// swap, as in the real game, and the trainer's gameplay gate refuses during a switch): the controller drives the online character change
// and the saved look itself, every tick, through the online actions' begin / step (0 running, 1 ok, 2 fail), not through the trainer's
// queue and gate. A failure is logged with its reason and retried after RetryMs.
// Hold evidence (v1050 / v1054): the Story boot card is taken down by startup_positioning's SHUTDOWN_LOADING_SCREEN (the shutdown
// sub_825C7E08, lr in the script VM) ~47 s after boot; loading_hold_hook.inc holds it in Online mode until the controller releases it.
#include "local_session_flag.h"
#include "online_descent.h"
#include "online_leave_flag.h"   // v1085: the leave to the launcher   // v1082: CloudsDown (the game's own SWITCH_TO_MULTI first / second part)
#include "online_curtain_flag.h"   // v1066: holdSession
#include "online_freemode_gate.h"   // v1093: savedCharacter (the later-visit MP character answers)
#include "online_mt_adopt.h"   // v1102: entered (maintransition's T18 adopted our host)
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>   // v1170: std::strstr (the look's outcome detail)
#include "lower_backdrop.h"   // v1103: creatorScreen (the glue publishes creatorRunning())

namespace recomp::online_mode {
// Shared with the hook (main thread), the curtain and the arrival (trainer tick).
inline std::atomic<bool> releaseRequested{false};   // the controller says: the loading screen may go
inline std::atomic<unsigned> deferredShutdowns{0};   // shutdown calls held so far
inline std::atomic<bool> released{false};            // the hook ran the held shutdown (or there was none to run)
inline std::atomic<bool> pedConfirmed{false};        // v1056: the player is the online character (freemode model, blend done)
inline std::atomic<bool> frontendRelease{false};     // v1066: the creator's frontend owns the screen: the boot card may go (once)
inline std::atomic<bool> goOnlineDecided{false},skipGoOnline{false};   // v1066: the first visit runs the creator route, not the boot-card branch
// v1125 (t-onlcre124-20261109-0345, v1124 route default-on, first visit: the creator committed, then maintransition never started (G1344034 0,
// no thread) because the byte stayed 0 - selector's go-online check (0x42d4: static 158 == 0 && (106c8317() || d87f3a9e())) re-runs every
// frame until it latches - so l1 hosted on its own after 90 s, freemode never ran, the player was left without control): on the freemode
// route, once the creator has saved the character, the glue sets the go-online byte (the later visit's own write, trainer_glue.inc) until
// selector has started maintransition (G1351512, selector 0x4551), which then runs the later visit's path (ui 3, T4, T17, T18 ENTER adopted,
// freemode 0x61dfa). The saved character / sex answers (online_freemode_gate.h) are latched again from the new character.json, as the next
// process would latch them at its first tick. Route off (diag-fm-route-off): unchanged (the v1066 path). The intro record is not touched:
// a new character has no intro, so freemode runs its own fm_intro (online_arrival GameIntro).
inline std::atomic<bool> goOnlineAfterCreator{false};
// v1075 (t-onlnoc72-20261108-0011): set by the boot-card hook once the game's freeze sub_825C8060 holds the choice (0x83895C13 = 1). From then
// on only the game writes the byte: maintransition clears it itself (c0b971ea(0) at 0x290f / 0x42a3e; setter #4 lr=8343a208 after the release)
// and the early loop no longer sets it back.
inline std::atomic<bool> choiceFrozen{false};
// v1080 (lead 2026-11-08: one world frame showed between the last card and the intro's first cutscene shot; a later visit's card dropped
// 1.26 s after the placement): the arrival's phase (online_arrival.h phase(), published by the glue after each arrival tick: 0 idle,
// 1 waiting, 2 intro asked, 3 placed / settling, 4 the intro's cutscene owns the camera or done). The release waits while it is 1..3, at
// most RevealWaitMaxMs (never a stuck loading screen).
inline std::atomic<int> arrivalPhase{0};
constexpr std::uint64_t RevealWaitMaxMs=15000;
// v1085: the leave. The session must stay down LeaveDownTicks controller ticks (a flicker is not a leave); the arrival saves the spot on
// the same edge; LeaveSettleMs later state bit 26 tells the app to close the game window.
constexpr std::uint32_t LeaveDownTicks=10;constexpr std::uint64_t LeaveSettleMs=2000;
// v1086 INTERIM: the frontend release waits FrontendWaitMs after Freemode (the sweep runs every 30 frames) and tries at most FrontendMaxMs.
constexpr std::uint64_t FrontendWaitMs=1500,FrontendMaxMs=8000;
// v1088: the sweep is a FALLBACK only. maintransition is left to end itself and launch freemode (0x61dfa) once its state 4 passes (the
// stats slot answered locally, online_stats_hook.inc). The sweep runs only if, SweepFallbackMs after Freemode, freemode has no thread and
// maintransition still runs. freemode running (or maintransition gone) = no sweep, logged once.
constexpr std::uint64_t SweepFallbackMs=60000;
// v1104 (owner on t-onlrev82-20261108-1409, v1101 later visit: "my character is stuck in mid air, the game hung and the character select
// screen wasnt showing"): with the freemode route off maintransition parks at T5 (the MP character select, ui 4, its frontend invisible)
// and holds the player (ctl 0, pinned at the spot) until the sweep; after the sweep control came back (ctl 1) but the player never moved
// under the owner's stick (16:48:40-46, pos unchanged to 16:56) and showed no ped: maintransition's own exit (0x43111(1): SET_ENTITY_VISIBLE,
// SET_ENTITY_COLLISION, FREEZE_ENTITY_POSITION, SET_PLAYER_INVINCIBLE, control) never ran. So:
//  - at T5 (mt word 5, the known dead end of a later visit without the MP character answers) the sweep waits CharSelectSweepMs, not 60 s;
//  - once maintransition is gone without freemode (our sweep, or it ended by itself) the controller restores the player as 0x43111(1)
//    does (PedOps::playerRestore: visible, unfrozen, collision on, not invincible, control on, HUD / radar on), RestoreDelayMs after it
//    went, then reads it back RestoreCheckMs later and re-applies (at most RestoreTries) while it reads hidden or without control.
//    freemode running = freemode owns the player: no restore.
constexpr std::uint64_t CharSelectSweepMs=5000,RestoreDelayMs=500,RestoreCheckMs=3000;constexpr int CharSelectMt=5,RestoreTries=3;
// v1102 (round 5 stage C): after maintransition's T18 ENTER adopted our host, the swap waits until maintransition is past T26 (mt >= 27, or its
// thread gone): T26's RESURRECT / SET_PLAYER_MODEL / CLONE_PED_TO_TARGET land first, our saved look after. Bounded: the mt word unchanged
// MtStallMs, or MtPassMaxMs since the session (T26's own 240 s plus a margin), or T58 (the join failed) -> Failed (the screen stays covered).
constexpr std::uint64_t MtStallMs=120000,MtPassMaxMs=270000;
// v1105 (FREEMODE-GATE-6 B4-B6): on the freemode route (the route switch on and maintransition's T18 ENTER adopted) the game makes the freemode
// player itself (T26): the controller never calls 15, adopts the game's ped once (PedOps::adoptGameCharacter), waits for the blend, then 17 0.
// FmModelWaitMs without a freemode model after T26 -> Failed (covered). Every route: a refused / failed 17 is retried after RetryMs, at most
// LookTriesMax times, then Failed with the detail (never a silent PedSwapMaxMs). On the route: no CloudsDown, and one hair read-back before the
// release (a mismatch -> 17 0 once more behind the held card).
constexpr std::uint64_t FmModelWaitMs=10000;constexpr int LookTriesMax=3;
// v1093 (lead 10:28): the fallback is keyed on maintransition's own progress: its transition word G1344034 UNCHANGED for SweepFallbackMs while
// maintransition is alive and freemode is not (a real T4 -> T17 -> T18 -> T25 progression is never killed). An unreadable word (-1) keeps the
// v1088 rule (SweepFallbackMs after Freemode).
constexpr std::uint32_t FreemodeScript=0xC875557Du;   // FreemodeScript = joaat("freemode") (checked by the test)
constexpr std::uint64_t HardArrivalExtraMs=60000;   // v1088
constexpr std::uint64_t HoldMaxMs=150000,HardHoldMs=180000,PedSwapMaxMs=60000,RetryMs=3000,CreatorStartMaxMs=90000,CreatorLoadWaitMs=120000;
constexpr std::uint32_t SettleFrames=60,LookSettleFrames=45;
constexpr std::uint32_t MaleModel=0x705E61F2u,FemaleModel=0x9C9EFFD8u;   // joaat("mp_m_freemode_01"), joaat("mp_f_freemode_01")
constexpr bool isFreemode(std::uint32_t m) noexcept {return m==MaleModel || m==FemaleModel;}

// The hook's decision for one shutdown call: true = hold it (do not run the original).
inline bool holdShutdown(bool onlineMode) noexcept {
    if(!onlineMode || released.load(std::memory_order_acquire))return false;
    deferredShutdowns.fetch_add(1,std::memory_order_acq_rel);return true;
}
// The hook's per-frame check (main thread): run the held shutdown now? Only on the controller's request; the hard limit releases only once
// the online character is confirmed (v1056: never a Story character revealed by a timeout).
inline bool shutdownDue(bool onlineMode,std::uint64_t nowMs,std::uint64_t& firstHoldMs) noexcept {
    if(!onlineMode || released.load(std::memory_order_acquire))return false;
    const unsigned d=deferredShutdowns.load(std::memory_order_acquire);
    if(d && !firstHoldMs)firstHoldMs=nowMs?nowMs:1;
    // v1088 (lead after t-onlcre74-20261108-0337: this hard limit released at 03:42:05 while fm_intro was placing the player, so the hill
    // showed ~3.5 s before the intro's first shot): while the arrival is in phases 1..3 the hard limit waits too, up to HardArrivalExtraMs more.
    const int ph=arrivalPhase.load(std::memory_order_acquire);
    const std::uint64_t held=firstHoldMs?nowMs-firstHoldMs:0;
    const bool hard=firstHoldMs && held>=HardHoldMs && pedConfirmed.load(std::memory_order_acquire) && (ph<1 || ph>3 || held>=HardHoldMs+HardArrivalExtraMs);
    if(frontendRelease.load(std::memory_order_acquire)){released.store(true,std::memory_order_release);return true;}   // v1066: the creator
    if(!(releaseRequested.load(std::memory_order_acquire) || hard))return false;
    released.store(true,std::memory_order_release);
    // v1062 (t-onlctl6-20261107-2125: the swap confirmed, but on the game's own Online path nothing called the shutdown, so the boot card
    // stayed up over fm_intro for the whole run): once the online character is confirmed the card is taken down even when nothing was held.
    return d>0 || pedConfirmed.load(std::memory_order_acquire);
}

// What the controller needs from the game (the glue: trainer requests + natives; the tests: a mock).
struct PedOps {
    virtual ~PedOps()=default;
    virtual std::uint32_t playerModel()=0;    // GET_ENTITY_MODEL(PLAYER_PED_ID())
    virtual bool blendDone()=0;               // HAS_PED_HEAD_BLEND_FINISHED(PLAYER_PED_ID())
    virtual bool inVehicle()=0;               // IS_PED_IN_ANY_VEHICLE(PLAYER_PED_ID(), 0)
    virtual void leaveVehicleNow()=0;         // CLEAR_PED_TASKS_IMMEDIATELY(PLAYER_PED_ID()) (behind the hold)
    virtual int savedSex()=0;                 // the saved character.json: 0 male, 1 female, -1 no record
    // v1058: one frame of an online action (15 sex = the character change, 17 0 = the saved look): begins it when start is true, else
    // steps the running one. 0 running, 1 ok, 2 fail (detail holds the reason).
    virtual int driveAction(int action,int a0,bool start,char* detail,int cap)=0;
    // v1064: is the boot card still up (the card state word)? Freemode is entered only once it is not.
    virtual bool cardUp()=0;
    // v1064: diag-online-gameflow: the controller drives no swap (the game's own frontend is to make the online character); the reveal gate
    // (a freemode model, blend done) is unchanged.
    virtual bool gameFlow()=0;
    // v1066: the first-visit creator (creator_route.h, started directly, kept dark): start it (1 started, 2 refused, detail = why), and its
    // phase: 0 idle, 1 running (bootstrap .. pin, teardown .. save), 2 the creator frontend is up, 3 done (character.json saved), 4 failed.
    virtual int creatorStart(char* detail,int cap) {if(detail && cap>0)detail[0]=0;return 2;}
    virtual int creatorPhase() {return 0;}
    virtual bool creatorAvailable() {return false;}   // the route exists (Online / Sandbox with the local path on)
    // v1082: CloudsDown on a later visit (online_descent.h): the game's switch natives + G1332498 (nullptr = not available), and whether it
    // is wanted (files/private-trace/diag-online-cloudsdown for the first device runs).
    virtual ::recomp::online_descent::DescentOps* descentOps() {return nullptr;}
    virtual bool cloudsDown() {return false;}
    // v1085: cover the screen at once (the leave: DO_SCREEN_FADE_OUT(0)), never a Story frame after the session ends
    virtual void coverNow() {}
    // v1086 INTERIM: maintransition's join frontend left active after it ended: is the pause menu flagged, and release it (SET_FRONTEND_ACTIVE(0),
    // as maintransition's own 0x44a12-0x44a20 does)
    virtual bool pauseMenuUp() {return false;}
    virtual void releaseFrontend() {}
    // v1088: GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH for "freemode" / "maintransition" (-1 = unreadable)
    virtual int freemodeThreads() {return -1;}
    virtual int transitionThreads() {return -1;}
    virtual int mtState() {return -1;}   // v1093: G1344034, maintransition's transition word (-1 = unreadable)
    // v1104: the player after maintransition went without its own exit: apply = restore as 0x43111(1) (visible, unfrozen, collision,
    // not invincible, control, HUD / radar) and read back; apply false = read only. 1 visible with control, 0 not, -1 unreadable.
    virtual int playerRestore(bool apply,char* detail,int cap) {(void)apply;if(detail && cap>0)detail[0]=0;return -1;}
    // v1105 (FREEMODE-GATE-6 B3): the freemode route (fm_route::routeOn() && mt_adopt::entered), adopting the game's freemode player as the
    // online character (1 adopted, 2 refused: detail), and the hair read-back (now = GET_PED_DRAWABLE_VARIATION(ped,2), applied = the saved
    // look's drawable; false when either is unknown)
    virtual bool fmRoute() {return false;}
    virtual bool routeSwitch() {return false;}   // v1125: fm_route::routeOn() alone (the switch, before any ENTER was adopted)
    virtual int adoptGameCharacter(char* detail,int cap) {if(detail && cap>0)std::snprintf(detail,std::size_t(cap),"no-glue");return 2;}
    virtual bool lookHair(int& now,int& applied) {now=applied=-1;return false;}
    // v1105 (FREEMODE-GATE-6 C4): our arrival record, read once: 1 valid with intro 1, 0 missing / intro 0 / unreadable, -1 not available
    virtual int introRecord() {return -1;}
    // v1170 (diagnostic, read only): why HAS_PED_HEAD_BLEND_FINISHED(PLAYER_PED_ID()) reads 0 - HAVE_ALL_STREAMING_REQUESTS_COMPLETED(ped)
    // (the native's second half, sub_82892318) and the player switch (IS_PLAYER_SWITCH_IN_PROGRESS / GET_PLAYER_SWITCH_STATE / _TYPE)
    virtual void blendDetail(char* detail,int cap) {if(detail && cap>0)std::snprintf(detail,std::size_t(cap),"n/a");}
};

using LogFn=void(*)(const char*);
class Controller {
public:
    enum class State : std::uint8_t {Off,Boot,Creator,SessionHost,PedSwap,Reveal,Freemode,Leave,Failed};
    explicit Controller(LogFn log) noexcept:log_(log) {}
    // Every Sandbox tick. Not Online mode: nothing (one branch).
    void tick(std::uint32_t frame,std::uint64_t nowMs,bool onlineMode,bool gip,bool carBusy,PedOps& ops) noexcept {
        if(!onlineMode || !log_)return;
        const bool session=::recomp::local_session::active() && gip;
        switch(st_) {
        case State::Off:
            startMs_=nowMs;enter(State::Boot,nowMs,"hold the boot loading screen");
            {const int sx=ops.savedSex();   // v1100: one source for the stats answer (savedSexSeed) and the pedswap
             ::recomp::freemode_gate::savedSexSeed.store(sx,std::memory_order_release);
             ::recomp::freemode_gate::savedCharacter.store(sx>=0?1:0,std::memory_order_release);}   // v1093: latched once per process
            {const int ir=ops.introRecord();::recomp::freemode_gate::introRecord.store(ir,std::memory_order_release);   // v1105 (FREEMODE-GATE-6 C4)
             char w[64];std::snprintf(w,sizeof w,"intro record latched intro=%d",ir);note(nowMs,w);}
            // v1066 (lead / spec 1.4): the first visit (no saved character) goes through the game's own creator, before the session
            if(ops.savedSex()<0 && !ops.gameFlow() && ops.creatorAvailable()) {
                skipGoOnline.store(true,std::memory_order_release);::recomp::online_curtain::holdSession.store(true,std::memory_order_release);
                creatorAt_=nowMs;lastTry_=0;creatorStarted_=false;frontendSeen_=false;enter(State::Creator,nowMs,"first visit: the game's own creator before the session");
            }
            goOnlineDecided.store(true,std::memory_order_release);
            return;
        case State::Creator: {   // v1066
            if(!creatorStarted_) {
                // v1068 (t-onlcre-20261107-2303: started 1.9 s after boot, the frontend up at 2.7 s and the card shut down mid-load: the
                // renderer stopped "Native EndVertices did not complete its real draw" 0.14 s later): the creator starts only once the
                // game itself asked for the boot card to go (startup_positioning's held SHUTDOWN_LOADING_SCREEN, the world loaded behind it)
                if(!loadDone_) {
                    if(deferredShutdowns.load(std::memory_order_acquire)>0){loadDone_=true;creatorAt_=nowMs;note(nowMs,"the game asked for the boot card to go (world loaded): the creator may start");}
                    else if(nowMs-creatorAt_>=CreatorLoadWaitMs){creatorEnd(nowMs,"no load-done signal in time: the default online character instead");return;}
                    else return;
                }
                if(nowMs-creatorAt_>=CreatorStartMaxMs){creatorEnd(nowMs,"the creator did not start in time: the default online character instead");return;}
                if(lastTry_ && nowMs-lastTry_<RetryMs)return;
                lastTry_=nowMs;char d[96]{};const int r=ops.creatorStart(d,int(sizeof d));
                char w[150];std::snprintf(w,sizeof w,"creator start -> %d (%s)",r,d);note(nowMs,w);
                creatorStarted_=r==1;return;
            }
            const int ph=ops.creatorPhase();
            if(ph==2 && !frontendSeen_){frontendSeen_=true;frontendRelease.store(true,std::memory_order_release);note(nowMs,"creator frontend up: the boot card gives way to it");}
            if(ph==3) {
                if(ops.routeSwitch()) {   // v1125: the created character goes online the later visit's way
                    const int sx=ops.savedSex();
                    if(sx>=0) {
                        ::recomp::freemode_gate::savedSexSeed.store(sx,std::memory_order_release);
                        ::recomp::freemode_gate::savedCharacter.store(1,std::memory_order_release);
                        goOnlineAfterCreator.store(true,std::memory_order_release);
                        char w[160];std::snprintf(w,sizeof w,"fm-route: the created character (%s) goes online through selector (go-online byte; saved character 1)",sx==1?"female":"male");note(nowMs,w);
                    } else note(nowMs,"fm-route: the creator reported done but no character.json reads back: the v1066 path");
                }
                creatorEnd(nowMs,"creator done: character saved");return;
            }
            if(ph==4){creatorEnd(nowMs,"creator failed: the default online character instead");return;}
            return;
        }
        case State::Boot:
            if(session){enter(State::SessionHost,nowMs,"local session hosted");sessionAt_=frame;sessionMs_=nowMs;return;}
            if(nowMs-limitBase()>=HoldMaxMs)fail(nowMs,"no session within the hold limit");
            return;
        case State::SessionHost:
            if(!session){fail(nowMs,"the session went down before the swap");return;}
            if(::recomp::mt_adopt::entered.load(std::memory_order_acquire) && !mtPast_) {   // v1102: maintransition's T21-T26 first
                const int mt=ops.mtState(),th=ops.transitionThreads();
                if(mt==58){fail(nowMs,"maintransition's join failed (T58) after the adopted ENTER");return;}
                if(mt>=27 || th==0){mtPast_=true;fmRoute_=ops.fmRoute();char w[120];std::snprintf(w,sizeof w,"maintransition past T26 (mt=%d threads=%d)%s",mt,th,fmRoute_?"; fm-route":"");note(nowMs,w);}
                else {
                    if(mt!=mtWait_){mtWait_=mt;mtWaitSince_=nowMs;char w[96];std::snprintf(w,sizeof w,"swap waits for maintransition T21-T26 (mt=%d)",mt);note(nowMs,w);}
                    if(nowMs-mtWaitSince_>=MtStallMs || nowMs-sessionMs_>=MtPassMaxMs){char w[96];std::snprintf(w,sizeof w,"maintransition stuck at T%d after the adopted ENTER",mt);fail(nowMs,w);}
                    return;   // (the HoldMaxMs settle check does not apply during this wait)
                }
            }
            if((!mtPast_ && frame-sessionAt_<SettleFrames) || carBusy){   // v1105: past T26 the settle wait is skipped (T26 already settled it)
if(!::recomp::mt_adopt::entered.load(std::memory_order_acquire) && nowMs-limitBase()>=HoldMaxMs)fail(nowMs,"session did not settle");return;}   // v1102: after an adopted ENTER the T21-T26 wait already bounded it
            sex_=ops.savedSex();swapAt_=nowMs;lastTry_=0;lookAt_=0;lookAsked_=false;lookDone_=false;running_=false;lookTries_=0;fmWaitAt_=0;adoptTried_=false;
            lookFinal_=false;blendSeen_=false;blendDropped_=false;   // v1170
            {char w[80];std::snprintf(w,sizeof w,"to the online character (%s)",sex_==1?"female, saved":sex_==0?"male, saved":"male, no record yet");
             enter(State::PedSwap,nowMs,w);}
            return;
        case State::PedSwap: {
            if(!session){fail(nowMs,"the session went down during the swap");return;}
            if(nowMs-swapAt_>=PedSwapMaxMs){fail(nowMs,"the online character was not confirmed in time (stays covered)");return;}
            const std::uint32_t m=ops.playerModel();
            if(!isFreemode(m))blendSeen_=false;   // v1170: the latch belongs to the online character it was read on; any non-freemode model voids it
            if(running_) {   // v1058: a job of ours is running: one more frame of it
                char d[64]{};const int r=ops.driveAction(runningAction_,runningArg_,false,d,int(sizeof d));
                if(r==0)return;
                running_=false;
                if(r==2){lastTry_=nowMs;
                    if(runningAction_==17){lookRefused(nowMs,d);return;}   // v1105: retried, at most LookTriesMax
                    char w[120];std::snprintf(w,sizeof w,"action %d %d failed (%s); retry in %llu ms",runningAction_,runningArg_,d,(unsigned long long)RetryMs);note(nowMs,w);return;}
                if(runningAction_==17){lookDone_=true;lookAt_=frame;lookDoneMs_=nowMs;   // v1108: lookDoneMs_
                    lookFinal_=std::strstr(d," final")!=nullptr;}   // v1170: "look applied clamped=N final" = the applier saw finished + streaming and sent FINALIZE
                return;
            }
            if(fmRoute_) {   // v1105 (FREEMODE-GATE-6 B4): the game makes the freemode player; never 15
                if(!isFreemode(m)) {
                    if(!fmWaitAt_)fmWaitAt_=nowMs;
                    if(nowMs-fmWaitAt_>=FmModelWaitMs){fail(nowMs,"maintransition made no freemode player after T26");}
                    return;
                }
                if(!adoptTried_) {
                    adoptTried_=true;char d[64]{};const int r=ops.adoptGameCharacter(d,int(sizeof d));
                    char w[160];std::snprintf(w,sizeof w,"fm-route: the game made the freemode player (model=%08x); no action 15; adopt -> %d (%s)",m,r,d);note(nowMs,w);
                }
            }
            if(!isFreemode(m) && ops.gameFlow()) {   // v1064: wait for the game's own character (logged once)
                if(!flowLogged_){flowLogged_=true;note(nowMs,"game flow: no swap driven, waiting for the game's own online character");}
                return;
            }
            if(!isFreemode(m)) {
                if(ops.inVehicle()){ops.leaveVehicleNow();note(nowMs,"out of the vehicle first (the swap is on foot only)");return;}
                if(lastTry_ && nowMs-lastTry_<RetryMs)return;
                start(ops,nowMs,15,sex_==1?1:0,m);return;
            }
            // v1170 (t-onlrevr-20261110-1019 and 7 more runs, 8/8 with the game's SHORT descent switch, 0 of ~230 route runs with the long one):
            // the blend is an EVENT of our look, not a level that must hold through the settle. Our applier returned "final" only after
            // HAS_PED_HEAD_BLEND_FINISHED && HAVE_ALL_STREAMING_REQUESTS_COMPLETED, then sent FINALIZE_HEAD_BLEND; the controller read 1 for
            // ~2 s; then freemode's descent switch (0x095b3, START_PLAYER_SWITCH type auto -> short, ~1.6 s) ended under the held card and the
            // native read 0 for the rest of the run (55 s) - with the model and the hair unchanged. A long switch ends only after the release,
            // so v1108's level wait never saw it there. Once the controller itself has read 1 after a final look, a later 0 no longer holds the
            // confirmation (logged once with its parts); the per-tick freemode-model guard (never a Story character) is unchanged. Before
            // that edge (the first visit's 15 wait, a look not ended "final" - e.g. "blend=timeout" -, or a 0 from the look's end on) the v1108 level wait stays.
            const bool blendNow=ops.blendDone();
            if(blendNow && lookDone_ && lookFinal_ && !blendSeen_)blendSeen_=true;
            if(!blendNow && !blendSeen_){   // v1108 (diagnostic, read only): after a finished look the head blend never reported finished (t-onlroute-race-1626)
                if(lookDone_ && (!blendLogAt_ || nowMs-blendLogAt_>=BlendLogEveryMs)){blendLogAt_=nowMs;int hn=-1,ha=-1;ops.lookHair(hn,ha);char bd[96]{};ops.blendDetail(bd,int(sizeof bd));
                    char w[260];std::snprintf(w,sizeof w,"look applied, waiting for the head blend: finished=0 model=%08x hair now=%d saved=%d (%llu ms after the look) %s",m,hn,ha,
                        (unsigned long long)(lookDoneMs_?nowMs-lookDoneMs_:0),bd);note(nowMs,w);}
                return;}
            if(!blendNow && !blendDropped_){blendDropped_=true;int hn=-1,ha=-1;ops.lookHair(hn,ha);char bd[96]{};ops.blendDetail(bd,int(sizeof bd));   // v1170
                char w[300];std::snprintf(w,sizeof w,"the head blend read 0 again after the final look (%llu ms after it): the look stays applied, the settle continues; model=%08x hair now=%d saved=%d %s",
                    (unsigned long long)(lookDoneMs_?nowMs-lookDoneMs_:0),m,hn,ha,bd);note(nowMs,w);}
            if(lookDone_ && blendLogAt_ && !blendOkLogged_ && blendNow){blendOkLogged_=true;char w[96];std::snprintf(w,sizeof w,"the head blend finished %llu ms after the look",(unsigned long long)(nowMs-lookDoneMs_));note(nowMs,w);}
            if(sex_>=0 && !lookAsked_){if(lastTry_ && nowMs-lastTry_<RetryMs)return;   // v1105: a refused look waits RetryMs
                lookAsked_=true;lookAt_=frame;start(ops,nowMs,17,0,m);return;}
            if(lookAsked_ && (!lookDone_ || frame-lookAt_<LookSettleFrames))return;
            pedConfirmed.store(true,std::memory_order_release);
            ::recomp::online_leave::hold.store(true,std::memory_order_release);   // v1085: from here on the Story init never runs
            {char w[120];std::snprintf(w,sizeof w,"online character confirmed model=%08x blend=%s look=%s",m,blendNow?"1":"1-then-0",lookAsked_?"saved":"defaults");   // v1170
             enter(State::Reveal,nowMs,w);}
            revealAt_=nowMs;return;   // v1080: the release waits for the arrival (Reveal)
        }
        case State::Reveal:
            if(running_) {   // v1105: the read-back's re-apply runs behind the held card
                char d[64]{};const int r=ops.driveAction(runningAction_,runningArg_,false,d,int(sizeof d));
                if(r==0)return;
                running_=false;char w[120];std::snprintf(w,sizeof w,"look re-apply finished -> %d (%s)",r,d);note(nowMs,w);
            }
            if(fmRoute_ && !readbackDone_ && !releaseRequested.load(std::memory_order_acquire)) {   // v1105 (B6): one hair read-back
                readbackDone_=true;int now=-1,applied=-1;
                if(ops.lookHair(now,applied) && applied>0 && now!=applied) {
                    char w[140];std::snprintf(w,sizeof w,"look re-applied (the game changed it: hair %d -> %d)",applied,now);note(nowMs,w);
                    start(ops,nowMs,17,0,ops.playerModel());return;
                }
                {char w[120];std::snprintf(w,sizeof w,"look read-back hair now=%d applied=%d",now,applied);note(nowMs,w);}
            }
            if(!releaseRequested.load(std::memory_order_acquire)) {   // v1080: the card stays until the arrival owns the picture
                const int ph=arrivalPhase.load(std::memory_order_acquire);const std::uint64_t waited=nowMs-revealAt_;
                if(ph==3)placedSeen_=true;   // v1082: a later visit (placed at the saved spot)
                if(ph>=1 && ph<=3 && waited<RevealWaitMaxMs){
                    if(!revealWaitLogged_){revealWaitLogged_=true;char w[96];std::snprintf(w,sizeof w,"release waits for the arrival (phase %d)",ph);note(nowMs,w);}
                    return;}
                // v1082: CloudsDown. A later visit, wanted, available: the game's ascent first (behind the card), the release once in the clouds
                ::recomp::online_descent::DescentOps* dops=ops.descentOps();
                if(placedSeen_ && dops && ops.cloudsDown() && !descentTried_ && !fmRoute_) {   // v1105: never on the freemode route
                    descentTried_=true;
                    if(descent_.ascend(*dops,nowMs)){note(nowMs,"cloudsdown: the ascent behind the card (SWITCH_TO_MULTI_FIRSTPART)");return;}
                    note(nowMs,"cloudsdown: ascent refused (a switch is running): plain release");
                }
                if(descent_.state()==::recomp::online_descent::Descent::St::Ascending){descent_.tick(*dops,nowMs);
                    if(!descent_.inSky() && !descent_.failed())return;}
                char w[140];std::snprintf(w,sizeof w,"release requested (arrival phase %d after %llu ms%s%s)",ph,(unsigned long long)waited,
                                          ph>=1 && ph<=3?": the wait limit":"",descent_.inSky()?"; in the clouds":descent_.failed()?"; cloudsdown failed: plain":"");note(nowMs,w);
                releaseRequested.store(true,std::memory_order_release);return;
            }
            if(descent_.inSky() || descent_.state()==::recomp::online_descent::Descent::St::Descending) {   // v1082: down once the card is gone
                ::recomp::online_descent::DescentOps* dops=ops.descentOps();
                if(dops) {
                    if(descent_.inSky() && released.load(std::memory_order_acquire) && !ops.cardUp())descent_.descend(*dops,nowMs);
                    descent_.tick(*dops,nowMs);
                    if(!descent_.done() && !descent_.failed())return;
                    note(nowMs,descent_.done()?"cloudsdown: landed (the game's second part over)":"cloudsdown failed after the release: freemode without the descent");
                }
            }
            // v1064 (lead: t-onlctl6 logged "loading screen down" while the card stayed up): Freemode only once the card is really gone
            if(released.load(std::memory_order_acquire) && !ops.cardUp()){enter(State::Freemode,nowMs,"loading screen down (card state word confirms)");return;}
            if(released.load(std::memory_order_acquire) && !cardLogged_){cardLogged_=true;note(nowMs,"released, but the boot card is still up (card state word 1)");}
            return;
        case State::Freemode:   // v1085: the session ending (the pause menu's quit, the Interaction Menu, a drop) -> Leave
            if(!fmEnterAt_)fmEnterAt_=nowMs;
            if(!transitionEnded_ && !gameLaunch_) {   // v1088: the game's own launch first; the v1086 sweep only as the fallback
                const int fm=ops.freemodeThreads(),mt=ops.transitionThreads();
                if(fm>0){gameLaunch_=true;freemodeOwns_=true;char w[120];std::snprintf(w,sizeof w,"freemode running (the game's own launch) %llu ms after Freemode; no sweep",(unsigned long long)(nowMs-fmEnterAt_));note(nowMs,w);}
                else if(mt==0){gameLaunch_=true;note(nowMs,"maintransition ended by itself, freemode not running; no sweep");}
                else if(stalled(ops.mtState(),nowMs)) {   // v1086 INTERIM, now the fallback: end maintransition (its join frontend takes B as "Return to GTA V")
                    transitionEnded_=true;freemodeAt_=nowMs;::recomp::online_leave::endTransition.store(true,std::memory_order_release);
                    char w[200];std::snprintf(w,sizeof w,"fallback: maintransition ended by the sweep (freemode threads %d, maintransition threads %d, %llu ms after Freemode, mt word %d unchanged %llu ms)",fm,mt,(unsigned long long)(nowMs-fmEnterAt_),mtLast_,(unsigned long long)(mtLast_>=0?nowMs-mtSince_:0));note(nowMs,w);
                }
            }
            restoreTick(ops,nowMs);   // v1104
            if(nowMs-fmEnterAt_<BlendFollowMs && (!blendFollowAt_ || nowMs-blendFollowAt_>=BlendLogEveryMs)) {   // v1170 (diagnostic, read only)
                blendFollowAt_=nowMs;const int b=ops.blendDone()?1:0;
                if(b!=blendFollowLast_){blendFollowLast_=b;char bd[96]{};ops.blendDetail(bd,int(sizeof bd));
                    char w[200];std::snprintf(w,sizeof w,"head blend after the release: finished=%d (%llu ms after Freemode) %s",b,(unsigned long long)(nowMs-fmEnterAt_),bd);note(nowMs,w);}
            }
            if(transitionEnded_ && !frontendDone_ && nowMs-freemodeAt_>=FrontendWaitMs) {
                if(ops.pauseMenuUp()){ops.releaseFrontend();if(!frontendLogged_){frontendLogged_=true;note(nowMs,"fallback: its frontend released (SET_FRONTEND_ACTIVE 0)");}}
                else frontendDone_=true;
                if(nowMs-freemodeAt_>=FrontendWaitMs+FrontendMaxMs)frontendDone_=true;   // bounded: the player's own pause menu is never touched after this
            }
            if(session){downTicks_=0;return;}
            if(++downTicks_<LeaveDownTicks)return;
            ops.coverNow();leaveAt_=nowMs;enter(State::Leave,nowMs,"the session ended: screen covered, the Story init held");return;
        case State::Leave:
            if(!::recomp::online_leave::done.load(std::memory_order_acquire) && nowMs-leaveAt_>=LeaveSettleMs) {
                ::recomp::online_leave::done.store(true,std::memory_order_release);
                note(nowMs,"leave done (state bit 26): the app returns to the launcher");
            }
            if(nowMs-leaveAt_<LeaveSettleMs+2000u)ops.coverNow();   // keep it black until the app closes the window
            return;
        case State::Failed:return;
        }
    }
    State state() const noexcept {return st_;}
    bool creatorRunning() const noexcept {return st_==State::Creator && creatorStarted_;}   // v1103: the lower screen's backdrop (lower_backdrop.h)
private:
    static const char* name(State s) noexcept {
        switch(s){case State::Off:return "Off";case State::Boot:return "Boot";case State::Creator:return "Creator";case State::SessionHost:return "SessionHost";case State::PedSwap:return "PedSwap";
                  case State::Reveal:return "Reveal";case State::Freemode:return "Freemode";case State::Leave:return "Leave";case State::Failed:return "Failed";}
        return "?";
    }
    void enter(State s,std::uint64_t nowMs,const char* why) noexcept {
        st_=s;char l[220];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE state=%s t=%llu held=%u (%s)",name(s),(unsigned long long)(nowMs-startMs_),
                                       deferredShutdowns.load(std::memory_order_acquire),why);log_(l);
    }
    void note(std::uint64_t nowMs,const char* what) noexcept {
        char l[480];std::snprintf(l,sizeof l,"RENDER_ONLINE_MODE pedswap t=%llu %s",(unsigned long long)(nowMs-startMs_),what);log_(l);   // v1105: 480 (t-onlrev104: the restore line was cut at 200)
    }
    void fail(std::uint64_t nowMs,const char* why) noexcept {enter(State::Failed,nowMs,why);}   // v1056: no release (never a Story reveal)
    std::uint64_t limitBase() const noexcept {return creatorEndMs_?creatorEndMs_:startMs_;}   // v1066: the hold limits count from the creator's end
    void creatorEnd(std::uint64_t nowMs,const char* why) noexcept {   // v1066: the session may come up now
        ::recomp::online_curtain::holdSession.store(false,std::memory_order_release);creatorEndMs_=nowMs?nowMs:1;enter(State::Boot,nowMs,why);
    }
    void start(PedOps& ops,std::uint64_t nowMs,int action,int a0,std::uint32_t m) noexcept {   // v1058: begin one of our online actions
        char d[64]{};const int r=ops.driveAction(action,a0,true,d,int(sizeof d));
        char w[140];std::snprintf(w,sizeof w,"action %d %d started -> %d (%s) model=%08x",action,a0,r,d,m);note(nowMs,w);
        runningAction_=action;runningArg_=a0;running_=r==0;
        if(r==2)lastTry_=nowMs;
        if(r==2 && action==17)lookRefused(nowMs,d);   // v1105
        if(r==1 && action==17){lookDone_=true;lookDoneMs_=nowMs;lookFinal_=std::strstr(d," final")!=nullptr;}   // v1108, v1170
    }
    // v1105 (FREEMODE-GATE-6 B5): a refused / failed saved look: asked again after RetryMs, at most LookTriesMax times, then Failed (covered)
    void lookRefused(std::uint64_t nowMs,const char* d) noexcept {
        lookAsked_=false;++lookTries_;
        char w[160];std::snprintf(w,sizeof w,"action 17 0 refused (%s) try %d/%d",d,lookTries_,LookTriesMax);note(nowMs,w);
        if(lookTries_>=LookTriesMax){char f[160];std::snprintf(f,sizeof f,"saved look refused %d times: %s",LookTriesMax,d);fail(nowMs,f);}
    }
    LogFn log_;State st_=State::Off;std::uint64_t startMs_=0,swapAt_=0,lastTry_=0;std::uint32_t sessionAt_=0,lookAt_=0;int sex_=-1,runningAction_=0,runningArg_=0;bool lookAsked_=false,lookDone_=false,running_=false,flowLogged_=false,cardLogged_=false,revealWaitLogged_=false,placedSeen_=false,descentTried_=false,creatorStarted_=false,frontendSeen_=false,loadDone_=false;std::uint64_t creatorAt_=0,creatorEndMs_=0,revealAt_=0,leaveAt_=0,freemodeAt_=0;std::uint32_t downTicks_=0;bool transitionEnded_=false,frontendDone_=false,frontendLogged_=false,gameLaunch_=false;std::uint64_t fmEnterAt_=0;int mtLast_=-2;std::uint64_t mtSince_=0;
    bool mtPast_=false;int mtWait_=-3;std::uint64_t mtWaitSince_=0,sessionMs_=0;   // v1102
    bool fmRoute_=false,adoptTried_=false,readbackDone_=false;int lookTries_=0;std::uint64_t fmWaitAt_=0;   // v1105
    std::uint64_t blendLogAt_=0,lookDoneMs_=0;bool blendOkLogged_=false;static constexpr std::uint64_t BlendLogEveryMs=5000;   // v1108 (diagnostic)
    bool lookFinal_=false,blendSeen_=false,blendDropped_=false;   // v1170: the look ended "final"; the controller read the blend 1 after it; a later 0 logged
    // v1170 (diagnostic, read only): the head blend after the release, every BlendLogEveryMs for BlendFollowMs, logged on each change - does
    // the LONG descent switch's end (after the release) also turn it to 0? (H1: the same switch-end drop the short switch shows before it)
    static constexpr std::uint64_t BlendFollowMs=60000;int blendFollowLast_=-1;std::uint64_t blendFollowAt_=0;
    // v1093: has the transition word stood still for SweepFallbackMs (or, unreadable, SweepFallbackMs passed since Freemode)?
    bool stalled(int mt,std::uint64_t nowMs) noexcept {
        if(mt<0)return nowMs-fmEnterAt_>=SweepFallbackMs;
        if(mt!=mtLast_){if(mtLast_!=-2){char w[96];std::snprintf(w,sizeof w,"maintransition moved: transition %d -> %d",mtLast_,mt);note(nowMs,w);}mtLast_=mt;mtSince_=nowMs;return false;}
        return nowMs-mtSince_>=(mt==CharSelectMt?CharSelectSweepMs:SweepFallbackMs);   // v1104: T5 (character select) is a dead end
    }
    // v1104: the player restore once maintransition is gone without freemode (see CharSelectSweepMs)
    void restoreTick(PedOps& ops,std::uint64_t nowMs) {
        if(freemodeOwns_ || restoreDone_ || !(transitionEnded_ || gameLaunch_))return;
        if(!goneAt_){if(ops.transitionThreads()!=0)return;goneAt_=nowMs;nextRestore_=nowMs+RestoreDelayMs;note(nowMs,"maintransition gone without freemode: the player restore follows");return;}
        if(nowMs<nextRestore_)return;
        char d[300]{};char w[380];
        if(checkDue_) {   // the read-back RestoreCheckMs after an apply
            checkDue_=false;const int r=ops.playerRestore(false,d,int(sizeof d));
            std::snprintf(w,sizeof w,"player check after restore #%d -> %d (%s)",restoreApplied_,r,d);note(nowMs,w);
            if(r==1){restoreDone_=true;note(nowMs,"player restored: visible with control");return;}
            if(restoreApplied_>=RestoreTries){restoreDone_=true;note(nowMs,"player restore: still hidden or without control after the last try");return;}
        }
        const int r=ops.playerRestore(true,d,int(sizeof d));++restoreApplied_;
        std::snprintf(w,sizeof w,"player restore #%d -> %d (%s)",restoreApplied_,r,d);note(nowMs,w);
        nextRestore_=nowMs+RestoreCheckMs;checkDue_=true;
    }
    bool freemodeOwns_=false,restoreDone_=false,checkDue_=false;int restoreApplied_=0;std::uint64_t goneAt_=0,nextRestore_=0;   // v1104
    ::recomp::online_descent::Descent descent_{log_};
};
}
