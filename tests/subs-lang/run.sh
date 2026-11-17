#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
SDL_CFLAGS="${SDL_CFLAGS:-$(pkg-config --cflags sdl2 2>/dev/null || true)}"
cc $SDL_CFLAGS -std=c11 -D_GNU_SOURCE -ffunction-sections -fdata-sections -Isrc -Ithird_party/cjson \
  tests/subs-lang/test.c src/core/subs.c src/core/textenc.c -Wl,--gc-sections ${ZLIB_LIBS:--lz} -lm -pthread -o "$T/test"
"$T/test"
