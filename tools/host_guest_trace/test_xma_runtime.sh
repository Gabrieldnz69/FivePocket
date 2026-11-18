#!/bin/sh
# Codec-backed focused tests. Builds only these tests, never PPC code or APKs.
# Pass the already built FFmpeg headers/archives and an Android C++ compiler,
# or host-built archives and a host compiler. Does not install/run on a device.
set -eu
if [ "$#" -ne 5 ]; then
    echo 'Usage: sh test_xma_runtime.sh CXX FFMPEG_HEADERS FFMPEG_ARCHIVES NEW_OUTPUT_DIR host|android' >&2
    exit 2
fi
xma_cxx=$1
xma_headers=$2
xma_archives=$3
xma_output=$4
xma_platform=$5
case "$xma_platform" in host) xma_link_flags=;; android) xma_link_flags=-static-libstdc++;; *) exit 2;; esac
test -f "$xma_headers/libavcodec/avcodec.h"
test -f "$xma_archives/libavcodec.a"
test -f "$xma_archives/libavutil.a"
test ! -e "$xma_output"
mkdir -p "$xma_output"
xma_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
xma_native=$xma_root/android/app/src/main/cpp
for xma_test in native_xma_loop native_xma_context_pool native_xma_frame_decoder native_xma_pcm_blocks; do
    "$xma_cxx" -std=c++20 -O2 -fno-strict-aliasing -ffp-model=strict -fwrapv \
        -UNDEBUG $xma_link_flags -I"$xma_native" -I"$xma_headers" \
        "$xma_root/android/tests/${xma_test}_test.cpp" \
        "$xma_native/native_xma_context_pool.cpp" \
        "$xma_native/native_xma_frame_decoder.cpp" \
        "$xma_native/guest_execution_guard.cpp" \
        "$xma_archives/libavcodec.a" "$xma_archives/libavutil.a" -lm \
        -o "$xma_output/$xma_test"
done
echo "Built four codec-backed/native PCM tests in $xma_output; execute them on the matching architecture."
