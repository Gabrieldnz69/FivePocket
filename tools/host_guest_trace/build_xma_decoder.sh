#!/bin/sh
# Build only the XMA decoder dependency; no game data, network service or APK.
# Source must be the exact public Xenia FFmpeg revision, already fetched locally.
set -eu
if [ "$#" -ne 3 ]; then
    echo "Usage: sh build_xma_decoder.sh FFMPEG_SOURCE NEW_BUILD_DIR NDK_ROOT" >&2
    exit 2
fi
xma_source=$1
xma_build=$2
xma_ndk=$3
xma_revision=15ece0882e8d5875051ff5b73c5a8326f7cee9f5
test "$(git -C "$xma_source" rev-parse HEAD)" = "$xma_revision"
test -z "$(git -C "$xma_source" status --porcelain --untracked-files=no)"
case "$xma_source:$xma_build:$xma_ndk" in *' '*)
    echo "FFmpeg Makefiles require paths without spaces; use a task-owned temporary alias." >&2; exit 2;;
esac
case "$xma_source:$xma_build:$xma_ndk" in /*:/*:/*) ;; *) exit 2;; esac
xma_tools=$xma_ndk/toolchains/llvm/prebuilt/darwin-x86_64/bin
test -x "$xma_tools/aarch64-linux-android26-clang"
test ! -e "$xma_build/config.mak"
test ! -e "$xma_build/libavcodec/avcodec.h"
mkdir -p "$xma_build"
# This fork checks in Premake config.h, which prevents an out-of-tree configure.
# Build a pinned Git archive in isolation; configure may replace that archive's
# generated configuration without dirtying the fetched reference checkout.
git -C "$xma_source" archive "$xma_revision" | tar -x -C "$xma_build"
cd "$xma_build"
# FFmpeg's version script asks git to describe the folder it builds in. The archive holds no repository, so
# inside another project's checkout git would answer with that project's revision and the libraries would carry
# it as the FFmpeg version (seen 2026-10-08: the builder's work folder lies in this repository, and avcodec.o
# came out 32 bytes longer). Git is told not to look above the build folder: the version is the release's.
GIT_CEILING_DIRECTORIES=$(dirname "$xma_build")
export GIT_CEILING_DIRECTORIES
./configure --target-os=android --arch=aarch64 --enable-cross-compile \
    --cc="$xma_tools/aarch64-linux-android26-clang" \
    --cxx="$xma_tools/aarch64-linux-android26-clang++" \
    --ar="$xma_tools/llvm-ar" --ranlib="$xma_tools/llvm-ranlib" \
    --nm="$xma_tools/llvm-nm" --strip="$xma_tools/llvm-strip" \
    --disable-everything --disable-autodetect --disable-programs --disable-doc \
    --disable-network --disable-avdevice --disable-avformat --disable-avfilter \
    --disable-swscale --disable-swresample --disable-postproc \
    --enable-decoder=xma1,xma2,xmaframes --enable-parser=xma \
    --enable-static --disable-shared --enable-pic --disable-debug --enable-small \
    --extra-cflags=-O2
make -j3 libavcodec/libavcodec.a libavutil/libavutil.a
echo "Android ARM64 XMA libraries built from $xma_revision; decoding is not yet validated."
