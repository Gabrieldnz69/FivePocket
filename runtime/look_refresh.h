#pragma once
// look_refresh.h (v1509, shops subagent 2026-11-11; lead: "real purchases ... survive a game restart, fixed the game's own way").
// The problem (t-onlcashc A/B 2026-11-10, v1504 lineage): the Discount Store glasses bought in phase A were gone after the restart.
//  - The game keeps the worn outfit in the character's packed stats: texture 387+c, drawable 399+c, palette 411+c (c = 0..11), prop
//    423+k, prop texture 432+k (k = anchor 0..8, 255 = none). clothes_shop_mp writes ALL of them from the player ped when the shop is
//    left (0x30593(PLAYER_PED_ID, -1) at 0x40a2 / 0x4192: 0x30ac6 reads the ped, 0x305d3 stores 65 values through 0x3099d =
//    GET_PACKED_INT_STAT_KEY + STAT_SET_MASKED_INT); hairdo_shop_mp has the same writer (one 387 base) and stores the beard / makeup
//    overlays (packed 447+k, float 87+k) through 0x13a2e / 0x13a6d.
//  - The game restores it from those stats when it makes the freemode player: maintransition 0x584d(0, .., ped, slot) -> 0x34ce0 (packed
//    387..) -> 0x1fb1d (SET_PED_COMPONENT_VARIATION x12 at 0x1fb60, SET_PED_PROP_INDEX / CLEAR_PED_PROP x9). Phase B showed it: ip 0x1fb60
//    set hair d=11 t=3 from the saved slot (phase A, nothing saved yet: d=0).
//  - Then OUR saved look (character.json, captured once from the creator's commit) was applied over it (online_mode.h: "action 17 0",
//    character_creator.h Applier: 12 components + CLEAR_PED_PROP for a prop the record does not have) - the glasses were cleared. The
//    respawn re-apply (Creator::tickIdle) does the same after a death.
// The fix: before the saved look is applied in Online mode, the record takes what the game itself changed since the record was made,
// read from the game's own stats: every component / prop / overlay whose stat value differs from the creator's raw capture
// (creator.json, the same stats at commit time). Unchanged values keep the record's (the creator-hair and eyebrow corrections of
// v1083 / v1106 stay). Nothing is taken when the stats were not written by the game (defaults: a slot never saved, or the in-memory
// slot of a first session) - those read 0 for the prop anchors no freemode ped has (3 mouth, 4 left hand, 5 right hand), which the
// game's writer always stores as 255 (GET_PED_PROP_INDEX -1). No stat is written; the record (character.json) is rewritten only when
// a value changed. Head blend: always the record's.
// Overlays are written by other code than the outfit (the creator's commit, the barber's 0x13a2e / 0x13a6d), so a slot whose stats
// read the default (index 0, opacity 0) is never taken: a slot first saved from defaults (t-onlcashc's seed: no mpstats file, the
// shop's exit wrote only the outfit) keeps the record's overlay. A removal written by the game (index 255 / opacity 0) is taken.
// Pure parts (gameWritten, merge, parseRaw) are host-tested (tests/look_refresh_test.cpp); GameRefresher calls the game's natives.
#include "character_creator.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"   // (creator_route.h Route::save; host -Wextra builds)
#include "creator_route.h"
#pragma GCC diagnostic pop
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

