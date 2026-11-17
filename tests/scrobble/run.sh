#!/bin/bash
# tests/scrobble/run.sh : builds the Trakt scrobbler's test (Mac) and runs it
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; mkdir -p "$ROOT/build"
cc -std=c11 -D_GNU_SOURCE -DSB_TEST_CLOCK -Wall -Wextra -Wno-unused-parameter -O1 -g ${SB_ASAN:+-fsanitize=address} -I"$ROOT/src" -I"$ROOT/third_party/cjson" \
  "$ROOT/tests/scrobble/scrobble_test.c" "$ROOT/src/core/scrobble.c" "$ROOT/third_party/cjson/cJSON.c" -lm -o "$ROOT/build/scrobble_test"
"$ROOT/build/scrobble_test"
