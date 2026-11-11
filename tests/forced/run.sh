#!/bin/sh
# Run the fast, SDK-independent forced subtitle unit tests on Linux/macOS.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
OUT="$(mktemp)"
trap 'rm -f "$OUT"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation \
  -I"$ROOT/src" -I"$ROOT/third_party/cjson" \
  "$ROOT/tests/forced/test_names.c" "$ROOT/src/core/forcedsubs.c" \
  "$ROOT/third_party/cjson/cJSON.c" -lm -o "$OUT"
"$OUT"
