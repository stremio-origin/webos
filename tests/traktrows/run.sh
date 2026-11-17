#!/bin/bash
# tests/traktrows/run.sh : Trakt's rows on Home (src/core/traktrows.c) against stand-in answers
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; mkdir -p "$ROOT/build"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wno-unused-parameter -Wno-deprecated-declarations -O1 -g ${SB_ASAN:+-fsanitize=address,undefined} -I"$ROOT/src" -I"$ROOT/third_party/cjson" \
  "$ROOT/tests/traktrows/test_traktrows.c" "$ROOT/src/core/traktrows.c" "$ROOT/third_party/cjson/cJSON.c" -lm -o "$ROOT/build/traktrows_test"
"$ROOT/build/traktrows_test"
