#!/bin/bash
# tests/audorig/run.sh: "Original" audio picks the original language's track (real files' track lists)
cd "$(dirname "$0")/../.." || exit 1
T=$(mktemp -d); cc -std=c11 -Wall -Wextra -Isrc tests/audorig/test.c src/core/audorig.c -o "$T/t" && "$T/t"; r=$?; rm -rf "$T"; exit $r
