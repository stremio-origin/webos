#!/bin/bash
# tests/supcard/run.sh: the support card's timing rules (a fake store), then its words in every language measured
# against the card (needs the Mac build: tools/build.sh mac)
cd "$(dirname "$0")/../.." || exit 1
T=$(mktemp -d); cc -std=c11 -Wall -Wextra -DSUPC_TEST -Isrc tests/supcard/test.c src/core/supcard.c -o "$T/t" && "$T/t"; r=$?; rm -rf "$T"
if [ -x build/mac/stremioorigin ]; then
  # the card shows, Back closes it: the fade plays, then nothing of it is left to take a tap (a store of its own, the guide seen)
  H=$(mktemp -d); D="$H/Library/Application Support/StremioOrigin"; mkdir -p "$D"; V=$(python3 -c "import json;print(json.load(open('assets/whatsnew.json'))['version'])")
  printf '{"guideSeen":"1","whatsNewSeen":"%s"}' "$V" > "$D/state.json"
  HOME="$H" SB_SUPPORTCARD=1 SB_SUPCARD_SELFTEST=1 SB_ASSETS=assets SB_SKIP_LOGIN=1 SB_ALARM=60 build/mac/stremioorigin 2>&1 | grep "\[supcard-selftest\]"; [ "${PIPESTATUS[0]}" = 0 ] || r=1; rm -rf "$H"
fi
if [ -x build/mac/stremioorigin ]; then SB_SUPCARD_FIT=1 SB_ASSETS=assets SB_SKIP_LOGIN=1 SB_ALARM=60 build/mac/stremioorigin 2>&1 | grep "\[fit\]"; f=${PIPESTATUS[0]}; [ "$f" = 0 ] || r=1; else echo "fit: no Mac build, skipped"; fi
exit $r
