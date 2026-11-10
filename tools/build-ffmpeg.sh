#!/bin/bash
# Builds the sound converter library the app loads when a file's only sound is one the TV can't decode (DTS, TrueHD):
#   tools/build-ffmpeg.sh webos  -> build/webos/libsbdts.so      (ARMv7 soft-float, glibc 2.17)
#   tools/build-ffmpeg.sh mac    -> build/mac/libsbdts.dylib     (the Mac preview)
# Inside: tools/dts/sbdts.c (Matroska / MP4 in, MPEG-TS segments out: video copied, a Dolby Vision profile 7 picture
# as 8.1, the chosen track decoded and encoded as AC3 or copied) over a minimal static FFmpeg (LGPL 2.1+, no GPL or nonfree parts, no network code: every read goes
# through the app's own range reader). The exact configure line, the source pin and the LGPL text are kept in
# third_party/licenses so the library can be rebuilt and relinked; it is a separate shared library for that reason
# too. Sources and build folders live in $ORIGIN_BUILD/ffmpeg.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ORIGIN_BUILD="${ORIGIN_BUILD:-$HOME/origin-build}"   # the private build folder outside the repo: toolchain, SDK, sources, keys
TARGET="${1:-webos}"
W="${FFMPEG_WORK:-$ORIGIN_BUILD/ffmpeg}"; mkdir -p "$W"
ZIG="${ZIG:-$ORIGIN_BUILD/zig-aarch64-macos-0.17.0/zig}"
SDK="${SDK:-$ORIGIN_BUILD/sdk/arm-webos-linux-gnueabi_sdk-buildroot/arm-webos-linux-gnueabi/sysroot}"
VERSION=7.1.1
SHA256=733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1
TARBALL="$ORIGIN_BUILD/src/ffmpeg-$VERSION.tar.xz"
mkdir -p "$(dirname "$TARBALL")"
[ -f "$TARBALL" ] || curl -fsSL -o "$TARBALL" "https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"
echo "$SHA256  $TARBALL" | shasum -a 256 -c - >/dev/null
SRC="$W/ffmpeg-$VERSION"; [ -f "$SRC/configure" ] || tar -xJf "$TARBALL" -C "$W"
B="$W/build-$TARGET"; P="$W/prefix-$TARGET"; mkdir -p "$B"
# What the converter needs and nothing more: the DTS decoder (the core only is decoded, never the lossless XLL
# extension), TrueHD / MLP, the AC3 encoder, the Matroska and MP4 readers, the MPEG-TS writer, the parsers and the
# bitstream filters that turn Matroska's length-prefixed H.264 / HEVC into the start-code form MPEG-TS carries.
COMPONENTS="--disable-everything --enable-decoder=dca,truehd,mlp --enable-encoder=ac3 --enable-demuxer=matroska,mov --enable-muxer=mpegts,adts \
  --enable-parser=dca,h264,hevc,ac3,aac,mlp --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,dca_core"
COMMON="--disable-autodetect --enable-zlib --disable-shared --enable-static --enable-pic --disable-programs --disable-doc --disable-debug \
  --disable-network --disable-avdevice --disable-avfilter --disable-swscale --disable-postproc --enable-avcodec --enable-avformat \
  --enable-avutil --enable-swresample --disable-iconv"
case "$TARGET" in
  webos)
    ZT="-target arm-linux-gnueabi.${GLIBC:-2.17} -mcpu=cortex_a9-neon -mfloat-abi=softfp"   # soft-float ABI like the app
    TC="$W/tc-webos"; mkdir -p "$TC"
    printf '#!/bin/sh\nexec "%s" cc %s "$@"\n' "$ZIG" "$ZT" > "$TC/cc"; printf '#!/bin/sh\nexec "%s" ar "$@"\n' "$ZIG" > "$TC/ar"; printf '#!/bin/sh\nexec "%s" ranlib "$@"\n' "$ZIG" > "$TC/ranlib"
    chmod 755 "$TC/cc" "$TC/ar" "$TC/ranlib"
    ARCH="--enable-cross-compile --arch=arm --target-os=linux --cc=$TC/cc --ar=$TC/ar --ranlib=$TC/ranlib --nm=nm"   # NEON assembly kept (softfp calling convention detected); --cpu is left out, this compiler names the cores its own way
    XC="-O2 -ffunction-sections -fdata-sections -idirafter $SDK/usr/include -ffile-prefix-map=$HOME=/src"
    XL="-L$SDK/usr/lib"   # (the TV's own libz, as the app uses)
    OUT="$ROOT/build/webos/libsbdts.so" ;;
  mac)
    ARCH="--cc=cc"; XC="-O2 -ffile-prefix-map=$HOME=/src"; XL=""; OUT="$ROOT/build/mac/libsbdts.dylib" ;;
  *) echo "unknown target $TARGET"; exit 1 ;;
