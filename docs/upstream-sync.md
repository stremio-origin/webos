# Upstream integration — 1.1.1

Base: `stremio-origin/webos` tag `v1.1.1` (`911e61a`), integrated on 2026-11-17.

## Hungarian branch (PR #26)

Since 1.1.0, upstream ships its own Hungarian translation, together with
playback-language recognition, language options, trailer captions, ratings and MKV
audio-language fallback. Keep those implementations, including the new Original audio
default and all 28 UI languages. The README language count follows upstream (28); the
old count of 12 no longer applies.

The PR is therefore no longer about adding Hungarian. It is a native speaker's
correction of the Hungarian wording, which upstream cannot check itself: the original
translations for existing keys, and a reviewed version of the keys upstream added.

How the dictionary loads: `i18n.c` reads `hu.json` first, then every `hu.<part>.json`,
and a key in a part file replaces the same key from `hu.json`. Between part files the
order is not fixed (it is the `readdir` order). So a key that is in more than one file
must have the same value in each. Otherwise a part file silently overrides the
translation. After the 1.1.1 merge, 18 of the original translations were overridden
this way. They were restored, and every duplicate key now has one value.

Review of the 1.1.x keys: upstream added 362 Hungarian keys with its own wording. No
UI text is missing: every `T()` string and every `assets/whatsnew.json` string has a
Hungarian entry. The keys only in `ar` (`, `, `72%`…`180%`) are for Arabic digits and are
not needed. The new keys were aligned with the branch glossary:
- Addon is "kiegészítő", not "bővítmény".
- Episode is "epizód".
- Informal "te", "TV" (not "tévé"), "alkalmazás" (not "app").
- "Statisztika kockáknak", "Intro …", "formázott feliratok".
- Trakt with hyphenated suffixes ("Trakt-ról", "Trakt-on", "Trakt-hoz").

Clear mistranslations were also fixed, for example "Writer", "Part {n} of {m}" and
the compact next-episode card. Literal phrasings in `hu.wait.json` were rewritten as
natural Hungarian, and the missing articles before brand names were added.

Not changed:
- The hyphenated brand possessives ("Samsung-od"), kept like "Trakt-ról".
- Keys the code no longer uses (`{n}s`, `Pin`/`Unpin`).

To check after the next upstream sync:
1. Every `T()` literal and every `assets/whatsnew.json` string is a key in some `hu*.json`.
2. No key appears in two `hu*.json` files with different values.
3. New upstream values follow the glossary above.

## Pointer branch (PR #27)

Based on the Hungarian branch. Upstream now sets the same 5000 ms webOS SDL cursor
hint before SDL starts. Keep one setting only, alongside upstream's updated pointer
handling. The remaining pointer-specific differences are explanatory comments.
