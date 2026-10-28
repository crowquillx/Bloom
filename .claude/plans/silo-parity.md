# Silo parity plan (2026-10-27)

Baseline: Bloom pins Silo `35ef973`; Silo main is `62b1a6e82`. No breaking API
changes in between. Subtitle rule for every step: **embedded (internal) tracks
come first**. Bloom selects them in mpv from the original file; server sidecars
are only for true external or downloaded files.

## Step 1: playback bugs

1. **Embedded-first subtitles**
   - Send `subtitle_track_index` as Silo's combined ordinal (externals first,
     then embedded), computed in `SiloPlaybackProvider` from the `/watch`
     source. Today Bloom sends the ffmpeg stream index, so Silo picks the
     wrong track.
   - Negotiate `embedded_subtitles_v1` and declare `native_embedded` claims
     (`ffmpeg_stream_index`) for every container and codec mpv renders, so
     Silo answers `render` + `embedded` instead of extracting a sidecar or
     forcing burn-in.
   - Never `sub-add` inventory entries whose `source` is `embedded`; mpv
     already has those tracks. This removes the duplicate menu entries and the
     per-start server extraction.
   - Merge external inventory entries into the `/watch` external tracks they
     belong to (`PlaybackTrack::sourceIndex`) instead of appending a second
     synthetic copy. Downloaded entries stay synthetic.
   - Make `PlaybackPolicy` prefer an internal track over an external one when
     both match a rule (language, forced, file default). This applies to both
     providers.
   - Add missing direct-play containers (`m2ts`, `wmv`, `flv`, `mpeg`), which
     mpv plays but Bloom never claimed.
2. **Session map leak**
   - Evict the oldest tracked session instead of refusing every new start once
     64 accumulate.
3. Tests for each in `SiloPlaybackProviderTest`, `PlaybackPolicyTest`, and
   the PlayerController subtitle tests.

## Step 2: pin bump to `62b1a6e82`

- Regenerate `tests/contracts/silo-v2-openapi-manifest.json`.
- Update `provider-contracts.json` (revision, hashes, deployment id) and
  `docs/provider-compatibility.md`.
- Correct the `native.auth.providers` claim if still unused.

## Step 3: session lifecycle

1. Progress/replan `404` means the session expired: restart a session at the
   current position. `410 playback_session_ended` means leave playback with a
   message, no restart.
2. Send a progress heartbeat while paused (Silo reaps paused sessions after 30
   minutes).
3. Refresh a near-expiry Silo access token before starting playback, because
   the stream URL carries the token for the whole movie.
4. Multipart: start part N+1's session only when part N starts playing,
   instead of starting every part up front (Silo reaps idle sessions after
   45 s).
5. Surface Silo's terminal reasons instead of "unsupported or malformed
   playback decision".

## Step 4: features

1. **Remux/transcode fallback**: accept Silo HLS/remux plans so non-direct
   files still play.
2. **Device-code sign-in** (`/auth/device/*`): show the code and activation
   URL, then poll.
3. **Silo Home rows** (`/home/sections`) rendered on Home for Silo
   connections.
4. **Watchlist**: detail-page toggle plus a Watchlist row.

Each step ends with build + targeted ctest. Docs updated in the same change.
`nix flake check` at the end.
