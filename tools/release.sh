#!/bin/bash
# A release in one step:  tools/release.sh VERSION [site-dir]
# Sets the version (pkg/appinfo.json; the What's new card keeps its own: a new card, a new card version), builds the IPK
# (tools/package.sh) and writes the Homebrew Channel files into the site folder, the GitHub Pages
# repository users add to the Homebrew Channel:
#   <id>_<version>_arm.ipk, icon.png, api/apps/<id>/manifest.json (+ releases/latest.json,
#   full_description.html) and the repository list (apps.json + api/apps.json).
# The app's own update check reads the same manifest (src/core/update.c). No git, no push: review the
# site folder, then commit and push it yourself. The version must go up every time.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="${1:?usage: tools/release.sh VERSION [site-dir]}"
SITE="${2:-$ROOT/../stremioorigin-site}"
ID=tv.stremioorigin.app
BASE="https://stremio-origin.github.io/webos"
SRC="https://github.com/stremio-origin/webos"
[[ "$VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "version must look like 1.0.1"; exit 1; }
mkdir -p "$SITE"

python3 - "$ROOT" "$VER" <<'EOF'
import re, sys
root, ver = sys.argv[1], sys.argv[2]
for f in ('pkg/appinfo.json',): # the version alone, the file as it is written (the What's new card keeps its own version: it is keyed by it, so a fix release with the same card never shows it twice)
    p = root + '/' + f; t = open(p, encoding='utf-8').read(); n = re.subn(r'("version"\s*:\s*")[^"]*(")', r'\g<1>' + ver + r'\g<2>', t, count=1)
    if n[1] != 1: sys.exit('no version in ' + f)
    open(p, 'w', encoding='utf-8').write(n[0])
EOF

"$ROOT/tools/build.sh" webos # always built now: the version just set is the one inside, and its debug build is kept below
rm -f "$ROOT/dist/${ID}_${VER}_arm.ipk"
"$ROOT/tools/package.sh" >/dev/null
IPK="$ROOT/dist/${ID}_${VER}_arm.ipk"
[ -f "$IPK" ] || { echo "no $IPK"; exit 1; }
SHA=$(shasum -a 256 "$IPK" | cut -d' ' -f1)
mkdir -p "$ROOT/dist/dbg/$VER" && cp "$ROOT/build/webos/stremioorigin.dbg" "$ROOT/dist/dbg/$VER/" # kept per version: a shared crash report names its code from it (getlogs)

rm -f "$SITE"/${ID}_*_arm.ipk
cp "$IPK" "$SITE/"
cp "$ROOT/pkg/largeIcon.png" "$SITE/icon.png"
python3 - "$SITE" "$ID" "$VER" "$SHA" "$BASE" "$SRC" "$ROOT/tools/homebrew" <<'EOF'
import json, os, sys
site, pid, ver, sha, base, src, hb = sys.argv[1:]
# the Homebrew Channel listing text, approved as written: tools/homebrew/ (the one place it lives, so it cannot drift)
short = open(os.path.join(hb, 'shortDescription.txt'), encoding='utf-8').read().strip()
full = open(os.path.join(hb, 'full_description.html'), encoding='utf-8').read()
manifest = {
  'id': pid, 'version': ver, 'type': 'native', 'title': 'Stremio Origin',
  'appDescription': 'A native app for Stremio addons, written in C. You bring the account and addons; it ships empty. 28 languages. Unofficial.',
  'iconUri': base + '/icon.png', 'sourceUrl': src, 'rootRequired': False,
  'ipkUrl': '%s/%s_%s_arm.ipk' % (base, pid, ver), 'ipkHash': {'sha256': sha}}
d = os.path.join(site, 'api', 'apps', pid)
os.makedirs(os.path.join(d, 'releases'), exist_ok=True)
for f in (os.path.join(d, 'manifest.json'), os.path.join(d, 'releases', 'latest.json')):
    open(f, 'w').write(json.dumps(manifest, indent=2) + '\n')
listing = {'paging': {'page': 1, 'count': 1, 'maxPage': 1, 'itemsTotal': 1}, 'packages': [{
  'id': pid, 'title': 'Stremio Origin', 'iconUri': base + '/icon.png', 'pool': 'main',
  'manifestUrl': '%s/api/apps/%s/manifest.json' % (base, pid), 'shortDescription': short,
  'fullDescriptionUrl': 'apps/%s/full_description.html' % pid, 'manifest': manifest}]}
open(os.path.join(site, 'api', 'apps.json'), 'w').write(json.dumps(listing, indent=2) + '\n')
listing['packages'][0]['fullDescriptionUrl'] = 'api/' + listing['packages'][0]['fullDescriptionUrl'] # the same list at the root: paths from there
open(os.path.join(site, 'apps.json'), 'w').write(json.dumps(listing, indent=2) + '\n')
open(os.path.join(d, 'full_description.html'), 'w', encoding='utf-8').write(full)
print('site updated:', pid, ver, sha)
EOF
ls -l "$SITE"/${ID}_*_arm.ipk
