#pragma once
#include "guest_cooperative_executor.h"
#include "guest_import_registry.h"
#include "native_tile_command_interrupt.h"
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace recomp::bootstrap {
// Shared by the live runner and original-worker diagnostic. The
// original waiter is not replaced: a distinct registered CPU2 IRQ signals it,
// then returns exclusively to the submitter before any ordinary waiter runs.
// Caller owns the serialized executor token and current command allocations.
// Execute must run the real ordered native packet consumer (host fixtures must
// explicitly identify their authored prefix). No ring retirement is supplied.
struct NativeTileSceneHandoffResult {
    bool admitted=false,returned=false;
    unsigned worker=0,scratchWrites=0,scratchWaits=0,guestReturns=0;
};
using NativeTileScenePacketExecute=std::function<bool(
    const gpu::NativeTileCommandInterrupt::Memory&,const gpu::NativeTileCommandInterrupt::Guest&)>;
using NativeTileSceneGuestInvoke=std::function<bool(const GuestPreparedThread&,const GuestThreadEntryState&)>;
inline NativeTileSceneHandoffResult executeNativeTileSceneHandoff(
        GuestImportRegistry& registry,GuestCooperativeExecutor& executor,unsigned device,
        const gpu::Gta5TileCommandInterrupt& request,const NativeTileScenePacketExecute& execute,
        const NativeTileSceneGuestInvoke& invoke) {
    using Plan=gpu::Gta5TileCommandInterrupt;using Kind=Plan::Kind;
    struct State {
        Plan request;NativeTileScenePacketExecute execute;NativeTileSceneGuestInvoke invoke;
        std::optional<GuestImportRegistry::StoppedPhysicalOwnership> scratchOwner,contextOwner;
        NativeTileSceneHandoffResult result;bool armed=false;
    };
    // A predicated continuation may use this only when its real engine selects
    // the IRQ. A skipped IRQ must not allocate a worker or enter this helper.
    if(!execute || !invoke)return {};
    auto s=std::make_shared<State>(State{request,execute,invoke,{},{},{},false});
    const auto same=[](const auto& a,const auto& b) {
        return a && b && a->physicalBase==b->physicalBase && a->virtualBase==b->virtualBase &&
            a->byteCount==b->byteCount && a->protection==b->protection && a->writable==b->writable;
    };
    // This checks that we actually own an executor scope; a naked callback may
    // not allocate/run an IRQ while unrelated guest writers remain runnable.
    const bool admitted=executor.runNativeNonYielding([&] {
        if(registry.snapshotNativeInterruptDeviceRoot(device)!=std::optional{true})return;
        s->scratchOwner=registry.stoppedGraphicsPhysicalOwnership(request.scratchPhysical(),32);
        s->contextOwner=registry.stoppedGraphicsPhysicalOwnership(request.callbackContext()-0xC0000000,4);
        if(!s->scratchOwner || !s->scratchOwner->writable || !s->contextOwner ||
           !registry.copySerializedGraphicsData(request.scratchPhysical(),32) ||
           !registry.copySerializedGraphicsData(request.callbackContext()-0xC0000000,4))return;
        const auto worker=registry.createNativeTileSceneInterruptWorker(device,request.callbackContext(),
            [&,s,same,device](const auto& thread,const auto& entry) {
                return executor.enqueue(thread.layout.threadAddress,[&,s,same,thread,entry,device] {
                    if(!s->armed)throw std::runtime_error("Scene IRQ entered without exclusive native handoff");
                    const auto owned=[&] {
                        return same(s->scratchOwner,registry.stoppedGraphicsPhysicalOwnership(s->request.scratchPhysical(),32)) &&
                            same(s->contextOwner,registry.stoppedGraphicsPhysicalOwnership(s->request.callbackContext()-0xC0000000,4)) &&
                            registry.snapshotNativeInterruptDeviceRoot(device)==std::optional{true};
                    };
                    const gpu::NativeTileCommandInterrupt::Memory memory=[&](const auto& op) {
                        if(!owned() || op.target>=8 || (op.kind!=Kind::ScratchWrite && op.kind!=Kind::WaitScratch) ||
                           std::find(s->request.operations().begin(),s->request.operations().end(),op)==s->request.operations().end())return false;
                        const auto physical=s->request.scratchPhysical()+op.target*4;
                        const auto before=registry.copySerializedGraphicsData(physical,4);
                        if(!before || before->size()!=4)return false;
                        const unsigned value=(unsigned((*before)[0])<<24)|(unsigned((*before)[1])<<16)|
                            (unsigned((*before)[2])<<8)|(*before)[3];
                        if(op.kind==Kind::WaitScratch) {
                            if(value!=op.value)return false;++s->result.scratchWaits;return true;
                        }
                        std::vector<std::uint8_t> after(4);
                        for(unsigned b=0;b<4;++b)after[b]=op.value>>(24-b*8);
                        const auto address=s->scratchOwner->virtualBase+(physical-s->scratchOwner->physicalBase);
                        if(!registry.publishSerializedGraphicsData(address,*before,after))return false;
                        ++s->result.scratchWrites;return true;
                    };
                    const gpu::NativeTileCommandInterrupt::Guest guest=[&](const auto& actual) {
                        if(actual!=s->request || s->result.guestReturns || !owned())return false;
                        bool originalReturned=false;
                        if(!registry.runNativeTileSceneInterruptScope(device,s->request.callbackContext(),
                            [&](const auto& f){return executor.runNativeNonYielding(f,4096);},
                            [&]{originalReturned=s->invoke(thread,entry);}) || !originalReturned || !owned())return false;
                        const auto status=registry.nativeTileSceneInterruptStatus();
                        if(!status.returned || !status.balanced || status.failed || status.held ||
                           status.worker!=s->result.worker || status.lockPairs!=1 || status.eventSignals!=1)return false;
                        ++s->result.guestReturns;return true;
                    };
                    if(!owned() || !s->execute(memory,guest) || s->result.guestReturns!=1 ||
                       s->result.scratchWrites!=4 || s->result.scratchWaits!=4 || !owned())
                        throw std::runtime_error("Scene native packets/original IRQ did not complete; ring not retired");
                    if(!registry.markGuestThreadReturned(s->result.worker,0))
                        throw std::runtime_error("Scene IRQ owned worker return rejected");
                    s->result.returned=true;
                });
            });
        if(worker){s->result.worker=worker->layout.threadAddress;s->result.admitted=true;}
    },1);
    if(!admitted || !s->result.admitted)return s->result;
    s->armed=true;
    // Deliberately OUTSIDE the non-yielding scope; this is the executor's
    // exclusive worker handoff, not an ordinary yield admitting other writers.
    const bool returned=executor.runNativeWorkerNext(s->result.worker,4096);
    s->armed=false;s->result.returned=returned && s->result.returned;
    return s->result;
}
}
