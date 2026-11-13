# Bilinear upscaling for cropped and rounded artwork

## Problem

`src/ui/img.c` uses an area-average resampler to produce exact-sized,
rounded/cropped cards. This is correct for reduction. But when an addon provides
artwork smaller than the target card, the old resampler chooses one source pixel
per destination pixel, effectively nearest-neighbour enlargement and visible
blocks on larger displays.

**Plain images** are already displayed using OpenGL's `GL_LINEAR` sampling, so
they are not changed by this PR. Higher-resolution CDN imagery is still the
preferred solution; filtering cannot invent missing detail.

## Implementation

- Move the resampling routine to `src/ui/resample.c` so it can be tested without
  SDL, OpenGL or network dependencies.
- Keep the existing area-average reduction path byte-for-byte unchanged.
- For enlargements, sample at destination pixel centres using bilinear
  interpolation. Clamp interpolation within the cropped source area so pixels
  outside a rounded card's crop do not bleed in.
- Mixed scaling (one axis enlarged, one reduced) uses bilinear interpolation on
  the enlarged axis and area averaging on the reduced axis, to avoid aliasing.
- Interpolate transparent pixels using premultiplied RGBA values and
  unpremultiply for the unchanged straight-alpha renderer (prevents dark edges).
- Precompute pixel-coordinate mappings outside the inner loop. Opaque images
  enlarged along both axes use a 4-tap, fixed-point fast path with no division.
  This work is done by the existing bounded background decoder workers, not the
  UI rendering thread. No changes to GPU filtering or texture allocations.
- Bump only the **shaped** small-image disk cache key to `sm-bilinear2` so old
  nearest-neighbour derived images are not reused. Plain-image derivative keys
  and original downloaded-image cache keys remain unchanged. The first shaped
  visit after upgrading may need one new resize from the cached original.

## Performance

On the Linux build server (not a webOS TV), a reference benchmark measured:

| Opaque resize | Initial prototype | Fast path |
|---|---:|---:|
| 400×225 → 589×331 | 5.72 ms | 1.52 ms |
| 240×360 → 480×720 | 10.13 ms | 2.67 ms |

The pre-existing area-average path is unchanged. These values are indicative
CPU timings only; real webOS hardware validation is required.

## Validation

- `tests/resample/run.sh`: fixed expected bilinear samples, interpolated alpha,
  crop limits, mixed-axis resampling, edge cases, and 200 randomized
  byte-for-byte comparisons against the previous area-average algorithm.
- Host tests run with AddressSanitizer and UndefinedBehaviorSanitizer.
- Cross-compile using `tools/build.sh webos`.
- **Physical webOS testing still needed:** compare cards from undersized
  third-party artwork, check transparent edges, scrolling responsiveness and
  time spent decoding on older TVs.
