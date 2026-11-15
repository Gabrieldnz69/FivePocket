#pragma once
// face_camera.h (v959, Update 1 Online World: the character creator's face camera). Included by character_creator.h; online_world.h's
// Actions owns one FaceCamera and drives it from the creator's results and the character's reconciliation. Pure: no guest memory, no
// files, no platform calls; natives through trainer::Natives on the script thread. Notes: local-analysis/update-1/v959/NOTES.md.
//
// What the player's own disc says (names-only bytecode read, NOTES.md section 1):
//  - The disc's own creator (maintransition) has NO script camera: its creator subtree (0x38c97, 473 functions) calls no CREATE_CAM*,
//    no RENDER_SCRIPT_CAMS; it is a frontend menu (ACTIVATE_FRONTEND_MENU, TAKE_CONTROL_OF_FRONTEND) with its own ped.
//  - The disc's own player face shot is the interaction menu's (am_pi_menu 0x2b5dc / 0x2b75b / teardown 0x13709):
//      CREATE_CAM_WITH_PARAMS("DEFAULT_SCRIPTED_CAMERA", GET_ENTITY_COORDS(PLAYER_PED_ID, TRUE), 0, 0, 0, 50.0, FALSE, 2)
//      ATTACH_CAM_TO_PED_BONE(cam, PLAYER_PED_ID, 31086, <0.0, 0.7, 0.0>, TRUE)      (static 4:79 = 0, 0.7, 0 at 0x95..0xa0)
//      POINT_CAM_AT_PED_BONE(cam, PLAYER_PED_ID, 31086, <static 4:82>, TRUE)         (never stored; the static table holds 0: INFERENCE)
//      SET_CAM_FOV(cam, 50.0); SET_CAM_ACTIVE(cam, TRUE); RENDER_SCRIPT_CAMS(TRUE, FALSE, 3000, TRUE, FALSE)
//    and its teardown: SET_CAM_ACTIVE(cam, FALSE); RENDER_SCRIPT_CAMS(FALSE, FALSE, 3000, TRUE, FALSE); DESTROY_CAM(cam, FALSE).
//    (It also turns player control off and clears the ped's tasks; this camera does not: nothing else to give back on an exit path.)
//    31086 is the head bone the disc passes to GET_PED_BONE_COORDS at 384 literal sites.
//
// Argument slots (our recompiled handlers, validated-ppc; tools/check_cam_natives.py re-checks them and the disc's NATIVE shapes):
//  CREATE_CAM_WITH_PARAMS sub_82D583B8 10 -> cam (name +0, position +4..+12, rotation +16..+24, fov float +28, active +32, order +36).
//  ATTACH_CAM_TO_PED_BONE sub_82D6E018 7 (cam, ped, bone, offset +12..+20, relative +24); POINT_CAM_AT_PED_BONE sub_82D6E078 7 (same).
//  SET_CAM_FOV sub_82D588F0 2 (float +4). SET_CAM_ACTIVE sub_82D585D0 2. RENDER_SCRIPT_CAMS sub_82D58310 5. DESTROY_CAM sub_82D584E8 2.
//  DOES_CAM_EXIST sub_82D58570 1 -> BOOL. GET_ENTITY_COORDS sub_82D773A0 2 -> vector (the trainer's spawn path uses it the same way).
//
// The rule: the camera is on only between a successful creator edit (or a finished StartEditing) and the first of: finalize, cancel,
// apply-saved, any refused / failed creator request, a blocked creator job, a character change (action 15), the game replacing the
// player's ped or model, the camera no longer existing, the end of Sandbox. Every release issues RENDER_SCRIPT_CAMS(FALSE) exactly once
// (and SET_CAM_ACTIVE / DESTROY_CAM when the camera still exists); a second release is a no-op. A missing native: no camera at all
// (logged once), the edits work as before.
//
// Frame rate: off = no native call. On = one DOES_CAM_EXIST per Sandbox tick while editing. Begin: 8 calls once; release: 4 calls once.
#include "trainer.h"
#include <cstdint>
#include <cstdio>

