#!/bin/bash
# Fetches the sources the Android TV build compiles in (nothing prebuilt), at pinned tags, into androidtv/deps/:
#   SDL 2.30.12 (window, GL context, keys, audio), mbedTLS 3.6.2 + curl 8.11.1 (the app's requests: webOS lends its own
#   libcurl, Android has none). Run once; tools/build-android.sh calls it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
D="$ROOT/androidtv/deps"; mkdir -p "$D"
get() { # name url tag
  if [ -f "$D/$1/.tag" ] && [ "$(cat "$D/$1/.tag")" = "$3" ]; then return; fi
  rm -rf "$D/$1"; git -c advice.detachedHead=false clone -q --depth 1 --recurse-submodules --shallow-submodules -b "$3" "$2" "$D/$1"; echo "$3" > "$D/$1/.tag"; echo "== $1 $3"
}
get SDL https://github.com/libsdl-org/SDL release-2.30.12
get mbedtls https://github.com/Mbed-TLS/mbedtls v3.6.2
get curl https://github.com/curl/curl curl-8_11_1
