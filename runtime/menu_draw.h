#pragma once
// menu_draw.h (Update 1, Sandbox creator menu; lane upd1-creator-menu/shop-menu-look, 2026-11-07). The game's OWN story-mode shop menu
// look, drawn by the game library through the trainer's native path (trainer::Natives, script thread, once per game frame). Pure: no
// guest memory, no files, no platform calls, no allocation (fixed arrays). Notes and evidence: ../NOTES.md.
//
// What it reproduces (OBSERVED: the shared menu library compiled into hairdo_shop_sp / clothes_shop_sp / tattoo_shop, and the same
// library in am_pi_menu; offsets are hairdo_shop_sp code offsets, decoded with tools/dis2.py, and the whole draw was RUN on the host by
// tools/xsc_emu.py + tools/shop_reference.py: tests/shop_reference.cmds is that run's render stream, which this file must match):
//   draw 0x6d37 (per frame) - SET_SCRIPT_GFX_ALIGN('L','T') + PARAMS(-0.05,-0.05,0,0); header DRAW_SPRITE(dict,dict) 0.225 wide, height =
//   texture height / screen height (0x9bfb); subtitle DRAW_RECT 0.225 x 0.034722 black 204; title text font 0 scale 0.35 HUD colour 1
//   at (+0.00390625, +0.00416664); "CM_ITEM_COUNT" (~1~/~1~) right-aligned when more than 10 items; one DRAW_RECT per visible row in HUD
//   colour 140; the "shop_arrows_upANDdown" bar when more than 10 items; the description box (lines x character height + 13 + 5 x
//   (lines-1) units of 0.00138888) in HUD colour 140 with font 0 / 0.35 / leading 2 text; the instructional_buttons movie (0x938f);
//   then per visible row: selection DRAW_RECT in HUD colour 1, text (selected 0,0,0,204 = FLOOR(255*0.8); else HUD colour 1;
//   disabled 155,155,155,255), right-justified value column with "arrowleft" / "arrowright" (CommonMenu) on the selected row.
//   THEFEED_SET_SCRIPTED_MENU_HEIGHT(bottom), HIDE_HUD_COMPONENT_THIS_FRAME 10, 6, 7, 9, 8, RESET_SCRIPT_GFX_ALIGN.
// What it does NOT do: prices, tick boxes, item icons, separators, per-row heights, custom column x, centred columns, the timed
// description (all supported by the library, unused by the creator). Two columns only: a left label and a right-justified value.
//
// EXTENDED (lane upd1-imenu/draw-glue, 2026-11-07) for the disc's interaction menu (am_pi_menu), which compiles in a variant of the same
// library (am_pi_menu code offsets: draw 0x21175, decoded and RUN on the host by tools/imi_reference.py): a Page may carry a Style
// (nullptr = the shop look above, byte-for-byte unchanged): the title row hidden by the keyword "HIDE" (no subtitle bar, title or
// CM_ITEM_COUNT counter: 0x211e7 / 0x2123e jump past 0x2141f..0x21480), no banner (header graphic flag 51.185 = 0: the caller draws its
// own header), the list origin y (global 51.184 = 0.05 + 3 rows, am_pi_menu 0x2a410), at most N visible rows (f3990 = 7, 0x259d8), the
// page colour overrides (f6184.. subtitle 0x2128d, f6189.. rows 0x2148e, f6194.. arrows bar 0x215b8, f6199.. description 0x21822;
// am_pi_menu 0x259b5 sets 0,71,133 with alpha 255 / 191 / 191 / 191), and the CommonMenu request repeated every frame (0x2a173).
// A Row may hold GXT label KEYS (drawn with BEGIN_TEXT_COMMAND_DISPLAY_TEXT(key): the game's own text, every disc language) with one
// integer component (PIM_CASH + ADD_TEXT_COMPONENT_INTEGER), and a value cell enabled separately from its label (the disc greys
// PIM_QGPS0 next to a white PIM_TQGPS). The help may be a key too. Icon cells (shop_Lock, ticks) are NOT supported: no row the
// replica ships has one (the lock is an Online rank lock).
// Frame rate: nothing at all while closed. While open: draw calls only (about 11 per row); every query (screen size, texture sizes, HUD
// colours, character height, text widths, description line count) is cached and re-asked only when its input changes (a row's value
// text, the help text, the counter, the header). Strings go to the per-tick guest scratch through one intern table per frame.
#include "trainer.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace recomp::menudraw {
using trainer::Natives;
using trainer::joaat;
using trainer::bits;
using trainer::asFloat;

