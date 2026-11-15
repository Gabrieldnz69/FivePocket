// online_photo_cloud_hook.inc (v1181, trainer-mods; OWNER ORDER "check to make sure the phone in GTA O works": Snapmatic hangs). Included by
// renderer_boundary_trace.cpp after online_stats_cloud_hook.inc (uses its localOn / setStatus).
// The game's own path (t-onlcam v1170 trace + v1180 diag-photoq, recompiled code): appcamera queues QUEUE_OPERATION_TO_CREATE_SORTED_LIST_OF_PHOTOS
// (a type-1 request on the save-operation queue 0x839CDB70) and shows the busy spinner until GET_STATUS_OF_SORTED_LIST_OPERATION leaves 1;
// its CELL_CAM_ACTIVATE and the B handling come after. Type 1 (sub_8294F6A0) starts the photo op 0x837C72CC on the worker queue 0x839C9D50;
// the op (vtable 0x8203FE2C, process sub_82955318) asks the photo manager (*0x839C9F58) to list photos: sub_8294F460 = the cloud gate
// sub_824A7CB8, then sub_82FB4438 = an rlCloud request (Snapmatic photos live in the cloud in GTA Online) with the netStatus at manager+5392;
// manager+5400 = 1 (busy). The manager's update sub_8227F490 waits for that netStatus to leave 1 (pending): op 3 (succeeded) stores the
// listing count (manager+5388, zeroed before the call; 96 max) and sets +5400 = 2; the op then finishes (status 0) and the camera opens.
// In Online local mode nothing answers the request: the netStatus stays 1, the spinner forever (v1180: worker queue head = the photo op, st 1).
// This hook, Online local mode only (online_stats_cloud::localOn(); diag-cloud-real keeps it real), the call at lr 0x8294F514 only: no request;
// the netStatus pending then succeeded 200 with 0 photos (an empty cloud gallery), returns 1 (issued), as the stats legs answer theirs.
// Every other caller of sub_82FB4438 (one more in the corpus): the original. LINK CONDITION: redirect-v1181-photos.ld (= redirect-v1504-charslot.ld
// without the wrapper of sub_82FB4438).
namespace online_photo_cloud {
constexpr std::uint32_t LrPhotoList=0x8294F514u,ManagerStatusOff=5392u;
inline std::atomic<unsigned> answered{0};
}
PPC_FUNC_IMPL(__imp__sub_82FB4438);
PPC_FUNC(sub_82FB4438) {   // rlCloud request (the photo manager's listing at lr 0x8294F514 answered locally)
    namespace op=online_photo_cloud;namespace oc=online_stats_cloud;
    if(std::uint32_t(ctx.lr)!=op::LrPhotoList){__imp__sub_82FB4438(ctx,base);return;}
    if(!oc::localOn()) {   // v1182 (diagnostic): the same request outside Online local mode goes to the original (logged: 4 lines)
        static std::atomic<unsigned> n{0};if(n.fetch_add(1,std::memory_order_relaxed)<4){char l[160];std::snprintf(l,sizeof l,"RENDER_ONLINE_PHOTOS cloud listing (manager %08x) NOT answered: Online local mode off (the original runs)",ctx.r31.u32);loading_hold_hook::log(l);}
        __imp__sub_82FB4438(ctx,base);return;}
    const PPCContext saved=ctx;const std::uint32_t mgr=ctx.r31.u32,status=mgr+op::ManagerStatusOff;
    oc::setStatus(ctx,base,status,1,0);oc::setStatus(ctx,base,status,3,200);
    if(op::answered.fetch_add(1,std::memory_order_relaxed)<8){char l[200];std::snprintf(l,sizeof l,"RENDER_ONLINE_PHOTOS cloud listing (manager %08x) answered locally: succeeded 200, 0 photos (count %u)",mgr,PPC_LOAD_U32(mgr+5388u));loading_hold_hook::log(l);}
    ctx=saved;ctx.r3.u64=1;
}
