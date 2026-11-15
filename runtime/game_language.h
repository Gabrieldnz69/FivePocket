#pragma once
// v1511 (lead subagent, Discord F1 "choose the game's language"): the player's language choice, answered the game's own way.
//
// The app writes files/settings/language.txt holding ONE word: the name of one of the disc's text containers
// (x360 xbox360a.rpf data/lang/<word>.rpf; the owner's Disc 2 carries exactly these eleven, all with the same 598 text files).
// No file, an empty file or an unknown word = "american" (English), today's answer.
//
// How the game picks its text (read from the executable, default.xex image; see update-1/subagents-2026-11-11/language/REPORT.md):
//   XGetLanguage   sub_83639E20: ExGetXConfigSetting(XCONFIG_USER_CATEGORY 3, XCONFIG_USER_LANGUAGE 9); 0 or > 17 -> by game region.
//   XGetLocale     sub_83639D48: ExGetXConfigSetting(3, XCONFIG_USER_COUNTRY 14), one byte, through a country -> locale table
//                  (0x83878670, 43 pairs; country 103 -> locale 36, country 31 -> locale 31).
//   game language  sub_8257A2A8: jump table 0x8257A2F0 on XLanguage-1:
//                  1 -> 0 american, 3 -> 2 german, 4 -> 1 french, 5 -> 4 spanish (11 mexican when the locale is one of
//                  4, 6, 8, 22, 36, 40), 6 -> 3 italian, 7 -> 8 korean, 8 -> 9 chinese, 9 -> 5 portuguese, 11 -> 6 polish,
//                  12 -> 7 russian; 2 and 10 -> -1 (no container). The game's names table 0x83810D68 lists the containers in
//                  that order: american french german italian spanish portuguese polish russian korean chinese japanese mexican.
// So the kernel binds XCONFIG_USER_LANGUAGE (and, for the two Spanish containers, XCONFIG_USER_COUNTRY) before the title starts:
//   mexican = Spanish + the US country (103, the library's standing answer: locale 36 is in the Latin-American set),
//   spanish = Spanish + country 31 (locale 31, not in that set). Every other word keeps country 103.
// The same two values answer XamGetLanguage / XamUserGetOnlineCountryFromXUID on the online-local path (signin_spoof_registry.inc).
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace recomp::game_language {

struct Choice {
    const char* word;          // the container name (american ... mexican)
    std::uint32_t xLanguage;   // XC_LANGUAGE_* (XCONFIG_USER_LANGUAGE)
    std::uint32_t country;     // XONLINE_COUNTRY_* byte (XCONFIG_USER_COUNTRY)
    int gameLanguage;          // what sub_8257A2A8 then returns (the index into the game's names table; logged)
};

inline constexpr std::uint32_t CountryUs = 103, CountrySpain = 31;

// The eleven containers on the disc, in the game's own enum order (japanese, 10, is not on this disc and is not offered).
inline constexpr Choice Choices[] = {
    {"american",   1, CountryUs,    0},
    {"french",     4, CountryUs,    1},
    {"german",     3, CountryUs,    2},
    {"italian",    6, CountryUs,    3},
    {"spanish",    5, CountrySpain, 4},
    {"portuguese", 9, CountryUs,    5},
    {"polish",    11, CountryUs,    6},
    {"russian",   12, CountryUs,    7},
    {"korean",     7, CountryUs,    8},
    {"chinese",    8, CountryUs,    9},
    {"mexican",    5, CountryUs,   11},
};
inline constexpr std::size_t ChoiceCount = sizeof Choices / sizeof Choices[0];
inline constexpr const Choice& Default = Choices[0];

// The first word of text (ASCII letters; leading whitespace skipped, any case); Default when there is none or it is unknown.
// known (optional) is set to whether the word named a container.
inline const Choice& parse(const char* text, bool* known = nullptr) noexcept {
    if (known) *known = false;
    if (!text) return Default;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
    char word[16]{};
    std::size_t n = 0;
    for (; text[n] && text[n] != ' ' && text[n] != '\t' && text[n] != '\r' && text[n] != '\n'; ++n) {
        if (n + 1 >= sizeof word) return Default;     // longer than any container name
        char c = text[n];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c < 'a' || c > 'z') return Default;
        word[n] = c;
    }
    if (n == 0) return Default;
    for (const Choice& c : Choices) {
        if (std::strcmp(c.word, word) == 0) { if (known) *known = true; return c; }
    }
    return Default;
}

} // namespace recomp::game_language
