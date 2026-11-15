// census_rect.h (bugfix-stability 2026-11-08; lead: dump a TILE, not a whole image): the census resolved-image dump's optional rectangle.
// files/private-trace/diag-census-resolved-rect = "x y w h" (or "x,y,w,h") in 1280x720 screen coordinates. Each listed resolved image is
// copied only inside that rectangle, scaled to the image's own size (a 640x360 target gets half) and clipped to its bounds; no file or a bad
// one = the whole image (v590 behaviour). Pure (host-tested: census_rect_test.cpp).
#pragma once
#include <cstdint>
#include <cstdio>

namespace recomp::census_rect {

constexpr unsigned ScreenW = 1280, ScreenH = 720;

struct Rect { unsigned x = 0, y = 0, w = 0, h = 0; bool whole = true; };

// The text of the rect file (nullptr = no file). Refused (whole image): fewer than four numbers, a zero or negative size.
inline Rect parse(const char* text) noexcept {
    Rect r; int x = -1, y = -1, w = -1, h = -1;
    if (!text) return r;
    if (std::sscanf(text, "%d %d %d %d", &x, &y, &w, &h) != 4 && std::sscanf(text, "%d,%d,%d,%d", &x, &y, &w, &h) != 4) return r;
    if (x < 0 || y < 0 || w <= 0 || h <= 0) return r;
    r.x = unsigned(x); r.y = unsigned(y); r.w = unsigned(w); r.h = unsigned(h); r.whole = false;
    return r;
}

// The rect for an image of width x height: scaled from screen coordinates, clipped. An empty intersection gives w = h = 0 (the caller
// skips the dump and logs it).
inline Rect forImage(const Rect& screen, unsigned width, unsigned height) noexcept {
    if (screen.whole || !width || !height) return Rect{0, 0, width, height, true};
    const auto sx = [&](unsigned v) { return unsigned((std::uint64_t(v) * width + ScreenW / 2) / ScreenW); };
    const auto sy = [&](unsigned v) { return unsigned((std::uint64_t(v) * height + ScreenH / 2) / ScreenH); };
    unsigned x0 = sx(screen.x), y0 = sy(screen.y), x1 = sx(screen.x + screen.w), y1 = sy(screen.y + screen.h);
    if (x1 > width) x1 = width;
    if (y1 > height) y1 = height;
    if (x0 >= x1 || y0 >= y1) return Rect{0, 0, 0, 0, false};
    return Rect{x0, y0, x1 - x0, y1 - y0, false};
}

} // namespace recomp::census_rect
