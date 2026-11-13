#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
OUT="$(mktemp)"; trap 'rm -f "$OUT"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/src" \
  "$ROOT/tests/bgquality/test_quality.c" "$ROOT/src/core/bgquality.c" -o "$OUT"
"$OUT"
