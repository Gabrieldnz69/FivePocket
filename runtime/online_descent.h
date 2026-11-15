#pragma once
// online_descent.h (draft for v1082+, trainer-mods; lead 2026-11-08 02:0x: "drive maintransition's own descent behind our card, so G1332498
// goes 1 -> 2 -> 4 -> 0, selector re-enables the phone, and the later visit descends from the clouds"). Pure: the game is reached through
// DescentOps (the glue: natives + the global; the tests: a mock).
// The game's own sequence (maintransition, decoded 2026-11-08):
//  - ascent 0x51b (0x666-0x69e): SwitchMpFlag(1) (native 932fdb81), SWITCH_TO_MULTI_FIRSTPART(PLAYER_PED_ID, flags | 32 [| 128], type),
//    G1332498 = 1; once IS_SWITCH_TO_MULTI_FIRSTPART_FINISHED, 1 -> 2. On the boot card's Online path (go-online byte set) 0x51b skips the
//    ascent and writes 2 at once (0x57b-0x588): nothing is up in the sky on our path, which is why the probe never saw a switch.
//  - descent 0x449fa(0,1,0,1): 0 = nothing to do; 1 -> 2; in progress && FIRSTPART finished -> G1332498 = 4, then
//    SWITCH_TO_MULTI_SECONDPART(PLAYER_PED_ID) (0x44c02-0x44c0b); the switch ending -> cleanup (0x44d03-0x44d16: SET_PLAYER_INVINCIBLE(p, 0),
//    SwitchMpFlag(0), G1332498 = 0). selector 0x41c6 waits while G1332498 is 1 or 4 (0x45ed) before it re-enables the phone (0x45b4).
// This class: ascend() starts the game's ascent (behind the held card), inSky() once the first part has finished (the controller swaps and
// places the character there, then drops the card), descend() sends the second part; done() after the cleanup. Every phase is capped
// (PhaseMaxMs); a cap or a lost switch is the failure exit: STOP_PLAYER_SWITCH + the cleanup, failed() true, and the controller keeps the
// card up (never a reveal mid-switch). One log line per transition.
#include <cstdint>
#include <cstdio>

namespace recomp::online_descent {
struct DescentOps {
    virtual ~DescentOps()=default;
    virtual int mpState()=0;                          // G1332498 (-1 unreadable)
    virtual void putMpState(int v)=0;
    virtual bool switchOn()=0;                        // IS_PLAYER_SWITCH_IN_PROGRESS()
    virtual bool firstDone()=0;                       // IS_SWITCH_TO_MULTI_FIRSTPART_FINISHED()
    virtual void firstPart(unsigned flags,int type)=0;// SwitchMpFlag(1); SWITCH_TO_MULTI_FIRSTPART(PLAYER_PED_ID(), flags, type)
    virtual void secondPart()=0;                      // SWITCH_TO_MULTI_SECONDPART(PLAYER_PED_ID())
    virtual void stop()=0;                            // STOP_PLAYER_SWITCH()
    virtual void cleanup()=0;                         // SET_PLAYER_INVINCIBLE(PLAYER_ID(), 0); SwitchMpFlag(0)
};
constexpr std::uint64_t PhaseMaxMs=15000;
constexpr unsigned AscentFlags=32u;   // 0x67d-0x681: local14 (0 here) | 32; 128 only on 0x51b's param-0 branch
constexpr int AscentType=1;           // 0x646-0x664: local3 = 1 (long) unless GET_IDEAL_PLAYER_SWITCH_TYPE says otherwise
using LogFn=void(*)(const char*);
class Descent {
public:
    enum class St : std::uint8_t {Idle,Ascending,Sky,Descending,Done,Failed};
    explicit Descent(LogFn log) noexcept:log_(log) {}
    St state() const noexcept {return st_;}
    bool inSky() const noexcept {return st_==St::Sky;}
    bool done() const noexcept {return st_==St::Done;}
    bool failed() const noexcept {return st_==St::Failed;}
    // Start the game's ascent (behind the card). Refused (false) unless idle and no other switch is running.
    bool ascend(DescentOps& ops,std::uint64_t nowMs) noexcept {
        if(st_!=St::Idle || ops.switchOn())return false;
        ops.firstPart(AscentFlags,AscentType);ops.putMpState(1);
        enter(St::Ascending,nowMs,"SWITCH_TO_MULTI_FIRSTPART sent, G1332498 = 1");return true;
    }
    // From the sky: the second part onto the player ped as it now is (swapped, placed).
    bool descend(DescentOps& ops,std::uint64_t nowMs) noexcept {
        if(st_!=St::Sky)return false;
        ops.putMpState(4);ops.secondPart();                       // 0x44c02-0x44c0b, in this order
        enter(St::Descending,nowMs,"G1332498 = 4, SWITCH_TO_MULTI_SECONDPART sent");return true;
    }
    void tick(DescentOps& ops,std::uint64_t nowMs) noexcept {
        switch(st_) {
        case St::Ascending:
            if(!ops.switchOn() && nowMs-at_>2000){fail(ops,nowMs,"the ascent did not start (no switch in progress after 2 s)");return;}
            if(ops.switchOn() && ops.firstDone()){if(ops.mpState()==1)ops.putMpState(2);enter(St::Sky,nowMs,"FIRSTPART finished, G1332498 = 2 (in the clouds)");return;}
            if(nowMs-at_>=PhaseMaxMs)fail(ops,nowMs,"ascent cap");
            return;
        case St::Sky:
            if(!ops.switchOn()){fail(ops,nowMs,"the switch ended in the sky");return;}
            if(nowMs-at_>=PhaseMaxMs)fail(ops,nowMs,"sky cap (no descend)");
            return;
        case St::Descending:
            if(!ops.switchOn()){ops.cleanup();ops.putMpState(0);enter(St::Done,nowMs,"switch over: cleanup, G1332498 = 0");return;}
            if(nowMs-at_>=PhaseMaxMs)fail(ops,nowMs,"descent cap");
            return;
        case St::Idle: case St::Done: case St::Failed:return;
        }
    }
private:
    void enter(St s,std::uint64_t nowMs,const char* why) noexcept {
        st_=s;char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_DESCENT state=%s t+%llu (%s)",name(s),(unsigned long long)(at_?nowMs-at_:0),why);
        at_=nowMs?nowMs:1;if(log_)log_(l);
    }
    void fail(DescentOps& ops,std::uint64_t nowMs,const char* why) noexcept {   // the game's abort: stop, cleanup, the word back to 0
        if(ops.switchOn())ops.stop();
        ops.cleanup();ops.putMpState(0);enter(St::Failed,nowMs,why);
    }
    static const char* name(St s) noexcept {
        switch(s){case St::Idle:return "Idle";case St::Ascending:return "Ascending";case St::Sky:return "Sky";case St::Descending:return "Descending";
                  case St::Done:return "Done";case St::Failed:return "Failed";}
        return "?";
    }
    LogFn log_;St st_=St::Idle;std::uint64_t at_=0;
};
}
