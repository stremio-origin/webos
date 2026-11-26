#!/usr/bin/env python3
"""sbkeys.h for the Android TV build, made exactly as tools/build.sh makes it: the app's own keys (Trakt, TMDB) from the
private keys file, scrambled. No file (or an empty path): empty keys, and each user then needs their own."""
import hashlib, os, sys
src, out = sys.argv[1], sys.argv[2]; kv = {}
if src and os.path.isfile(src):
    for ln in open(src):
        ln = ln.strip()
        if ln and not ln.startswith('#') and '=' in ln: k, v = ln.split('=', 1); kv[k.strip()] = v.strip().strip('"\'')
def arr(name):
    v = kv.get(name, '').encode(); pad = hashlib.sha256(b'sb:' + name.encode()).digest()
    return 'static const unsigned char SBK_%s[] = { %s0 }, SBK_%s_PAD[32] = { %s };\n#define SBK_%s_LEN %d\n' % (name, ''.join('%d,' % (c ^ pad[i % 32]) for i, c in enumerate(v)), name, ','.join(str(x) for x in pad), name, len(v))
body = '/* made by tools/android-keys.py from the private keys file: never commit */\n' + arr('TRAKT_CLIENT_ID') + arr('TMDB_API_KEY')
os.makedirs(os.path.dirname(out), exist_ok=True)
if not os.path.isfile(out) or open(out).read() != body: open(out, 'w').write(body)
print('== keys built in: %s' % (', '.join(n for n in ('TRAKT_CLIENT_ID', 'TMDB_API_KEY') if kv.get(n)) or 'none'))
