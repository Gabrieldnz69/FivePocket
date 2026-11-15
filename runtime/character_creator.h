#pragma once
// character_creator.h (Update 1, Online World: the online character creator, route B = our own creator UI driving the game's own
// appearance natives). Lane local-analysis/.agents/upd1-online/character-creator (NOTES.md has the evidence and the device checks).
// Pure: no guest memory, no files, no platform calls. The trainer (trainer.h) drives it through trainer::Natives on the script thread;
// the glue gives it a Store (character_store.h) for the one JSON record in the Sandbox save root. Sandbox mode only (the glue hands
// it over only there, like online_world.h's Actions).
//
// What the player's own disc says (names-only bytecode read of maintransition / mp_menuped / the MP shops, and TOC names of
// streamedpeds_mp.rpf / streamedpedprops.rpf / ped_mp_overlay_txds.rpf; NOTES.md section 2):
//  - The disc's creator lives in maintransition (the online transition script). Its head is
//    SET_PED_HEAD_BLEND_DATA(ped, A, B, 0, 0, 0, 0, mix, 0.0, 0.0, TRUE) at all 4 creator call sites; A/B come from a 6-entry heritage
//    table per parent group (pairs 0/1 .. 10/11 for one group, 12/13 .. 22/23 for the other) or one explicit head for both (choice 7).
//  - Overlays are slots 0..6 only, SET_PED_HEAD_OVERLAY(ped, slot, index, opacity); the creator clears one with (slot, 0, 0.0) and draws
//    random ones with index < GET_PED_HEAD_OVERLAY_NUM(slot), opacity 1.0. No overlay colour, hair colour, eye colour or face-feature
//    native is registered on this build: hair colour is the hair component's texture (16 hair drawables, up to 7 textures each).
//  - Clothes: SET_PED_COMPONENT_VARIATION(ped, comp, drawable, texture, palette) over 12 components; props on 3 anchors only
//    (p_head, p_eyes, p_lwrist: 20 / 16 / 3 male, 20 / 16 / 2 female drawables) with SET_PED_PROP_INDEX(ped, anchor, d, t, 0)
//    (347 literal sites on the disc pass 0 as the fifth argument; none pass anything else) and CLEAR_PED_PROP(ped, anchor).
//  - The MP shops end a change with HAS_PED_HEAD_BLEND_FINISHED(ped) && HAVE_ALL_STREAMING_REQUESTS_COMPLETED(ped) ->
//    FINALIZE_HEAD_BLEND(ped) (clothes_shop_mp 0x58cb5..0x58cd1). The apply job below does the same, bounded.
//
// Argument slots (our recompiled handlers, validated-ppc; tests/check_natives.py re-checks each one against the corpus):
//  SET_PED_HEAD_BLEND_DATA sub_82DD3BC0 11 (ints +0..+24, floats +28/+32/+36, int +40). SET_PED_HEAD_OVERLAY sub_82DD3C38 4 (3 ints,
//  float +12). GET_PED_HEAD_OVERLAY_NUM sub_82DAF078 1 -> int. SET_PED_COMPONENT_VARIATION sub_82DD3AD0 5 ints. SET_PED_PROP_INDEX
//  sub_82DD3ED8 5 ints (+16 tested != 0). CLEAR_PED_PROP sub_82DD3F10 2. GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS sub_82E04BA8 2 -> int;
//  GET_NUMBER_OF_PED_TEXTURE_VARIATIONS sub_82E04BE8 3 -> int; the PROP variants sub_82E04C30 2 / sub_82E04C70 3 -> int.
//  HAS_PED_HEAD_BLEND_FINISHED sub_82DD3C50 1 -> BOOL. HAVE_ALL_STREAMING_REQUESTS_COMPLETED sub_82DD62B0 1 -> BOOL.
//  FINALIZE_HEAD_BLEND sub_82DD3C90 1. GET_ENTITY_MODEL sub_82D8A300 1 -> hash.
//
// Frame rate: nothing runs per frame. An apply is one tick of at most 12 + 12 + 7 + 7 + 6 + 12 + 3 native calls, then one or two
// cheap polls per tick until the blend is done (bounded). An edit is 1-3 native calls. The background re-apply after a respawn costs
// one branch per Sandbox tick while idle.
#include "trainer.h"
#include "face_camera.h"   // v959: the creator's face camera (online_world.h Actions drives it; update-1/v959/NOTES.md)
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace recomp::creator {
using trainer::Natives;
using trainer::joaat;
using trainer::bits;

struct Named {const char* name;std::uint32_t hash;};
namespace n {
constexpr std::uint32_t HeadBlend=joaat("SET_PED_HEAD_BLEND_DATA"),HeadOverlay=joaat("SET_PED_HEAD_OVERLAY"),
    OverlayNum=joaat("GET_PED_HEAD_OVERLAY_NUM"),Component=joaat("SET_PED_COMPONENT_VARIATION"),PropIndex=joaat("SET_PED_PROP_INDEX"),
    ClearProp=joaat("CLEAR_PED_PROP"),Drawables=joaat("GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS"),
    Textures=joaat("GET_NUMBER_OF_PED_TEXTURE_VARIATIONS"),PropDrawables=joaat("GET_NUMBER_OF_PED_PROP_DRAWABLE_VARIATIONS"),
    PropTextures=joaat("GET_NUMBER_OF_PED_PROP_TEXTURE_VARIATIONS"),BlendFinished=joaat("HAS_PED_HEAD_BLEND_FINISHED"),
    StreamingDone=joaat("HAVE_ALL_STREAMING_REQUESTS_COMPLETED"),Finalize=joaat("FINALIZE_HEAD_BLEND"),
    EntityModel=joaat("GET_ENTITY_MODEL"),PlayerPedId=joaat("PLAYER_PED_ID");
}
inline constexpr Named Used[]{{"SET_PED_HEAD_BLEND_DATA",n::HeadBlend},{"SET_PED_HEAD_OVERLAY",n::HeadOverlay},
    {"GET_PED_HEAD_OVERLAY_NUM",n::OverlayNum},{"SET_PED_COMPONENT_VARIATION",n::Component},{"SET_PED_PROP_INDEX",n::PropIndex},
    {"CLEAR_PED_PROP",n::ClearProp},{"GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS",n::Drawables},
    {"GET_NUMBER_OF_PED_TEXTURE_VARIATIONS",n::Textures},{"GET_NUMBER_OF_PED_PROP_DRAWABLE_VARIATIONS",n::PropDrawables},
    {"GET_NUMBER_OF_PED_PROP_TEXTURE_VARIATIONS",n::PropTextures},{"HAS_PED_HEAD_BLEND_FINISHED",n::BlendFinished},
    {"HAVE_ALL_STREAMING_REQUESTS_COMPLETED",n::StreamingDone},{"FINALIZE_HEAD_BLEND",n::Finalize},{"GET_ENTITY_MODEL",n::EntityModel},
    {"PLAYER_PED_ID",n::PlayerPedId}};
inline constexpr std::uint32_t UsedNatives[]{n::HeadBlend,n::HeadOverlay,n::OverlayNum,n::Component,n::PropIndex,n::ClearProp,
    n::Drawables,n::Textures,n::PropDrawables,n::PropTextures,n::BlendFinished,n::StreamingDone,n::Finalize,n::EntityModel,n::PlayerPedId};
static_assert(std::size(Used)==std::size(UsedNatives));

constexpr std::uint32_t MaleModel=joaat("mp_m_freemode_01"),FemaleModel=joaat("mp_f_freemode_01");
constexpr int Components=12,OverlaySlots=7,PropSlots=3;
constexpr std::array<std::uint32_t,PropSlots> PropAnchor{0,1,6};   // p_head, p_eyes, p_lwrist (the only freemode prop anchors on the disc)
// Head ids: mp_headtargets has 28 head drawables (head_000..027) on this disc; the creator's heritage pairs reach 23 and its
// explicit choice any id. 27 is the highest id with a blend target (INFERENCE; NEEDS DEVICE CHECK for 24..27).
constexpr int MaxHeadId=27;
constexpr int Permille=1000;
constexpr std::uint32_t BlendWaitFramesMax=300;
// The disc creator's heritage table (maintransition 0x3b0bf): choice 0..5 -> (first, second); group 0 = the "which == 2" branch.
constexpr int HeritageChoices=6;
constexpr int heritageFirst(int group,int choice) noexcept {return (group==0?0:12)+2*choice;}

// ---------------------------------------------------------------------------------------------------- the record
struct Overlay {int index=0;int opacity=0;};                 // opacity in permille; 0 = off (the disc clears with index 0, opacity 0)
struct Part {int drawable=0,texture=0,palette=0;};
struct Prop {int drawable=-1,texture=0;};                    // -1 = none (CLEAR_PED_PROP)
struct Look {
    std::uint32_t model=MaleModel;
    int shapeFirst=0,shapeSecond=1,shapeMix=500;              // mix in permille
    bool skinExplicit=false;                                  // false: the disc creator's pattern (skin ids 0, skin mix 0)
    int skinFirst=0,skinSecond=0,skinMix=0;
    std::array<Overlay,OverlaySlots> overlay{};
    std::array<Part,Components> part{};
    std::array<Prop,PropSlots> prop{};
    bool operator==(const Look& o) const noexcept {
        if(model!=o.model || shapeFirst!=o.shapeFirst || shapeSecond!=o.shapeSecond || shapeMix!=o.shapeMix || skinExplicit!=o.skinExplicit
           || skinFirst!=o.skinFirst || skinSecond!=o.skinSecond || skinMix!=o.skinMix)return false;
        for(int i=0;i<OverlaySlots;++i)if(overlay[i].index!=o.overlay[i].index || overlay[i].opacity!=o.overlay[i].opacity)return false;
        for(int i=0;i<Components;++i)if(part[i].drawable!=o.part[i].drawable || part[i].texture!=o.part[i].texture || part[i].palette!=o.part[i].palette)return false;
        for(int i=0;i<PropSlots;++i)if(prop[i].drawable!=o.prop[i].drawable || prop[i].texture!=o.prop[i].texture)return false;
        return true;
    }
};
constexpr bool isFreemode(std::uint32_t m) noexcept {return m==MaleModel || m==FemaleModel;}
// Defaults: heritage choice 0 of group 0 (male) / group 1 (female) (which group is which sex is an INFERENCE: NEEDS DEVICE CHECK C2),
// eyebrows (slot 2) index 0 at full opacity, hair drawable 1 (hair_000 is the smallest hair file on both models), no props.
inline Look defaultLook(std::uint32_t model) noexcept {
    Look l;l.model=model==FemaleModel?FemaleModel:MaleModel;
    const int g=l.model==FemaleModel?1:0;l.shapeFirst=heritageFirst(g,0);l.shapeSecond=l.shapeFirst+1;
    l.overlay[2]={0,Permille};l.part[2].drawable=1;
    return l;
}
// Static ranges (what a record may hold at all). The game's own counts are checked at apply time (Applier::clampToGame).
inline bool inRange(const Look& l) noexcept {
    const auto head=[](int v){return v>=0 && v<=MaxHeadId;};
    const auto mix=[](int v){return v>=0 && v<=Permille;};
    if(!isFreemode(l.model) || !head(l.shapeFirst) || !head(l.shapeSecond) || !mix(l.shapeMix))return false;
    if(l.skinExplicit && (!head(l.skinFirst) || !head(l.skinSecond) || !mix(l.skinMix)))return false;
    for(const auto& o:l.overlay)if(o.index<0 || o.index>254 || !mix(o.opacity))return false;
    for(const auto& p:l.part)if(p.drawable<0 || p.drawable>254 || p.texture<0 || p.texture>254 || p.palette<0 || p.palette>3)return false;
    for(const auto& p:l.prop)if(p.drawable<-1 || p.drawable>254 || p.texture<0 || p.texture>254)return false;
    return true;
}

// ---------------------------------------------------------------------------------------------------- JSON (the Sandbox save root)
// {"format":"fivepocket-online-character","version":1,"model":"male"|"female",
//  "blend":{"shape":[A,B],"shape_mix":M,"skin":"disc"|[A,B],"skin_mix":M},
//  "overlays":[[index,opacity] x7],"components":[[drawable,texture,palette] x12],"props":[[drawable,texture] x3]}
// Numbers are integers (mix / opacity in permille). Unknown keys are skipped; a missing key, a wrong count or an out-of-range value
// rejects the whole record (nothing partial is ever applied).
constexpr const char* FormatName="fivepocket-online-character";
constexpr int FormatVersion=1;
constexpr int RecordMax=2048;

inline int serialize(const Look& l,char* out,int cap) noexcept {
    if(!out || cap<=0)return -1;
    int k=0;bool ok=true;
    const auto put=[&](const char* f,auto... a){if(!ok)return;const int w=std::snprintf(out+k,std::size_t(cap-k),f,a...);if(w<0 || w>=cap-k){ok=false;return;}k+=w;};
    put("{\"format\":\"%s\",\"version\":%d,\"model\":\"%s\",\n",FormatName,FormatVersion,l.model==FemaleModel?"female":"male");
    put(" \"blend\":{\"shape\":[%d,%d],\"shape_mix\":%d,",l.shapeFirst,l.shapeSecond,l.shapeMix);
    if(l.skinExplicit)put("\"skin\":[%d,%d],\"skin_mix\":%d},\n",l.skinFirst,l.skinSecond,l.skinMix);
    else put("%s","\"skin\":\"disc\",\"skin_mix\":0},\n");
    put("%s"," \"overlays\":[");
    for(int i=0;i<OverlaySlots;++i)put("%s[%d,%d]",i?",":"",l.overlay[i].index,l.overlay[i].opacity);
    put("%s","],\n \"components\":[");
    for(int i=0;i<Components;++i)put("%s[%d,%d,%d]",i?",":"",l.part[i].drawable,l.part[i].texture,l.part[i].palette);
    put("%s","],\n \"props\":[");
    for(int i=0;i<PropSlots;++i)put("%s[%d,%d]",i?",":"",l.prop[i].drawable,l.prop[i].texture);
    put("%s","]}\n");
    return ok?k:-1;
}

namespace json {
struct Cursor {const char* p;const char* e;bool bad=false;};
inline void ws(Cursor& c) noexcept {while(c.p<c.e && (*c.p==' ' || *c.p=='\n' || *c.p=='\r' || *c.p=='\t'))++c.p;}
inline bool eat(Cursor& c,char ch) noexcept {ws(c);if(c.p<c.e && *c.p==ch){++c.p;return true;}return false;}
inline bool str(Cursor& c,char* out,int cap) noexcept {   // plain ASCII strings only (no escapes needed by this format)
    ws(c);if(c.p>=c.e || *c.p!='"')return false;++c.p;int k=0;
    while(c.p<c.e && *c.p!='"'){if(*c.p=='\\' || static_cast<unsigned char>(*c.p)<0x20)return false;if(k+1<cap)out[k++]=*c.p;else return false;++c.p;}
    if(c.p>=c.e)return false;++c.p;out[k]=0;return true;
}
inline bool integer(Cursor& c,int& v) noexcept {
    ws(c);const char* s=c.p;bool neg=false;if(c.p<c.e && *c.p=='-'){neg=true;++c.p;}
    long long x=0;int digits=0;while(c.p<c.e && *c.p>='0' && *c.p<='9'){x=x*10+(*c.p-'0');if(++digits>9)return false;++c.p;}
    if(!digits){c.p=s;return false;}
    if(c.p<c.e && (*c.p=='.' || *c.p=='e' || *c.p=='E'))return false;   // integers only
    v=int(neg?-x:x);return true;
}
inline bool skipValue(Cursor& c,int depth=0) noexcept {   // any JSON value (for unknown keys), bounded depth
    if(depth>8)return false;ws(c);if(c.p>=c.e)return false;
    char tmp[64];
    if(*c.p=='"'){ws(c);++c.p;while(c.p<c.e && *c.p!='"'){if(*c.p=='\\'){++c.p;if(c.p>=c.e)return false;}++c.p;}if(c.p>=c.e)return false;++c.p;return true;}
    if(*c.p=='{'){++c.p;if(eat(c,'}'))return true;do{if(!str(c,tmp,sizeof tmp) || !eat(c,':') || !skipValue(c,depth+1))return false;}while(eat(c,','));return eat(c,'}');}
    if(*c.p=='['){++c.p;if(eat(c,']'))return true;do{if(!skipValue(c,depth+1))return false;}while(eat(c,','));return eat(c,']');}
    if(*c.p=='-' || (*c.p>='0' && *c.p<='9')){++c.p;while(c.p<c.e && ((*c.p>='0' && *c.p<='9') || *c.p=='.' || *c.p=='e' || *c.p=='E' || *c.p=='-' || *c.p=='+'))++c.p;return true;}
    for(const char* w:{"true","false","null"}){const std::size_t n=std::strlen(w);if(std::size_t(c.e-c.p)>=n && !std::strncmp(c.p,w,n)){c.p+=n;return true;}}
    return false;
}
template<std::size_t N> inline bool ints(Cursor& c,std::array<int,N>& v) noexcept {
    if(!eat(c,'['))return false;for(std::size_t i=0;i<N;++i){if(i && !eat(c,','))return false;if(!integer(c,v[i]))return false;}return eat(c,']');
}
template<std::size_t N,class F> inline bool rows(Cursor& c,int count,F f) noexcept {
    if(!eat(c,'['))return false;
    for(int i=0;i<count;++i){if(i && !eat(c,','))return false;std::array<int,N> v{};if(!ints(c,v))return false;f(i,v);}
    return eat(c,']');
}
} // namespace json

// Parse one record. False (and `out` untouched) on anything malformed, missing, mis-counted or out of range.
inline bool parse(const char* text,int len,Look& out) noexcept {
    if(!text || len<=0 || len>RecordMax)return false;
    json::Cursor c{text,text+len};Look l;
    bool format=false,version=false,model=false,blend=false,overlays=false,components=false,props=false;
    char key[32];
    if(!json::eat(c,'{'))return false;
    do {
        if(!json::str(c,key,sizeof key) || !json::eat(c,':'))return false;
        if(!std::strcmp(key,"format")){char v[48];if(!json::str(c,v,sizeof v) || std::strcmp(v,FormatName))return false;format=true;}
        else if(!std::strcmp(key,"version")){int v=0;if(!json::integer(c,v) || v!=FormatVersion)return false;version=true;}
        else if(!std::strcmp(key,"model")){char v[16];if(!json::str(c,v,sizeof v))return false;
            if(!std::strcmp(v,"male"))l.model=MaleModel;else if(!std::strcmp(v,"female"))l.model=FemaleModel;else return false;model=true;}
        else if(!std::strcmp(key,"blend")) {
            bool shape=false,shapeMix=false,skin=false,skinMix=false;
            if(!json::eat(c,'{'))return false;
            do {
                if(!json::str(c,key,sizeof key) || !json::eat(c,':'))return false;
                if(!std::strcmp(key,"shape")){std::array<int,2> v{};if(!json::ints(c,v))return false;l.shapeFirst=v[0];l.shapeSecond=v[1];shape=true;}
                else if(!std::strcmp(key,"shape_mix")){if(!json::integer(c,l.shapeMix))return false;shapeMix=true;}
                else if(!std::strcmp(key,"skin")){
                    json::ws(c);
                    if(c.p<c.e && *c.p=='"'){char v[8];if(!json::str(c,v,sizeof v) || std::strcmp(v,"disc"))return false;l.skinExplicit=false;}
                    else {std::array<int,2> v{};if(!json::ints(c,v))return false;l.skinExplicit=true;l.skinFirst=v[0];l.skinSecond=v[1];}
                    skin=true;}
                else if(!std::strcmp(key,"skin_mix")){if(!json::integer(c,l.skinMix))return false;skinMix=true;}
                else if(!json::skipValue(c))return false;
            } while(json::eat(c,','));
            if(!json::eat(c,'}') || !shape || !shapeMix || !skin || !skinMix)return false;
            if(!l.skinExplicit){if(l.skinMix!=0)return false;l.skinFirst=l.skinSecond=0;}
            blend=true;
        }
        else if(!std::strcmp(key,"overlays")){if(!json::rows<2>(c,OverlaySlots,[&](int i,const std::array<int,2>& v){l.overlay[std::size_t(i)]={v[0],v[1]};}))return false;overlays=true;}
        else if(!std::strcmp(key,"components")){if(!json::rows<3>(c,Components,[&](int i,const std::array<int,3>& v){l.part[std::size_t(i)]={v[0],v[1],v[2]};}))return false;components=true;}
        else if(!std::strcmp(key,"props")){if(!json::rows<2>(c,PropSlots,[&](int i,const std::array<int,2>& v){l.prop[std::size_t(i)]={v[0],v[1]};}))return false;props=true;}
        else if(!json::skipValue(c))return false;
    } while(json::eat(c,','));
    if(!json::eat(c,'}'))return false;
    json::ws(c);if(c.p!=c.e)return false;
    if(!(format && version && model && blend && overlays && components && props) || !inRange(l))return false;
    out=l;return true;
}

// ---------------------------------------------------------------------------------------------------- storage (the glue's side)
// One record per Sandbox generation: <base>/sandbox-mode/g<N>/online/character.json (character_store.h). Never the story profile.
struct Store {
    virtual ~Store()=default;
    virtual int read(char* buf,int cap) noexcept=0;            // bytes read; 0 = no record; < 0 = error
    virtual bool write(const char* buf,int len) noexcept=0;    // atomic replace; false = nothing changed
};

// ---------------------------------------------------------------------------------------------------- applying a look
struct Outcome {enum Kind:std::uint8_t {Running,Ok,Fail} kind=Running;char detail[64]{};};
inline Outcome running() noexcept {return Outcome{};}
template<class... A> inline Outcome finished(Outcome::Kind k,const char* f,A... a) noexcept {
    Outcome o;o.kind=k;
    if constexpr(sizeof...(a)==0)std::snprintf(o.detail,sizeof o.detail,"%s",f);else std::snprintf(o.detail,sizeof o.detail,f,a...);
    return o;
}
inline std::uint32_t call1(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a,bool* ok=nullptr) noexcept {
    std::uint32_t out[3]{};const bool r=nat.call(h,a.begin(),unsigned(a.size()),out);if(ok)*ok=r;return r?out[0]:0u;
}
inline void call0(Natives& nat,std::uint32_t h,std::initializer_list<std::uint32_t> a) noexcept {
    std::uint32_t out[3]{};(void)nat.call(h,a.begin(),unsigned(a.size()),out);
}
inline float frac(int permille) noexcept {return float(permille)/float(Permille);}
inline std::uint32_t u(int v) noexcept {return std::uint32_t(v);}

// The calls for one part of a look (shared by the full apply and the single-field edits).
inline void applyBlend(Natives& nat,std::uint32_t ped,const Look& l) noexcept {
    if(l.skinExplicit)
        call0(nat,n::HeadBlend,{ped,u(l.shapeFirst),u(l.shapeSecond),0,u(l.skinFirst),u(l.skinSecond),0,bits(frac(l.shapeMix)),bits(frac(l.skinMix)),bits(0.0f),1});
    else   // the disc creator's own pattern (maintransition 0x364a7 / 0x3951e / 0x395f5 / 0x3afa7)
        call0(nat,n::HeadBlend,{ped,u(l.shapeFirst),u(l.shapeSecond),0,0,0,0,bits(frac(l.shapeMix)),bits(0.0f),bits(0.0f),1});
}
inline void applyOverlay(Natives& nat,std::uint32_t ped,const Look& l,int slot) noexcept {
    const Overlay& o=l.overlay[std::size_t(slot)];
    call0(nat,n::HeadOverlay,{ped,u(slot),u(o.opacity?o.index:0),bits(frac(o.opacity))});
}
inline void applyPart(Natives& nat,std::uint32_t ped,const Look& l,int comp) noexcept {
    const Part& p=l.part[std::size_t(comp)];
    call0(nat,n::Component,{ped,u(comp),u(p.drawable),u(p.texture),u(p.palette)});
}
inline void applyProp(Natives& nat,std::uint32_t ped,const Look& l,int k) noexcept {
    const Prop& p=l.prop[std::size_t(k)];
    if(p.drawable<0)call0(nat,n::ClearProp,{ped,PropAnchor[std::size_t(k)]});
    else call0(nat,n::PropIndex,{ped,PropAnchor[std::size_t(k)],u(p.drawable),u(p.texture),0});
}

// The game's own counts for the current ped: a value the game does not have is reset (component/texture -> 0, overlay -> off,
// prop -> none) and counted, so a record made on another build or edited by hand can never ask for a missing asset. A count native
// that is not available leaves the value as it is (the static range still holds).
inline int clampToGame(Natives& nat,std::uint32_t ped,Look& l) noexcept {
    int clamped=0;bool ok=false;
    for(int c=0;c<Components;++c) {
        Part& p=l.part[std::size_t(c)];
        const std::uint32_t d=call1(nat,n::Drawables,{ped,u(c)},&ok);
        if(ok && p.drawable>=int(d)){if(p.drawable || p.texture)++clamped;p.drawable=0;p.texture=0;}
        const std::uint32_t t=call1(nat,n::Textures,{ped,u(c),u(p.drawable)},&ok);
        if(ok && p.texture>=int(t) && p.texture){++clamped;p.texture=0;}
    }
    for(int s=0;s<OverlaySlots;++s) {
        Overlay& o=l.overlay[std::size_t(s)];if(!o.opacity)continue;
        const std::uint32_t k=call1(nat,n::OverlayNum,{u(s)},&ok);
        if(ok && o.index>=int(k)){++clamped;o.index=0;o.opacity=0;}
    }
    for(int k=0;k<PropSlots;++k) {
        Prop& p=l.prop[std::size_t(k)];if(p.drawable<0)continue;
        const std::uint32_t d=call1(nat,n::PropDrawables,{ped,PropAnchor[std::size_t(k)]},&ok);
        if(ok && p.drawable>=int(d)){++clamped;p.drawable=-1;p.texture=0;continue;}
        const std::uint32_t t=call1(nat,n::PropTextures,{ped,PropAnchor[std::size_t(k)],u(p.drawable)},&ok);
        if(ok && p.texture>=int(t) && p.texture){++clamped;p.texture=0;}
    }
    return clamped;
}

// One full apply: the ped must carry the record's model; everything in ONE tick (blend, overlays, components, props), then wait
// (bounded) for the blend and the streaming, as the MP shops do, and FINALIZE_HEAD_BLEND when `finalize` (not while editing).
class Applier {
public:
    bool busy() const noexcept {return step_!=Step::Idle;}
    const Look& applied() const noexcept {return applied_;}
    Outcome begin(Natives& nat,std::uint32_t ped,const Look& look,bool finalize,std::uint32_t frame) noexcept {
        step_=Step::Idle;
        if(!ped)return finished(Outcome::Fail,"no-ped");
        const std::uint32_t m=call1(nat,n::EntityModel,{ped});
        if(m!=look.model)return finished(Outcome::Fail,"model-mismatch want=%08x got=%08x",look.model,m);
        Look l=look;clamped_=clampToGame(nat,ped,l);
        applyBlend(nat,ped,l);
        for(int s=0;s<OverlaySlots;++s)applyOverlay(nat,ped,l,s);
        for(int c=0;c<Components;++c)applyPart(nat,ped,l,c);
        for(int k=0;k<PropSlots;++k)applyProp(nat,ped,l,k);
        applied_=l;ped_=ped;finalize_=finalize;start_=frame;step_=Step::Wait;
        // v997 (the creator's "Pigtails Blonde" looked dark and short on the device): the hair as set, with the game's own counts
        hairN_=int(call1(nat,n::Drawables,{ped,2}));hairT_=int(call1(nat,n::Textures,{ped,2,std::uint32_t(l.part[2].drawable)}));
        return running();
    }
    Outcome step(Natives& nat,std::uint32_t frame) noexcept {
        if(step_!=Step::Wait)return finished(Outcome::Fail,"idle");
        const std::uint32_t ped=call1(nat,n::PlayerPedId,{});
        if(ped!=ped_){step_=Step::Idle;return finished(Outcome::Fail,"ped-changed");}
        if(call1(nat,n::BlendFinished,{ped}) && call1(nat,n::StreamingDone,{ped})) {
            if(finalize_)call0(nat,n::Finalize,{ped});
            step_=Step::Idle;return finished(Outcome::Ok,"look applied clamped=%d%s",clamped_,finalize_?" final":"");
        }
        if(frame-start_>=BlendWaitFramesMax){step_=Step::Idle;return finished(Outcome::Ok,"look applied clamped=%d blend=timeout",clamped_);}
        return running();
    }
    const char* abort() noexcept {const char* w=step_==Step::Wait?"applied":"idle";step_=Step::Idle;return w;}
    int clamped() const noexcept {return clamped_;}
    int hairCount() const noexcept {return hairN_;}       // v997: GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS(ped, 2) at the apply
    int hairTextures() const noexcept {return hairT_;}    // v997: GET_NUMBER_OF_PED_TEXTURE_VARIATIONS(ped, 2, drawable)
private:
    enum class Step:std::uint8_t {Idle,Wait};
    Step step_=Step::Idle;Look applied_{};std::uint32_t ped_=0,start_=0;int clamped_=0;bool finalize_=true;int hairN_=-1,hairT_=-1;   // v997
};

// ---------------------------------------------------------------------------------------------------- the trainer's view
// PROPOSED allocation (NOTES.md "Proposed allocations"): 17 CharacterLook, 18 CharacterEdit. Both Sandbox mode only.
constexpr int CharacterLookAction=17,CharacterEditAction=18;
// 17 a0: the operation.
enum LookOp : int {ApplySaved=0,SaveEdits=1,DiscardEdits=2,StartEditing=3,LookOpCount=4};
// 18 a0 = field << 16 | value (value 0..0xFFFF; 0xFFFF = -1 for a prop's "none").
enum Field : int {
    ShapeFirst=1,ShapeSecond=2,ShapeMix=3,SkinFirst=4,SkinSecond=5,SkinMix=6,SkinDisc=7,   // 7: value 1 = back to the disc pattern
    OverlayBase=16,      // 16 + 2*slot = index, 17 + 2*slot = opacity (slots 0..6 -> 16..29)
    PartBase=32,         // 32 + 3*comp = drawable, +1 texture, +2 palette (comps 0..11 -> 32..67)
    PropBase=72,         // 72 + 2*k = drawable (0xFFFF none), +1 texture (k 0..2 -> 72..77)
    FieldEnd=78
};
constexpr int encodeEdit(int field,int value) noexcept {return (field<<16)|(value&0xFFFF);}
constexpr int editField(int a0) noexcept {return (a0>>16)&0x7FFF;}
constexpr int editValue(int a0) noexcept {return (a0&0xFFFF)==0xFFFF?-1:(a0&0xFFFF);}

// Change one field of `l`. False if the field or value is not valid (l unchanged). `what` gets the part to re-apply.
enum class Touch:std::uint8_t {None,Blend,Overlay,Part,Prop};
inline bool editLook(Look& l,int field,int value,Touch& what,int& index) noexcept {
    Look t=l;what=Touch::None;index=0;
    if(field>=ShapeFirst && field<=SkinDisc) {
        what=Touch::Blend;
        switch(field){case ShapeFirst:t.shapeFirst=value;break;case ShapeSecond:t.shapeSecond=value;break;case ShapeMix:t.shapeMix=value;break;
        case SkinFirst:t.skinFirst=value;t.skinExplicit=true;break;case SkinSecond:t.skinSecond=value;t.skinExplicit=true;break;
        case SkinMix:t.skinMix=value;t.skinExplicit=true;break;
        case SkinDisc:if(value!=1)return false;t.skinExplicit=false;t.skinFirst=t.skinSecond=t.skinMix=0;break;}
    } else if(field>=OverlayBase && field<OverlayBase+2*OverlaySlots) {
        index=(field-OverlayBase)/2;what=Touch::Overlay;
        if((field-OverlayBase)%2==0)t.overlay[std::size_t(index)].index=value;else t.overlay[std::size_t(index)].opacity=value;
    } else if(field>=PartBase && field<PartBase+3*Components) {
        index=(field-PartBase)/3;what=Touch::Part;Part& p=t.part[std::size_t(index)];
        switch((field-PartBase)%3){case 0:if(p.drawable!=value)p.texture=0;p.drawable=value;break;case 1:p.texture=value;break;default:p.palette=value;}
    } else if(field>=PropBase && field<PropBase+2*PropSlots) {
        index=(field-PropBase)/2;what=Touch::Prop;Prop& p=t.prop[std::size_t(index)];
        if((field-PropBase)%2==0){if(p.drawable!=value)p.texture=0;p.drawable=value;}else{if(p.drawable<0)return false;p.texture=value;}
    } else return false;
    if(!inRange(t))return false;
    l=t;return true;
}

// v1509 (shops subagent, look_refresh.h): before the saved record is applied (17 0 ApplySaved and the respawn re-apply) a Refresher may
// bring it up to date with what the GAME itself changed since the record was made (Online mode: the shops' outfit stats). True = `l`
// changed. Null (Sandbox, Story, the host tests that set none) = the record as stored.
struct Refresher {
    virtual ~Refresher()=default;
    virtual bool refresh(Natives& nat,Look& l) noexcept=0;
};
// The creator: the saved record (from the Store), the edit buffer, the apply job, and the background re-apply after a respawn.
class Creator {
public:
    void setStore(Store* s) noexcept {store_=s;}
    void setRefresher(Refresher* r) noexcept {refresher_=r;}   // v1509
    bool editing() const noexcept {return editing_;}
    bool busy() const noexcept {return applier_.busy();}
    const Look& edits() const noexcept {return edit_;}
    bool hasSaved() const noexcept {return haveSaved_;}
    const Look& saved() const noexcept {return saved_;}

    static bool validArgument(int action,int a0) noexcept {
        if(action==CharacterLookAction)return a0>=0 && a0<LookOpCount;
        if(action==CharacterEditAction){const int f=editField(a0);return f>=ShapeFirst && f<FieldEnd;}
        return false;
    }
    // Read the saved record (once per Sandbox start, and before every ApplySaved: the file is the truth).
    // 1 = loaded, 0 = no record, -1 = unreadable or rejected (the record on disk is left alone).
    int load() noexcept {
        if(!store_)return 0;
        char buf[RecordMax+1];const int k=store_->read(buf,RecordMax+1);
        if(k==0){haveSaved_=false;return 0;}
        Look l;if(k<0 || k>RecordMax || !parse(buf,k,l))return -1;
        saved_=l;haveSaved_=true;return 1;
    }
    // The freemode model on the player now (0 = none); from online_world.h's Character.
    Outcome begin(Natives& nat,int action,int a0,std::uint32_t ped,std::uint32_t model,std::uint32_t frame) noexcept {
        if(!isFreemode(model))return finished(Outcome::Fail,"no-online-character");
        if(action==CharacterEditAction)return edit(nat,ped,model,editField(a0),editValue(a0));
        background_=false;pendingPed_=0;   // a requested look replaces a background re-apply (one Applier)
        switch(a0) {
        case ApplySaved: {
            const int r=load();
            if(r<0)return finished(Outcome::Fail,"bad-record");
            if(r>0 && saved_.model==model)refreshSaved(nat);   // v1509
            const Look l=r>0 && saved_.model==model?saved_:defaultLook(model);
            editing_=false;
            return applier_.begin(nat,ped,l,true,frame);}
        case StartEditing: {
            const int r=load();
            edit_=r>0 && saved_.model==model?saved_:defaultLook(model);editing_=true;
            return applier_.begin(nat,ped,edit_,false,frame);}
        case SaveEdits: {
            if(!editing_)return finished(Outcome::Fail,"not-editing");
            if(edit_.model!=model)return finished(Outcome::Fail,"model-mismatch");
            char buf[RecordMax];const int k=serialize(edit_,buf,sizeof buf);
            if(k<=0 || !store_ || !store_->write(buf,k))return finished(Outcome::Fail,"save-failed");
            saved_=edit_;haveSaved_=true;editing_=false;
            return applier_.begin(nat,ped,saved_,true,frame);}
        case DiscardEdits: {
            if(!editing_)return finished(Outcome::Fail,"not-editing");
            editing_=false;
            const Look l=haveSaved_ && saved_.model==model?saved_:defaultLook(model);
            return applier_.begin(nat,ped,l,true,frame);}
        default: return finished(Outcome::Fail,"bad-op");
        }
    }
    Outcome step(Natives& nat,std::uint32_t frame) noexcept {return applier_.step(nat,frame);}
    const char* abort() noexcept {pendingPed_=0;return applier_.abort();}
    const Applier& applier() const noexcept {return applier_;}   // v997 (the hair log)

    // A new player ped with the same freemode model (a respawn): its look is put back in the background, from the saved record
    // (or the edit buffer while editing). online_world.h's reconcile tells us; tickIdle runs it while no trainer job runs.
    void noteRespawn(std::uint32_t ped) noexcept {pendingPed_=ped;}
    template<class Log> void tickIdle(Natives& nat,std::uint32_t ped,std::uint32_t model,std::uint32_t frame,Log log) noexcept {
        if(background_) {
            const Outcome o=applier_.step(nat,frame);
            if(o.kind==Outcome::Running)return;
            background_=false;char line[112];std::snprintf(line,sizeof line,"RENDER_ONLINE look respawn %s %s",o.kind==Outcome::Ok?"ok":"fail",o.detail);log(line);
            return;
        }
        if(!pendingPed_ || applier_.busy())return;
        if(ped!=pendingPed_ || !isFreemode(model)){pendingPed_=0;return;}
        pendingPed_=0;
        if(!editing_ && haveSaved_ && saved_.model==model)refreshSaved(nat);   // v1509: a purchase since the visit's apply stays after a death
        const Look* l=editing_?&edit_:haveSaved_ && saved_.model==model?&saved_:nullptr;
        if(!l)return;   // nothing customised: the game's own state stays
        const Outcome o=applier_.begin(nat,ped,*l,!editing_,frame);
        if(o.kind==Outcome::Running){background_=true;return;}
        char line[112];std::snprintf(line,sizeof line,"RENDER_ONLINE look respawn fail %s",o.detail);log(line);
    }
private:
    // v1509: the refreshed record is applied whatever the write gives; written back (atomic replace) only when it changed, so the
    // creator menu, the hair read-back (online_mode.h B6) and the next visit read the same record. A failed write leaves the file as it
    // was: the next apply refreshes again from the same stats.
    void refreshSaved(Natives& nat) noexcept {
        if(!refresher_)return;
        Look l=saved_;
        if(!refresher_->refresh(nat,l) || l==saved_)return;
        saved_=l;
        if(store_){char buf[RecordMax];const int k=serialize(l,buf,sizeof buf);if(k>0)(void)store_->write(buf,k);}
    }
    Outcome edit(Natives& nat,std::uint32_t ped,std::uint32_t model,int field,int value) noexcept {
        if(!editing_)return finished(Outcome::Fail,"not-editing");
        if(applier_.busy())return finished(Outcome::Fail,"busy");
        if(edit_.model!=model)return finished(Outcome::Fail,"model-mismatch");
        if(!ped || call1(nat,n::EntityModel,{ped})!=model)return finished(Outcome::Fail,"model-mismatch");
        Touch what;int index=0;Look t=edit_;
        if(!editLook(t,field,value,what,index))return finished(Outcome::Fail,"bad-value field=%d value=%d",field,value);
        // the game's counts for the touched part only (a value the game lacks is refused, not clamped: the UI asked for it)
        bool ok=false;
        if(what==Touch::Part) {
            const Part& p=t.part[std::size_t(index)];
            const std::uint32_t d=call1(nat,n::Drawables,{ped,u(index)},&ok);if(ok && p.drawable>=int(d))return finished(Outcome::Fail,"no-such-drawable");
            const std::uint32_t tx=call1(nat,n::Textures,{ped,u(index),u(p.drawable)},&ok);if(ok && p.texture>=int(tx) && p.texture)return finished(Outcome::Fail,"no-such-texture");
        } else if(what==Touch::Overlay) {
            const Overlay& o=t.overlay[std::size_t(index)];
            const std::uint32_t k=call1(nat,n::OverlayNum,{u(index)},&ok);if(ok && o.opacity && o.index>=int(k))return finished(Outcome::Fail,"no-such-overlay");
        } else if(what==Touch::Prop) {
            const Prop& p=t.prop[std::size_t(index)];
            if(p.drawable>=0) {
                const std::uint32_t d=call1(nat,n::PropDrawables,{ped,PropAnchor[std::size_t(index)]},&ok);if(ok && p.drawable>=int(d))return finished(Outcome::Fail,"no-such-prop");
                const std::uint32_t tx=call1(nat,n::PropTextures,{ped,PropAnchor[std::size_t(index)],u(p.drawable)},&ok);if(ok && p.texture>=int(tx) && p.texture)return finished(Outcome::Fail,"no-such-texture");
            }
        }
        edit_=t;
        switch(what){case Touch::Blend:applyBlend(nat,ped,edit_);break;case Touch::Overlay:applyOverlay(nat,ped,edit_,index);break;
        case Touch::Part:applyPart(nat,ped,edit_,index);break;case Touch::Prop:applyProp(nat,ped,edit_,index);break;default:break;}
        return finished(Outcome::Ok,"edit field=%d value=%d",field,value);
    }
    Refresher* refresher_=nullptr;   // v1509
    Store* store_=nullptr;Applier applier_;Look saved_{},edit_{};bool haveSaved_=false,editing_=false,background_=false;std::uint32_t pendingPed_=0;
};

// The creator never calls a network, stats, cloud or save native (route B is fully local).
constexpr bool forbiddenName(const char* s) noexcept {
    const auto starts=[](const char* a,const char* p){while(*p){if(*a!=*p)return false;++a;++p;}return true;};
    return starts(s,"NETWORK_") || starts(s,"STAT_") || starts(s,"NET_") || starts(s,"UGC_") || starts(s,"CLOUD_") || starts(s,"DATAFILE_")
        || starts(s,"SC_") || starts(s,"PLAYSTATS_") || starts(s,"SET_SAVE") || starts(s,"DO_AUTO_SAVE");
}
constexpr bool noForbidden() noexcept {for(const auto& x:Used)if(forbiddenName(x.name))return false;return true;}
static_assert(noForbidden(),"the character creator must not call network / stats / save natives");

} // namespace recomp::creator