namespace recomp::facecam {
using trainer::Natives;
using trainer::joaat;
using trainer::bits;

struct Named {const char* name;std::uint32_t hash;};
namespace n {
constexpr std::uint32_t CreateCamWithParams=joaat("CREATE_CAM_WITH_PARAMS"),AttachToPedBone=joaat("ATTACH_CAM_TO_PED_BONE"),
    PointAtPedBone=joaat("POINT_CAM_AT_PED_BONE"),SetFov=joaat("SET_CAM_FOV"),SetActive=joaat("SET_CAM_ACTIVE"),
    RenderScriptCams=joaat("RENDER_SCRIPT_CAMS"),DestroyCam=joaat("DESTROY_CAM"),DoesCamExist=joaat("DOES_CAM_EXIST"),
    EntityCoords=joaat("GET_ENTITY_COORDS");
}
inline constexpr Named Used[]{{"CREATE_CAM_WITH_PARAMS",n::CreateCamWithParams},{"ATTACH_CAM_TO_PED_BONE",n::AttachToPedBone},
    {"POINT_CAM_AT_PED_BONE",n::PointAtPedBone},{"SET_CAM_FOV",n::SetFov},{"SET_CAM_ACTIVE",n::SetActive},
    {"RENDER_SCRIPT_CAMS",n::RenderScriptCams},{"DESTROY_CAM",n::DestroyCam},{"DOES_CAM_EXIST",n::DoesCamExist},
    {"GET_ENTITY_COORDS",n::EntityCoords}};
inline constexpr std::uint32_t UsedNatives[]{n::CreateCamWithParams,n::AttachToPedBone,n::PointAtPedBone,n::SetFov,n::SetActive,
    n::RenderScriptCams,n::DestroyCam,n::DoesCamExist,n::EntityCoords};
static_assert(std::size(Used)==std::size(UsedNatives));

// The shot (am_pi_menu's own values; whether it frames the face from the front on our build: NEEDS DEVICE CHECK, NOTES.md 5).
constexpr const char* CameraName="DEFAULT_SCRIPTED_CAMERA";
constexpr std::uint32_t HeadBone=31086;         // SKEL_Head
constexpr float AttachX=0.0f,AttachY=0.7f,AttachZ=0.0f,PointX=0.0f,PointY=0.0f,PointZ=0.0f,Fov=50.0f;
constexpr std::uint32_t RotOrder=2,RenderEaseMs=3000;

using LogFn=void(*)(const char*);
using HasFn=bool(*)(std::uint32_t hash);       // the glue: is this native registered on this build (null: assume so)

class FaceCamera {
public:
    void setLog(LogFn f) noexcept {log_=f;}
    void setNativeCheck(HasFn f) noexcept {has_=f;}
    bool on() const noexcept {return on_;}

