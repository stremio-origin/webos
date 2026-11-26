#!/bin/bash
# The torrent engine for Android TV: the same engine, pinned sources and patches as tools/build-engine.sh (webOS), built with
# the Android NDK instead. Kept apart so the webOS build never changes because of Android.
#   tools/build-engine-android.sh [abi]   -> build/android-jni/<abi>/libsbengine.so  (armeabi-v7a by default; x86_64 for the emulator)
# The APK picks it up from build/android-jni (androidtv/app/build.gradle). Needs ANDROID_HOME with NDK 27.2.12479018, cmake, ninja.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ABI="${1:-armeabi-v7a}"
: "${ANDROID_HOME:=${ANDROID_SDK_ROOT:-}}"; [ -n "$ANDROID_HOME" ] || { echo "Set ANDROID_HOME"; exit 1; }
NDK="${ANDROID_NDK:-$ANDROID_HOME/ndk/27.2.12479018}"
W="${ENGINE_WORK:-$ROOT/androidtv/deps/engine}"; mkdir -p "$W"
COMMIT=$(sed -n 's/^COMMIT=//p' "$ROOT/tools/build-engine.sh")   # the same engine commit as webOS
SRC="$W/src"
if [ ! -d "$SRC/.git" ]; then git -c advice.detachedHead=false clone -q https://github.com/NuvioMedia/nuvio-engine "$SRC"; fi
git -C "$SRC" checkout -q "$COMMIT"; git -C "$SRC" reset -q --hard "$COMMIT"; git -C "$SRC" clean -qfd -- src include tests cmake
git -C "$SRC" apply --whitespace=nowarn "$ROOT/tools/engine/engine.patch"
DEPS="$W/deps"; mkdir -p "$DEPS"
if [ ! -f "$DEPS/nuvio_boost-src/boost/version.hpp" ]; then
  [ -f "$DEPS/boost.tar.bz2" ] || curl -fsSL -o "$DEPS/boost.tar.bz2" https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.bz2
  echo "1bed88e40401b2cb7a1f76d4bab499e352fa4d0c5f31c0dbae64e24d34d7513b  $DEPS/boost.tar.bz2" | sha256sum -c -
  rm -rf "$DEPS/nuvio_boost-src"; mkdir -p "$DEPS/nuvio_boost-src"; tar -xjf "$DEPS/boost.tar.bz2" -C "$DEPS/nuvio_boost-src" --strip-components 1; rm -f "$DEPS/boost.tar.bz2"
fi
if [ ! -d "$DEPS/nuvio_libtorrent-src/.git" ]; then
  git clone -q https://github.com/arvidn/libtorrent.git "$DEPS/nuvio_libtorrent-src"
  git -C "$DEPS/nuvio_libtorrent-src" -c advice.detachedHead=false checkout -q 740a0b9aeabe00e762cc0efe4a0f27593db2550b
  git -C "$DEPS/nuvio_libtorrent-src" submodule update -q --init deps/try_signal
fi
git -C "$DEPS/nuvio_libtorrent-src" checkout -q -- src/peer_connection.cpp   # (the engine applies its own reject_piece fix, ours before 1.1.1)

B="$W/build-$ABI"
XFL="-fexperimental-library"   # the NDK's libc++ keeps std::jthread and stop_token (the engine's HTTP server) behind this
FL="-ffunction-sections -fdata-sections -fvisibility=hidden -fvisibility-inlines-hidden -D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -ffile-prefix-map=$ROOT=/app -ffile-prefix-map=$W=/engine"
cmake -S "$SRC" -B "$B" -G Ninja -DNUVIO_ENGINE_ENABLE_LIBTORRENT=ON -DNUVIO_ENGINE_BUILD_TESTS=OFF \
  -DFETCHCONTENT_SOURCE_DIR_NUVIO_BOOST="$DEPS/nuvio_boost-src" -DFETCHCONTENT_SOURCE_DIR_NUVIO_LIBTORRENT="$DEPS/nuvio_libtorrent-src" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-24 -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON -DCMAKE_DISABLE_FIND_PACKAGE_GnuTLS=ON -DCMAKE_DISABLE_FIND_PACKAGE_LibGcrypt=ON \
  -DCMAKE_C_FLAGS="$FL" -DCMAKE_CXX_FLAGS="$FL $XFL" >/dev/null
ninja -j "${ENGINE_JOBS:-4}" -C "$B" nuvio_engine
case "$ABI" in armeabi-v7a) TRIPLE=armv7a-linux-androideabi24 ;; arm64-v8a) TRIPLE=aarch64-linux-android24 ;; x86_64) TRIPLE=x86_64-linux-android24 ;; x86) TRIPLE=i686-linux-android24 ;; *) echo "abi?"; exit 1 ;; esac
CXX="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ --target=$TRIPLE"
OUT="$ROOT/build/android-jni/$ABI/libsbengine.so"; mkdir -p "$(dirname "$OUT")"
echo '{ global: sbe_get; local: *; };' > "$B/exports.map"
$CXX -std=c++20 -Os -fPIC $FL $XFL -shared -I"$SRC/include" -I"$ROOT/src" "$ROOT/tools/engine/sbengine.cpp" \
  "$B/libnuvio_engine.a" "$B/_deps/nuvio_libtorrent-build/libtorrent-rasterbar.a" \
  -static-libstdc++ -Wl,--gc-sections -Wl,--version-script="$B/exports.map" -Wl,--strip-all -Wl,-soname,libsbengine.so -llog -o "$OUT"
ls -l "$OUT"
