// dlc_compat_order.h (v1356, update-1 session 2026-11-11): extra-content (DLC) pack order for the 1.0 executable.
//
// Evidence (disc 1.0 image, generated corpus; t-dlc-20261111-0209 on v1355):
// - sub_829E6818 walks the installed-content entries (116 bytes each). Pass 1 asks sub_829CAD48 (mount "extra:", parse
//   extra:/setup2.xml into SSetupData at entry+8) and erases the entries whose state is not 1. Then sub_829E6768 sorts the
//   survivors by (type +52, order +56) (comparator sub_829E4E18). Pass 2 mounts an entry under its deviceName (sub_829B4318,
//   state 2) only if type != 0, or if type == 0 and order == its index in the sorted array.
// - The 1.0 enum table at 0x837CC21C knows two types: EXTRACONTENT_COMPAT_PACK = 0, EXTRACONTENT_PAID_PACK = 1.
// - Holiday Gift's setup2.xml: type EXTRACONTENT_COMPAT_PACK, order 1. On v1355 it was found, mounted as extra:, its setup2.xml
//   opened, and then never mounted as dlc_mpChristmas: (it is the only entry, index 0, order 1). Packs written for later
//   executables start their COMPAT order at 1, so the 1.0 rule refuses every one of them.
//
// Rule (diag-dlc-compat-order): after the game's own sort, a COMPAT entry's order becomes its index. The sort already
// placed the entries; renumbering keeps their relative order and only makes the 1.0 "order == index" check accept the
// ordering the sort produced. Entries of other types are left alone. Idempotent (a second pass changes nothing).
#pragma once
#include <cstdint>

namespace recomp::dlc_order {

constexpr std::uint32_t EntrySize = 116;
constexpr std::uint32_t TypeOffset = 52;
constexpr std::uint32_t OrderOffset = 56;
constexpr std::uint32_t CompatType = 0;
constexpr std::uint32_t MaxEntries = 256;

struct Change {std::uint32_t index; std::uint32_t type; std::uint32_t before; std::uint32_t after;};

// Valid range: begin <= end, a whole number of entries, at most MaxEntries. Anything else: no change.
inline bool validRange(std::uint32_t begin, std::uint32_t end) noexcept {
    if (end < begin) return false;
    const std::uint32_t bytes = end - begin;
    return bytes % EntrySize == 0 && bytes / EntrySize <= MaxEntries;
}

// Mem: load(addr) / store(addr, value) of big-endian guest words. Returns how many orders changed; fills out[0..cap) with
// every entry seen (changed or not) so the caller can log what the game's check would have done.
template <class Mem>
unsigned renumber(Mem& m, std::uint32_t begin, std::uint32_t end, bool apply, Change* out, unsigned cap, unsigned* seen) noexcept {
    unsigned changed = 0, n = 0;
    if (seen) *seen = 0;
    if (!validRange(begin, end)) return 0;
    const std::uint32_t count = (end - begin) / EntrySize;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t e = begin + i * EntrySize;
        const std::uint32_t type = m.load(e + TypeOffset), order = m.load(e + OrderOffset);
        std::uint32_t after = order;
        if (type == CompatType && order != i) {
            if (apply) {m.store(e + OrderOffset, i); after = i; ++changed;}
        }
        if (n < cap && out) out[n] = Change{i, type, order, after};
        ++n;
    }
    if (seen) *seen = n;
    return changed;
}

// What the 1.0 pass-2 check decides for an entry (state 1 assumed): mounted or skipped.
inline bool wouldMount(std::uint32_t type, std::uint32_t order, std::uint32_t index) noexcept {
    return type != CompatType || order == index;
}

}  // namespace recomp::dlc_order