    // An edit session wants the face shot on `ped`. Idempotent: on the same ped with the camera still there nothing is created.
    // True when the camera is on afterwards.
    bool begin(Natives& nat,std::uint32_t ped,const char* why) noexcept {
        if(on_) {
            if(ped==ped_ && exists(nat))return true;
            release(nat,ped!=ped_?"ped-changed":"lost");
        }
        if(unavailable_ || !ped)return false;
        if(has_)for(const auto& u:Used)if(!has_(u.hash)){unavailable_=true;logf("RENDER_ONLINE look camera=off reason=missing-native name=%s",u.name);return false;}
        std::uint32_t at[3]{};const std::uint32_t ca[2]{ped,1};
        if(!nat.call(n::EntityCoords,ca,2,at))return skip("coords-failed");
        const std::uint32_t name=nat.text(CameraName);
        if(!name)return skip("no-scratch");
        std::uint32_t out[3]{};
        const std::uint32_t cp[10]{name,at[0],at[1],at[2],bits(0.0f),bits(0.0f),bits(0.0f),bits(Fov),0,RotOrder};
        if(!nat.call(n::CreateCamWithParams,cp,10,out))return skip("create-failed");
        const std::uint32_t cam=out[0];
        if(!existsCam(nat,cam))return skip("create-failed");
        const std::uint32_t attach[7]{cam,ped,HeadBone,bits(AttachX),bits(AttachY),bits(AttachZ),1};
        const std::uint32_t point[7]{cam,ped,HeadBone,bits(PointX),bits(PointY),bits(PointZ),1};
        const std::uint32_t fov[2]{cam,bits(Fov)},active[2]{cam,1};
        if(!nat.call(n::AttachToPedBone,attach,7,out) || !nat.call(n::PointAtPedBone,point,7,out) || !nat.call(n::SetFov,fov,2,out)
           || !nat.call(n::SetActive,active,2,out)) {
            destroy(nat,cam);return skip("setup-failed");
        }
        const std::uint32_t render[5]{1,0,RenderEaseMs,1,0};
        if(!nat.call(n::RenderScriptCams,render,5,out)){deactivate(nat,cam);destroy(nat,cam);return skip("render-failed");}
        on_=true;cam_=cam;ped_=ped;
        logf("RENDER_ONLINE look camera=on reason=%s cam=%u ped=%08x",why?why:"edit",cam,ped);
        return true;
    }
    // Every exit path. False when the camera was already off (nothing called).
    bool release(Natives& nat,const char* reason) noexcept {
        if(!on_)return false;
        on_=false;
        const bool still=existsCam(nat,cam_);
        if(still)deactivate(nat,cam_);
        std::uint32_t out[3]{};const std::uint32_t render[5]{0,0,RenderEaseMs,1,0};
        (void)nat.call(n::RenderScriptCams,render,5,out);   // always: script cams are never left rendering by us
        if(still)destroy(nat,cam_);
        logf("RENDER_ONLINE look camera=off reason=%s%s",reason?reason:"release",still?"":" cam=gone");
        cam_=0;ped_=0;
        return true;
    }
    // Every Sandbox tick (online_world.h Actions::reconcile): nothing while off; while on, the reasons to let go, then one existence check.
    // (Every way the edit session itself ends, 17 0 / 1 / 2, releases at its request: Actions::begin.)
    void tick(Natives& nat,std::uint32_t ped,bool onlineCharacter) noexcept {
        if(!on_)return;
        if(!onlineCharacter){release(nat,"model-changed");return;}
        if(ped!=ped_){release(nat,"ped-changed");return;}
        if(!existsCam(nat,cam_))release(nat,"lost");
    }
private:
    bool exists(Natives& nat) noexcept {return existsCam(nat,cam_);}
    static bool existsCam(Natives& nat,std::uint32_t cam) noexcept {
        std::uint32_t out[3]{};const std::uint32_t a[1]{cam};return nat.call(n::DoesCamExist,a,1,out) && out[0];
    }
    static void deactivate(Natives& nat,std::uint32_t cam) noexcept {std::uint32_t out[3]{};const std::uint32_t a[2]{cam,0};(void)nat.call(n::SetActive,a,2,out);}
    static void destroy(Natives& nat,std::uint32_t cam) noexcept {std::uint32_t out[3]{};const std::uint32_t a[2]{cam,0};(void)nat.call(n::DestroyCam,a,2,out);}
    bool skip(const char* why) noexcept {logf("RENDER_ONLINE look camera=off reason=%s",why);return false;}
    template<class... A> void logf(const char* f,A... a) const noexcept {if(!log_)return;char line[128];std::snprintf(line,sizeof line,f,a...);log_(line);}
    LogFn log_=nullptr;HasFn has_=nullptr;bool on_=false,unavailable_=false;std::uint32_t cam_=0,ped_=0;
};

// No network / stats / save native here either.
constexpr bool forbiddenName(const char* s) noexcept {
    const auto starts=[](const char* a,const char* p){while(*p){if(*a!=*p)return false;++a;++p;}return true;};
    return starts(s,"NETWORK_") || starts(s,"STAT_") || starts(s,"NET_") || starts(s,"UGC_") || starts(s,"CLOUD_") || starts(s,"DATAFILE_")
        || starts(s,"SC_") || starts(s,"PLAYSTATS_") || starts(s,"SET_SAVE") || starts(s,"DO_AUTO_SAVE");
}
constexpr bool noForbidden() noexcept {for(const auto& x:Used)if(forbiddenName(x.name))return false;return true;}
static_assert(noForbidden(),"the face camera must not call network / stats / save natives");

} // namespace recomp::facecam
