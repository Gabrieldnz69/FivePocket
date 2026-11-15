#pragma once
// lower_backdrop.h (v1103 + v1104 part 2, trainer-mods; lead 2026-11-08 relaying the owner: "on the character select screen, its blurred for the lower
// screen, when it should be just the background of the top screen without the character with the blur overlayed. maybe zoom like how you
// do for the phone on the lower screen").
// Today (t-onlcre74-20261108-1350, creator-start-lower.jpg): while the Online creator's frontend is up the frames pass the paused-frame
// rule (RENDER_MAPTAP resume=1 pill=0), so the lower screen shows the Resume backdrop = the WHOLE present image at 20x11 texels
// (map_tap_renderer.inc mapTapMirror): menu panels and the ped come through as coloured blobs.
// v1103: while the creator runs (online_mode controller: State::Creator once the creator was started, until it ends), the mirror's
// source is the creator's BACKDROP only: the left margin of the frame (x 0..192 of 1280; the creator's panels start at x 207, the
// "Ready?" ped at x 433), which shows nothing but the grey bird / bee pattern in every creator page (creator-start / -gender /
// -parents / -lifestyle / -go-online shots of that run). The strip (192 x 540, centred vertically) is blitted into the 640x360 first
// mirror level as Tiles side-by-side copies (every second one mirrored, so neighbours meet without a seam) at 2/3 scale, then blurred
// lighter than the pause look (down to BlurLast = 80x45 and back up to 320x180). The lower screen's cover-fit then shows the pattern
// zoomed about 2x, softened. Same pass, same images, fewer blits than the full-frame mirror (frame-neutral); the screen art is only
// sampled, never redrawn. Pure (host-tested): the tile geometry and the mode rule.
#include <array>
#include <atomic>
#include <cstdint>
#ifdef RECOMP_APP_PATH
#include <unistd.h>
#endif
namespace recomp::gpu::lower_backdrop {
// Set by the trainer glue after each controller tick (true: the creator was started and has not ended); read by the renderer thread.
inline std::atomic<bool> creatorScreen{false};
constexpr unsigned RefW=1280,RefH=720;         // the present image the creator layout was measured on
constexpr unsigned StripW=192,StripH=540;      // the backdrop-only left margin (panels from x 207 of 1280)
constexpr unsigned PanelX=207;                 // the first creator panel's left edge (heritage / lifestyle pages), for the test
constexpr unsigned Tiles=5;                    // 5 x 128 = the 640-wide first level
constexpr unsigned BlurLast=3;                 // v1103/v1104: the last down level (80x45); v1106: NOT used for the creator any more
// v1106 (owner: "on the character select screen, the blur tints the bottom screen, I actually don't think the blur is needed"): the
// creator's lower screen is the tiled backdrop itself - the 640x360 first level straight to the lower screen, no down / up blur chain, so
// the bird / bee lines keep the top's colours (the blur averaged them into a lighter grey).
constexpr bool CreatorBlur=false;
struct Rect {std::int32_t x0,y0,x1,y1;};       // blit offsets: x1 < x0 = mirrored
struct Tile {Rect src,dst;};
// The strip in a pw x ph present image, tiled over a dw x dh first level.
inline std::array<Tile,Tiles> tiles(unsigned pw,unsigned ph,unsigned dw,unsigned dh) noexcept {
    std::array<Tile,Tiles> t{};
    const std::int32_t sw=std::int32_t(std::uint64_t(StripW)*pw/RefW),sh=std::int32_t(std::uint64_t(StripH)*ph/RefH);
    const std::int32_t sy=(std::int32_t(ph)-sh)/2;
    for(unsigned k=0;k<Tiles;++k) {
        const std::int32_t a=std::int32_t(std::uint64_t(k)*dw/Tiles),b=std::int32_t(std::uint64_t(k+1)*dw/Tiles);
        t[k].src={0,sy,sw,sy+sh};
        t[k].dst=(k&1u)?Rect{b,0,a,std::int32_t(dh)}:Rect{a,0,b,std::int32_t(dh)};
    }
    return t;
}
// v1109 (owner, the "character select screen" of his first request; t-lowerev106 / t-cloudcensus106: a later visit shows maintransition's MP
// character select - the same bird / bee backdrop and the same left margin, the menu from x 207 of 1280 - and the lower screen mirrored it blurred):
// the backdrop source also while maintransition's UI word G1344033 reads CharSelectUi (ACTIVATE_FRONTEND_MENU FE_MENU_VERSION_MP_CHARACTER_SELECT).
constexpr int CharSelectUi=4;
constexpr bool chooserUi(int ui) noexcept {return ui==CharSelectUi;}
// The backdrop source only on the Resume look during the creator (the Loading screen keeps its own full-frame blur).
constexpr bool use(bool resume,bool creator) noexcept {return resume && creator;}

// ---- v1104 part 2 (owner via the lead 2026-11-08, corrected: "I actually do want the plain black screen during that loading screen"):
// while one of the game's loading cards is up the lower screen is PLAIN BLACK: no mirror, no blur, no HUD. The card is the loading
// screen's state word 0x83895C5C == CardWord (t-onlcre74-20261108-1350 probe: 1 -> 3 (legal pages) -> 5 at the first key-art card, 5
// while it is held, 0 when it goes; t-onlrev82-20261108-1409: 5 through the held card, 0 at the CloudsDown descent). Both present paths
// stamp the time of a frame drawn while it reads 5: the presenter's CPU frames (direct_present.h presentCpu: the boot cards are CPU
// frames, RENDER_DIRECT_PRESENT gpu=0 cpu=all) and the renderer's present (map_tap_renderer.inc, a card drawn by the GPU path). The lower
// screen (resume_screen.inc) shows black while the stamp is fresh (CardStaleMs): it wins over the Resume / Loading looks and the HUD.
// One guest load and one atomic store per frame; nothing drawn (frame-neutral).
constexpr std::uint32_t CardWordAddr=0x83895C5Cu,CardWord=5u;
constexpr std::int64_t CardStaleMs=250;
// v1109 (owner, v1106 standing: "the only screen that shows on the lower screen is the wall of text with disclosures right before the main loading
// screen"): black for the WHOLE boot loading screen, not only the key-art card: the word reads 1 -> 3 (the legal pages) -> 5 (cards) and 0 outside it
// (t-onlcre74-20261108-1350, t-onlrev82-20261108-1409 probes), so any word 1..5 is the loading screen.
constexpr std::uint32_t LoadingWordMin=1u,LoadingWordMax=5u;
constexpr bool cardShown(std::uint32_t word) noexcept {return word>=LoadingWordMin && word<=LoadingWordMax;}
constexpr bool cardFresh(std::int64_t nowMs,std::int64_t atMs) noexcept {return atMs>0 && nowMs>=atMs && nowMs-atMs<CardStaleMs;}
inline std::atomic<std::int64_t> cardMs{0};   // steady-clock ms of the last frame drawn while the card word read CardWord

// ---- v1106 part 3 (owner: "there's a slight delay before the upper screen has the spinning wheel, and the screen is black in the meantime.
// can you pause the spinning wheel on the bottom to appear on the exact frame it appears on the top screen"). After the creator's Go Online
// the lower screen showed its Loading look (pill + spinner) at once (t-onlcre104: RENDER_LOADING from 17:15:08.07) while the top was black
// before the game's busy spinner came up. In Online mode the lower screen's Loading spinner now FOLLOWS the game's own: drawn only while the
// game's busy-spinner byte is set - the byte BUSYSPINNER_IS_ON returns (b8b3a5d0 -> sub_82D60778 -> sub_8257AF88: lis -31863; lbz 15668 =
// 0x83893D34), read by the lower screen on each of its frames; without it the Loading look is its backdrop alone (no pill, no spinner).
// Story / Sandbox keep the v685 Loading screen as it was (the switch has no game spinner there).
constexpr std::uint32_t SpinnerByteAddr=0x83893D34u;
inline std::atomic<bool> onlineMode{false};   // set by the trainer glue while the Online mode controller runs
constexpr bool lowerSpinner(bool online,bool loading,bool gameSpinner) noexcept {return loading && (!online || gameSpinner);}
// v1109 (t-onlshow106: the byte stayed 0 while the top showed the spinner; t-spincensus106b: those top frames are the game's loading-screen quad
// alone, map_tap.h loadingScreenFrame): the renderer stamps every such frame here; the lower screen's "game spinner" = the byte OR a stamp younger
// than SpinnerStaleMs (about two top frames at 30 fps), so the lower spinner comes and goes with the top's frames.
inline std::atomic<std::int64_t> spinnerMs{0};
constexpr std::int64_t SpinnerStaleMs=70;
constexpr bool spinnerFresh(std::int64_t nowMs,std::int64_t atMs) noexcept {return atMs>0 && nowMs>=atMs && nowMs-atMs<SpinnerStaleMs;}

// ---- v1106 A/B switch (lead 2026-11-08 ~15:10: t-onlshow-20261108-1451, v1104, froze in fm_intro - RENDER_DIAG_FREEZEWATCH wait=3 (waitSlot)
// past its 10 s timeout with the slot fences signalled; the first build with this file's lower-screen code, so it is a suspect):
// files/private-trace/diag-lower-v1104-off (read once, at the first use) turns EVERY lower-screen change of v1104 / v1106 off - the creator
// backdrop source, the loading-card stamps / black, the Online spinner rule (the lower screen then behaves as v1101's).
#ifdef RECOMP_APP_PATH
inline bool off() noexcept {static const bool o=::access(RECOMP_APP_PATH("/private-trace/diag-lower-v1104-off"),F_OK)==0;return o;}
#endif
}
