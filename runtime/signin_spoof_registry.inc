// signin_spoof_registry.inc (lane upd1-spoof/signin-spoof): the Sandbox-only "signed in online" answers. Included by
// guest_saves_dispatch.inc right after `user0` is defined (member scope of dispatchUnderRegistryLock, registry lock held; `sv`
// is the local profile state, `writable` / `writeU32` / `handled` / `memoryFault` / `addressSpace_` are the file's own).
// Closed gate (Story, Unset, or no files/private-trace/diag-online-local): nothing here runs and every import falls through to
// the unchanged handlers below. Open gate: only the five answers below change; GetXUID and GetName keep the local profile's
// answers (offline XUID, "Player"), and every other user index keeps its answer.
// Marker (once per process, tag RecompRenderer): ONLINE_LOCAL signin=2 privileges=granted link=loopback xauth=unavailable
if (::recomp::online_local_glue::enabled() /* v968 B2: one latch */ && ::recomp::online_local_glue::signinSpoofOn() /* v991: L0 alone runs with the signin spoof off (R6); v1000: L1 keeps it on */) {
    namespace ol = ::recomp::online_local;
    static std::atomic<bool> signinSpoofLogged{false};
    if (!signinSpoofLogged.exchange(true, std::memory_order_relaxed)) {
        __android_log_print(ANDROID_LOG_INFO, "RecompRenderer",
                            "ONLINE_LOCAL signin=2 privileges=granted link=loopback xauth=unavailable");
    }
    if (importName == "__imp__XamUserGetSigninState" && user0) {
        call.returnValue = ol::SigninStateOnline;
        return handled(importName, "Sandbox online-local: user 0 answers signed in online (local, no service)");
    }
    if (importName == "__imp__XamUserGetSigninInfo" && user0) {
        const auto output = call.argument32(2);
        if (!output) { call.returnValue = XInvalidArg; return handled(importName, "Null sign-in info output"); }
        if (!writable(output, 40, 4)) { return memoryFault(importName, "Sign-in info output is not bounded writable storage"); }
        const auto info = ol::encodeSigninInfo(sv->identity.xuid, sv->identity.gamertag, call.argument32(1));
        std::memcpy(addressSpace_->translate(output), info.data(), info.size());
        call.returnValue = 0;
        return handled(importName, "Sandbox online-local: sign-in info, state 2, offline-marked XUID only");
    }
    if (importName == "__imp__XamUserCheckPrivilege" && (user0 || call.argument32(0) == 0xFF)) {
        const auto output = call.argument32(2);
        if (output && !writable(output, 4)) { return memoryFault(importName, "Privilege output is not bounded writable storage"); }
        const auto answer = ol::checkPrivilege(call.argument32(1), output != 0);
        if (answer.write) { (void)writeU32(output, answer.value); }
        call.returnValue = answer.returnValue;
        return handled(importName, "Sandbox online-local: privilege from the local allow-list");
    }
    if (importName == "__imp__XamUserGetAge" && user0) { // v970 (T1 stop): an adult; written only into a bounded writable r4
        const auto output = call.argument32(1);
        if (output && validGuestDataBuffer(output, 1, true, 1)) { addressSpace_->translate(output)[0] = 18; } // v971: ONE byte (lbz)
        call.returnValue = ol::ErrorSuccess;
        return handled(importName, "Sandbox online-local: user 0 is an adult (age 18), no parental restriction");
    }
    if (importName == "__imp__XamUserGetAgeGroup" && user0) { // v971 (T1 rerun stop): adult (2), a big-endian DWORD (lwz)
        const auto output = call.argument32(1);
        if (output && validGuestDataBuffer(output, 4, true, 4)) { (void)writeU32(output, 2); }
        call.returnValue = ol::ErrorSuccess;
        return handled(importName, "Sandbox online-local: user 0 age group adult");
    }
    if (importName == "__imp__XamCreateEnumeratorHandle" && call.argument32(1) == 252u) { // v973 (T1 v971 stop): XLive base app
        call.returnValue = ol::ErrorFunctionFailed;   // the callers' own "unavailable" branch; the out handle stays untouched
        return handled(importName, "Sandbox online-local: XLive enumerators unavailable (no network)");
    }
    // v974 (bulk 1): decoded call sites (update-1/spoof-bulk/callsites.txt); honest local answers, no network
    if (importName == "__imp__XamUserAreUsersFriends" && call.argument32(4) == 0) {   // no friends (a synchronous call only)
        const auto result = call.argument32(3);
        if (result && validGuestDataBuffer(result, 4, true, 4)) { (void)writeU32(result, 0); }
        call.returnValue = ol::ErrorSuccess;
        return handled(importName, "Sandbox online-local: no friends");
    }
    if (importName == "__imp__XamUserGetMembershipTierFromXUID") { call.returnValue = 6; return handled(importName, "Sandbox online-local: tier gold (local)"); }
    if (importName == "__imp__XamUserGetOnlineCountryFromXUID") { call.returnValue = userCountry_.value_or(103u) & 0xFFu; return handled(importName, "Sandbox online-local: XConfig country"); }
    if (importName == "__imp__XamGetLanguage") { call.returnValue = userLanguage_.value_or(1u); return handled(importName, "Sandbox online-local: XConfig language"); }
    if (importName == "__imp__XNetLogonGetTitleID") { call.returnValue = 0x545408A7u; return handled(importName, "Sandbox online-local: this title"); }
    // v1010 (lead host-task.md, verified): XGI 251 / 0xB0006 (XUserSetContextEx, 24 B) and 0xB0007 (XUserSetPropertyEx, 32 B) fall through to
    // the completing saves handler (guest_saves_dispatch.inc: ov[0] = ov[24] = 0, 997). This rule failed them with E_FAIL and no completion,
    // so the rlXbl ReadWriteAttrsTask's first write (0xB0006, 0x800B) failed and the offline host task finished in 54 ms (t-l1 1215).
    if (importName == "__imp__XMsgStartIORequest" && (call.argument32(0) == 251u || call.argument32(0) == 252u)
            && !(call.argument32(0) == 251u && (call.argument32(1) == 0x000B0006u || call.argument32(1) == 0x000B0007u))) {   // v979 (bulk XMsg)
        static std::array<std::uint32_t, 32> seenIo{}; static unsigned seenIoCount = 0;
        const auto key = (call.argument32(0) << 24) ^ call.argument32(1);
        if (std::find(seenIo.begin(), seenIo.begin() + seenIoCount, key) == seenIo.begin() + seenIoCount && seenIoCount < seenIo.size()) {
            seenIo[seenIoCount++] = key;
            __android_log_print(ANDROID_LOG_INFO, "RecompRenderer", "RENDER_ONLINE_LOCAL first-use registry import=__imp__XMsgStartIORequest app=%u msg=%08x overlapped=%08x",
                                call.argument32(0), call.argument32(1), call.argument32(2));
        }
        call.returnValue = 0x80004005u;   // E_FAIL synchronously: the wrappers' failure path (1627); no output, no completion
        return handled(importName, "Sandbox online-local: XGI / XLive service request unavailable (no network)");
    }
    if (importName.rfind("__imp__XMsg", 0) == 0) {   // v979 (diagnostic): any other XMsg app/message, once, then the unchanged handling
        static std::array<std::uint64_t, 32> seenOther{}; static unsigned seenOtherCount = 0;
        const std::uint64_t key = (std::uint64_t(importName.size()) << 56) ^ (std::uint64_t(call.argument32(0)) << 32) ^ call.argument32(1);
        if (std::find(seenOther.begin(), seenOther.begin() + seenOtherCount, key) == seenOther.begin() + seenOtherCount && seenOtherCount < seenOther.size()
            && !(importName == "__imp__XMsgInProcessCall" && call.argument32(0) == 252u)) {
            seenOther[seenOtherCount++] = key;
            __android_log_print(ANDROID_LOG_INFO, "RecompRenderer", "RENDER_ONLINE_LOCAL xmsg-seen import=%.*s app=%u msg=%08x a2=%08x a3=%08x a4=%08x",
                                int(importName.size()), importName.data(), call.argument32(0), call.argument32(1), call.argument32(2), call.argument32(3), call.argument32(4));
        }
    }
    if (importName == "__imp__XMsgInProcessCall" && call.argument32(0) == 252u) {   // v978 (T1 v977 stop): an XLive service request
        static std::array<std::uint32_t, 16> seenMessages{}; static unsigned seenCount = 0;
        const auto message = call.argument32(1);
        if (std::find(seenMessages.begin(), seenMessages.begin() + seenCount, message) == seenMessages.begin() + seenCount && seenCount < seenMessages.size()) {
            seenMessages[seenCount++] = message;
            __android_log_print(ANDROID_LOG_INFO, "RecompRenderer", "RENDER_ONLINE_LOCAL first-use registry import=__imp__XMsgInProcessCall app=252 msg=%08x", message);
        }
        call.returnValue = 0x80004005u;   // E_FAIL: the wrappers' own "unavailable" path; nothing written
        return handled(importName, "Sandbox online-local: XLive service request unavailable (no network)");
    }
    // v977 (lead): XamAlloc / XamFree as a real allocation from the registry's bounded guest arena (port_xamalloc_real.py)
    if (importName == "__imp__XamAlloc" || importName == "__imp__XamFree") {
        static std::set<std::uint32_t> xamOwned;   // the blocks XamAlloc handed out (XamFree frees only these)
        if (importName == "__imp__XamFree") {
            const auto p = call.argument32(0);
            if (p != 0 && xamOwned.count(p) != 0) {
                auto release = call; release.arguments[0] = 0; release.arguments[1] = p;
                (void)dispatchMmFreePhysicalMemory(release);
                xamOwned.erase(p);
            }
            call.returnValue = 0;
            return handled(importName, "Sandbox online-local: XamFree of an XamAlloc block");
        }
        const auto size = call.argument32(1), out = call.argument32(2);
        if (size == 0 || out == 0 || !validGuestDataBuffer(out, 4, true, 4)) {
            call.returnValue = 0x80070057u;   // E_INVALIDARG
            return handled(importName, "Sandbox online-local: XamAlloc bad size or out pointer");
        }
        auto request = call;
        request.arguments[0] = 0; request.arguments[1] = size; request.arguments[2] = PageReadWrite;
        request.arguments[3] = 0; request.arguments[4] = 0xFFFFFFFFu; request.arguments[5] = 0;
        const auto allocated = dispatchMmAllocatePhysicalMemoryEx(request);
        const auto address = static_cast<std::uint32_t>(request.returnValue);
        if (!allocated.handled() || address == 0 || !validGuestDataBuffer(address, size, true, 1)) {
            call.returnValue = 0x8007000Eu;   // E_OUTOFMEMORY: the callers' own failure branch
            return handled(importName, "Sandbox online-local: XamAlloc arena exhausted");
        }
        std::memset(addressSpace_->translate(address), 0, size);
        (void)writeU32(out, address);
        xamOwned.insert(address);
        call.returnValue = 0;
        return handled(importName, "Sandbox online-local: XamAlloc from the bounded guest arena");
    }
    // v968 (B3): NetDll_XNetGetEthernetLinkStatus / NetDll_XNetGetTitleXnAddr are answered by the session lane's registry
    // (online_local_registry.inc runs before guest_saves_dispatch.inc; same loopback XNADDR, link ACTIVE|100MBPS|FULL_DUPLEX).
}
