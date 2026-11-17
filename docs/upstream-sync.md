# Upstream integration — 1.1.1

Base: `stremio-origin/webos` tag `v1.1.1` (`911e61a`), integrated on 2026-11-17.

## Hungarian branch (PR #26)

Upstream now provides Hungarian UI, playback-language recognition, language options,
trailer captions, ratings and MKV audio-language fallback. Keep those implementations,
including the new Original audio default and all 28 UI languages. This branch retains
the original Hungarian translations for existing keys and includes upstream's new keys.
The README language count follows upstream (28); the old count of 12 no longer applies.

## Pointer branch (PR #27)

Based on the Hungarian branch. Upstream now sets the same 5000 ms webOS SDL cursor
hint before SDL starts. Keep one setting only, alongside upstream's updated pointer
handling. The remaining pointer-specific differences are explanatory comments.
