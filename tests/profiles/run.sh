#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cc -ffunction-sections -fdata-sections -Wl,--gc-sections -std=c11 -D_GNU_SOURCE -I src -I third_party/cjson \
  tests/profiles/test.c tests/profiles/stubs.c src/core/profiles.c src/core/store.c src/core/stremio.c src/core/notify.c third_party/cjson/cJSON.c \
  -lSDL2 -lpthread -lm -o "$scratch/test"
for phase in setup reload default secondary cleanup; do
  "$scratch/test" "$scratch" "$phase"
done
mkdir "$scratch/picker-data"
cc -std=c11 -D_GNU_SOURCE -I src -I third_party/cjson \
  tests/profiles/picker.c src/screens/profiles.c src/core/profiles.c src/core/store.c \
  third_party/cjson/cJSON.c -lSDL2 -lpthread -lm -o "$scratch/picker"
"$scratch/picker" "$scratch/picker-data"
