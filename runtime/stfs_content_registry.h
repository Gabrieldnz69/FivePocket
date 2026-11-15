#pragma once
// v1506 (lead subagent block, owner DLC task): installed-content rows beyond the explicit catalog file.
//
// GuestContentCatalog (android/app/src/main/cpp/guest_content_catalog.h, shared by every lane) holds at most 16 rows.
// The install disc already uses 4 and the owner's DLC folder holds 14 packages, so rows read from STFS packages are
// appended here instead of raising the shared header's cap (the registry's class layout is unchanged).
// The combined list is the catalog's rows, in order, then the STFS rows in the order given (package file name order).
// Each appended row passes the catalog's own record validation, names stay unique (case-insensitive) across the
// combined list, and the total is at most 64 (the largest enumerator capacity the registry accepts; the game asks 64).

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "guest_content_catalog.h"
#include "guest_file_metadata.h"
#include "guest_read_only_file.h"

namespace recomp::bootstrap {

constexpr std::size_t MaximumCombinedContentRows = 64;

// Pure merge (host tested): base rows + extra rows, each extra row validated by GuestContentCatalog::load.
inline bool mergeContentRows(const std::vector<GuestContentCatalog::Record>& base,
                             const std::vector<GuestContentCatalog::Record>& extra,
                             std::vector<GuestContentCatalog::Record>& combined, std::string& why) {
    combined.clear();
    if (base.size() + extra.size() > MaximumCombinedContentRows) { why = "more than 64 content rows"; return false; }
    const auto nameOf = [](const GuestContentCatalog::Record& record) {
        std::string name;
        for (std::size_t at = 264; at < GuestContentCatalog::RecordBytes && record[at]; ++at) {
            const auto c = record[at];
            name.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
        }
        return name;
    };
    std::set<std::string> names;
    for (const auto& record : base) { names.insert(nameOf(record)); }
    for (const auto& record : extra) {
        std::vector<std::uint8_t> blob{'G', 'X', 'C', 'O', 'N', 'T', '1', 0, 0, 0, 0, 1, 0, 0, 0, 1};
        blob.insert(blob.end(), record.begin(), record.end());
        GuestContentCatalog single;
        if (!single.load(blob)) { why = "row fails catalog validation"; return false; }
        if (!names.insert(nameOf(record)).second) { why = "duplicate content name"; return false; }
    }
    combined = base;
    combined.insert(combined.end(), extra.begin(), extra.end());
    return true;
}

// The appended rows, owned per registry (keyed by its address; the process has one). Written once at setup, before
// any guest thread runs; read by the registry's content handlers under the registry lock. Cleared with the catalog.
namespace detail {
struct ExtraContentRows {
    const void* owner{nullptr};
    std::size_t baseCount{0};
    std::vector<GuestContentCatalog::Record> combined;
};
inline ExtraContentRows& extraContentRows() { static ExtraContentRows rows; return rows; }
} // namespace detail

// Call once, after registry.bindInstalledContent(base, title) succeeded and before execution starts. Rows extra[i]
// become installed package index base.records().size() + 1 + i, bound with bindContentPackage like any other row.
[[nodiscard]] inline bool bindExtraContentRows(const void* registry, const GuestContentCatalog& base,
                                               const std::vector<GuestContentCatalog::Record>& extra, std::string& why) {
    auto& rows = detail::extraContentRows();
    if (registry == nullptr || base.title() == 0 || rows.owner != nullptr) { why = "unbound catalog or rows already bound"; return false; }
    std::vector<GuestContentCatalog::Record> combined;
    if (!mergeContentRows(base.records(), extra, combined, why)) { return false; }
    rows.owner = registry; rows.baseCount = base.records().size(); rows.combined = std::move(combined);
    return true;
}

inline void clearExtraContentRows(const void* registry) {
    auto& rows = detail::extraContentRows();
    if (rows.owner == registry) { rows = {}; }
}

// The registry's view: the combined rows when they were bound for this registry over this very catalog, else the
// catalog's own rows (unchanged behaviour without STFS packages).
inline const std::vector<GuestContentCatalog::Record>& contentRows(const void* registry, const GuestContentCatalog& base) {
    const auto& rows = detail::extraContentRows();
    if (rows.owner != registry || base.title() == 0 || rows.baseCount != base.records().size()
            || !std::equal(base.records().begin(), base.records().end(), rows.combined.begin())) { return base.records(); }
    return rows.combined;
}


// The registry's handlers see rows through this view: a local `installedContent_` of this type, declared on one whole added
// line, shadows the member in that handler (guest_import_registry.cpp keeps every original line; lane checks pin them).
struct ContentRowsView {
    const std::vector<GuestContentCatalog::Record>& rows;
    std::uint32_t titleId;
    [[nodiscard]] const std::vector<GuestContentCatalog::Record>& records() const noexcept { return rows; }
    [[nodiscard]] std::uint32_t title() const noexcept { return titleId; }
};

// Index (one-based) names an appended STFS row of this registry.
inline bool stfsContentRow(const void* registry, const GuestContentCatalog& base, std::uint32_t index) {
    return index > base.records().size() && index <= contentRows(registry, base).size();
}

// An STFS row's namespace: the same consistency rules bindContentPackage applies to catalog rows (content:\ root first, every
// regular metadata record backed by a source of exactly its size, no unlisted source), bounded by GuestFileMetadataCatalog
// (16384 records) instead of the extracted-file caps; all packages together hold at most 65536 files.
inline bool stfsPackageAcceptable(const GuestFileMetadataCatalog& metadata,
                                  const std::map<std::string, std::shared_ptr<const GuestReadOnlyFile>>& files,
                                  std::size_t existingFiles) {
    if (files.empty() || existingFiles > 65536 || files.size() > 65536 - existingFiles
            || metadata.pathAt(1) != std::optional<std::string>{"content:\\"}) { return false; }
    std::size_t regular = 0;
    for (std::uint32_t i = 1; i <= metadata.size(); ++i) {
        const auto path = metadata.pathAt(i);
        if (!path) { return false; }
        const auto entry = metadata.query(*path);
        if (entry.state != GuestFileQueryState::Found) { return false; }
        if (entry.metadata.attributes == 0x11) { continue; }
        const auto source = files.find(*path);
        if (entry.metadata.attributes != 1 || source == files.end() || !source->second
                || source->second->size() != entry.metadata.values[5] || source->second->size() > (8ULL << 30)) { return false; }
        ++regular;
    }
    return regular == files.size();
}

} // namespace recomp::bootstrap
