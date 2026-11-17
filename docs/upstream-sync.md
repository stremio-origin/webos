# Upstream integration — 1.1.1

Base: `stremio-origin/webos` tag `v1.1.1` (`911e61a`), integrated on 2026-11-17.

## Subtitle branch (PR #28)

Upstream now decodes legacy subtitles with the shared `textenc.c` implementation,
including Windows-1250, additional Windows code pages and detection when the language
is unknown. External subtitle playback uses upstream's `subs_load_lang()` and preserves
its styled ASS support. The old branch-local Windows-1250 tables are superseded.

The remaining change passes the reference subtitle language from automatic subtitle
sync to `subs_parse_body()`. That entry point canonicalises ISO language aliases and
BCP-47 codes before selecting the code page. NULL or empty language keeps upstream's
automatic detection; Unicode decoding stays upstream's implementation.

Validation: `bash tests/subs-lang/run.sh` runs the real parser for Hungarian aliases,
Polish, French, UTF-8 and unknown/empty language, including cue timings. SDL2 headers
and zlib development libraries are needed; `SDL_CFLAGS` may specify their location when pkg-config is unavailable. `ZLIB_LIBS` can override the default `-lz`.
