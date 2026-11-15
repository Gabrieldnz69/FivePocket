#pragma once
// online_curtain_flag.h (v1043, trainer-mods): the three flags between online_l0.h's clean leave (the hook thread) and online_curtain.h
// (the trainer tick). armed: the curtain is on (Online mode or diag-online-curtain); leaveAsked: L3a wants the screen covered before it
// ends the session; leaveCovered: the screen is out with the spinner up. Nothing else writes them.
#include <atomic>
namespace recomp::online_curtain {
inline std::atomic<bool> armed{false},leaveAsked{false},leaveCovered{false};
// v1056: the curtain covers the screen now (trainer.h's gate treats it as our own fade, as the place transition's)
inline std::atomic<bool> covering{false};
// v1066: the OnlineMode controller holds the local session while the first-visit creator runs (the creator route refuses a network
// game in progress, and its own maintransition must not meet our session); online_l0.h's WaitStart waits while it is set.
inline std::atomic<bool> holdSession{false};
}
