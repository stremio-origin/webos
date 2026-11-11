# Forced subtitle preference

Settings → Subtitles → Default subtitles: choose a language (e.g. French).
Settings → Subtitles → Subtitle preference: **Forced only** or **All subtitles** (the existing behaviour and default).

**Forced only** means that the player automatically selects a *forced* track in the preferred language, including Matroska `FlagForced` and tracks named `fr.forced.srt`, `Français forcés`, `Español forzado`, `Português forçado`, `Deutsch erzwungen`, etc. Embedded tracks take precedence over downloaded ones. A non-forced track is **never** substituted if there is no forced track, so subtitles remain off. The original audio, decoder, rendering, subtitle-sync and manual track selection are unchanged.

- Supports metadata `forced`, `isForced`, `is_forced`, `forcedSubtitle`, `disposition.forced`; uses subtitle filenames, labels and (as a fallback) release names for addons.
- Language matching keeps using the existing ISO-639-1/2 and BCP-47 normalisation. Tracks with an unknown language do not auto-select, even if their title contains a forced tag, avoiding subtitles in the wrong language.
- Selection is performed as tracks or addon results arrive (the MKV tracks get an initial opportunity). A manual subtitle choice or manual Off during playback wins for the remainder of that playback.
- A forced track is not generated from full dialogue subtitles. The source must actually offer a forced-only track; this is a selection feature, not speech translation.
- No automatic downloads or external APIs are added.

To run the SDK-independent name-detection tests:

```sh
cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -Isrc -Ithird_party/cjson tests/forced/test_names.c src/core/forcedsubs.c third_party/cjson/cJSON.c -lm -o /tmp/stremio-forced-tests
/tmp/stremio-forced-tests
```

Follow-up on a TV: test an MKV with `FlagForced`, one with a forced word only in `TrackName`, an addon-provided sidecar track named `French Forced`, and a video with only full subtitle tracks. Confirm selection on a new playback and that manual Off is not reverted when addon requests finish.
