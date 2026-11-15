#pragma once
// online_script_name.h (v1088, trainer-mods): which script is calling a native. At a native handler's entry the VM's r23 still holds the
// calling thread's context (the VM NATIVE handler 0x8343A170..0x8343A208 stores IP / frame / SP at +12 / +16 / +20 of r23 and reads the
// state at +8 after the call): word +4 = the script's name hash (scrThreadContext). Names for the scripts the Online runs care about.
#include <cstdint>
namespace recomp::online_script {
inline const char* name(std::uint32_t h) noexcept {
    switch(h){
    case 0xefe7c380u:return "fm_intro";case 0xc875557du:return "freemode";case 0x5ff2841fu:return "maintransition";
    case 0x76f8df52u:return "selector";case 0x6d3e6acbu:return "startup_positioning";case 0x5700179cu:return "main_persistent";
    case 0x52f86d6au:return "pausemenu_multiplayer";case 0x06f27211u:return "am_pi_menu";case 0x918fcbcdu:return "fm_main_menu";case 0x27eb33d7u:return "main";
    default:return nullptr;}
}
}
