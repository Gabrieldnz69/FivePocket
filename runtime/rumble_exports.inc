// rumble_exports.inc (Update 1 P2, v955): the registry's translation unit defines the two rumble exports
// (recomp_rumble_bind_v1 / recomp_rumble_wait_v1) here. One include line in guest_import_registry.cpp replaces a blank line, so
// every original registry line keeps its number (the my-music patch's hunks stay valid). Checked by
// update-1/haptics/tools/check_rumble_exports.py (it scans .inc files for the define).
#define RECOMP_RUMBLE_EXPORTS
#include "rumble.h"
