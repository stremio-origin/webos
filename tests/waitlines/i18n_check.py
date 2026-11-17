#!/usr/bin/env python3
# tests/waitlines/i18n_check.py : every loading line (all pools but the old humour, which stays in the wait files) is
# translated in every language the app has: present, not just the English again, 60 characters at most, its "…" and
# its question mark kept, brand names unchanged. Run by tests/waitlines/run.sh.
import json, os, re, sys, glob
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
s = open(os.path.join(ROOT, 'src/core/waitlines_pools.h'), encoding='utf-8').read()
a = s.index('WL_LINES[] = {'); body = s[a:s.index('};', a)]
lines = []; pool = {}; oldLines = []
for row in body.split('\n'):
    m = re.match(r'\s*/\* (\w+) \*/ (.*),$', row)
    if m:
        for x in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2)):
            t = x.encode('latin-1').decode('unicode_escape').encode('latin-1').decode('utf-8')
            if m.group(1) == 'OLD': oldLines.append(t)
            else: lines.append(t); pool[t] = m.group(1)
CARD = ['Enjoying Stremio Origin?', 'One person makes it in their spare time. A coffee or a star keeps it going.', 'Maybe later', "Don't show again"]
DEVICE = ('LG', 'LG_OLED', 'LG_QNED', 'LG_NANO', 'SAMSUNG', 'ATV', 'ATV_SHIELD', 'ATV_SONY', 'ATV_CHROMECAST', 'ATV_FIRETV', 'ATV_XIAOMI', 'ATV_PHILIPS', 'ATV_TCL', 'PHONE')
# bugs found once and fixed (2026-11-11 sweep), each kept from coming back: (language, which lines, a pattern that must not match, why)
NEVER = [
    ('ru', DEVICE, r'^(?!.*этим).*\bзанимается\b', '"занимается" alone means "studies": "этим занимается"'),
    ('uk', DEVICE, r'^(?!.*цим).*\bзаймається\b', '"займається" alone means "studies": "цим займається"'),
    ('hu', DEVICE, r'^(?!A |Az )[\w ]+-(d|ed|od|det|edet|odat|dat|et)\b', 'a possessed name needs its article: "Az LG-d", "A Samsung-od"'),
    ('hu', DEVICE, r'^(A|Az) (Philips|TCL)-et\b', '"your" was lost: "-edet"'),
    ('ar', ('FRIDAY',), r'ليلة الجمعة', '"ليلة الجمعة" is Thursday night; the pool runs Friday evening'),
    ('fa', ('FRIDAY',), r'شب جمعه', '"شب جمعه" is Thursday night; Friday evening is "جمعه شب"'),
    ('id', ('FRIDAY',), r'Malam Jumat', '"Malam Jumat" is Thursday night; Friday evening is "Jumat malam"'),
    ('tr', ('ATV_SHIELD',), r"Shield'ı", 'Shield is said "şild": front vowels, "Shield\'in"'),
    ('sr', ('*',), r'[\u0400-\u04ff]', 'the Serbian app is written in Latin letters'),
    ('hr', ('*',), r'\b[Vv]am\b|\b[Vv]aš', 'the Croatian app says "ti", not "vi"'),
    ('ro', ('*',), r'\bÎți\b|\btău\b|\bta\b', 'the Romanian app is formal: "vă", "dvs."'),
    ('fr', ('*',), r"\btu\b|\bt'|\bton\b|\bta\b|\btes\b", 'the French app says "vous"'),
    ('de', ('PHONE',), r'^Telefon\b', 'a common noun needs its article: "Das Telefon"'),
    ('nl', ('PHONE',), r'^Telefoon\b', 'a common noun needs its article: "De telefoon"'),
]
src = open(os.path.join(ROOT, 'src/core/i18n.c'), encoding='utf-8').read()
codes = re.findall(r'"([a-z]{2}(?:-[A-Z]{2})?)"', src[src.index('codes[]'):src.index('};', src.index('codes[]'))])
codes = [c for c in codes if c != 'en']
BRANDS = ['LG', 'OLED', 'QNED', 'NanoCell', 'Samsung', 'Android TV', 'Shield', 'Sony', 'Chromecast', 'Fire TV', 'Xiaomi', 'Philips', 'TCL']
SAME_OK = set()   # a line a language may rightly leave as the English (none so far)
fails = 0
for c in codes:
    d = {}
    for f in sorted(glob.glob(os.path.join(ROOT, 'assets/i18n', c + '.*json'))):   # as the app reads them: <lang>.json, then every <lang>.<part>.json
        base = os.path.basename(f)
        if base != c + '.json' and not base.startswith(c + '.'): continue
        if base.count('.') > 1 and base.split('.')[0] != c: continue
        d.update(json.load(open(f, encoding='utf-8')))
    bad = []
    for en in CARD + oldLines:   # the support card and the old humour: present, translated, punctuation and spacing
        t = d.get(en)
        if not t: bad.append('missing: ' + en); continue
        if t == en: bad.append('the English again: ' + en)
        if '?' in en and '?' not in t and '؟' not in t and ';' not in t and ';' not in t: bad.append('lost its question mark: ' + t)
    for en in lines + CARD + oldLines:
        t = d.get(en)
        if not t: continue
        if '  ' in t or t != t.strip(): bad.append('a double or stray space: "%s"' % t)
        if '...' in t: bad.append('three dots, not "…": ' + t)
        if c in ('ar', 'fa') and '?' in t: bad.append('a Latin "?" in right-to-left text (؟): ' + t)
        for (lc, pools, pat, why) in NEVER:
            if lc == c and ('*' in pools or pool.get(en) in pools) and re.search(pat, t): bad.append('%s: %s' % (why, t))
    for en in lines:
        t = d.get(en)
        if not t: bad.append('missing: ' + en); continue
        if t == en and en not in SAME_OK: bad.append('the English again: ' + en)
        if len(t) > 60: bad.append('%d characters: %s' % (len(t), t))
        if en.endswith('…') and not t.endswith('…'): bad.append('lost its "…": ' + t)
        if '?' in en and '?' not in t and '؟' not in t and not (c == 'el' and (';' in t or ';' in t)): bad.append('lost its question mark: ' + t)  # (Arabic and Persian write ؟, Greek ;)
        for b in BRANDS:
            if re.search(r'\b' + re.escape(b) + r'\b', en) and b not in t: bad.append('lost "%s": %s' % (b, t))
    if bad: fails += len(bad); print('%s: %d problem(s)\n  ' % (c, len(bad)) + '\n  '.join(bad[:8]))
print('translations: %d languages x %d lines, %s' % (len(codes), len(lines), 'all present and sound' if not fails else '%d problem(s)' % fails))
sys.exit(1 if fails else 0)
