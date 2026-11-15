#pragma once
// story_create_local.h (v1352, update-1 session 2026-11-10; lead 01:3x; update-1/activities-end/NOTES.md section 8). Pure: no guest memory, no
// platform calls; story_create_local.inc supplies the memory and the wrappers.
// OBSERVED (Thor, v1155, Sandbox + our local session; t-storewanted-20261110-0118, shoprobberies traced): the Story store-robbery brain creates
// its clerk with CREATE_PED(4, model, x, y, z, heading, isNetwork 1, scriptHost 1) (shoprobberies 0xd4ae) and gets 0; every later
// IS_ENTITY_DEAD(clerk) reads 1, so the script raises the wanted level itself (0xaec -> SET_PLAYER_WANTED_LEVEL 0xb23) and police come.
// In Story the isNetwork flag is ignored (no network game); with our session up a network game is in progress and the engine refuses a
// networked entity from a script that never registered as a network script (no reservation). Any Story world script that passes 1 breaks so.
// Answer: in Sandbox mode with our local session up, a create by a NON-network script with isNetwork 1 is made with 0 - Story's own result,
// a local entity. Network scripts (every am_* activity, freemode, maintransition: they call NETWORK_SET_THIS_SCRIPT_IS_NETWORK_SCRIPT) keep
// their networked entities; Online mode and plain Sandbox / Story (no session) are never touched.
// "Network script" is the engine's own test, called as the native calls it: NETWORK_GET_THIS_SCRIPT_IS_NETWORK_SCRIPT sub_82DA7270 ->
// sub_82A954D8 (the current thread sub_825453F8 = *0x83C5CA24, its network handler at +168, then the handler's virtual 6 -> the handler or 0).
// v1354: v1353 used "a handler is present" and made NOTHING local (t-storelocalon-20261110-0315: 0 RENDER_STORY_LOCAL lines, the clerk still
// 0): with our session up the Story scripts have a handler too; only the virtual says no. The wrapper supplies the test (isNet below).
// Switch: default ON since v1165 (lead 03:2x; device-proven on v1354: t-storelocalon-20261110-0353, t-actsoak-20261110-0358); files/private-trace/diag-story-local-create-off turns it off.
#include <cstdint>
namespace recomp::story_local {
inline constexpr const char* SwitchName="diag-story-local-create-off";
constexpr std::uint32_t CurrentThread=0x83C5CA24u,NetHandlerOff=168u,ArgsOff=8u;
// The creates and their isNetwork argument (word index in the native's argument block, ctx+8): from the disc scripts' own calls
// (shoprobberies 0xd4ae CREATE_PED a=8 (type, model, x, y, z, heading, isNetwork, scriptHost); 0xd41a CREATE_OBJECT a=7 (model, x, y, z,
// isNetwork, scriptHost, dynamic); 0xc004 CREATE_VEHICLE a=7 (model, x, y, z, heading, isNetwork, scriptHost)).
struct Create {const char* name;std::uint32_t handler;unsigned netArg;unsigned argc;};
inline constexpr Create Ped{"CREATE_PED",0x82DAE978u,6,8},Vehicle{"CREATE_VEHICLE",0x82E06618u,5,7},Object{"CREATE_OBJECT",0x82E03B20u,4,7},
    ObjectNoOffset{"CREATE_OBJECT_NO_OFFSET",0x82E03B30u,4,7};
inline bool plausible(std::uint32_t p) noexcept {return p>=0x10000u && p<0xFFFF0000u && !(p&3u);}
// The decision, from the facts the wrapper reads.
inline bool wanted(bool switchOn,bool sandbox,bool onlineMode,bool session,bool networkScript,std::uint32_t netArg) noexcept {
    return switchOn && sandbox && !onlineMode && session && !networkScript && netArg==1u;
}
// Mem: load(addr) -> word, store(addr, word). IsNet: () -> bool, the engine's test for the calling script (sub_82A954D8 != 0); it is asked
// only when every other fact holds and the argument is 1 (a guest call). Decision (optional): sees (isNetwork value, network script) of
// every call it was asked about, for the wrapper's log.
template<class Mem,class IsNet> bool apply(Mem& m,std::uint32_t ctxp,const Create& c,bool switchOn,bool sandbox,bool onlineMode,bool session,
                                           IsNet&& isNet,int* askedNet=nullptr) noexcept {
    if(askedNet)*askedNet=-1;
    if(!switchOn || !sandbox || onlineMode || !session)return false;   // the cheap facts first: no guest load otherwise
    if(!plausible(ctxp))return false;
    const std::uint32_t args=m.load(ctxp+ArgsOff);
    if(!plausible(args))return false;
    const std::uint32_t at=args+4u*c.netArg,v=m.load(at);
    if(v!=1u)return false;                                            // nothing to change: the engine is not asked
    const bool net=isNet();
    if(askedNet)*askedNet=net?1:0;
    if(!wanted(switchOn,sandbox,onlineMode,session,net,v))return false;
    m.store(at,0u);
    return true;
}
}