struct Named {const char* name;std::uint32_t hash;};
namespace n {
constexpr std::uint32_t DrawRect=joaat("DRAW_RECT"),DrawSprite=joaat("DRAW_SPRITE"),
    TextFont=joaat("SET_TEXT_FONT"),TextScale=joaat("SET_TEXT_SCALE"),TextColour=joaat("SET_TEXT_COLOUR"),TextWrap=joaat("SET_TEXT_WRAP"),
    TextJust=joaat("SET_TEXT_JUSTIFICATION"),TextCentre=joaat("SET_TEXT_CENTRE"),TextLeading=joaat("SET_TEXT_LEADING"),
    TextShadow=joaat("SET_TEXT_DROPSHADOW"),TextEdge=joaat("SET_TEXT_EDGE"),
    BeginText=joaat("BEGIN_TEXT_COMMAND_DISPLAY_TEXT"),EndText=joaat("END_TEXT_COMMAND_DISPLAY_TEXT"),
    AddLiteral=joaat("ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME"),AddInt=joaat("ADD_TEXT_COMPONENT_INTEGER"),
    BeginWidth=joaat("BEGIN_TEXT_COMMAND_GET_SCREEN_WIDTH_OF_DISPLAY_TEXT"),EndWidth=joaat("END_TEXT_COMMAND_GET_SCREEN_WIDTH_OF_DISPLAY_TEXT"),
    BeginLines=joaat("BEGIN_TEXT_COMMAND_GET_NUMBER_OF_LINES_FOR_STRING"),EndLines=joaat("END_TEXT_COMMAND_GET_NUMBER_OF_LINES_FOR_STRING"),
    CharHeight=joaat("GET_RENDERED_CHARACTER_HEIGHT"),HudColour=joaat("GET_HUD_COLOUR"),TexRes=joaat("GET_TEXTURE_RESOLUTION"),
    ScreenRes=joaat("GET_SCREEN_RESOLUTION"),Widescreen=joaat("GET_IS_WIDESCREEN"),
    RequestTxd=joaat("REQUEST_STREAMED_TEXTURE_DICT"),TxdLoaded=joaat("HAS_STREAMED_TEXTURE_DICT_LOADED"),
    ReleaseTxd=joaat("SET_STREAMED_TEXTURE_DICT_AS_NO_LONGER_NEEDED"),
    RequestMovie=joaat("REQUEST_SCALEFORM_MOVIE"),MovieLoaded=joaat("HAS_SCALEFORM_MOVIE_LOADED"),
    ReleaseMovie=joaat("SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED"),BeginMethod=joaat("BEGIN_SCALEFORM_MOVIE_METHOD"),
    EndMethod=joaat("END_SCALEFORM_MOVIE_METHOD"),ParamInt=joaat("SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT"),
    ParamFloat=joaat("SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT"),BeginSfText=joaat("BEGIN_TEXT_COMMAND_SCALEFORM_STRING"),
    EndSfText=joaat("END_TEXT_COMMAND_SCALEFORM_STRING"),DrawMovieFull=joaat("DRAW_SCALEFORM_MOVIE_FULLSCREEN"),
    GroupButton=joaat("GET_CONTROL_GROUP_INSTRUCTIONAL_BUTTON"),
    Align=joaat("SET_SCRIPT_GFX_ALIGN"),AlignParams=joaat("SET_SCRIPT_GFX_ALIGN_PARAMS"),AlignReset=joaat("RESET_SCRIPT_GFX_ALIGN"),
    HudPos=joaat("SET_HUD_COMPONENT_POSITION"),HudReset=joaat("RESET_HUD_COMPONENT_VALUES"),FeedHeight=joaat("THEFEED_SET_SCRIPTED_MENU_HEIGHT"),
    HideHud=joaat("HIDE_HUD_COMPONENT_THIS_FRAME"),Sound=joaat("PLAY_SOUND_FRONTEND");
}
inline constexpr Named Used[]{
    {"DRAW_RECT",n::DrawRect},{"DRAW_SPRITE",n::DrawSprite},{"SET_TEXT_FONT",n::TextFont},{"SET_TEXT_SCALE",n::TextScale},
    {"SET_TEXT_COLOUR",n::TextColour},{"SET_TEXT_WRAP",n::TextWrap},{"SET_TEXT_JUSTIFICATION",n::TextJust},{"SET_TEXT_CENTRE",n::TextCentre},
    {"SET_TEXT_LEADING",n::TextLeading},{"SET_TEXT_DROPSHADOW",n::TextShadow},{"SET_TEXT_EDGE",n::TextEdge},
    {"BEGIN_TEXT_COMMAND_DISPLAY_TEXT",n::BeginText},{"END_TEXT_COMMAND_DISPLAY_TEXT",n::EndText},
    {"ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME",n::AddLiteral},{"ADD_TEXT_COMPONENT_INTEGER",n::AddInt},
    {"BEGIN_TEXT_COMMAND_GET_SCREEN_WIDTH_OF_DISPLAY_TEXT",n::BeginWidth},{"END_TEXT_COMMAND_GET_SCREEN_WIDTH_OF_DISPLAY_TEXT",n::EndWidth},
    {"BEGIN_TEXT_COMMAND_GET_NUMBER_OF_LINES_FOR_STRING",n::BeginLines},{"END_TEXT_COMMAND_GET_NUMBER_OF_LINES_FOR_STRING",n::EndLines},
    {"GET_RENDERED_CHARACTER_HEIGHT",n::CharHeight},{"GET_HUD_COLOUR",n::HudColour},{"GET_TEXTURE_RESOLUTION",n::TexRes},
    {"GET_SCREEN_RESOLUTION",n::ScreenRes},{"GET_IS_WIDESCREEN",n::Widescreen},{"REQUEST_STREAMED_TEXTURE_DICT",n::RequestTxd},
    {"HAS_STREAMED_TEXTURE_DICT_LOADED",n::TxdLoaded},{"SET_STREAMED_TEXTURE_DICT_AS_NO_LONGER_NEEDED",n::ReleaseTxd},
    {"REQUEST_SCALEFORM_MOVIE",n::RequestMovie},{"HAS_SCALEFORM_MOVIE_LOADED",n::MovieLoaded},
    {"SET_SCALEFORM_MOVIE_AS_NO_LONGER_NEEDED",n::ReleaseMovie},{"BEGIN_SCALEFORM_MOVIE_METHOD",n::BeginMethod},
    {"END_SCALEFORM_MOVIE_METHOD",n::EndMethod},{"SCALEFORM_MOVIE_METHOD_ADD_PARAM_INT",n::ParamInt},
    {"SCALEFORM_MOVIE_METHOD_ADD_PARAM_FLOAT",n::ParamFloat},{"BEGIN_TEXT_COMMAND_SCALEFORM_STRING",n::BeginSfText},
    {"END_TEXT_COMMAND_SCALEFORM_STRING",n::EndSfText},{"DRAW_SCALEFORM_MOVIE_FULLSCREEN",n::DrawMovieFull},
    {"GET_CONTROL_GROUP_INSTRUCTIONAL_BUTTON",n::GroupButton},{"SET_SCRIPT_GFX_ALIGN",n::Align},
    {"SET_SCRIPT_GFX_ALIGN_PARAMS",n::AlignParams},{"RESET_SCRIPT_GFX_ALIGN",n::AlignReset},{"SET_HUD_COMPONENT_POSITION",n::HudPos},
    {"RESET_HUD_COMPONENT_VALUES",n::HudReset},{"THEFEED_SET_SCRIPTED_MENU_HEIGHT",n::FeedHeight},
    {"HIDE_HUD_COMPONENT_THIS_FRAME",n::HideHud},{"PLAY_SOUND_FRONTEND",n::Sound}};
