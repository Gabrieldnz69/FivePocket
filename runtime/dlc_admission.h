#pragma once
// v1524 (lead subagent 2026-11-11, owner DLC task): the STFS content loader's admission POLICY for packs the 2013 executable cannot consume.
// Evidence (Fold run-v1523-11111123, 14 packs): Hipster's content.xml lists WEAPONINFO_FILE entries on "dlcMPHipsterCRC:/..." - a second
// "<deviceName>CRC:" device that title-update executables mount for CRC-checked files. The 1.0 executable never mounts it: no kernel open
// reached our registry for that device (RENDER_DLC_OPEN logs every dlc* open), the game's own stream open returned NULL, and its
// WEAPONINFO_FILE reader (sub_82EACAD8 via the data-file handler sub_82EBD900) reads that NULL stream without a check (sub_829855F8 ->
// sub_82985550 -> sub_831E75D0 -> sub_831E2DB8: device vtable slot 6 through a NULL stream = call target 0). On a real 1.0 console the same
// pack ends the same way, and the game has no graceful path for it, so such a pack is refused at ADMISSION with a logged reason instead of
// loading half-way and stopping. Packs without CRC-device paths are unaffected. This is the loader's policy on the player's own files; no
// game data is changed, nothing is rewritten or removed.
// v1526 rule 2 (evidence: Fold run-v1525-11111212, the next stop after rule 1): a DLC_ITYP_REQUEST file ENABLED by a GROUP_STARTUP change set.
// The 1.0 parser does not know DLC_ITYP_REQUEST; at startup Independence's mpIndependence_AUTOGEN enables dlc_lev_des.ityp, the game queries
// the loose path (attributes miss on dlc_mpindependence:\xbox360\levels\gta5\props\dlc_lev_des.ityp - the .ityp lives inside
// dlc_lev_des.rpf), sub_8345D7F0 returns -1 and sub_829B4A10 executes its unconditional trap (twi 31 at 0x829B4AB0; RENDER_GUEST_TRAP
// host_off 85a6ad4 in __imp__sub_829B4A10). High Life's v_business2.ityp sits in GROUP_MAP (not activated at startup) and loaded fine.
// Startup ITYP requests: Independence (dlc_lev_des), Festive Surprise (v_christmas_tree), Flight School (mppilot_props).
// The 14 owner packs (update-1/dlc/work/*/content.xml): CRC-device paths in Ill-Gotten Gains 1 (42) and 2 (46), Heists (74), Last Team
// Standing (40), Hipster (8); none in Beach, Valentines, Business, High Life, Holiday, Festive, Independence, Flight School, Collectors.
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
namespace recomp::stfs::admission {
struct Verdict { bool admit{true}; std::size_t crcFiles{0}; std::string firstDevice; std::string reason; };
inline char lower(char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
// content.xml text -> verdict. Every <filename>DEVICE:/...</filename> whose DEVICE ends with "CRC" (any case) counts.
inline Verdict judgeContentXml(std::string_view xml) {
    Verdict v;
    constexpr std::string_view Open = "<filename>", Close = "</filename>";
    std::size_t at = 0;
    while ((at = xml.find(Open, at)) != std::string_view::npos) {
        const auto start = at + Open.size();
        const auto end = xml.find(Close, start);
        if (end == std::string_view::npos) break;
        const auto value = xml.substr(start, end - start);
        const auto colon = value.find(':');
        if (colon != std::string_view::npos && colon > 3) {   // a device NAME plus the CRC suffix
            const auto device = value.substr(0, colon);
            const auto tail = device.substr(device.size() - 3);
            if (lower(tail[0]) == 'c' && lower(tail[1]) == 'r' && lower(tail[2]) == 'c') {
                if (v.crcFiles++ == 0) v.firstDevice = std::string(device);
            }
        }
        at = end + Close.size();
    }
    if (v.crcFiles) {
        v.admit = false;
        v.reason = "needs-title-update: content.xml names " + std::to_string(v.crcFiles) + " file(s) on " + v.firstDevice +
                   ":, a CRC device only title-update executables mount (1.0 reads its NULL stream)";
    }
    return v;
}
inline std::string lowerCopy(std::string_view v) { std::string o; o.reserve(v.size()); for (const char c : v) o.push_back(lower(c)); return o; }
// all <tag>value</tag> values inside [text]
inline std::vector<std::string> values(std::string_view text, std::string_view tag) {
    std::vector<std::string> out; const std::string open = "<" + std::string(tag) + ">", close = "</" + std::string(tag) + ">";
    std::size_t at = 0;
    while ((at = text.find(open, at)) != std::string_view::npos) {
        const auto start = at + open.size(); const auto end = text.find(close, start);
        if (end == std::string_view::npos) break;
        std::string_view v = text.substr(start, end - start);
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t' || v.front() == '\r' || v.front() == '\n')) v.remove_prefix(1);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r' || v.back() == '\n')) v.remove_suffix(1);
        out.emplace_back(v); at = end + close.size();
    }
    return out;
}
// setup2.xml -> the change sets of the GROUP_STARTUP group (lower case)
inline std::vector<std::string> startupChangeSets(std::string_view setup2) {
    std::vector<std::string> out;
    std::size_t at = 0; constexpr std::string_view Group = "<NameHash>GROUP_STARTUP</NameHash>";
    while ((at = setup2.find(Group, at)) != std::string_view::npos) {
        const auto open = setup2.find("<ContentChangeSets>", at), close = setup2.find("</ContentChangeSets>", at);
        if (open == std::string_view::npos || close == std::string_view::npos || close < open) break;
        for (auto& v : values(setup2.substr(open, close - open), "Item")) out.push_back(lowerCopy(v));
        at = close;
    }
    return out;
}
// rule 2: a DLC_ITYP_REQUEST data file enabled by a startup change set
inline Verdict judgeStartupItyp(std::string_view setup2, std::string_view content) {
    Verdict v;
    const auto startup = startupChangeSets(setup2);
    if (startup.empty()) return v;
    // data files: each <Item> of <dataFiles> with its filename and fileType (in order: the fileType follows its filename)
    std::vector<std::string> itypFiles;
    { std::size_t at = 0;
      while ((at = content.find("<filename>", at)) != std::string_view::npos) {
          const auto next = content.find("<filename>", at + 10);
          const auto item = content.substr(at, next == std::string_view::npos ? std::string_view::npos : next - at);
          const auto f = values(item, "filename"), t = values(item, "fileType");
          if (!f.empty() && !t.empty() && t[0] == "DLC_ITYP_REQUEST") itypFiles.push_back(lowerCopy(f[0]));
          at += 10; } }
    if (itypFiles.empty()) return v;
    // change sets: <changeSetName>N</changeSetName> ... its <filesToEnable> items, up to the next change set
    std::size_t at = 0;
    while ((at = content.find("<changeSetName>", at)) != std::string_view::npos) {
        const auto next = content.find("<changeSetName>", at + 15);
        const auto block = content.substr(at, next == std::string_view::npos ? std::string_view::npos : next - at);
        const auto names = values(block, "changeSetName");
        if (!names.empty() && std::find(startup.begin(), startup.end(), lowerCopy(names[0])) != startup.end()) {
            const auto open = block.find("<filesToEnable>"), close = block.find("</filesToEnable>");
            if (open != std::string_view::npos && close != std::string_view::npos && close > open)
                for (auto& f : values(block.substr(open, close - open), "Item"))
                    if (std::find(itypFiles.begin(), itypFiles.end(), lowerCopy(f)) != itypFiles.end()) {
                        v.admit = false; v.firstDevice = f;
                        v.reason = "needs-title-update: startup change set " + names[0] + " enables DLC_ITYP_REQUEST " + f +
                                   " (1.0 does not know the type, registers it as a loose image and traps)";
                        return v;
                    }
        }
        at += 15;
    }
    return v;
}
// The package's root content.xml (no content.xml = nothing to judge: admitted, as the setup2-only Collectors pack).
template <class PackageT>
inline Verdict judgePackage(const PackageT& package) {
    const auto& entries = package.entries();
    std::string content, setup2; bool haveContent = false;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (e.directory || e.size == 0 || e.size > (4u << 20)) continue;
        std::string p; for (const char c : e.path) p.push_back(lower(c));
        std::string* into = p == "content.xml" ? &content : p == "setup2.xml" ? &setup2 : nullptr;
        if (!into) continue;
        into->assign(static_cast<std::size_t>(e.size), '\0');
        if (!package.read(i, 0, reinterpret_cast<std::uint8_t*>(into->data()), into->size())) {
            Verdict v; v.admit = false; v.reason = p + " unreadable"; return v;
        }
        if (into == &content) haveContent = true;
    }
    if (!haveContent) return {};   // nothing to judge (the setup2-only Collectors pack)
    if (auto v = judgeContentXml(content); !v.admit) return v;   // rule 1 (v1524): CRC device
    return judgeStartupItyp(setup2, content);                  // rule 2 (v1526): startup ITYP request
}
}
