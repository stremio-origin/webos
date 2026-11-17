#!/bin/bash
# tests/ssreq/run.sh : the TV's screen-saver request parser (Mac)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; mkdir -p "$ROOT/build"
cc -std=c11 -Wall -Wextra -O1 -g ${SB_ASAN:+-fsanitize=address} -I"$ROOT/src" "$ROOT/tests/ssreq/ssreq_test.c" -o "$ROOT/build/ssreq_test"
"$ROOT/build/ssreq_test"
