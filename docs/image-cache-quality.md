# Size-aware plain-image cache

## Problem

The old `img_get(url, width, height)` cache used the URL alone as the key.
If a logo was first decoded for a small settings preview and later displayed
as large title artwork, `request_as()` returned the earlier GPU texture without
considering the larger requested dimensions. This could look blurry even when
the original downloaded image was high resolution.

## Approach

- Keep the original URL entry and all existing shaped-card cache keys intact.
- Create an immutable *plain image* variant only when its requested dimensions
  exceed the available decoded sizes by more than 8%. This avoids racing with
  worker threads already decoding or uploading another version of the image.
- While the new variant downloads, decodes, and uploads, display the old GPU
  texture so navigation remains responsive. The raw network response is reused
  from the existing disk cache. Wait for the original to finish its download
  before scheduling a larger copy, avoiding unnecessary concurrent fetches.
- Keep no more than three plain variants per URL (original plus two upgrades),
  deny upgrades when GPU memory is tight, and preserve existing LRU eviction.
  The width/height of the original cannot decrease.
- Include the source texture ID in the already-cached backdrop bake state so
  a sharper source triggers a new bake instead of leaving the low-resolution
  offscreen backdrop on screen indefinitely.

This does **not** request larger image files from the provider: it uses more of
an existing high-quality source when the application needs a larger texture.
Source URL resolution changes belong in the separate artwork PRs.

## Validation

`tests/imgquality/run.sh` checks resolution thresholds and upgrade limits.
WebOS cross-compilation verifies the full renderer integration. Physical TV
checks are still required: open a small-logo preview then its title detail,
quickly switch between a backdrop and a player overlay, and check navigation
latency / image cache use at both low and high memory pressure.
