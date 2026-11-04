#!/bin/sh
# Usage: build_ffmpeg.sh verified-ffmpeg-source build-dir install-prefix
# Source: FFmpeg 7.1.5, archive SHA256 in README.md. No downloads here.
set -eu
: "${VITASDK:?Set VITASDK}"
[ "$#" -eq 3 ] || { echo "usage: $0 source build prefix" >&2; exit 2; }
source_dir=$(realpath "$1")
mkdir -p "$2" "$3"
build_dir=$(realpath "$2")
prefix=$(realpath "$3")
[ "$source_dir" != "$build_dir" ] || { echo "Use an out-of-tree build" >&2; exit 2; }
cd "$build_dir"
"$source_dir/configure" --prefix="$prefix" --enable-cross-compile \
    --cross-prefix="$VITASDK/bin/arm-vita-eabi-" --arch=arm --cpu=cortex-a9 \
    --target-os=none --enable-static --disable-shared --disable-programs \
    --disable-doc --disable-avdevice --disable-avformat --disable-avfilter \
    --disable-swscale --disable-swresample --disable-network --disable-autodetect \
    --disable-everything --enable-decoder=h264 --enable-decoder=atrac3 \
    --enable-decoder=atrac3p --enable-pthreads --extra-libs=-lpthread \
    --extra-cflags=-fno-short-enums --disable-w32threads --disable-os2threads \
    --disable-asm --disable-debug
make -j"${POCKET_BUILD_JOBS:-2}"
make install
[ -s "$prefix/lib/libavcodec.a" ] && [ -s "$prefix/lib/libavutil.a" ]
