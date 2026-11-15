// online_local_registry.inc (Update 1; lane upd1-spoof/session-stubs, on lane upd1-online/local-session-stubs). PROTOTYPE,
// NOT IN ANY BUILD. Included inside GuestImportRegistry::dispatchUnderRegistryLock immediately BEFORE
// `#include "guest_saves_dispatch.inc"` (patch 0001, v963). Member scope: `importName`, `call`, the registry lock and the
// private helpers are in scope. Host-tested against a fake registry with the same member signatures (tests/glue_harness.cpp;
// tests/check_fakes.py checks those signatures against the real header and the v963 registry source).
//
// Cost in Story mode / switch off: online_local_glue::enabled() is one relaxed atomic load of a latched int (after the
// play-mode latch settles) and a branch; nothing else runs. Switch ON in Sandbox: Service::claims() (a prefix compare and,
// for ObDereferenceObject / NtClose, a 16-bit tag compare of r3; no lock) keeps every other import, the hot waits/events/
// critical sections/TLS included, on today's path; only claimed names build the adapter and take the service mutex.
//
// Memory: every guest access goes through validGuestDataBuffer (+ hasActiveCriticalSectionInRange for writes), the same
// bounds the saves path uses. The last error goes to the guest thread's field (KTHREAD+0x160), as dispatchNetCleanup does.
// XOVERLAPPED completion (S7 XGI session messages) is written and its event signalled exactly as guest_saves_dispatch.inc's
// `complete` does: [0] result, [1] length, [6] extended error; no APC routine; the event must be an owned event handle.
// v1009: the L1 import trace window (2 s after Host): every import that reaches the registry, BEFORE the online-local service, the
// saves path or the signin spoof answer it (first-use logs cannot show repeated calls). Closed: one relaxed load.
if (::recomp::online_local_glue::enabled() && (importName.rfind("__imp__X", 0) == 0 || importName.rfind("__imp__NetDll", 0) == 0)) {   // XAM / XNet / XMsg / XeCrypt / sockets only
    if (::recomp::online_local_glue::traceOn()) {
        char hexBuf[72] = "-";   // XMsg*: the first 32 bytes of the request buffer (r6 for XMsgStartIORequest / InProcessCall) when readable
        if (importName.rfind("__imp__XMsg", 0) == 0 && call.argument32(3) && validGuestDataBuffer(call.argument32(3), 32, false, 4)) {
            const auto* b = static_cast<const std::uint8_t*>(addressSpace_->translate(call.argument32(3)));
            for (int i = 0; i < 32; ++i) std::snprintf(hexBuf + 2 * i, 3, "%02x", b[i]);
        }
        __android_log_print(ANDROID_LOG_INFO, "RecompRenderer", "RENDER_ONLINE_LOCAL l1 trace registry import=%.*s r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x buf=%s",
                            int(importName.size()), importName.data(), call.argument32(0), call.argument32(1), call.argument32(2), call.argument32(3),
                            call.argument32(4), hexBuf);
    }
}
if (::recomp::online_local_glue::enabled() && ::recomp::online_local::Service::claims(importName, call.argument32(0))) {
    struct RegistryGuestMemory final : ::recomp::online_local::GuestMemory {
        GuestImportRegistry* r;
        explicit RegistryGuestMemory(GuestImportRegistry* registry) : r(registry) {}
        bool read(std::uint32_t at, void* out, std::uint32_t n) const override {
            if (n == 0) return true;
            if (!r->validGuestDataBuffer(at, n, false, 1)) return false;
            std::memcpy(out, r->addressSpace_->translate(at), n); return true;
        }
        bool write(std::uint32_t at, const void* in, std::uint32_t n) override {
            if (n == 0) return true;
            if (!r->validGuestDataBuffer(at, n, true, 1) || r->hasActiveCriticalSectionInRange(at, n)) return false;
            std::memcpy(r->addressSpace_->translate(at), in, n); return true;
        }
    } onlineLocalMemory{this};
    ::recomp::online_local::Call onlineLocalCall;
    for (unsigned i = 0; i < 8; ++i) onlineLocalCall.a[i] = call.argument32(i);
    // XGI session messages with an XOVERLAPPED: the overlapped and its event are checked BEFORE the service runs, so a bad
    // one changes no session state (the completion below then cannot fail).
    KernelObject* onlineLocalEvent = nullptr;
    if (importName == "__imp__XMsgStartIORequest" && onlineLocalCall.a[0] == 0xFB && onlineLocalCall.a[2] != 0
            && (::recomp::online_local::isSessionMessage(onlineLocalCall.a[1]) || onlineLocalCall.a[1] == 0x000B0041)) {   // v989: + S2 async
        const auto overlapped = onlineLocalCall.a[2];
        if (!validGuestDataBuffer(overlapped, 28, true, 4) || hasActiveCriticalSectionInRange(overlapped, 28))
            return memoryFault(importName, "online-local: XOVERLAPPED is not bounded writable guest memory");
        const std::uint8_t* const o = addressSpace_->translate(overlapped);
        const auto be32 = [](const std::uint8_t* p) { return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]; };
        if (be32(o + 16) != 0) return memoryFault(importName, "online-local: XOVERLAPPED completion routines are not modelled");
        if (const auto event = be32(o + 12)) {
            const auto found = kernelHandles_.find(event);
            if (found == kernelHandles_.end() || (found->second.kind != KernelObjectKind::NotificationEvent
                    && found->second.kind != KernelObjectKind::SynchronizationEvent))
                return memoryFault(importName, "online-local: XOVERLAPPED event is not an owned event handle");
            onlineLocalEvent = &found->second;
        }
    }
    auto onlineLocal = ::recomp::online_local_glue::service().dispatchRegistry(importName, onlineLocalCall, onlineLocalMemory);
    if (onlineLocal.kind == ::recomp::online_local::Kind::NotMine)
        onlineLocal = ::recomp::online_local_glue::service().dispatchBridge(importName, onlineLocalCall, onlineLocalMemory);
    switch (onlineLocal.kind) {
        case ::recomp::online_local::Kind::NotMine: break; // fall through to today's path, unchanged
        case ::recomp::online_local::Kind::Handled: {
            if (onlineLocal.setLastError) {
                const auto* thread = findThreadObject(RECOMP_CURRENT_GUEST_TOKEN);
                if (!thread || thread->returned) return rejected(importName, "online-local: last error needs a live guest thread");
                (void)writeU32(RECOMP_CURRENT_GUEST_TOKEN + 0x160, onlineLocal.lastError);
            }
            if (onlineLocal.complete) { // the operation already ran; complete the caller's XOVERLAPPED now (saves-path rule)
                const auto overlapped = onlineLocal.overlapped; // == call argument 2, validated above (invariant checked)
                if (importName != "__imp__XMsgStartIORequest" || overlapped != onlineLocalCall.a[2])
                    return rejected(importName, "online-local: completion for an unvalidated XOVERLAPPED");
                (void)writeU32(overlapped, onlineLocal.ovlResult); (void)writeU32(overlapped + 4, onlineLocal.ovlLength);
                (void)writeU32(overlapped + 24, onlineLocal.ovlExtended);
                if (onlineLocalEvent) {
                    std::uint32_t event = 0; (void)readU32(overlapped + 12, event);
                    onlineLocalEvent->signalCount = 1; wakeHandleWaiters(this, event);
                }
            }
            ::recomp::online_local_glue::pendingWaitMicros = onlineLocal.waitMicros; // slept by the bridge, outside this lock
            call.returnValue = onlineLocal.ret;
            return handled(importName, onlineLocal.why);
        }
        case ::recomp::online_local::Kind::Refuse: return rejected(importName, onlineLocal.why);
        case ::recomp::online_local::Kind::MemoryFault: return memoryFault(importName, onlineLocal.why);
    }
}
