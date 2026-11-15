#pragma once
// v1520 (lead subagent 2026-11-11, DLC text packs): an NtCreateFile open whose DEVICE cannot exist in the guest namespace.
// Why: Fold run-v1517-11110827 (14 packs) stopped at NtCreateFile right after dlcmpbeach:\common\data\dlctext.meta
// (hasGlobalTextFile=true): the 2013 exe builds "(null):\xbox360\data\lang\americanDLC.rpf" for a compat pack (NULL device name), our
// resolver refuses "(null)" as a device name, and the registry's refusal was a guarded stop.
// The Xbox answer, as Xenia models it (local-analysis/reference/xenia-canary, read-only):
//  - kernel/xboxkrnl/xboxkrnl_io.cc NtCreateFile_entry: IsValidPath (kernel/xfile.h: every char 32..126, none of " + , < > | and no *
//    outside a pattern) else X_STATUS_OBJECT_NAME_INVALID; then VirtualFileSystem::OpenFile;
//  - vfs/virtual_file_system.cc OpenFile: the parent path ("(null):\xbox360\data\lang") resolves through ResolvePath, which finds no device
//    for the "(null):" prefix ("device not found") -> *out_action = kDoesNotExist (5), return X_STATUS_OBJECT_PATH_NOT_FOUND (0xC000003A);
//  - NtCreateFile then writes io_status_block {status, information = file_action} and *handle_out = X_INVALID_HANDLE_VALUE.
// 0xC000003A is in RtlNtStatusToDosError's verified subset (-> 3 ERROR_PATH_NOT_FOUND).
// unknownDevicePath(raw) is deliberately narrow: only a path that Xenia's IsValidPath accepts, whose device name has a character no device
// of ours can have (normalizeGuestFilePath allows [A-Za-z0-9_] before the colon, so it cannot name a bound device or a symbolic link), and
// whose rest is valid for our normalizer (the device name is the ONLY reason the resolver refused it). Every other refusal stays a stop.
#include <cstdint>
#include <string>
#include <string_view>
#include "guest_file_metadata.h"
namespace recomp::bootstrap::ntcreate_v1520 {
constexpr std::uint32_t StatusObjectPathNotFound = 0xC000003AU;   // X_STATUS_OBJECT_PATH_NOT_FOUND
constexpr std::uint32_t FileDoesNotExist = 5;                     // vfs::FileAction::kDoesNotExist
constexpr std::uint32_t InvalidHandle = 0xFFFFFFFFU;              // X_INVALID_HANDLE_VALUE
inline bool xeniaValidPath(std::string_view s) noexcept {   // kernel/xfile.h IsValidPath(s, is_pattern = false)
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c <= 31 || c >= 127) return false;
        switch (c) { case '"': case '+': case ',': case '<': case '>': case '|': case '*': return false; default: break; }
    }
    return true;
}
inline bool unknownDevicePath(std::string_view raw) {
    if (raw.substr(0, 4) == "\\??\\") raw.remove_prefix(4);
    if (raw.empty() || raw.size() > 1024 || !xeniaValidPath(raw)) return false;
    const auto colon = raw.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon > 32) return false;
    bool foreign = false;
    for (std::size_t i = 0; i < colon; ++i) {
        const char c = raw[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) foreign = true;
    }
    if (!foreign) return false;   // a well-formed device name: not this case (a bound device / link, or another refusal)
    std::string rest = "x";       // the same path under a well-formed device name must be valid for our normalizer
    rest.append(raw.substr(colon));
    return normalizeGuestFilePath(rest).has_value();
}
}