inline constexpr std::uint32_t UsedNatives[]{
    n::DrawRect,n::DrawSprite,n::TextFont,n::TextScale,n::TextColour,n::TextWrap,n::TextJust,n::TextCentre,n::TextLeading,n::TextShadow,
    n::TextEdge,n::BeginText,n::EndText,n::AddLiteral,n::AddInt,n::BeginWidth,n::EndWidth,n::BeginLines,n::EndLines,n::CharHeight,
    n::HudColour,n::TexRes,n::ScreenRes,n::Widescreen,n::RequestTxd,n::TxdLoaded,n::ReleaseTxd,n::RequestMovie,n::MovieLoaded,
    n::ReleaseMovie,n::BeginMethod,n::EndMethod,n::ParamInt,n::ParamFloat,n::BeginSfText,n::EndSfText,n::DrawMovieFull,n::GroupButton,
    n::Align,n::AlignParams,n::AlignReset,n::HudPos,n::HudReset,n::FeedHeight,n::HideHud,n::Sound};
static_assert(std::size(Used)==std::size(UsedNatives));

// ---- the shop library's layout (hairdo_shop_sp offsets) ----
constexpr float MenuX=0.05f,MenuY=0.05f;          // clear 0x10789: 0x10b15 / 0x10b1d (globals 13239 / 13240)
constexpr float Width=0.225f;                      // 0x6e1e, 0x7100, 0x97a7
constexpr float RowH=0.034722f;                    // 0x6dbf, 0x70d9, 0x76d1 (25/720)
constexpr float TextInset=0.0046875f;              // 0x979e, 0x7283, 0x7778 (6/1280)
constexpr float TitleX=0.00390625f,TitleY=0.00416664f;   // 0x6fcb, 0x6fd7 (count: 0x7007, 0x7028)
constexpr float RowTextY=0.00277776f;              // 0x7764 (and the arrow y: 0x7e85 subtracts it again)
constexpr float ValueNudge=0.00078125f;            // 0x7d5e (1/1280)
constexpr float DescGap=0.00277776f,DescUnit=0.00138888f;  // 0x7278; 0x7435 (x13), 0x7441 (x5 per extra line)
constexpr float TextScaleY=0.35f;                  // 0x9786, 0x90c5, 0x96c1
constexpr int Font=0,DescLeading=2;                // 0x9780; 0x96ca
constexpr int HudText=1,HudMenuBg=140;             // HUD_COLOUR ids: 0x9773 (text, selection, arrow bar), 0x7079 (rows, description)
constexpr int BlackAlpha=204;                      // 0x6e81: subtitle bar and arrow bar (0,0,0,204)
constexpr int SelectedAlpha=204;                   // 0x9073: SET_TEXT_COLOUR(0,0,0,FLOOR(255*0.8))
constexpr int DisabledGrey=155;                    // 0x904f: 155,155,155,255
constexpr int MaxVisible=10;                       // clear 0x10a60 (f3990)
constexpr int AlignLeft='L',AlignTop='T',AlignBottom='B';  // 0x6d5d (76,84), 0x95a9 (76,66)
constexpr float AlignOffset=-0.05f;                // 0x6d64
constexpr int SubtitleHudComponent=15;constexpr float SubtitleShiftY=-0.0375f;  // 0x95c2 (once), reset 0x6947
constexpr int HideComponents[]{10,6,7,9,8};        // 0x8eba..0x8ed2 (10 because the shops pass draw p2 = 1: 0x46f2)
constexpr int ButtonBackground[4]{0,0,0,80};       // 0x94fc
constexpr const char* CommonMenu="CommonMenu";     // 0x9bf4
constexpr const char* ArrowLeft="arrowleft";       // sprite 26 (0x9b06)
constexpr const char* ArrowRight="arrowright";     // sprite 27 (0x9b1b)
constexpr const char* ArrowsBar="shop_arrows_upANDdown";  // 0x71dd
constexpr const char* ButtonsMovie="instructional_buttons"; // 0x1a064
constexpr const char* CountLabel="CM_ITEM_COUNT";  // 0x7010
constexpr const char* Literal="STRING";            // the game's "~a~" label, filled by ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME
// The shop sounds (hairdo_shop_sp 0xc441.., am_pi_menu for left / right); all HUD_FRONTEND_DEFAULT_SOUNDSET.
enum class Sound {UpDown,LeftRight,Select,Back,Error};
constexpr const char* SoundSet="HUD_FRONTEND_DEFAULT_SOUNDSET";
constexpr const char* soundName(Sound s) noexcept {
    switch(s){case Sound::UpDown:return "NAV_UP_DOWN";case Sound::LeftRight:return "NAV_LEFT_RIGHT";case Sound::Select:return "SELECT";
              case Sound::Back:return "BACK";case Sound::Error:return "ERROR";}
    return "SELECT";
}
// Header banners the shop scripts request (OBSERVED string tables; dictionary name == texture name, 0x1074e(1, d, d)).
constexpr const char* HeaderBarber="ShopUI_Title_Barber";        // hairdo_shop_sp, Herr Kutz: hair, beard, eyebrows, make-up
constexpr const char* HeaderClothes="ShopUI_Title_MidFashion";   // clothes_shop_sp, Suburban: tops, legs, shoes, accessories
constexpr const char* HeaderMasks="ShopUI_Title_Movie_Masks";    // clothes_shop_sp (masks shop): masks
constexpr const char* HeaderTattoos="ShopUI_Title_Tattoos";      // tattoo_shop

