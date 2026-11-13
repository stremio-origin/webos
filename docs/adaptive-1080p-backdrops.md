# Adaptive 1080p backdrops on webOS

## Why only backdrops?

Stremio Origin's interface uses a 1920×1080 logical canvas, but the two
pre-shaded/crossfading backdrop render targets and source images are currently
requested at 1280×720. On capable TVs the 720p baked texture is then displayed
across the 1080p canvas. A 1080p source and bake can look sharper, especially
on large panels, without raising the rendering resolution of the rest of the UI.

The larger texture is **not a 4K user interface**. If the upstream image itself
is limited to 1280 pixels (or a smaller cached source was already used), this
change cannot invent higher-frequency image detail. Provider URL improvements
and the separate size-aware image cache PR complement this feature.

## Selection and compatibility

| Startup check | Result |
|---|---|
| Drawable is smaller than 1920×1080 | Keep existing 1280×720 |
| Reported system RAM below 1792 MiB | Keep existing 1280×720 |
| `/proc/meminfo` MemAvailable below 384 MiB, or unreadable | Keep existing 1280×720 |
| All checks pass | Use 1920×1080 source-size requests and baked targets |

System RAM deliberately uses Linux's **MemTotal**, not the TV's marketed
physical memory: hardware reservations matter. The cutoff is 1.75 GiB so
capable TVs with approximately 2 GiB reported to Linux are eligible. This is
not a GPU speed guarantee; the runtime has safety checks as well.

## Runtime safeguards

1. Existing image loading and banded GPU uploads are retained; no synchronous
   full-resolution image decoding is added to the render thread.
2. Two 1920×1080 RGBA baked textures consume about 15.8 MiB versus about
   7.0 MiB for the existing two 1280×720 textures. Source images also cost
   memory, and the existing image memory budget / eviction still applies.
3. If MemAvailable goes below 256 MiB or the app RSS exceeds 25% of Linux
   MemTotal, the next frame releases *both* larger baked targets and returns to
   720p for the rest of the app session.
4. If a 1080p framebuffer cannot be created, the app retries the original
   720p size before falling back to the existing direct drawing path.
5. If a backdrop is visible and static, and frame times are worse than 40 ms
   for a sustained period (90 slow-frame strikes, reduced by recovered fast
   frames), the app also downgrades for the rest of the session. Trailer/video
   playback, transitions and empty backdrops are excluded from this check.
6. No repeated up/down toggling is allowed. Fades and backdrop shading remain
   unchanged, with the stored bake invalidated on downgrade.

For an A/B performance comparison, set `SB_BG_720=1` in the app's environment
to force the old quality, regardless of the automatic decision.

## Tests

- `tests/bgquality/run.sh` tests surface sizes, memory thresholds, unknown
  memory readings, and sustained frame-time fallback with recovery.
- Run an ARM/webOS build (`tools/build.sh webos`).
- **Pending physical validation:** test on an eligible webOS TV and a low-RAM
  TV, verify logs (`[bg] backdrop ...`, `[bg] fallback ...`), image sharpness,
  transition continuity, memory use, and responsiveness during rapid navigation
  and video playback. Compare `SB_BG_720=1` against automatic mode.

This PR intentionally does **not** change TMDB/CDN URLs, the GPU texture cache,
or other page textures. They are addressed separately and are compatible with
this policy-based resolution choice.
