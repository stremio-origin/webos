#!/bin/sh
# the resampler: bilinear samples, alpha, crops, mixed scales, and shrinking byte for byte as before: tests/resample/run.sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
OUT="$(mktemp)"; trap 'rm -f "$OUT"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror ${SB_ASAN:+-fsanitize=address,undefined} -I"$ROOT/src" \
 "$ROOT/tests/resample/test_resample.c" "$ROOT/src/ui/resample.c" -o "$OUT" -lm
"$OUT"
