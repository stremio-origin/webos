#!/bin/bash
# tools/tv.sh [wait-seconds]: build + package "Stremio Origin C", copy the IPK to the TV's /tmp, install it, launch it, print its log
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; TV="${TV:?set TV to the TV address}"; KEY="${TV_KEY:?set TV_KEY to the SSH key for the TV}"; ID="${APPID:-tv.stremioorigin.app}"; WAIT="${1:-10}"
SSH="ssh -i $KEY -o BatchMode=yes -o ConnectTimeout=6 root@$TV"
IPK=$(APPID="$ID" TITLE="${TITLE:-Stremio Origin C}" "$ROOT/tools/package.sh" | tail -1 | awk "{print \$NF}"); echo "$IPK"  # (the package just built, whatever its version)
scp -q -i "$KEY" -o BatchMode=yes "$IPK" "root@$TV:/tmp/sb.ipk"
if [ -n "${RUNONLY:-}" ]; then   # RUNONLY=1 SBENV="SB_GLCFG=1" tools/tv.sh 320: run the installed build by hand with extra env, no rebuild
  $SSH -tt "luna-send -n 1 luna://com.webos.applicationManager/close '{\"id\":\"$ID\"}' >/dev/null 2>&1; killall stremioorigin 2>/dev/null; sleep 1; rm -f /tmp/$ID.log; cd /media/developer/apps/usr/palm/applications/$ID; rm -f /tmp/sb.ipk; ${SBENV:-} APPID=$ID LS2_APPID=$ID SDL_VIDEODRIVER=wayland XDG_RUNTIME_DIR=/tmp/xdg SB_SHOT=/tmp/sbp.png SB_SHOT_FRAME=${1:-320} SB_SHOT_EXIT=1 timeout -t ${RUNFOR:-120} ./stremioorigin >/dev/null 2>&1; grep -E '${GREP:-start\]|frame\] 300|crash}' /tmp/$ID.log | tail -${TAIL:-3}" 2>&1 | grep -v "^Connection to" | cut -c1-300
  exit 0
fi
if [ -n "${PROF:-}" ]; then   # PROF=1: run it by hand with the sampling profiler instead of through the launcher
  $SSH -tt "luna-send -n 1 luna://com.webos.applicationManager/close '{\"id\":\"$ID\"}' >/dev/null 2>&1; timeout -t 90 luna-send -i luna://com.webos.appInstallService/dev/install '{\"id\":\"com.ares.defaultName\",\"ipkUrl\":\"/tmp/sb.ipk\",\"subscribe\":true}' 2>&1 | grep -o '\"state\":\"installed\"' | tail -1; rm -f /tmp/sb.ipk; cd /media/developer/apps/usr/palm/applications/$ID; SB_PROF=1 ${SBENV:-} APPID=$ID LS2_APPID=$ID SDL_VIDEODRIVER=wayland XDG_RUNTIME_DIR=/tmp/xdg SB_SHOT=/tmp/sbp.png SB_SHOT_FRAME=${1:-320} SB_SHOT_EXIT=1 timeout -t 120 ./stremioorigin >/dev/null 2>&1; grep -E 'maps\]|prof\]|frame\] (300|600)|crash' /tmp/$ID.log | tail -40" 2>&1 | grep -v "^Connection to" | cut -c1-300
  exit 0
fi
$SSH -tt "luna-send -n 1 luna://com.webos.applicationManager/close '{\"id\":\"$ID\"}' >/dev/null 2>&1; timeout -t 90 luna-send -i luna://com.webos.appInstallService/dev/install '{\"id\":\"com.ares.defaultName\",\"ipkUrl\":\"/tmp/sb.ipk\",\"subscribe\":true}' 2>&1 | grep -o '\"state\":\"[a-z ]*\"' | tail -1; rm -f /tmp/sb.ipk /tmp/$ID.log; luna-send -n 1 luna://com.webos.applicationManager/launch '{\"id\":\"$ID\"}' >/dev/null; sleep $WAIT; echo ---PS; P=\$(pidof stremioorigin); echo pid=\$P; [ -n \"\$P\" ] && grep -E \"VmRSS|Threads\" /proc/\$P/status; echo ---LOG; cat /tmp/$ID.log 2>/dev/null | tail -60; echo ---FREE; free -m | sed -n 2p" 2>&1 | grep -v "^Connection to" | cut -c1-220
