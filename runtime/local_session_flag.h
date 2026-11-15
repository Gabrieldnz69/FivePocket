#pragma once
// local_session_flag.h (v1013, trainer-mods; lead 12:4x after t-l1-20261107-1237: the local session runs, then activities / the creator /
// am_pi_menu IN that session). True only after online_l0.h's L1 called snSession::Host and it returned 1 in THIS process (Sandbox +
// diag-online-local + diag-online-l0 + diag-online-l1); false again at L1's close. Read by:
//  - trainer.h's gate: NETWORK_IS_GAME_IN_PROGRESS refuses every trainer action ("network-game") unless the network game is this local
//    session (a real online game can never set it: nothing but L1 writes it, and L1 needs the three developer switches in Sandbox);
//  - activities.h: the am_pi_menu probe index is accepted only while it is set.
#include <atomic>
namespace recomp::local_session {
inline std::atomic<bool> hostedByL1{false};
inline bool active() noexcept {return hostedByL1.load(std::memory_order_acquire);}
}
