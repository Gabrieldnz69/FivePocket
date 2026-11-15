#pragma once
// menu_view.h (PROPOSED, lane upd1-creator-menu/creator-controller): the creator menu's own view of what is on screen - the rows,
// values, selection, help - kept by creator_menu.h, rebuilt only when something shown changes (revision + 1), and handed to the
// drawer as a menudraw::Page (lane upd1-creator-menu/shop-menu-look, menu_draw.h: Renderer::load / draw / close) once per game frame
// while the menu is open. The host tests read it too. Pure data: no natives, no guest memory, no allocation.
//
// Split of duties (so nothing is done twice):
//  - controller (creator_menu.h): reads the pad, plays the frontend sounds (PLAY_SOUND_FRONTEND, one call per event), calls
//    HIDE_HUD_AND_RADAR_THIS_FRAME once per open frame (as the shops do), moves the camera, applies the edits, and calls the drawer's
//    load() every frame from the open request on, draw() on open frames, close() on every exit;
//  - drawer (menu_draw.h): draws exactly the Page (banner, title, rows, selection bar, value arrows, help box, buttons) and hides its
//    own HUD components; it reads no input and plays no sound for the menu.
#include <cstdint>

namespace recomp::creatormenu {

constexpr int MaxRows=16;           // the top level has 14 rows; a category page has at most 2
constexpr int ValueChars=24;

struct RowView {
    const char* label="";           // static English text (never freed)
    char value[ValueChars]{};       // "" = no value column (a plain entry such as "Save & Exit")
    bool arrows=false;              // left / right change this value (draw "<  value  >" like the shop's selectable rows)
    bool enabled=true;              // false: drawn greyed, the cursor skips it
};

struct Menu {
    const char* title="";           // the banner text ("Character")
    const char* subtitle="";        // the category line ("HAIR", or "CHARACTER" at the top level)
    const char* help="";            // the help line under the list (static text)
    RowView rows[MaxRows]{};
    int count=0;                    // rows in use
    int selected=0;                 // the highlighted row (0 .. count-1)
    char counter[12]{};             // "3/14": the shop menus' position counter (selected + 1 / count)
    bool busy=false;                // a model change / apply is running: draw the list dimmed, ignore nothing else
    std::uint32_t revision=0;       // + 1 whenever any field above changes
};

} // namespace recomp::creatormenu