struct Row {
    const char* label;const char* value=nullptr;bool cycles=false;bool enabled=true;
    bool labelKey=false,valueKey=false;      // the text is a GXT label key (BEGIN_TEXT_COMMAND_DISPLAY_TEXT(key)), not a literal
    bool valueHasInt=false;int valueInt=0;   // one ADD_TEXT_COMPONENT_INTEGER after a value key (PIM_CASH: "$~1~")
    signed char valueOn=-1;                  // the value cell's own enabled flag: -1 = as the label, 0 grey, 1 normal
    bool valueEnabled() const noexcept {return valueOn<0?enabled:valueOn!=0;}
};
// The interaction menu's variant of the library look (am_pi_menu); default-constructed = the shop look.
struct Style {
    float menuY=MenuY;int maxVisible=MaxVisible;
    bool hideTitle=false;                    // "HIDE": no subtitle bar / title / counter; rows start at menuY
    bool banner=true;                        // the library draws Page::header itself (false: the caller draws its own header)
    bool requestEveryFrame=false;            // REQUEST_STREAMED_TEXTURE_DICT("CommonMenu") every frame, as am_pi_menu 0x2a173 does
    const int* subtitleBg=nullptr;const int* rowBg=nullptr;const int* arrowsBg=nullptr;const int* descBg=nullptr;   // rgba overrides
};
inline constexpr Style ShopStyle{};
// icon: the movie's button id the shops pass (30 select, 31 back, 35 zoom: hairdo 0xe4ad..); group >= 0: icon =
// GET_CONTROL_GROUP_INSTRUCTIONAL_BUTTON(group, control, 1) asked once per button set (hairdo 0xe4c3: (2, 18, 1) for ITEM_MOV_CAM).
struct Button {int icon;const char* label;int group=-1;int control=0;};
struct Page {
    const char* header=HeaderBarber;const char* title=nullptr;const Row* rows=nullptr;int rowCount=0;int selected=0;
    const char* help=nullptr;const Button* buttons=nullptr;int buttonCount=0;
    const Style* style=nullptr;bool helpKey=false;   // EXTENDED: nullptr = ShopStyle; helpKey: help is a GXT key
    bool titleKey=false;                             // EXTENDED: the title is a GXT key (am_pi_menu sub pages: PIM_TITLE2)
};
using LogFn=void(*)(const char*);
using HasFn=bool(*)(std::uint32_t hash);

constexpr std::uint32_t fnv(const char* s) noexcept {std::uint32_t h=2166136261u;if(s)for(;*s;++s){h^=std::uint8_t(*s);h*=16777619u;}return h;}

class Renderer {
public:
    static constexpr int MaxRows=48,MaxButtons=6,MaxInterned=40;
    void setLog(LogFn f) noexcept {log_=f;}
    void setNativeCheck(HasFn f) noexcept {has_=f;}
    bool ready() const noexcept {return ready_;}
    bool open() const noexcept {return open_;}
    int top() const noexcept {return top_;}
    int textMisses() const noexcept {return textMisses_;}
    std::uint32_t movieHandle() const noexcept {return movie_;}   // PROPOSED (lane upd1-imenu/diag-run, E2): the buttons movie's handle, 0 before the request