// (say() passes a format parameter through: every caller gives a literal)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
namespace recomp::look_refresh {
using creatorroute::DiscResult;
namespace cr=creatorroute;

constexpr std::array<int,3> PropFreeAnchors{3,4,5};   // no freemode prop drawables there: the game's writer stores 255
inline bool gameWritten(const DiscResult& r) noexcept {
    for(int a:PropFreeAnchors)if(r.prop[std::size_t(a)]!=255)return false;
    return true;
}

struct Changes {
    int parts=0,props=0,overlays=0;
    bool any() const noexcept {return parts || props || overlays;}
};
// The record takes every value the game changed since the raw capture `base`; `now` = the stats today. Mapping as creator_route toLook.
inline Changes merge(creator::Look& l,const DiscResult& base,const DiscResult& now) noexcept {
    Changes ch;creator::Look t=l;
    for(int c=0;c<cr::Components;++c) {
        const std::size_t i=std::size_t(c);
        if(now.drawable[i]==base.drawable[i] && now.texture[i]==base.texture[i] && now.palette[i]==base.palette[i])continue;
        const int d=now.drawable[i],x=now.texture[i],p=now.palette[i];
        const creator::Part np{d==255?0:d,x==255?0:x,(p>=0 && p<=3)?p:0};
        if(np.drawable!=t.part[i].drawable || np.texture!=t.part[i].texture || np.palette!=t.part[i].palette){t.part[i]=np;++ch.parts;}
    }
    for(int s=0;s<creator::PropSlots;++s) {
        const std::size_t a=std::size_t(creator::PropAnchor[std::size_t(s)]);
        if(now.prop[a]==base.prop[a] && now.propTexture[a]==base.propTexture[a])continue;
        const int d=now.prop[a],x=now.propTexture[a];
        const creator::Prop np=d==255?creator::Prop{-1,0}:creator::Prop{d,x==255?0:x};
        if(np.drawable!=t.prop[std::size_t(s)].drawable || np.texture!=t.prop[std::size_t(s)].texture){t.prop[std::size_t(s)]=np;++ch.props;}
    }
    for(int k=0;k<cr::Overlays;++k) {
        const std::size_t i=std::size_t(k);
        if(now.overlayIndex[i]==base.overlayIndex[i] && now.overlayOpacity[i]==base.overlayOpacity[i])continue;
        if(now.overlayIndex[i]==0 && now.overlayOpacity[i]<=0)continue;   // the stats' default (index 0, opacity 0): never taken (header)
        const int x=now.overlayIndex[i],o=now.overlayOpacity[i];
        const creator::Overlay no=(x==255 || x<0 || o<=0)?creator::Overlay{0,0}:creator::Overlay{x,o};
        if(no.index!=t.overlay[i].index || no.opacity!=t.overlay[i].opacity){t.overlay[i]=no;++ch.overlays;}
    }
    if(!ch.any())return ch;
    if(!creator::inRange(t))return Changes{};   // a value outside the record's ranges: the record stays as it was
    l=t;return ch;
}

// creator.json (creator_route serializeRaw): the arrays this needs. False: not that format / a missing or short array.
inline bool parseRaw(const char* text,int len,DiscResult& out) noexcept {
    namespace js=creator::json;
    if(!text || len<=0 || len>cr::RawMax)return false;
    js::Cursor c{text,text+len};DiscResult r;
    bool format=false,dr=false,tx=false,pa=false,pr=false,pt=false,oi=false,oo=false;
    char key[32];
    if(!js::eat(c,'{'))return false;
    do {
        if(!js::str(c,key,sizeof key) || !js::eat(c,':'))return false;
        if(!std::strcmp(key,"format")){char v[48];if(!js::str(c,v,sizeof v) || std::strcmp(v,cr::RawFormat))return false;format=true;}
        else if(!std::strcmp(key,"version")){int v=0;if(!js::integer(c,v) || v!=cr::RawVersion)return false;}
        else if(!std::strcmp(key,"drawable")){if(!js::ints(c,r.drawable))return false;dr=true;}
        else if(!std::strcmp(key,"texture")){if(!js::ints(c,r.texture))return false;tx=true;}
        else if(!std::strcmp(key,"palette")){if(!js::ints(c,r.palette))return false;pa=true;}
        else if(!std::strcmp(key,"prop")){if(!js::ints(c,r.prop))return false;pr=true;}
        else if(!std::strcmp(key,"prop_texture")){if(!js::ints(c,r.propTexture))return false;pt=true;}
        else if(!std::strcmp(key,"overlay_index")){if(!js::ints(c,r.overlayIndex))return false;oi=true;}
        else if(!std::strcmp(key,"overlay_opacity")){if(!js::ints(c,r.overlayOpacity))return false;oo=true;}
        else if(!js::skipValue(c))return false;
    } while(js::eat(c,','));
    if(!js::eat(c,'}') || !(format && dr && tx && pa && pr && pt && oi && oo))return false;
    out=r;return true;
}

// The outfit part of the capture plan (creator_route's own reads: packed through GET_PACKED_INT_STAT_KEY + STAT_GET_MASKED_INT,
// the overlay opacities through STAT_GET_FLOAT on the character-0 hashes). False: a read failed (nothing is taken).
inline bool readOutfit(trainer::Natives& nat,int character,DiscResult& out) noexcept {
    DiscResult r;std::uint32_t v=0;
    const auto rd=[&](cr::Kind k,int en){if(!cr::readOne(nat,cr::Read{k,en},character,v))return false;cr::store(r,cr::Read{k,en},v);return true;};
    for(int k=0;k<cr::Overlays;++k)if(!rd(cr::Kind::Packed,cr::pk::OverlayIndexBase+k) || !rd(cr::Kind::Float,87+k))return false;
    for(int c=0;c<cr::Components;++c)
        if(!rd(cr::Kind::Packed,cr::pk::DrawableBase+c) || !rd(cr::Kind::Packed,cr::pk::TextureBase+c) || !rd(cr::Kind::Packed,cr::pk::PaletteBase+c))return false;
    for(int a=0;a<cr::PropAnchors;++a)if(!rd(cr::Kind::Packed,cr::pk::PropBase+a) || !rd(cr::Kind::Packed,cr::pk::PropTextureBase+a))return false;
    out=r;return true;
}

// The glue's refresher (Online mode only; files/private-trace/diag-look-refresh-off = the v1183 behaviour). Character 0 = the stats
// slot 1, as the creator route (it always creates slot 0) and maintransition's MPPLY_LAST_MP_CHAR on every device run so far.
class GameRefresher final : public creator::Refresher {
public:
    using LogFn=void(*)(const char*);
    using OnFn=bool(*)();
    GameRefresher(creator::Store* raw,OnFn on,LogFn log) noexcept:raw_(raw),on_(on),log_(log) {}
    bool refresh(trainer::Natives& nat,creator::Look& l) noexcept override {
        if(!on_ || !on_() || !raw_)return false;
        std::uint32_t out[3]{};const std::uint32_t slot=CharacterSlot;
        const bool loaded=nat.call(trainer::joaat("STAT_SLOT_IS_LOADED"),&slot,1,out) && (out[0]&0xFFu)!=0u;
        const bool failed=nat.call(trainer::joaat("STAT_CLOUD_SLOT_LOAD_FAILED"),&slot,1,out) && (out[0]&0xFFu)!=0u;
        if(!loaded || failed)return say(false,"skipped: character slot %u loaded=%d failed=%d (the record stays)",slot,int(loaded),int(failed));
        char buf[cr::RawMax+1];const int k=raw_->read(buf,int(sizeof buf));DiscResult base;
        if(k<=0 || k>cr::RawMax || !parseRaw(buf,k,base))return say(false,"skipped: no usable creator.json (read %d; the record stays)",k);
        DiscResult now;
        if(!readOutfit(nat,Character,now))return say(false,"skipped: an outfit stat read failed (the record stays)");
        if(!gameWritten(base) || !gameWritten(now))
            return say(false,"skipped: the outfit stats were not written by the game (props 3/4/5 base %d,%d,%d now %d,%d,%d; the record stays)",
                       base.prop[3],base.prop[4],base.prop[5],now.prop[3],now.prop[4],now.prop[5]);
        const Changes ch=merge(l,base,now);
        return say(ch.any(),"from the game's outfit stats (character %d): parts %d props %d overlays %d changed; hair d=%d t=%d eyes prop d=%d t=%d",
                   Character,ch.parts,ch.props,ch.overlays,l.part[2].drawable,l.part[2].texture,l.prop[1].drawable,l.prop[1].texture);
    }
    static constexpr int Character=0;
    static constexpr std::uint32_t CharacterSlot=1;
private:
    template<class... A> bool say(bool r,const char* f,A... a) noexcept {
        if(log_){char m[200];std::snprintf(m,sizeof m,"RENDER_ONLINE look refresh ");const std::size_t n=std::strlen(m);
            std::snprintf(m+n,sizeof m-n,f,a...);log_(m);}
        return r;
    }
    creator::Store* raw_;OnFn on_;LogFn log_;
};
} // namespace recomp::look_refresh
#pragma GCC diagnostic pop
