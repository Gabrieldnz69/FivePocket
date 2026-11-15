#pragma once

// Update 1 (Sandbox, SB §1.2; ALLOCATION.md §2-3): the game window's play-mode step. private_trace_jni.cpp's runTrace calls
// apply() once per process, after the game code is opened and before its entry is called (so before the game thread can
// make any saves state). Pure: no Android, no lock, no allocation; the host test compiles it alone.
//
//   wanted   what the launcher named in the start request's "play_mode" extra (PlayMode.nativeValue):
//            -1 nothing named (a measurement script's start), 0 Story Mode, 1 Sandbox.
//   setMode  the game code's recomp_play_mode_v1 (dlsym), or null when the game code has no such export.
//   answer   receives what setMode answered; -100 when it was not called.
// Returns 0 when the start may go on, else the refusal (PlayMode.GATE_*; the game window shows PlayMode.refusalWord):
//   NotInCode  Sandbox was asked for and the game code has no play mode: it would play Sandbox on the story saves;
//   OtherMode  the game code answered -1: this process is already in the other mode, or its saves state already exists;
//   Answer     any other answer (-2: a mode the game code does not know).
// Story Mode with older game code (no export) goes on: Story is what that code always plays.

namespace recomp::play_mode_gate {

enum : int { Ok = 0, NotInCode = 1, OtherMode = 2, Answer = 3 };

inline int apply(int wanted, int (*setMode)(int), int* answer) noexcept {
    if (answer) { *answer = -100; }
    if (wanted < 0) { return Ok; }
    if (!setMode) { return wanted == 0 ? Ok : NotInCode; }
    const int said = setMode(wanted);
    if (answer) { *answer = said; }
    if (said == 0) { return Ok; }
    return said == -1 ? OtherMode : Answer;
}

} // namespace recomp::play_mode_gate