    // Every frame while the menu should show; false (nothing drawn) until the assets are in. Requests are repeated each frame until
    // loaded, as the shops' 0x19fd0 does. A header change keeps the old banner on screen until the new dictionary is in.
    bool load(Natives& nat,const char* header,const Style* style=nullptr) noexcept {
        if(unavailable_)return false;
        const Style& st=style?*style:ShopStyle;
        if(!open_){
            if(has_)for(const auto& u:Used)if(!has_(u.hash)){unavailable_=true;logf("RENDER_ONLINE menu=off reason=missing-native name=%s",u.name);return false;}
            *this=Renderer{log_,has_};open_=true;logf("RENDER_ONLINE menu=loading header=%s",header?header:"-");
        }
        frame_.reset();
        if(header && header[0]){
            if(std::strcmp(header,header_)==0){if(pending_[0]){call(nat,n::ReleaseTxd,{str(nat,pending_)});pending_[0]=0;}}
            else if(std::strcmp(header,pending_)!=0){if(pending_[0])call(nat,n::ReleaseTxd,{str(nat,pending_)});copy(pending_,header);}
        }
        if(pending_[0] && requestTxd(nat,pending_)){         // the new banner is in: swap, release the old one
            if(header_[0])call(nat,n::ReleaseTxd,{str(nat,header_)});
            copy(header_,pending_);pending_[0]=0;headerMeasured_=false;
        }
        if(ready_){if(st.requestEveryFrame)(void)requestTxd(nat,CommonMenu);return true;}   // (shop: nothing re-requested once in)
        const bool menuIn=requestTxd(nat,CommonMenu);
        if(!movie_){const std::uint32_t m=str(nat,ButtonsMovie);std::uint32_t out[3]{};if(m && nat.call(n::RequestMovie,&m,1,out))movie_=out[0];}
        std::uint32_t out[3]{};const std::uint32_t mv=movie_;
        const bool movieIn=movie_ && nat.call(n::MovieLoaded,&mv,1,out) && out[0];
        // v977: no header = online's property menu (am_mp_property_ext 0x9aeb never calls the banner setter): the same library draw with
        // the banner height 0 (the shop library run with the banner off: every element moves up by exactly the banner's height).
        // v989: + draw-glue's Style without a banner (the Interaction Menu's HIDE page) is ready without a header too.
        const bool headerless=!(header && header[0]) && !pending_[0];
        if(menuIn && movieIn && (header_[0] || headerless || !st.banner)){measureFixed(nat);ready_=true;logf("RENDER_ONLINE menu=ready header=%s",header_[0]?header_:"-");}
        return ready_;
    }
    // One frame of the menu (call after load() returned true, same tick).
    void draw(Natives& nat,const Page& p) noexcept {
        if(!ready_)return;
        const Style& st=p.style?*p.style:ShopStyle;
        const int maxV=st.maxVisible<1?1:st.maxVisible;
        const float menuY=st.menuY;
        const int count=p.rowCount<0?0:(p.rowCount>MaxRows?MaxRows:p.rowCount);
        const int sel=count?(p.selected<0?0:(p.selected>=count?count-1:p.selected)):0;
        if(sel<top_)top_=sel;else if(sel>=top_+maxV)top_=sel-maxV+1;   // = SET_CURRENT_MENU_ITEM 0xd57a for all-selectable rows
        if(top_>count-maxV)top_=count>maxV?count-maxV:0;
        if(!headerMeasured_)measureHeader(nat);
        for(int r=count;r<MaxRows;++r)measured_[r]=false;

        alignMenu(nat);
        const float hdrH=st.banner?headerH_:0.0f;                                                                       // l61 (0 without a banner)
        if(st.banner && header_[0])sprite(nat,header_,header_,MenuX+Width*0.5f,menuY+(hdrH-0.0f)*0.5f,Width,hdrH-0.0f,255,255,255,255);  // 0x6e48
        float y=menuY;                                                                                                   // l55 (HIDE: menuY)
        const bool many=count>maxV;
        if(!st.hideTitle){
            if(st.subtitleBg)rect(nat,MenuX,menuY+hdrH,Width,RowH,st.subtitleBg[0],st.subtitleBg[1],st.subtitleBg[2],st.subtitleBg[3]);
            else rect(nat,MenuX,menuY+hdrH,Width,RowH,0,0,0,BlackAlpha);                                                // 0x6ea0
            y=menuY+hdrH+RowH+0.0f;
            if(p.title && p.title[0]){styleTitle(nat);cell(nat,p.title,p.titleKey,false,0,MenuX+TitleX,menuY+hdrH+TitleY);}  // 0x6fdd
            if(many){                                                                                                    // 0x6fed
                styleTitle(nat);
                if(countSel_!=sel+1 || countTotal_!=count){countSel_=sel+1;countTotal_=count;countW_=measureCount(nat,sel+1,count);styleTitle(nat);}
                countText(nat,sel+1,count,MenuX+Width-TitleX-countW_,menuY+hdrH+TitleY);
            }
        }
        const int* rowBg=st.rowBg?st.rowBg:bg_;
        const int visible=count-top_<maxV?count-top_:maxV;
        for(int k=0;k<visible;++k){rect(nat,MenuX,y,Width,RowH,rowBg[0],rowBg[1],rowBg[2],rowBg[3]);y+=RowH;}           // 0x710f
        if(many){                                                                                                        // 0x71d2 / 0x724a
            if(st.arrowsBg)rect(nat,MenuX,y+0.0f,Width,RowH,st.arrowsBg[0],st.arrowsBg[1],st.arrowsBg[2],st.arrowsBg[3]);
            else rect(nat,MenuX,y+0.0f,Width,RowH,0,0,0,BlackAlpha);
            sprite(nat,CommonMenu,ArrowsBar,MenuX+Width*0.5f,y+0.0f+RowH*0.5f,barW_,barH_,white_[0],white_[1],white_[2],white_[3]);
            y=y+(0.0f+RowH);
        }
        if(p.help && p.help[0]){                                                                                         // 0x7276..0x7621
            y+=DescGap;const float x=MenuX+TextInset;
            const std::uint32_t h=fnv(p.help);
            if(h!=helpHash_ || p.helpKey!=helpKeyed_){helpHash_=h;helpKeyed_=p.helpKey;styleDesc(nat,x);helpLines_=measureLines(nat,p.help,p.helpKey,x,y+DescGap);}
            const float boxH=charH_*float(helpLines_)+DescUnit*13.0f+DescUnit*5.0f*float(helpLines_-1);
            const int* d=st.descBg?st.descBg:bg_;
            rect(nat,MenuX,y,Width,boxH,d[0],d[1],d[2],d[3]);
            styleDesc(nat,x);cell(nat,p.help,p.helpKey,false,0,x,y+DescGap);
            y+=boxH;
        }
        buttons(nat,p);                                                                                                  // 0x7659 -> 0x938f
        alignMenu(nat);                                                                                                  // 0x765d
        const float firstRow=st.hideTitle?menuY:menuY+hdrH+RowH+0.0f;                                                    // l62 (0x211ef)
        for(int k=0;k<visible;++k){                                                                                      // 0x76ca..0x8e29
            const int r=top_+k;const Row& row=p.rows[r];const bool selected=r==sel;
            const float rowTop=firstRow+RowH*float(k),ty=rowTop+RowTextY;
            if(selected)rect(nat,MenuX,rowTop,Width,RowH,white_[0],white_[1],white_[2],white_[3]);                      // 0x7802
            const float x0=MenuX+TextInset;
            styleRow(nat,selected,row.enabled);cell(nat,row.label?row.label:"",row.labelKey,false,0,x0,ty);
            if(!row.value)continue;
            const float w=valueWidth(nat,r,row,selected);                                                                // 0x7b19 (cached)
            const float avail=(MenuX+Width-TextInset)-x0;                                                               // l51 (column 1 x = l39)
            float off=avail-(w+0.0f)+ValueNudge*1.0f;                                                                   // 0x7d54 (right)
            if(row.cycles && selected){                                                                                   // 0x7e1d
                off-=leftTrimW_*2.0f;
                sprite(nat,CommonMenu,ArrowLeft,x0+off+leftTrimW_*0.5f,ty-RowTextY+RowH*0.5f,leftW_,leftH_,0,0,0,255);
                off+=rightTrimW_;
                sprite(nat,CommonMenu,ArrowRight,x0+off+rightTrimW_*0.5f+(w+0.0f),ty-RowTextY+RowH*0.5f,rightW_,rightH_,0,0,0,255);
            }
            styleRow(nat,selected,row.valueEnabled());cell(nat,row.value,row.valueKey,row.valueHasInt,row.valueInt,x0+off,ty);
        }
        call(nat,n::FeedHeight,{bits(y)});                                                                               // 0x8e9c
        for(int c:HideComponents)call(nat,n::HideHud,{std::uint32_t(c)});
        call(nat,n::AlignReset,{});
    }
    // Every exit path (creator closed, Sandbox left, page torn down). Idempotent.
    void close(Natives& nat,const char* why) noexcept {
        if(!open_)return;
        frame_.reset();
        if(hudMoved_)call(nat,n::HudReset,{std::uint32_t(SubtitleHudComponent)});                                        // 0x6949
        call(nat,n::FeedHeight,{bits(0.0f)});                                                                            // 0x6955
        call(nat,n::ReleaseTxd,{str(nat,CommonMenu)});                                                                   // 0x6989
        if(header_[0])call(nat,n::ReleaseTxd,{str(nat,header_)});                                                        // 0x675a
        if(pending_[0])call(nat,n::ReleaseTxd,{str(nat,pending_)});
        if(movie_){const std::uint32_t w=nat.word(movie_);if(w)call(nat,n::ReleaseMovie,{w});}                          // 0x69e0 (by pointer)
        logf("RENDER_ONLINE menu=closed reason=%s textMisses=%d",why?why:"close",textMisses_);
        *this=Renderer{log_,has_};
    }
    static void sound(Natives& nat,Sound s) noexcept {
        const std::uint32_t name=nat.text(soundName(s)),set=nat.text(SoundSet);if(!name || !set)return;
        const std::uint32_t a[3]{0xFFFFFFFFu,name,set};std::uint32_t out[3]{};(void)nat.call(n::Sound,a,3,out);
    }
private:
    Renderer(LogFn l,HasFn h) noexcept:log_(l),has_(h) {}
public:
    Renderer()=default;
private:
    struct Intern {
        const char* key[MaxInterned]{};std::uint32_t sum[MaxInterned]{},at[MaxInterned]{};int n=0;
        void reset() noexcept {n=0;}
    };
    // ---- native helpers ----
    static bool call(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a,std::uint32_t* res=nullptr) noexcept {
        std::uint32_t args[12]{};unsigned k=0;for(auto v:a){if(k<12)args[k++]=v;}
        std::uint32_t out[3]{};const bool ok=nat.call(h,args,k,out);if(res){res[0]=out[0];res[1]=out[1];res[2]=out[2];}return ok;
    }
    std::uint32_t str(Natives& nat,const char* s) noexcept {
        const std::uint32_t h=fnv(s);       // (pointer AND content: a caller may format several rows through one buffer)
        for(int i=0;i<frame_.n;++i)if(frame_.key[i]==s && frame_.sum[i]==h)return frame_.at[i];
        const std::uint32_t a=nat.text(s);
        if(!a){++textMisses_;return 0;}
        if(frame_.n<MaxInterned){frame_.key[frame_.n]=s;frame_.sum[frame_.n]=h;frame_.at[frame_.n]=a;++frame_.n;}
        return a;
    }
    bool requestTxd(Natives& nat,const char* d) noexcept {
        const std::uint32_t s=str(nat,d);if(!s)return false;
        call(nat,n::RequestTxd,{s,0});std::uint32_t r[3]{};return call(nat,n::TxdLoaded,{s},r) && r[0];
    }
    void alignMenu(Natives& nat) noexcept {
        call(nat,n::Align,{std::uint32_t(AlignLeft),std::uint32_t(AlignTop)});
        call(nat,n::AlignParams,{bits(AlignOffset),bits(AlignOffset),bits(0.0f),bits(0.0f)});
    }
    // 0x97d1: top-left rectangle -> DRAW_RECT centre
    static void rect(Natives& nat,float x,float y,float w,float h,int r,int g,int b,int a) noexcept {
        call(nat,n::DrawRect,{bits(x+w*0.5f),bits(y+h*0.5f),bits(w),bits(h),std::uint32_t(r),std::uint32_t(g),std::uint32_t(b),std::uint32_t(a)});
    }
    void sprite(Natives& nat,const char* d,const char* t,float x,float y,float w,float h,int r,int g,int b,int a) noexcept {
        const std::uint32_t ds=str(nat,d),ts=str(nat,t);if(!ds || !ts)return;
        call(nat,n::DrawSprite,{ds,ts,bits(x),bits(y),bits(w),bits(h),bits(0.0f),std::uint32_t(r),std::uint32_t(g),std::uint32_t(b),std::uint32_t(a)});
    }
    static void common(Natives& nat) noexcept {
        call(nat,n::TextCentre,{0});call(nat,n::TextShadow,{0,0,0,0,0});call(nat,n::TextEdge,{0,0,0,0,0});
    }
    // 0x976e: the subtitle's title and counter
    void styleTitle(Natives& nat) noexcept {
        call(nat,n::TextFont,{std::uint32_t(Font)});call(nat,n::TextScale,{bits(0.0f),bits(TextScaleY)});
        call(nat,n::TextColour,{u(white_[0]),u(white_[1]),u(white_[2]),u(white_[3])});
        call(nat,n::TextWrap,{bits(MenuX+TextInset),bits(MenuX+Width-TextInset)});common(nat);
    }
    // 0x96a9(x): the description
    void styleDesc(Natives& nat,float x) noexcept {
        call(nat,n::TextFont,{std::uint32_t(Font)});call(nat,n::TextScale,{bits(0.0f),bits(TextScaleY)});
        call(nat,n::TextLeading,{std::uint32_t(DescLeading)});
        call(nat,n::TextColour,{u(white_[0]),u(white_[1]),u(white_[2]),u(white_[3])});
        call(nat,n::TextWrap,{bits(x),bits(MenuX+Width-TextInset)});common(nat);
    }
    // 0x8f78(selected, enabled, 0, ..): a row's text
    void styleRow(Natives& nat,bool selected,bool enabled) noexcept {
        if(!enabled)call(nat,n::TextColour,{u(DisabledGrey),u(DisabledGrey),u(DisabledGrey),255});
        else if(selected)call(nat,n::TextColour,{0,0,0,u(SelectedAlpha)});
        else call(nat,n::TextColour,{u(white_[0]),u(white_[1]),u(white_[2]),u(white_[3])});
        call(nat,n::TextScale,{bits(0.0f),bits(TextScaleY)});call(nat,n::TextJust,{1});call(nat,n::TextFont,{std::uint32_t(Font)});
        call(nat,n::TextWrap,{bits(0.0f),bits(1.0f)});common(nat);
    }
    void text(Natives& nat,const char* s,float x,float y) noexcept {
        const std::uint32_t l=str(nat,Literal),t=str(nat,s);if(!l || !t)return;
        call(nat,n::BeginText,{l});call(nat,n::AddLiteral,{t});call(nat,n::EndText,{bits(x),bits(y)});
    }
    // EXTENDED: a literal (STRING + the substring) or a GXT key (BEGIN(key) [+ one integer]); every BEGIN is closed by its END
    void cell(Natives& nat,const char* s,bool key,bool hasInt,int v,float x,float y) noexcept {
        if(!key){text(nat,s,x,y);return;}
        const std::uint32_t k=str(nat,s);if(!k)return;
        call(nat,n::BeginText,{k});if(hasInt)call(nat,n::AddInt,{u(v)});call(nat,n::EndText,{bits(x),bits(y)});
    }
    void countText(Natives& nat,int a,int b,float x,float y) noexcept {
        const std::uint32_t l=str(nat,CountLabel);if(!l)return;
        call(nat,n::BeginText,{l});call(nat,n::AddInt,{u(a)});call(nat,n::AddInt,{u(b)});call(nat,n::EndText,{bits(x),bits(y)});
    }
    // ---- cached queries ----
    void measureFixed(Natives& nat) noexcept {
        const std::uint32_t w=nat.word(0),h=nat.word(0);
        if(w && h && call(nat,n::ScreenRes,{w,h})){screenW_=int(nat.load(w));screenH_=int(nat.load(h));}
        if(screenW_<=0 || screenH_<=0){screenW_=1280;screenH_=720;}
        std::uint32_t r[3]{};wide_=!call(nat,n::Widescreen,{},r) || r[0];
        hud(nat,HudText,white_);hud(nat,HudMenuBg,bg_);
        float tw,th;
        if(tex(nat,CommonMenu,ArrowsBar,tw,th)){barW_=tw/float(screenW_);barH_=th/float(screenH_);}   // 0x7233: plain pixel / screen
        // sprite 26 / 27, trimmed (spacing) and full (drawn): 0x7e22 / 0x7e52, 0x7ea5 / 0x7ec5
        size(nat,ArrowLeft,true,leftTrimW_,th);size(nat,ArrowLeft,false,leftW_,leftH_);
        size(nat,ArrowRight,true,rightTrimW_,th);size(nat,ArrowRight,false,rightW_,rightH_);
        if(call(nat,n::CharHeight,{bits(TextScaleY),0},r))charH_=asFloat(r[0]);
    }
    void measureHeader(Natives& nat) noexcept {
        headerMeasured_=true;headerH_=0.0f;float w,h;
        if(header_[0] && size(nat,header_,false,w,h,header_))headerH_=h;     // 0x6d85: l61 = the banner's height
    }
    // 0x9bfb(sprite, ?, keepBorder): w = texW/sw * int(sw/sh); h = (texH/sh)/(texW/sw)*w; trim = 2 pixels off each; x1.33 when 4:3
    bool size(Natives& nat,const char* name,bool trim,float& w,float& h,const char* dict=CommonMenu) noexcept {
        float tw,th;if(!tex(nat,dict,name,tw,th))return false;
        if(trim){tw-=2.0f;th-=2.0f;}
        w=tw/float(screenW_)*float(screenW_/screenH_);
        h=(th/float(screenH_))/(tw/float(screenW_))*w;
        if(!wide_)w*=1.33f;
        return true;
    }
    bool tex(Natives& nat,const char* d,const char* t,float& w,float& h) noexcept {
        const std::uint32_t ds=str(nat,d),ts=str(nat,t);std::uint32_t r[3]{};
        if(!ds || !ts || !call(nat,n::TexRes,{ds,ts},r))return false;
        w=asFloat(r[0]);h=asFloat(r[1]);return w>0.0f && h>0.0f;
    }
    void hud(Natives& nat,int id,int* c) noexcept {
        std::uint32_t a[4];for(auto& x:a)x=nat.word(0);
        if(!a[0] || !a[3] || !call(nat,n::HudColour,{u(id),a[0],a[1],a[2],a[3]}))return;
        for(int k=0;k<4;++k)c[k]=int(nat.load(a[k]));
    }
    // A value's width, measured with that row's own text style (0x7b0c 0x8f78 then BEGIN/END ..WIDTH..), again only when its text changes.
    float valueWidth(Natives& nat,int r,const Row& row,bool selected) noexcept {
        std::uint32_t h=fnv(row.value);
        if(row.valueKey)h=(h^0x9e3779b9u)*16777619u;                          // EXTENDED: key / integer are part of what is measured
        if(row.valueHasInt)h=(h^std::uint32_t(row.valueInt))*16777619u;
        if(measured_[r] && h==valueHash_[r])return valueW_[r];
        styleRow(nat,selected,row.valueEnabled());
        std::uint32_t res[3]{};
        if(row.valueKey){
            const std::uint32_t k=str(nat,row.value);if(!k)return 0.0f;
            call(nat,n::BeginWidth,{k});if(row.valueHasInt)call(nat,n::AddInt,{u(row.valueInt)});
            valueW_[r]=call(nat,n::EndWidth,{1},res)?asFloat(res[0]):0.0f;valueHash_[r]=h;measured_[r]=true;
            return valueW_[r];
        }
        const std::uint32_t l=str(nat,Literal),t=str(nat,row.value);
        if(!l || !t)return 0.0f;                                              // (not cached: tried again next frame)
        call(nat,n::BeginWidth,{l});call(nat,n::AddLiteral,{t});
        valueW_[r]=call(nat,n::EndWidth,{1},res)?asFloat(res[0]):0.0f;valueHash_[r]=h;measured_[r]=true;
        return valueW_[r];
    }
    float measureCount(Natives& nat,int a,int b) noexcept {                 // 0x972c
        const std::uint32_t l=str(nat,CountLabel);std::uint32_t res[3]{};if(!l)return 0.0f;
        call(nat,n::BeginWidth,{l});call(nat,n::AddInt,{u(a)});call(nat,n::AddInt,{u(b)});
        return call(nat,n::EndWidth,{1},res)?asFloat(res[0]):0.0f;
    }
    int measureLines(Natives& nat,const char* s,bool key,float x,float y) noexcept {    // 0x72ca / 0x73dd
        std::uint32_t res[3]{};
        if(key){const std::uint32_t k=str(nat,s);if(!k)return 1;call(nat,n::BeginLines,{k});}
        else {const std::uint32_t l=str(nat,Literal),t=str(nat,s);if(!l || !t)return 1;call(nat,n::BeginLines,{l});call(nat,n::AddLiteral,{t});}
        if(!call(nat,n::EndLines,{bits(x),bits(y)},res))return 1;
        const int k=int(res[0]);return k<1?1:(k>8?8:k);
    }
    // 0x938f: the instructional buttons (sent again only when the set changes; drawn every frame)
    void buttons(Natives& nat,const Page& p) noexcept {
        const int nb=p.buttonCount<0?0:(p.buttonCount>MaxButtons?MaxButtons:p.buttonCount);
        if(!nb || !movie_)return;
        std::uint32_t h=2166136261u;for(int i=0;i<nb;++i){h=(h^fnv(p.buttons[i].label))*16777619u;h=(h^std::uint32_t(p.buttons[i].icon))*16777619u;}
        if(h!=buttonsHash_){
            buttonsHash_=h;
            method(nat,"CLEAR_ALL");call(nat,n::EndMethod,{});
            method(nat,"SET_MAX_WIDTH");call(nat,n::ParamFloat,{bits(1.0f-0.0f/100.0f)});call(nat,n::EndMethod,{});
            for(int i=0;i<nb;++i){
                const Button& b=p.buttons[i];std::uint32_t icon=u(b.icon),r[3]{};
                if(b.group>=0 && call(nat,n::GroupButton,{u(b.group),u(b.control),1},r))icon=r[0];
                method(nat,"SET_DATA_SLOT");call(nat,n::ParamInt,{u(i)});call(nat,n::ParamInt,{icon});
                const std::uint32_t l=str(nat,b.label);
                if(l){call(nat,n::BeginSfText,{l});call(nat,n::EndSfText,{});}
                call(nat,n::EndMethod,{});
            }
            method(nat,"SET_BACKGROUND_COLOUR");for(int c:ButtonBackground)call(nat,n::ParamInt,{u(c)});call(nat,n::EndMethod,{});
            method(nat,"DRAW_INSTRUCTIONAL_BUTTONS");call(nat,n::ParamInt,{0});call(nat,n::EndMethod,{});
        }
        call(nat,n::Align,{u(AlignLeft),u(AlignBottom)});call(nat,n::AlignParams,{bits(0.0f),bits(0.0f),bits(0.0f),bits(0.0f)});
        if(!hudMoved_){hudMoved_=true;call(nat,n::HudPos,{u(SubtitleHudComponent),bits(0.0f),bits(SubtitleShiftY)});}
        call(nat,n::AlignReset,{});
        call(nat,n::DrawMovieFull,{movie_,255,255,255,255});
    }
    void method(Natives& nat,const char* m) noexcept {
        const std::uint32_t s=str(nat,m);std::uint32_t r[3]{};if(s)call(nat,n::BeginMethod,{movie_,s},r);
    }
    static std::uint32_t u(int v) noexcept {return std::uint32_t(v);}
    static void copy(char* d,const char* s) noexcept {std::size_t k=0;for(;s && s[k] && k<31;++k)d[k]=s[k];d[k]=0;}
    template<class... A> void logf(const char* f,A... a) const noexcept {if(!log_)return;char line[160];std::snprintf(line,sizeof line,f,a...);log_(line);}

