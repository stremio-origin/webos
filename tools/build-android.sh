#!/bin/bash
# Builds Stremio Origin for Android TV (torrents included): dist/StremioClaude-AndroidTV-<version>.apk (armeabi-v7a, runs on every Android TV / Google TV).
#   tools/build-android.sh            # needs the Android SDK (ANDROID_HOME) with NDK 27.2.12479018, JDK 17+, Gradle 8.9+
#   KEYS=$ORIGIN_BUILD/keys.env ...   # the app's own Trakt/TMDB keys, as tools/build.sh
#   ORIGIN_KEYSTORE=... ORIGIN_KEYSTORE_PASSWORD=...   # sign with your own key (else the debug key: fine for trying it)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/tools/android-deps.sh"
: "${ANDROID_HOME:=${ANDROID_SDK_ROOT:-}}"; [ -n "$ANDROID_HOME" ] || { echo "Set ANDROID_HOME to the Android SDK"; exit 1; }
export ANDROID_HOME
GRADLE="${GRADLE:-gradle}"
# torrents: the engine, once (tools/build-engine-android.sh; ENGINE=0 builds without it)
[ "${ENGINE:-1}" = 0 ] || [ -f "$ROOT/build/android-jni/armeabi-v7a/libsbengine.so" ] || "$ROOT/tools/build-engine-android.sh" armeabi-v7a
(cd "$ROOT/androidtv" && "$GRADLE" --no-daemon -q :app:assembleRelease)
VER=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$ROOT/pkg/appinfo.json")
mkdir -p "$ROOT/dist"
cp "$ROOT/androidtv/app/build/outputs/apk/release/app-release.apk" "$ROOT/dist/StremioClaude-AndroidTV-$VER.apk"
ls -l "$ROOT/dist/StremioClaude-AndroidTV-$VER.apk"