esac
CONF="$COMMON $COMPONENTS $ARCH"
if [ ! -f "$P/lib/libavformat.a" ] || [ "$(cat "$B/.conf" 2>/dev/null)" != "$CONF $XC" ]; then
  echo "== FFmpeg $VERSION ($TARGET)"
  (cd "$B" && "$SRC/configure" --prefix="$P" $CONF --extra-cflags="$XC" --extra-ldflags="$XL" >/dev/null \
    && { [ "$TARGET" != webos ] || { sed 's/#define HAVE_SYSCTL 1/#define HAVE_SYSCTL 0/' config.h > config.h.tmp && mv config.h.tmp config.h; }; } \
    && make -j"${FFMPEG_JOBS:-4}" >/dev/null && make install >/dev/null)   # (the compiler's newer libc headers make configure see a sysctl glibc 2.17 never had)
  echo "$CONF $XC" > "$B/.conf"
fi
# the line a rebuild needs (local paths left out), kept beside the LGPL text
L="$ROOT/third_party/licenses"; mkdir -p "$L"
cp "$SRC/COPYING.LGPLv2.1" "$L/ffmpeg-LGPL-2.1.txt"
{ echo "FFmpeg $VERSION (LGPL 2.1 or later), linked statically into libsbdts, a shared library of its own: it can be rebuilt"
  echo "and replaced, or relinked against another FFmpeg built the same way. Source: https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"
  echo "SHA-256 $SHA256, unmodified. The converter's own source and the build recipe: tools/build-ffmpeg.sh, tools/dts/sbdts.c."
  echo; echo "configure (the TV):"; echo "  $COMMON $COMPONENTS" | sed "s#$HOME#~#g"
  echo "  --enable-cross-compile --arch=arm --target-os=linux (zig cc -target arm-linux-gnueabi.2.17 -mcpu=cortex_a9-neon -mfloat-abi=softfp), HAVE_SYSCTL 0"
} > "$L/ffmpeg-SOURCE.txt"
# The Dolby Vision step: tools/dovi (libdovi 3.3.2, MIT, pinned in Cargo.lock) as a static library, its licenses beside
# FFmpeg's. Rust's armv7-unknown-linux-gnueabi is the soft-float ABI the app uses, built for glibc 2.17; its unwinding
# symbols come from the TV's libgcc_s. Its source paths are rewritten (the build machine's folders never travel).
case "$TARGET" in webos) RT=armv7-unknown-linux-gnueabi ;; mac) RT=$(rustc -vV | sed -n 's/^host: //p') ;; esac
DV="$ORIGIN_BUILD/dovi"
RUSTFLAGS="--remap-path-prefix=$HOME=/src --remap-path-prefix=$ROOT=/app" cargo build --manifest-path "$ROOT/tools/dovi/Cargo.toml" --locked --release --target "$RT" --target-dir "$DV/target" \
  || { echo "the Dolby Vision library didn't build (rustup target add $RT)"; exit 1; }
DOVI="$DV/target/$RT/release/libsbdovi.a"
mkdir -p "$L/dovi"
cargo metadata --manifest-path "$ROOT/tools/dovi/Cargo.toml" --locked --format-version 1 --filter-platform "$RT" 2>/dev/null | python3 -c '
import json, pathlib, shutil, sys
out = pathlib.Path(sys.argv[1]); m = json.load(sys.stdin)
used = {n["id"] for n in m["resolve"]["nodes"]}
for p in m["packages"]:
    if p["id"] not in used or p["name"] == "sbdovi": continue
    root = pathlib.Path(p["manifest_path"]).parent
    for f in root.iterdir():
        if f.is_file() and f.name.upper().startswith(("LICENSE", "COPYING", "NOTICE")): shutil.copyfile(f, out / (p["name"] + "-" + p["version"] + "-" + f.name))
' "$L/dovi"
echo "libdovi (dolby_vision 3.3.2, MIT, https://github.com/quietvoid/dovi_tool) and its dependencies, linked statically into libsbdts; their licenses are here. Exact versions: tools/dovi/Cargo.lock." > "$L/dovi/SOURCE.txt"
mkdir -p "$(dirname "$OUT")"
LIBS="$P/lib/libavformat.a $P/lib/libavcodec.a $P/lib/libswresample.a $P/lib/libavutil.a $DOVI"
if [ "$TARGET" = webos ]; then
  echo '{ global: sbd_get; local: *; };' > "$TC/exports.map"
  "$TC/cc" -std=c11 -O2 -fPIC -shared -fvisibility=hidden -D_GNU_SOURCE -I"$P/include" -I"$ROOT/src" -ffile-prefix-map=$HOME=/src -ffile-prefix-map=$ROOT=/app \
    "$ROOT/tools/dts/sbdts.c" $LIBS "$SDK/usr/lib/libz.so" "$SDK/lib/libgcc_s.so.1" -Wl,--gc-sections -Wl,-T,"$ROOT/tools/webos-relocs.ld" -Wl,--version-script="$TC/exports.map" -Wl,--strip-all -Wl,-soname,libsbdts.so -lpthread -lm -o "$OUT"
else
  cc -std=c11 -O2 -shared -fvisibility=hidden -D_GNU_SOURCE -I"$P/include" -I"$ROOT/src" "$ROOT/tools/dts/sbdts.c" $LIBS -lz -Wl,-dead_strip -o "$OUT"
fi
ls -l "$OUT" | awk '{printf "%s  %.1f MB\n", $NF, $5/1048576}'
