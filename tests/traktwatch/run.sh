#!/bin/bash
# tests/traktwatch/run.sh : what the Trakt account has watched (src/core/traktwatch.c) against a stand-in Trakt
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; mkdir -p "$ROOT/build"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wno-unused-parameter -Wno-deprecated-declarations -O1 -g ${SB_ASAN:+-fsanitize=address,undefined} -I"$ROOT/src" -I"$ROOT/third_party/cjson" \
  "$ROOT/tests/traktwatch/test_traktwatch.c" "$ROOT/src/core/traktwatch.c" "$ROOT/third_party/cjson/cJSON.c" -lm -lpthread -o "$ROOT/build/traktwatch_test"
"$ROOT/build/traktwatch_test"
