#pragma once
// ocean-census (2026-11-13, PLAN U2; Discord D3 beach water + D2 grey plane, one reporter on an SD 7 Gen 4): fetch rounding
// for the 128x128 ocean simulation. v1700: DEFAULT OFF; v1740: default per DefaultOn below (kill switch diag-ocean-round-off). Evidence: local-analysis/update-1/subagents-2026-11-13/ocean-census.
//
// The census of our own captures (ocean_edge_census.py) shows that two of the six watertex simulation pixel shaders
// point-sample their 128x128 k_32_FLOAT maps exactly ON texel edges, in Xenos and in our renderer alike:
//   - the velocity update (key 168d4947239e3483): 9 height + 1 velocity point fetches per pixel at TEXCOORD1 + UpdateOffset.zw
//     (+-1/128), TEXCOORD1 = the pixel's own uv x/128 (D3D9 pixel centres; our half-pixel offset hp reproduces them) and
//     UpdateOffset.zw = floor(cells moved * 0.5) / 128 (the game's sub_824475B8): 163,840 fetches per frame at distance 0;
//   - the height integrate (bedba5134021b21d): its tiled-index gather fetches velocity and height at (L mod 128, L / 128)/128:
//     32,768 fetches per frame at distance 0.
// The texel a point fetch picks at an exact edge is decided by the GPU's interpolation / coordinate rounding: one ulp low
// gives texel x-1. The other four simulation shaders sample texel centres (+1/256) and are not touched.
//
// Switch files (files/private-trace/, read ONCE at initialize(); exactly one may be present, two or more = none applied):
//   diag-ocean-round      the two shaders re-translated with the pinned Xenia fetch translator's rounding offset, +1.5/1024
//                         texel on every fetch of these two shaders (the candidate fix; no other shader changes)
//   diag-ocean-bias-ulp   DIAGNOSTIC: tfetch2D samples one float ulp below its coordinate (emulates a GPU rounding low)
//   diag-ocean-bias-step  DIAGNOSTIC positive control: -1/256 texel on tfetch2D (a one-texel misread on any sampler with 8
//                         sub-texel bits: what a misread does to the simulation, whatever this GPU's rounding)
// RENDER_GENERIC_OCEAN_ROUND mode=off|round|bias-ulp|bias-step present=N applied=N words=N why=... says what happened.
// Not addressed (by design, not a fetch): the height integrate's own floor(TEXCOORD0 * 128) also sits on integers.
// Included (by ocean_round_load.inc) INSIDE generic_renderer.cpp's namespace recomp::gpu::generic: the namespace below is
// relative and the header includes nothing.

namespace ocean {

enum class Mode : unsigned char {Off,Round,BiasUlp,BiasStep};
struct Choice {Mode mode=Mode::Off;unsigned present=0;const char* why="-";};

// Which table the switch files select. More than one present is refused (an ambiguous experiment applies nothing).
constexpr Choice choose(bool round,bool biasUlp,bool biasStep) noexcept {
    Choice c{};
    c.present=unsigned(round)+unsigned(biasUlp)+unsigned(biasStep);
    if(c.present>1){c.why="conflict";return c;}
    c.mode=round?Mode::Round:biasUlp?Mode::BiasUlp:biasStep?Mode::BiasStep:Mode::Off;
    return c;
}

// v1740 (candidate lane; lead 2026-11-13 09:4x): round is the DEFAULT - no file needed - unless the kill switch diag-ocean-round-off
// exists (it always wins for round) or a diagnostic bias file asks for its own table instead; diag-ocean-round still names it
// explicitly. DefaultOn is the build's choice, set from the camera-matched Thor check (candidate-v1740 NOTES).
inline constexpr bool DefaultOn=true;
constexpr bool roundWanted(bool roundFile,bool killFile,bool biasUlp,bool biasStep) noexcept {
    return !killFile && (roundFile || (DefaultOn && !biasUlp && !biasStep));
}

constexpr const char* name(Mode m) noexcept {
    return m==Mode::Round?"round":m==Mode::BiasUlp?"bias-ulp":m==Mode::BiasStep?"bias-step":"off";
}

} // namespace ocean
