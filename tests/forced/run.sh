#!/bin/bash
# forced subtitles by name and by an answer's flags: tests/forced/run.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; mkdir -p "$ROOT/build"
cc -std=c11 -Wall -Wextra -Werror -Wno-deprecated-declarations -O1 -g ${SB_ASAN:+-fsanitize=address,undefined} -I"$ROOT/src" -I"$ROOT/third_party/cjson" \
  "$ROOT/tests/forced/test_forced.c" "$ROOT/src/core/forcedsubs.c" "$ROOT/third_party/cjson/cJSON.c" -lm -o "$ROOT/build/forced_test"
"$ROOT/build/forced_test"
