#pragma once
#include "native_gamepad_state.h"
#include <chrono>
#include <mutex>

namespace recomp::input {
class GamepadSnapshot {
public:
    /** t-latch 2026-11-09 20:41: the game reads the pad more than once per frame (two reads 1 ms apart), so "until the next read" could end
     *  inside the same frame and the phone did not open; a latched press is now held at least this long from its down edge (the touch pad's
     *  MIN_PRESS_MS: two or more game frames at 20-30 fps) and until a read has shown it. */
    static constexpr std::int64_t MinHoldMs=100;
    static std::int64_t nowMs() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    void publish(bool connected,GamepadState next) {publish(connected,next,nowMs());}
    void publish(bool connected,GamepadState next,std::int64_t now) {
        std::lock_guard lock(mutex_);
        if(!connected)next={};
        // 2026-11-09 (lead; trainer-mods' t-onlshop-quick on v1146: a ~5 ms gamepad tap opened nothing): the game samples this level once
        // per frame (33-50 ms), so a press whose down and up both land between two reads was lost. With the latch on, every down edge is
        // remembered until the game has read the pad once (read() below).
        if(latch_ && connected && connected_) {
            const std::uint16_t down=static_cast<std::uint16_t>(next.buttons&~state_.buttons);
            for(unsigned b=0;b<16;++b)if(down&(1u<<b)){downAt_[b]=now;shown_&=static_cast<std::uint16_t>(~(1u<<b));}
            pressedSinceRead_|=down;
        }
        next.packet=state_.packet;
        if(connected!=connected_ || encodeGamepadState(next)!=encodeGamepadState(state_))++next.packet;
        state_=next;connected_=connected;
        if(!connected){pressedSinceRead_=0;shown_=0;}
    }
    int read(std::uint32_t user,GamepadState& result) {return read(user,result,nowMs());}
    int read(std::uint32_t user,GamepadState& result,std::int64_t now) {
        std::lock_guard lock(mutex_);
        if(user || !connected_)return 0;
        result=state_;
        if(latch_) {
            // A button pressed and already released is shown pressed until MinHoldMs after its down edge AND until a read has shown it;
            // then the release (the level). A quick tap = one press of >= MinHoldMs and one release. Sticks and triggers stay levels.
            shown_=static_cast<std::uint16_t>(shown_|(pressedSinceRead_&state_.buttons));   // a held press is seen as it is read
            std::uint16_t latched=static_cast<std::uint16_t>(pressedSinceRead_&~state_.buttons);
            for(unsigned b=0;b<16;++b) {
                const std::uint16_t bit=static_cast<std::uint16_t>(1u<<b);
                if(!(latched&bit))continue;
                if((shown_&bit) && now-downAt_[b]>=MinHoldMs){latched=static_cast<std::uint16_t>(latched&~bit);pressedSinceRead_=static_cast<std::uint16_t>(pressedSinceRead_&~bit);}
                else shown_=static_cast<std::uint16_t>(shown_|bit);
            }
            pressedSinceRead_=static_cast<std::uint16_t>(pressedSinceRead_&(latched|state_.buttons));
            result.buttons=static_cast<std::uint16_t>(result.buttons|latched);
            lastLatched_=latched;
            // the packet changes exactly when what the game sees changes (a latched press and its release are two changes)
            GamepadState key=result;key.packet=0;
            const auto bytes=encodeGamepadState(key);
            if(bytes!=lastDelivered_){lastDelivered_=bytes;++deliveredPacket_;}
            result.packet=deliveredPacket_;
        }
        return 1;
    }
    /** The latch switch (default off: the level as published). Changing it forgets any remembered press. */
    void latchPresses(bool on) {std::lock_guard lock(mutex_);latch_=on;pressedSinceRead_=0;shown_=0;}
    /** The buttons the last read showed only because of the latch (for a bounded log line); 0 when none. */
    std::uint16_t lastLatched() {std::lock_guard lock(mutex_);return lastLatched_;}
    void capabilities(GamepadState supported) {std::lock_guard lock(mutex_);capabilities_=supported;}
    int readCapabilities(std::uint32_t user,GamepadState& result) {
        std::lock_guard lock(mutex_);
        if(user || !connected_)return 0;
        result=capabilities_;return 1;
    }
private:
    std::mutex mutex_;
    bool connected_=false;
    GamepadState state_;
    GamepadState capabilities_;
    bool latch_=false;
    std::uint16_t pressedSinceRead_=0,lastLatched_=0,shown_=0;
    std::int64_t downAt_[16]{};
    std::uint32_t deliveredPacket_=0;
    std::array<std::uint8_t,16> lastDelivered_{};
};
}
