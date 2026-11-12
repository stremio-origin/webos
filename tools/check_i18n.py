#!/usr/bin/env python3
"""Verifies Stremio Origin i18n integrity across all 10 translated locales."""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
I18N = os.path.join(ROOT, "assets", "i18n")
LOCALES = ["ar", "fr", "es", "de", "it", "pt", "ru", "tr", "pl", "nl"]
REQUIRED_ANIME_KEYS = [
    "Anime",
    "No anime catalogs installed",
    "No catalogs available",
    "Enable catalog addons in Settings › Addons",
]
PLACEHOLDER_RE = re.compile(r"\{[a-zA-Z0-9_]+\}")

def main():
    files = sorted(glob.glob(os.path.join(I18N, "*.json")))
    assert files, f"No JSON files found in {I18N}"
    merged = {loc: {} for loc in LOCALES}

    for path in files:
        name = os.path.basename(path)
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        assert isinstance(data, dict), f"{name}: root must be object"
        loc = name.split(".")[0]
        for k, v in data.items():
            assert isinstance(k, str) and isinstance(v, str), f"{name}: non-string entry {k!r}"
            if k.startswith("__"):
                continue
            kp = set(PLACEHOLDER_RE.findall(k))
            vp = set(PLACEHOLDER_RE.findall(v))
            if kp != vp:
                print(f"FAIL {name}: placeholder mismatch for {k!r}: {kp} vs {vp}", file=sys.stderr)
                return 1
            if loc in merged:
                merged[loc][k] = v

    ref_tmdb_keys = set(json.load(open(os.path.join(I18N, "de.tmdb.json"), encoding="utf-8")).keys())
    for loc in LOCALES:
        tmdb_path = os.path.join(I18N, f"{loc}.tmdb.json")
        anime_path = os.path.join(I18N, f"{loc}.anime.json")
        if not os.path.isfile(tmdb_path):
            print(f"FAIL missing {loc}.tmdb.json", file=sys.stderr)
            return 1
        if not os.path.isfile(anime_path):
            print(f"FAIL missing {loc}.anime.json", file=sys.stderr)
            return 1
        loc_tmdb = set(json.load(open(tmdb_path, encoding="utf-8")).keys())
        if loc_tmdb != ref_tmdb_keys:
            print(f"FAIL {loc}.tmdb.json key mismatch: diff={loc_tmdb ^ ref_tmdb_keys}", file=sys.stderr)
            return 1
        for rk in REQUIRED_ANIME_KEYS:
            if not merged[loc].get(rk, "").strip():
                print(f"FAIL {loc}: missing translation for {rk!r}", file=sys.stderr)
                return 1

    assert merged["ar"]["Anime"] == "أنمي", f"Unexpected ar Anime: {merged['ar']['Anime']!r}"
    print(f"i18n OK: {len(files)} files validated across {len(LOCALES)} locales (ar.tmdb.json + *.anime.json + placeholders clean).")
    return 0

if __name__ == "__main__":
    sys.exit(main())
