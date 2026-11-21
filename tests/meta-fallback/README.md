# Series metadata fallback regression tests

```sh
python3 tests/meta-fallback/run.py
```

Needs Python 3 and a C compiler with UndefinedBehaviorSanitizer. No SDK, service
keys, account, network access or TV is needed. The runner extracts the actual
metadata implementation from `src/core/stremio.c`, compiles it with the real
cJSON implementation and deterministic asynchronous network/cache stubs, and
runs with `-Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all`.

## Cases

The 19 cases exercise:

- Parallel TMDB identity lookup and Cinemeta fallback while the preferred addon
  is slow or returns HTTP 504.
- Preferred addon winning before identity lookup or Cinemeta completes.
- Both completion orders, one callback per consumer, and late results not
  overwriting the chosen metadata/cache.
- Preserving the original TMDB library ID and IMDb episode IDs.
- Invalid/missing/mismatched external identities and cancellation/failure paths.
- Rejecting results from a changed addon generation.
- No-key and unsupported-ID behavior.
- Incomplete series responses, including genuinely unaired series when neither
  provider has episodes.
- Immediate usable disk-cache delivery followed by refresh, respecting a valid
  addon cache age, and refusing to permanently deliver an empty-episode disk copy.
- Direct IMDb series fallback without duplicating a Cinemeta-only provider.

Request-context allocation/free accounting must balance after every case. The
test harness does not prove the absence of all JSON allocation leaks or replace
real worker-thread, SDK/ABI or TV testing.

## Behavior/tradeoff

The existing addon chain still tries providers in order. In parallel, an IMDb
series can ask Cinemeta directly; a numeric TMDB movie/series can resolve IMDb
identity with the configured TMDB key and then ask Cinemeta. The **first complete
response wins**, so an available Cinemeta response can replace waiting for a
preferred addon. This may change descriptions/artwork from that preferred addon
and adds bounded fallback traffic (one extra request for IMDb series, up to two
for TMDB titles). Other/custom IDs and ordinary IMDb movies keep their existing
provider behavior. No new credentials, timeout settings, threads or network services
are introduced; the existing configured TMDB key and network workers are used.

Callbacks remain one-shot: late answers cannot call consumers whose contexts may
already be freed. A series cache object without episodes must therefore not be
delivered as final while a background refresh silently obtains the episode list.

## Hardware validation

The same metadata implementation was exercised on LG C1 / webOS 6.5.3 with
release 1.1.1 assets and a local diagnostic build:

- Reproduced a preferred-addon metadata HTTP 504 after about 12.5 seconds, with
  only 0.3 ms of local queue wait; this was not inferred from catalog failures.
- Mapped-TMDB fallback succeeded while the original request still timed out;
  the original library ID and IMDb episode IDs were retained.
- Six fresh-process/cold-title-metadata launches passed: two each for The
  Marvelous Mrs. Maisel, Severance and Dexter, opened on frame 1. Season/episode
  controls were visually verified in compositor captures; relevant successful
  metadata requests took roughly 0.1–0.3 seconds in those runs.
- Video playback smoke test reached media-service `loadCompleted` and `playing`.

This is not an assurance that all providers/ID schemes or network failures are
covered. Private credentials, addon URLs, raw logs, screenshots, diagnostic
instrumentation, binaries and device configuration are intentionally excluded.
