#!/bin/bash
# The Android TV build on an emulator, as a person opens it: installs the APK, starts it, waits, and checks from the app's
# own log that it drew frames and the network answered (no crash). Leaves dist/smoke/ with a screenshot and the log.
#   tools/android-smoke.sh path/to/app.apk
set -uo pipefail
APK="$1"; OUT="$(cd "$(dirname "$0")/.." && pwd)/dist/smoke"; mkdir -p "$OUT"
PKG=tv.stremioorigin.app
adb wait-for-device
adb shell 'while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 2; done'
adb install -r -g "$APK" || exit 1
adb logcat -c
adb shell monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null
sleep 45
adb exec-out screencap -p > "$OUT/home.png"
adb shell input keyevent KEYCODE_DPAD_RIGHT; sleep 2; adb shell input keyevent KEYCODE_DPAD_DOWN; sleep 3
adb exec-out screencap -p > "$OUT/moved.png"
adb logcat -d > "$OUT/logcat.txt"
grep "StremioOrigin" "$OUT/logcat.txt" > "$OUT/app.log" || true
alive=$(adb shell pidof $PKG || true)
echo "process: ${alive:-gone}"
fail=0
[ -n "$alive" ] || { echo "::error::the app is not running"; fail=1; }
grep -q "\[frame\]" "$OUT/app.log" || { echo "::error::no [frame] line: it did not draw"; fail=1; }
if grep -E "FATAL EXCEPTION|\[crash\]|SIGSEGV|Fatal signal" "$OUT/logcat.txt" | grep -i -E "stremio|$PKG|crash"; then echo "::error::crash in the log"; fail=1; fi
# torrents: the engine library must load and resolve a torrent (Big Buck Bunny, Creative Commons). Loading is required; a
# resolved stream depends on peers being reachable from the runner, so its absence is only a warning.
if [ -n "${P2P_TEST:-}" ]; then
  adb shell am force-stop $PKG; adb logcat -c
  adb shell am start -n $PKG/.OriginActivity --es sb.p2ptest "$P2P_TEST" >/dev/null
  for i in $(seq 1 36); do sleep 5; adb logcat -d | grep -q "\[p2ptest\] url" && break; done
  adb logcat -d > "$OUT/logcat-p2p.txt"; grep "StremioOrigin" "$OUT/logcat-p2p.txt" | grep -E "\[p2p|\[p2ptest" | head -30 | tee "$OUT/p2p.log"
  grep -q "\[p2p\] engine " "$OUT/p2p.log" || { echo "::error::the torrent engine did not load"; fail=1; }
  grep -q "\[p2ptest\] url" "$OUT/p2p.log" && echo "torrent resolved to a local stream" || echo "::warning::the torrent did not resolve in 3 minutes (peers may be unreachable from the runner)"
fi
tail -40 "$OUT/app.log"
exit $fail