    LogFn log_=nullptr;HasFn has_=nullptr;
    bool open_=false,ready_=false,unavailable_=false,headerMeasured_=false,hudMoved_=false,wide_=true,helpKeyed_=false;
    char header_[32]{},pending_[32]{};
    std::uint32_t movie_=0,helpHash_=0,buttonsHash_=0;
    int screenW_=1280,screenH_=720,top_=0,helpLines_=1,countSel_=-1,countTotal_=-1,textMisses_=0;
    int white_[4]{255,255,255,255},bg_[4]{0,0,0,180};
    float headerH_=0.0f,barW_=0.0f,barH_=0.0f,leftW_=0.0f,leftH_=0.0f,leftTrimW_=0.0f,rightW_=0.0f,rightH_=0.0f,rightTrimW_=0.0f,charH_=0.0f,countW_=0.0f;
    float valueW_[MaxRows]{};std::uint32_t valueHash_[MaxRows]{};bool measured_[MaxRows]{};
    Intern frame_{};
};

constexpr bool forbiddenName(const char* s) noexcept {
    const auto starts=[](const char* a,const char* p){while(*p){if(*a!=*p)return false;++a;++p;}return true;};
    return starts(s,"NETWORK_") || starts(s,"STAT_") || starts(s,"NET_") || starts(s,"UGC_") || starts(s,"CLOUD_") || starts(s,"DATAFILE_")
        || starts(s,"SC_") || starts(s,"PLAYSTATS_") || starts(s,"SET_SAVE") || starts(s,"DO_AUTO_SAVE") || starts(s,"SET_PED_")
        || starts(s,"SET_PLAYER_") || starts(s,"SET_ENTITY_");
}
constexpr bool noForbidden() noexcept {for(const auto& x:Used)if(forbiddenName(x.name))return false;return true;}
static_assert(noForbidden(),"the menu renderer draws only: no network / stats / save / ped / player / entity natives");

} // namespace recomp::menudraw
