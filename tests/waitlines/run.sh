#!/bin/bash
# tests/waitlines/run.sh : builds and runs the loading lines' tests (Mac)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; mkdir -p "$ROOT/build"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -O1 -g ${SB_ASAN:+-fsanitize=address} -I"$ROOT/src" "$ROOT/tests/waitlines/test.c" "$ROOT/src/core/waitlines.c" -o "$ROOT/build/waitlines_test"
"$ROOT/build/waitlines_test"
python3 "$ROOT/tests/waitlines/i18n_check.py"
