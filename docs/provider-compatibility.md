# Provider compatibility and contract baseline

This page defines the server contract baseline for [issue #74](https://github.com/crowquillx/Bloom/issues/74), the first increment of the Silo support epic. It records Bloom's current MediaBrowser/Jellyfin wire assumptions, the Silo compatibility result, and machine-checkable native Silo expectations for [#77](https://github.com/crowquillx/Bloom/issues/77) through [#80](https://github.com/crowquillx/Bloom/issues/80).

The machine-readable source is [`tests/contracts/provider-contracts.json`](../tests/contracts/provider-contracts.json). Its validator requires semantic evidence for every result, pins every native expectation to the recorded Silo revision, and rejects unavailable capabilities labeled as supported; an HTTP `200` alone never means a feature is supported. The implementation boundaries and connection/credential migration are documented in [provider architecture](provider-architecture.md).

## Support terminology

- **Silo compatibility support**: Bloom connects to Silo's optional Jellyfin compatibility listener. The native API is not used, and household-profile login has compatibility-specific limitations.
- **Experimental native Silo support**: Bloom connects to `/api/v1` with Silo's compatibility listener disabled and implements native authentication, household-profile, catalog, signed-artwork, and legacy mpv playback boundaries from the pinned revision; optional v3/media3-only features remain unavailable. This is the release label for the current native path.
- **First-class Silo support**: A future claim requiring authentication, profile, catalog, playback, recovery, and platform gates in issue #73. Bloom does **not** make this claim for the current release.

These labels describe a connection's protocol mode, not an application-wide provider choice. A future provider can add another protocol surface and deployment to the contract matrix without changing existing Jellyfin or Silo rows. The machine contract currently records the native deployment as `research-only` because live native Silo/Jellyfin hosts and platform runtime hardware are unavailable; that evidence state does not convert unavailable capabilities into support.

Contract outcomes are:

| Matrix label | Meaning |
|---|---|
| `supported` | The required payload and observable behavior work. |
| `partial` | The core journey works, but requested behavior or data is ignored or absent. |
| `stubbed` | A route returns a success-shaped placeholder without the required behavior. |
| `missing` | **Unavailable**: the route is absent or cannot provide fields Bloom needs. This is never a claim of support. |
| `not-applicable` | **Intentionally out-of-scope**: Bloom does not claim this optional protocol surface in the pinned baseline. |

## Reproducible server pins

The baseline was prepared from:

| Component | Pin |
|---|---|
| Bloom | `571218325258fae98e5606f4b84cf92ff0c5b1e6` |
| Jellyfin control | `10.11.10`, `docker.io/jellyfin/jellyfin@sha256:f66273e014b307e4ac46778845ebc1e9ee24b2e57c1fc17d5ec5ac3015649bfa` |
| Silo | `8044eb84dd0cfa512ce8f2448cfd51cb7899a4c6` |
| Silo image | `ghcr.io/silo-server/silo-server@sha256:944ee9821de1d6a61876c9b7b06daa358118163d1e5f9b3aa9f5437856fd06e9` (tag `8044eb8`) |

Container digests are immutable. Short-SHA tags are included only for operator readability.

### Silo compatibility deployment

Silo exposes separate listeners:

| Surface | Typical compose host port | Use in this baseline |
|---|---:|---|
| Native web/API | `8090` | Onboarding, admin settings, and native probes |
| Jellyfin compatibility | `8096` | Bloom compatibility target |
| Audiobookshelf compatibility | `13378` | Out of scope |

Ports are deployment configuration, not protocol identity.

1. Clone Silo and check out the pinned revision.
2. Copy `.env.example` to `.env`. Set `MEDIA_ROOT` to a test library, set a durable `SILO_DATA_ROOT`, and generate the required `SECRET_KEY`.
3. Pin `SILO_IMAGE` to the digest above.
4. Start the bundled PostgreSQL, Redis, and Silo services.
5. Complete onboarding at `http://127.0.0.1:8090` and add a movie and series library. For compatibility coverage, enable Jellyfin-compatible app support and restart Silo.
6. For **Silo compatibility support**, point Bloom at `http://127.0.0.1:8096`. For **experimental native Silo support**, leave the compatibility listener disabled, confirm `GET http://127.0.0.1:8090/api/v1/health` returns `status: "ok"`, and point Bloom at `http://127.0.0.1:8090`. Bloom must record the selected `protocol_mode`; it must not switch protocol surfaces after a failed login.

Example Fish session after editing `.env`:

```fish
set -x SILO_IMAGE ghcr.io/silo-server/silo-server@sha256:944ee9821de1d6a61876c9b7b06daa358118163d1e5f9b3aa9f5437856fd06e9
docker compose up -d
curl --fail --silent http://127.0.0.1:8090/api/v1/health | jq
curl --fail --silent http://127.0.0.1:8096/System/Info | jq
```

The compatibility listener may be unbound before it is enabled. A connection refusal on `8096` is therefore different from invalid credentials.

### Native versus compatibility connection instructions

- Compatibility uses the MediaBrowser/Jellyfin login flow. A single matching profile may work with ordinary credentials; household profiles use Silo's compatibility `username#profile` and PIN `password#pin` suffixes.
- Native uses Silo provider discovery and `/api/v1/auth/login`, then optional `/api/v1/profiles/{id}/verify-pin`. Bloom stores access, refresh, and profile tokens only in `CredentialStore`; `app.json` keeps connection metadata and an opaque credential reference.
- Native detection is deterministic health detection (`status: "ok"`), not a successful compatibility `/System/Info` response. Native and compatibility connections are separate records and never silently fall back to one another.
- Native playback intentionally omits `protocol_version: 3`; the pinned capability is `media3_only`, not an mpv contract. Protocol v3 remains unavailable for Bloom/mpv.

### Silo troubleshooting

- **Connection refused on 8096**: the compatibility listener is disabled or unbound; this is different from invalid credentials. Enable it only for compatibility testing and restart Silo.
- **Native `404` or non-JSON health**: verify Bloom points to the native listener and the pinned Silo revision exposes `/api/v1/health`; do not identify Silo from `/System/Info`.
- **Login succeeds but profile access fails**: list profiles, select the account/profile identity, and complete PIN verification when `has_pin` is true. A wrong PIN is a valid `200 {"valid": false}` response, not a transport failure.
- **Artwork disappears after caching**: signed artwork URLs are opaque and may expire. Refetch the owning catalog/detail/person/chapter resource; never strip or rebuild its query parameters.
- **Playback cannot seek or show previews**: direct range playback is the strongest native/compatibility path; native trickplay is unavailable at the pin, and HLS/transcode/audio switching are conditional on Silo policy/runtime.
- **401 during playback**: shared transport refreshes once through the selected provider. Native recovery refetches catalog/version data and starts a native legacy session; it never retries by constructing a Jellyfin URL.

### Migration and rollback

Connection migration preserves the old `settings.jellyfin` metadata and legacy keychain entry until `CredentialStore` writes and reads back the provider-neutral credential. A failed write, verification, or cleanup leaves rollback material intact and retries later. Logout removes provider-neutral credential kinds plus matching legacy Jellyfin credentials without deleting another connection's metadata. See [provider architecture](provider-architecture.md) and [SECURITY.md](SECURITY.md) for the exact key schema and cleanup behavior.

For the baseline library, use at least:

- one movie with a poster, backdrop, chapter, external subtitle, and external provider IDs;
- one series with a season and at least two episodes;
- one episode with progress and intro/credits markers;
- one alternate-version or multipart title where available.

Silo compatibility household login uses `username#profile`. PIN-protected profiles additionally use `password#pin`. A single non-PIN profile matching the account name may be selected without suffixes. This is compatibility behavior only; native Bloom support must use Silo's account/profile/PIN API.

### Jellyfin control deployment

Run the pinned Jellyfin image with separate config/cache/media directories, finish its setup, and create the same minimum libraries and user-state scenarios. For example:

```fish
set jellyfin_image docker.io/jellyfin/jellyfin@sha256:f66273e014b307e4ac46778845ebc1e9ee24b2e57c1fc17d5ec5ac3015649bfa
podman run --rm --name bloom-jellyfin-contract \
  -p 18096:8096 \
  -v $PWD/.contract-data/jellyfin/config:/config \
  -v $PWD/.contract-data/jellyfin/cache:/cache \
  -v /path/to/test-media:/media:ro \
  $jellyfin_image
```

The live deployments are intentionally not started inside `nix flake check`. They require operator-owned media, credentials, mutable databases, and platform playback checks. The checked-in validator keeps the matrix complete and immutable between live runs.

The native deployment runs without the compatibility listener. Without credentials, the live runner performs only the deterministic public health check and never creates server state. Set `BLOOM_CONTRACT_USERNAME` and `BLOOM_CONTRACT_PASSWORD` (and, for a PIN profile, `BLOOM_CONTRACT_PROFILE_PIN`) to extend the run with provider discovery, login/refresh/error envelopes, account/profile shapes, catalog pagination/query/detail shapes, and one opaque artwork fetch; it logs no token, password, or PIN. Native playback start/progress/audio-switch/stop probes are destructive session lifecycle checks and are skipped unless `--allow-mutations` is explicitly supplied with the fixture credentials.

```fish
python3 tests/contracts/run_live_contracts.py \
  --deployment silo-8044eb8-native \
  --base-url http://127.0.0.1:8090 \
  --output .contract-data/silo-native-health-report.json
```

To opt into the native playback lifecycle, use a disposable fixture account/library:

```fish
python3 tests/contracts/run_live_contracts.py \
  --deployment silo-8044eb8-native \
  --base-url http://127.0.0.1:8090 \
  --allow-mutations \
  --output .contract-data/silo-native-playback-report.json
```
MediaBrowser deployments use the same provider-neutral authenticated probe. The password is read from the environment and is never written to the report:
```fish
set -x BLOOM_CONTRACT_USERNAME bloom-contract
read --silent --prompt-str 'Contract password: ' BLOOM_CONTRACT_PASSWORD
set -gx BLOOM_CONTRACT_PASSWORD $BLOOM_CONTRACT_PASSWORD

python3 tests/contracts/run_live_contracts.py \
  --deployment silo-8044eb8-compat \
  --base-url http://127.0.0.1:8096 \
  --allow-mutations \
  --output .contract-data/silo-report.json

python3 tests/contracts/run_live_contracts.py \
  --deployment jellyfin-supported \
  --base-url http://127.0.0.1:18096 \
  --allow-mutations \
  --output .contract-data/jellyfin-report.json
```

The live driver is selected by protocol surface, while expectations are selected by deployment. Native health is safe without credentials; credentialed native runs use a dedicated fixture account and perform auth/profile/catalog/artwork reads plus login/refresh/logout cleanup. Native playback start/progress/audio-switch/stop probes remain skipped unless `--allow-mutations` is explicitly supplied with fixture credentials. MediaBrowser mutating probes are also opt-in: run them only with a dedicated fixture account because progress and stop events overwrite that account's resume state and the harness does not restore its prior position. Reports never contain password, PIN, or token values.

## Bloom's current MediaBrowser contract

`JellyfinRequestFactory` owns MediaBrowser URL/header construction and `JellyfinAuthenticator` owns the login/validation wire contract. `JellyfinCatalogProvider`, `JellyfinArtworkProvider`, and `JellyfinPlaybackProvider` own Jellyfin request/response conversion at the provider boundary; `HttpTransport` owns shared execution policy. `LibraryService`, `PlaybackService`, and `SessionService` expose stable provider-neutral façades, while Silo counterparts implement their native routes without changing those QML contracts.

Shared assumptions:

- Base URLs have trailing slashes removed before endpoint concatenation.
- `JellyfinRequestFactory` sends `Content-Type: application/json` and a `MediaBrowser` authorization header with Bloom client, desktop device, stable device ID, version, and optional token.
- `JellyfinAuthenticator` expects `AccessToken`, `User.Id`, and `User.Name`.
- Lists generally use `{ "Items": [...], "TotalRecordCount": n }`.
- Jellyfin time is in 100-nanosecond ticks. Ticks must not escape the Jellyfin adapter.
- Current image, stream, subtitle, and trickplay URLs may contain `api_key`; provider artwork/playback boundaries replace persistent token-bearing URLs with opaque request descriptors.
- Bloom derives the home continue-watching rail from Next Up items whose `UserData.PlaybackPositionTicks` is positive.
- Application logout is currently local. `SessionService` uses `/Sessions/{id}/Logout` only for remote-session revocation; native Silo uses its caller-logout/auth-session routes.


## Compatibility results

This is the human-readable summary. Exact call sites, required semantics, and evidence are in the JSON matrix.

| Journey or route | Jellyfin 10.11.10 | Silo `8044eb8` compatibility | Effect in Bloom |
|---|---|---|---|
| Authenticate and validate `/Users/*` | Supported | Partial | Basic/single-profile login works; multi-profile/PIN requires suffix syntax. |
| Views, items, item details | Supported | Partial | Core browsing works; some optional fields/advanced query parameters are absent or ignored. |
| Latest, Next Up, search | Supported | Supported | Home and basic search should populate. |
| Paging and basic sort/type filters | Supported | Supported | Core library navigation works. |
| Advanced item filters | Supported | Partial | `Items/Filters` and Studios are stubs; genre identity differs; UI choices can silently do nothing. |
| Standard item artwork | Supported | Supported | Posters/backdrops/thumbs/logos work. |
| Chapter image route | Supported | Missing | Chapter thumbnails fail even though chapter metadata may exist. |
| `POST /Items/{id}/PlaybackInfo` | Supported | Partial | Direct/HLS playback works through a permissive fallback profile; negotiation is not Bloom-specific. |
| Range-capable direct stream | Supported | Supported | mpv direct playback and seeking are the strongest compatibility path. |
| `/Videos/{id}/AdditionalParts` | Supported | Missing | Bloom receives a nonfatal `404`; multipart playback is unavailable. |
| Trickplay metadata and tiles | Supported | Missing | Seek previews are unavailable. |
| `/Sessions/Playing*` reporting | Supported | Partial | Resume/progress and teardown work; some Bloom payload fields are ignored. |
| Played/favorite mutations | Supported | Supported | User-state controls work. |
| `/Items/{id}/Ancestors` | Supported | Missing | Episodic library/profile context resolution degrades. |
| Plugin `/Episode/{id}/IntroSkipperSegments` | Optional/partial | Missing | Bloom misses Silo's native markers and falls back to external providers. |
| Standard `/MediaSegments/{id}` | Supported | Supported | Silo exposes markers, but Bloom does not yet call this route. |
| `GET /Sessions` | Supported | Stubbed | Active Sessions is empty. |
| `/Sessions/{id}/Logout` | Supported | Missing | Remote revocation fails; Silo only has caller logout at `/Sessions/Logout`. |
| Provider IDs | Supported | Missing | `ProviderIds` is omitted from serialized details, weakening Seerr and external metadata/segment matching. |
| Theme songs | Supported | Stubbed | Series theme songs are unavailable. |
| External subtitle `DeliveryUrl` | Partial in Bloom | Partial in Bloom | Servers can advertise it; Bloom currently drops the URL before playback. |
| Conditional JSON `ETag`/`304` | Supported | Missing | Functional refresh, but no efficient not-modified path. |
| Expired/revoked token returns `401` | Supported | Supported | Bloom centralizes catalog, playback, and remote-session expiry policy through the shared transport/session façade. |
| Similar recommendations | Supported | Missing | Jellyfin's similar rail is consumed by Bloom; compatibility has no equivalent route and native Silo is a separate surface. |
| Server home sections | Supported | Partial | Core library/latest/Next Up sections degrade safely; optional hero/backdrop/random/screensaver sections are unavailable on compatibility. |
| Alternate versions | Supported | Missing | Jellyfin media sources are selectable; compatibility has no equivalent alternate-version contract. |
| Multipart reporting | Supported | Missing | Jellyfin reports physical parts while preserving logical aggregate progress; compatibility cannot discover parts. |
| Auth sessions versus playback sessions | Supported | Missing | Bloom keeps remote auth-session list/revoke separate from playback reports; compatibility list/revoke is stubbed or absent. |
| Client-local profile track preferences | Supported | Partial | Explicit movie/season choices are local and provider-neutral, but compatibility lacks hierarchy and the file is connection-scoped rather than profile-scoped. |
| Household overview, watchlist, watch together | Intentionally out-of-scope | Intentionally out-of-scope | These optional follow-ups are not claimed by the pinned Bloom contract; favorites and playback reports are not substitutes. |
| Requests, notifications, downloads | Intentionally out-of-scope | Intentionally out-of-scope | Seerr requests are an external integration; no provider-native request, notification, or offline-download surface is implemented. |
| Audiobooks and ebooks | Intentionally out-of-scope | Intentionally out-of-scope | Item type mapping is not audiobook/ebook UX or reader support. |

The table uses human words for readability: **Missing** is the matrix's canonical `missing` (**unavailable**) outcome, while **Intentionally out-of-scope** is canonical `not-applicable`. Neither category is supported. A `stubbed` route (such as compatibility `GET /Sessions`) is recorded separately because a success-shaped placeholder is not an unavailable route.

### Meaningful assertions for live runs

A route is not supported merely because it returns `200`. Live evidence should include:

- login yields a non-empty token and stable user/profile identity; invalid login returns `401`;
- pages do not overlap, sort is observable, and filters change results as requested;
- `PlaybackInfo` contains a non-empty play session and a usable media source URL;
- a stream range request returns `206` with the requested `Content-Range`;
- progress, watched, and favorite mutations are observable on a subsequent item read and are restored after the test;
- session stop tears down playback resources;
- a stub is recorded as `stubbed` even when its JSON shape deserializes successfully;
- `ETag` support requires a validator round trip that returns `304` while unchanged;
- external subtitle support requires the advertised URL to be fetchable and passed to mpv.

## Native Silo #77-#80 baseline

The native baseline is source-grounded in Silo revision `8044eb84dd0cfa512ce8f2448cfd51cb7899a4c6`. Each JSON requirement records its issue, route, outcome, capability state, request/response semantics, immutable upstream source links, and whether it is safe for the read-only live runner.

| Native requirement | Pinned outcome | Capability rule |
|---|---|---|
| Health detection | Partial | `status: "ok"` is required. `server_id` is optional and Silo's configured default is deterministic across installations, so it cannot be the sole globally unique connection identity. |
| Providers, login, error envelopes, `/auth/me` | Supported | Provider discovery precedes login; errors carry `error` and `message`. |
| Refresh | Partial | A fresh access/refresh pair is returned and the session window slides, but the prior refresh JWT is not revoked at this revision. Bloom must store the returned pair without treating old-token reuse as proof of failed rotation. |
| Caller logout and auth-session list/revoke | Supported | Logout revokes the caller; session deletion is account-scoped. These are auth sessions, not playback sessions. |
| Profile list and PIN verification | Supported | `profiles[].has_pin` gates verification. Wrong PIN is `200 {"valid": false}`; success returns a session/profile/policy-bound `profile_token`. |
| Libraries, canonical details, versions, hierarchy | Supported | `content_id` is catalog/state identity; `file_id` selects playback versions. |
| GET catalog snapshot and totals | Supported | Replay the returned RFC3339Nano `snapshot`; honor `total_exact` and `has_more`. Grouped work views deliberately report inexact totals. |
| POST catalog query/capability truthfulness | Partial | Invalid rules fail instead of disappearing, but this response has no snapshot and reports `total_exact: false` even when its window count is exact. Use GET `/catalog` for snapshot-aware paging. |
| Watched and favorite routes | Supported | Mutations are profile-scoped and observable through subsequent user-state/detail reads. |
| Artwork refetch | Supported when artwork exists | URLs are opaque, may expire, and are not cache identity. Refetch the owning catalog/detail/person/chapter resource after expiry; never reconstruct or strip signed query parameters. |
| Playback capability | Partial | Protocol v3 truthfully reports disabled state and runtime-probed transformations, but advertises `media3_only`; that is not an mpv capability and does not describe the legacy envelope. |
| Legacy start/progress/stop | Supported | Omit `protocol_version: 3`; preserve session/file identity, seconds, opaque URLs, pause state, and teardown. |
| Legacy transcode and audio switch | Partial/conditional | Start can return HLS/transcode and audio switch returns replacement URLs when policy/runtime permits; Bloom does not claim a standalone transcode route beyond the implemented native start/audio-switch paths. |
| Markers and chapters | Supported when media/routes expose them | Native markers cover intro, credits, recap, and preview with provenance; native chapters are file-version scoped, use seconds, and carry optional opaque thumbnail URLs. |
| Native trickplay | Missing | No native metadata/tile route or capability is registered. Hide/degrade seek previews. |
| Native media theme songs | Missing | `/api/v1/theme/*` is UI branding, not playable item theme media. Hide/degrade theme-song playback. |

### Detection, authentication, and profiles

`GET /api/v1/health` is public and must return JSON with `status: "ok"`. `server_name` and `server_id` use `omitempty`; minimal fixtures can omit them. The default `server_id` is derived deterministically from `https://silo.local/jellycompat`, so even a present value does not establish installation uniqueness. Do not identify Silo from compatibility `/System/Info`.

Authentication routes are:

```text
GET    /api/v1/auth/providers
POST   /api/v1/auth/login
POST   /api/v1/auth/refresh
GET    /api/v1/auth/me
POST   /api/v1/auth/logout
GET    /api/v1/auth/sessions
DELETE /api/v1/auth/sessions/{id}
GET    /api/v1/profiles
POST   /api/v1/profiles/{id}/verify-pin
```

Use bearer access tokens and store access, refresh, and profile tokens only in the platform secret store. Bloom's native client contract requires all five Silo client/device headers on its requests: `X-Silo-Client`, `X-Silo-Client-Version`, `X-Silo-Device-Id`, `X-Silo-Device-Name`, and `X-Silo-Device-Platform`. At the pinned server revision, the login handler consumes `User-Agent` as the auth-session device name, but the source does not establish that every `X-Silo-*` header is server-mandated or rejected when absent; treat the five-header set as Bloom's required request contract, not as a claim of universal upstream enforcement. Bloom sends `X-Profile-Id` after selection and `X-Profile-Token` after successful PIN verification.

### Catalog, state, and artwork

The canonical read routes are:

```text
GET  /api/v1/user/libraries
GET  /api/v1/catalog
POST /api/v1/catalog/query
GET  /api/v1/catalog/items/{id}
GET  /api/v1/catalog/items/{id}/versions
GET  /api/v1/catalog/items/{id}/episodes
GET  /api/v1/catalog/series/{id}/seasons
GET  /api/v1/catalog/series/{id}/seasons/{number}
GET  /api/v1/catalog/series/{id}/seasons/{number}/episodes
```

Use `content_id` for catalog, detail, hierarchy, watched, and favorite identity. Preserve `file_id` for playback/version and chapter selection. Detail/version responses carry provider IDs, technical tracks, alternate editions, multipart `playback_variants`, marker ranges, and per-version chapters when data exists. Silo durations, progress, markers, and chapters use seconds; Jellyfin ticks and chapter-image routes stay inside its adapter. Convert once at Bloom's provider boundary.

GET `/catalog` supports snapshot-aware offset pagination and reports `total_exact`. POST `/catalog/query` is useful for structured filters but is not an equivalent paging capability at the pin: it omits snapshots and conservatively leaves `total_exact` false. The adapter must choose based on those explicit response capabilities rather than silently pretending a filter or count mode is stronger than it is.

Profile state uses `POST`/`DELETE /api/v1/watched/{content_id}` and `GET`/`PUT`/`DELETE /api/v1/favorites/{content_id}`. Artwork fields are fetch locations, not durable identity. On an expired or missing signed URL, refetch the owning resource and replace the whole URL.

### Native playback, markers, and chapters

Native playback uses the pinned legacy envelope at Silo revision `8044eb84dd0cfa512ce8f2448cfd51cb7899a4c6`. The Bloom native provider intentionally omits `protocol_version: 3`: the pinned capability route advertises `media3_only`, which is not an mpv contract. Protocol v3 remains unavailable for mpv until Silo documents an mpv-capable engine contract.

The implemented legacy lifecycle is:

```text
POST   /api/v1/playback/start
POST   /api/v1/playback/{session_id}/progress
DELETE /api/v1/playback/{session_id}
PATCH  /api/v1/playback/{session_id}/audio
```

Start sends numeric `file_id`, selected `profile_id`, `start_position` in seconds, optional `audio_track_index`, and mpv codec/container/resolution/HDR/passthrough/bitmap-subtitle capabilities. It does not send an invented native variant field. The `201` response contains `session_id`, `media_file_id`, effective `play_method`, `position`, `stream_url`, subtitle URLs, selected audio index, duration, and `playback_info`, which the provider maps to `PlaybackDescriptor`. Direct play, remux, and HLS/transcode are accepted only when Silo policy/runtime returns them; range support is retained on direct URLs.

Progress sends `{ "seconds": <seconds>, "is_paused": <bool> }`; stop persists final progress and tears down the session. Audio switching sends the selected audio index and maps the replacement signed reload URL and effective method without changing the provider-neutral descriptor contract. All native playback requests use `AuthenticationService`/`HttpTransport` refresh-once semantics and current profile headers.

Native URLs are opaque signed fetch locations. Relative URLs resolve against the configured server origin with `QUrl`; absolute URLs and existing query bytes are preserved exactly, and Bloom never appends auth query parameters. Native `PlaybackInfo` comes from catalog item/version data rather than Jellyfin `/Items/{id}/PlaybackInfo` or `/Videos/*`.

Native multipart playback starts sequential native sessions by physical `file_id`, one session per file/part, while preserving logical-item aggregate progress and autoplay context. Version selection is catalog-driven and keeps `content_id` (logical identity) separate from `file_id` (playback identity).

Markers and version chapters remain supported when the native resources expose them. Native trickplay metadata/tiles and playable media theme songs are unavailable at the pin. HLS/transcode, audio switching, and subtitle delivery are conditional on server policy/runtime; capability v3 does not make those features available. Silo HEAD comparisons remain research-only and do not change the pinned revision or release evidence.

### Current upstream HEAD research

Research on 2026-08-07 inspected Silo HEAD `39efe308afe903c0fab4cc06fa1795449e6fc2e1`. Its [playback handler](https://github.com/Silo-Server/silo-server/blob/39efe308afe903c0fab4cc06fa1795449e6fc2e1/internal/api/handlers/playback.go) still dispatches requests without protocol v3 to the legacy decoder, and the pinned start/progress/stop/transcode/audio request fields remain compatible. The audio-switch response adds timeline fields. This is source research only: it is not a container pin, release claim, or live release validation, and it does not move the baseline away from `8044eb84dd0cfa512ce8f2448cfd51cb7899a4c6`.

## Open upstream contract questions

Upstream coordination started in [Silo Server #431](https://github.com/Silo-Server/silo-server/issues/431) (third-party native HTPC/mpv contract) and [#432](https://github.com/Silo-Server/silo-server/issues/432) (compatibility profile discovery).

Before native implementation is called stable, coordinate with Silo maintainers on:

1. A public server-info response carrying product, build revision, and native API revision.
2. Public stability/deprecation guarantees for `/api/v1`.
3. The supported capability-discovery set for optional routes.
4. The guaranteed lifetime of the legacy native playback envelope.
5. An mpv-capable protocol-v3 contract rather than `media3_only` semantics.
6. Required versus advisory Silo client/device headers.
7. Long-term compatibility profile discovery instead of typed `#profile`/`#pin` suffixes.
8. Trademark boundaries for referential text and any bundled artwork. Bloom should not bundle Silo logos without explicit permission.

## Validation

The offline validator checks matrix completeness, immutable image pins, Jellyfin/compatibility deployment coverage, native #77-#80 coverage, pinned source evidence, health identity caveats, capability states, safe live-probe declarations, and semantic assertions:

```fish
python3 tests/contracts/validate_contracts.py
python3 -m unittest discover -s tests/contracts -p provider_contracts_test.py
```

It is also registered as `ProviderContractValidationTest` in CTest and runs in the Nix test derivation.

Manual release evidence is tracked in the [manual validation ledger](manual-validation.md); unavailable live/platform rows must remain explicitly unavailable rather than inferred from source or HTTP status.
