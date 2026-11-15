#pragma once
// stat_write_natives.h (v1562): the stat write natives registered on this executable (local-analysis/natives/registrations.json), by joaat
// hash. indirect_call_resolver.cpp keeps their native probe on (probeNeeded) so recomp_trainer_after_native sees each call and
// periodic_save.h counts them as "stats dirty". Plain constants (no trainer.h here); periodic_save_test.cpp checks each against joaat(name).
#include <cstdint>
namespace recomp::periodic_save {
inline constexpr std::uint32_t StatWriteNatives[]{
    0xc9cc1c5cu,   // STAT_SET_INT
    0x6cea96f2u,   // STAT_SET_FLOAT
    0x55d79dfbu,   // STAT_SET_BOOL
    0xc1224aa7u,   // STAT_SET_GXT_LABEL
    0x36be807bu,   // STAT_SET_DATE
    0xb1ef2e21u,   // STAT_SET_STRING
    0x1192c9a3u,   // STAT_SET_POS
    0x2cbaa739u,   // STAT_SET_MASKED_INT
    0xdbe78ed7u,   // STAT_SET_USER_ID
    0x7842c4d6u,   // STAT_SET_BOOL_MASKED
    0xdfc5f71eu,   // STAT_INCREMENT
};
inline bool statWriteHash(std::uint32_t h) noexcept {for(const std::uint32_t s:StatWriteNatives)if(s==h)return true;return false;}
}
