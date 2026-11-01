# Provider compatibility and contract baseline

Bloom's native Silo provider uses stable API v2 and playback protocol 3. Silo 1.0 returns `410 Gone` for the frozen alpha API. Bloom has no native v1 fallback. The separate Jellyfin compatibility listener retains its existing contract.

Native Silo support is first-class: it passed the live release gates without Silo's Jellyfin compatibility listener. [Manual validation](manual-validation.md) records the evidence.

## Pins and artifacts

| Component | Pin |
| --- | --- |
| Native Silo source | `a103c2c885f20ea1b11160c233d04994a0884014` |
| Silo API | `2`, 732 paths |
| Silo OpenAPI SHA-256 | `eb3cb4a524d15602a87abd1e37c877ded646c098fba8a6cdbc35a1e659bbcccf` |
| Silo migration SHA-256 | `626e794c48090e37b7b6b9c086a13af6f2a31068c3cd5c6280a48d4078bd065d` |
| Historical Silo compatibility control | `8044eb84dd0cfa512ce8f2448cfd51cb7899a4c6`, image `ghcr.io/silo-server/silo-server@sha256:944ee9821de1d6a61876c9b7b06daa358118163d1e5f9b3aa9f5437856fd06e9` |
| Jellyfin control | `10.11.10`, image `docker.io/jellyfin/jellyfin@sha256:f66273e014b307e4ac46778845ebc1e9ee24b2e57c1fc17d5ec5ac3015649bfa` |
| Jellyfin API | `12.0.0`, SDK revision `592747ce7add446b9a14ad56aba8a7441a2e2618`, SHA-256 `f073a3e661f1c7249c8dc7df257b31f290c8931950f8563e9df537a46717f700` |

The native source and exact contract bytes are pinned. No new native container digest is asserted. The 2026-10-18 live run was against the earlier pin `35ef973b`. Between that pin and `a103c2c8` (through `62b1a6e8`) Silo removed no operation Bloom calls and made no request field newly required. On those operations it added only optional query parameters (`item_limit` on the Home and library section reads), optional response fields, and documented error statuses. The live runner has not yet run against `62b1a6e8` or `a103c2c8`. The historical compatibility image remains a separate baseline; it is not a v2 image pin.

[`provider-contracts.json`](../tests/contracts/provider-contracts.json) records native operation references, behavior, capability conditions, and exclusions. [`silo-v2-openapi-manifest.json`](../tests/contracts/silo-v2-openapi-manifest.json) contains the operations and transitive schemas Bloom consumes. The generator checks both artifact hashes before writing the manifest. The offline validator rejects pin drift, absent operations, unsupported claims, and frozen native API calls.

To reproduce the manifest, use a zsh-compatible shell:

```sh
mkdir -p /tmp/opencode
revision=a103c2c885f20ea1b11160c233d04994a0884014
curl --fail --location "https://raw.githubusercontent.com/Silo-Server/silo-server/$revision/contracts/api/v2/openapi.json" --output /tmp/opencode/openapi.json
curl --fail --location "https://raw.githubusercontent.com/Silo-Server/silo-server/$revision/contracts/api/v2/migration.json" --output /tmp/opencode/migration.json
jq '.paths["/api/v2/playback/start"].post.requestBody' /tmp/opencode/openapi.json
python3 tests/contracts/generate_silo_openapi_manifest.py /tmp/opencode/openapi.json /tmp/opencode/migration.json
```

The full specification is about 6 MB. Query individual endpoints and schemas with `jq`.

## Native discovery and authentication

Discovery probes `/health` and `/ready`, then reads `/api/v2/system/info`. Only `api_major: 2` and the system document identify the native protocol. A reverse proxy can serve HTML at the neutral probe paths, so those probes establish reachability only. Compatibility `/System/Info` is not native detection. The system document has no installation identity, and Bloom no longer has a special deterministic-server-ID exception.

`server_version` and `contract_digest` are diagnostic fields. They never enable a feature. Playback, themes, and previews require explicit capability documents and resource availability.

Login and refresh use `/api/v2/auth/login` and `/api/v2/auth/refresh`. Session validation and the playback recovery ping use `/api/v2/account/me`, which answers `401` to a refused access token; the public `/health` probe cannot tell an expired session from a live one. Explicit sign-out, and removing the live connection or a pooled member of the active space, send `POST /api/v2/auth/logout` with the session's bearer token before Bloom deletes it; a connection whose tokens are only in the keyring is removed without a server logout (see [profiles](profiles.md)). The operation declares no idempotency mechanism, so Bloom never replays it; if an idle-expired access token is refused with `401`, Bloom refreshes once with the captured refresh token and sends one logout with the new access token, and a failure does not stop the local sign-out. Profile and authentication-session lists use `items`. IDs remain opaque strings. Profile-PIN verification requires `valid: true` and a non-empty `profile_token`; `valid: false` is an incorrect PIN.

### Sign in with a code

Device sign-in uses `getDeviceLoginCapability`, `startDeviceLogin`, `pollDeviceLogin`, and `cancelDeviceLogin` (`native.auth.device`). When an address is entered on the login screen, Bloom reads system info, the device capability, and `listAuthProviders`. These are public `GET`s with 10-second attempt deadlines; the capability and provider reads may retry once. Bloom offers "Sign in with a code" only on native Silo with capability `state: available` and protocol version 2. `password_login: false` hides the password form, but only when codes are offered. Jellyfin and other servers keep the password form alone.

Start sends `device_name` and `device_platform` with no `client_purpose` or `temporary`, so the request is an ordinary `device_login`. The screen shows `user_code` as `4821 7730`, the `verification_uri_complete` link (falling back to `verification_uri`), and a countdown from the relative `expires_in`. Bloom polls after `max(interval, poll_after)` seconds, clamped to 1–60. A pending answer's `expires_at` moves the local deadline, corrected for the clock offset measured at start and capped at 31 minutes after start. `opened: true` changes the prompt to "Finish on your phone".

Start and poll are `non_retryable`. The transport never replays them, because an approved poll carries the token pair exactly once. The poll loop asks again on its own schedule.

| Answer | Bloom |
| --- | --- |
| `approved` with tokens | Signs in as after a password login. |
| `denied` | Shows "Sign-in was declined". |
| `expired`, `canceled`, 404, or 401 | Shows "This code expired". |
| `consumed` without tokens | Fails; the user gets a new code. |
| 429 | Adds five seconds to the interval for the rest of the code (RFC 8628 `slow_down`). |
| No response, 408, or 5xx | Retries after a growing delay; fails after three in a row. |

A local deadline with no decision also ends as expired. Every ended state offers "Get a new code". Backing out, leaving the signed-out state, or starting another sign-in withdraws a live code with `cancelDeviceLogin` when the capability reports `cancel`. Cancel is naturally idempotent, so it is sent with one transient retry and its answer is ignored. Approved tokens go through the password-login path: provider profiles and PIN, `CredentialStore`, the refresh token, and the access-token lifetime from `expires_in`. Bloom never starts `remote_playback` requests and never approves devices.

### Jellyfin Quick Connect

Jellyfin's code sign-in is Quick Connect: `GetQuickConnectEnabled`, `InitiateQuickConnect`, `GetQuickConnectState`, and `AuthenticateWithQuickConnect`, all non-obsolete in the pinned OpenAPI contract. When the address on the login screen is not native Silo, or Jellyfin is the selected provider, Bloom reads `GET /QuickConnect/Enabled` with the same 10-second deadline and one retry as the other discovery reads. Only a bare JSON `true` offers "Sign in with Quick Connect". `false`, a failed read, or any other answer leaves the password form alone. The password form always stays, because Jellyfin has no `password_login: false` equivalent. An explicit Silo selection never asks.

Every Quick Connect request carries the MediaBrowser authorization header with the stable Bloom `DeviceId` and no `Token`, the identity a password login uses. The resulting session is therefore the normal Bloom device.

1. `POST /QuickConnect/Initiate` has an empty body and is never replayed. Its `QuickConnectResult` supplies `Secret` (kept in memory only) and `Code`. A 401 means Quick Connect was turned off, which Bloom shows as "not offering sign-in with a code right now".
2. The screen shows the six-digit `Code` with no link, since Jellyfin has none. The person enters it in another Jellyfin app under Settings → Quick Connect. The result has no lifetime, so Bloom keeps a local deadline of ten minutes after Initiate, Jellyfin's own timeout, and counts it down.
3. Bloom polls `GET /QuickConnect/Connect?secret=…` every five seconds. The poll is a safe read with one transient retry, so the transport may replay it. No response, 408, or 5xx three times in a row ends the code; each failure lengthens the next delay as on Silo. `JellyfinRequestFactory` redacts the `secret` query parameter in logs.
4. `Authenticated: true` stops polling. Bloom then exchanges the secret exactly once with `POST /Users/AuthenticateWithQuickConnect` and body `{"Secret": "…"}`. The exchange is never retried, because it mints the session. Its `AuthenticationResult` has the `AuthenticateByName` shape and continues on the password-login path: `CredentialStore`, user validation on restore, and the Jellyfin session record.

| Answer | Bloom |
| --- | --- |
| `Authenticated: false` | Keeps polling. |
| `Authenticated: true` | Exchanges the secret once and signs in. |
| Poll 404 or 401, or a result without `Secret` | Shows "This code expired". |
| Local ten-minute deadline | Shows "This code expired". |
| Exchange fails, or answers without `AccessToken` and `User.Id` | Fails; the user gets a new code. |

Jellyfin has no cancel operation. Backing out, leaving the signed-out state, or starting another sign-in only stops polling, and the server forgets the request on its own timeout. Every ended state offers "Get a new code".

`SiloRequestFactory` sends bearer authorization and selected `X-Profile-Id` and `X-Profile-Token` headers on same-origin requests. Login, refresh, discovery, device start/poll/cancel/capability, and PIN verification exclude stale authentication/profile headers as required. Profile-scoped catalog, capability, watch, marker, and playback requests require the selected profile. Credentials remain in `CredentialStore`; connection metadata contains no token.

Errors use RFC 9457 `application/problem+json` with `type`, `title`, `status`, `detail`, and `instance`. The problem-type suffix supplies the stable error code. Bloom ignores unknown response fields and treats unknown capability, decision, and delivery values as unavailable. HTTP 406 and 415 are terminal format errors. Bounded retries cover 408 and 429 only when the operation is explicitly safe to replay.

## Catalog and conditional requests

Catalog reads use `cursor`, `page.next_cursor`, `page.has_more`, and `window_cursor`. Bloom keeps bounded cursors per connection and query. Explicit window jumps use `seek` with the window cursor. There are no offset or snapshot parameters. GET pages allow up to 200; structured POST queries allow up to 100. Bloom currently requests at most 100 for both paths.

Library IDs are strings in query bodies and URLs. Hierarchy requests use explicit series/season context rather than inferring a resource kind from whether an ID is numeric. Numeric season numbers remain numbers. Series and season children are requested through `LibraryService::getSeasons` and `getSeasonEpisodes`, which add the `Season` or `Episode` item type. A parent ID without a type is a library and becomes a `library_id` catalog query. GET sort direction uses a minus prefix; grouped GET filters use one JSON `groups` parameter.

Library filters and sorts use the base rule vocabulary, which `getCatalogSearchCapabilities` does not gate (`extended_query_rules` adds fields Bloom does not send). The "Added since" filter becomes `added_at` `between` the local start of the minimum date (as a UTC instant) and `9999-12-31T23:59:59Z`. The range is inclusive, so it covers the whole first day like Jellyfin's `MinDateLastSaved`, and it is the same on every page: Silo binds a paging cursor to the query's rules, so a span recomputed per request (`in_last`) would invalidate the cursor once the clock passed the hour. A library query restricted to movies or series is accepted with the view's `recursive` flag, since Silo's library catalog is already that flat list of titles; other recursive scopes stay unsupported. Silo has no tag or keyword rule field, and the query-source sort allowlist has no random order, so `sort=random` is a `422` even though the parameter description lists it. `SiloCatalogProvider::libraryQueryCapabilities` therefore leaves out the `tags` filter and the `random` sort. The library screen hides both, and a saved choice falls back to no tag filter and library order ([view models](viewmodels.md)).

Home section IDs come from the profile's layout, so Bloom never requests a fixed section ID. Next Up reads `GET /api/v2/home/sections` and keeps movie and episode cards from the `continue_watching` and `next_up` section types, in layout order and without duplicates. Silo folds Next Up cards into Continue Watching unless the profile shows Next Up separately. The `system-next-up` section exists only in that separate mode. Continue Listening and Continue Reading share the `continue_watching` type, and their cards are dropped. Home episode cards (`CatalogItem`) carry no thumb and no season ID. Their `poster_url` is the portrait season poster. In Continue Watching and Next Up rows their `backdrop_url` is the season backdrop, else the series backdrop, else the episode still. Bloom's landscape Home cards therefore fall back to the backdrop before the poster, as Jellyfin's fall back to the parent backdrop. Opening such an episode selects its season by `season_number` once the series' seasons load.

Home also shows the rows the profile curates in Silo (recommendations, Because You Watched, collections, seasonal picks, genre rows, its watchlist and so on). `LibraryService::getHomeSections` reads the same `GET /api/v2/home/sections`, whose rows come with their cards after the server's cross-row de-duplication and hide-watched rules, and `HomeSectionsViewModel` adds them below Bloom's own shelves in the server's layout order:

- Skipped section types: `continue_watching` and `next_up` (Continue Watching and Up Next), and `recently_added` and `new_to_library` (Bloom's per-library Recently Added; `new_to_library` is the same newest-first list).
- Every other type is kept, including types Silo adds later. Featured rows (Silo's page hero) are ordinary rows, since Bloom keeps its own hero.
- Only movie, series and episode cards are kept, once each per row. Rows of music, audiobooks, ebooks, manga or podcasts, and rows left empty, are dropped.
- Without a `watchlist` row in the layout, Home adds a "Watchlist" row first, from the first 20 entries of `GET /api/v2/watchlist` (newest first). An empty watchlist adds no row; a failed read leaves the other rows.
- Rows show their first page only; there is no "See all", as on Bloom's other Home rows. When `GET /api/v2/home/capabilities` reports `section_item_limit_max`, the read sends `item_limit=20` (or the server's maximum, if smaller), the same size as the Watchlist row. Silo's own rows default to 100 cards. Servers without that field reject `item_limit`, so the read goes uncapped. The capabilities are read once per connection; a failed read loads the rows uncapped and asks again on the next load. Next Up reads the layout separately and never sends `item_limit`, so no resume card is cut.

Rows load in the background with the other Home rows and refresh with them: after playback, on returning to Home after 30 seconds, on a library-visibility change, and after a watchlist change. A new Home (sign-in, profile or space switch) starts without rows. Merged spaces keep Bloom's own Home rows only, because a layout belongs to one Silo profile. Jellyfin has no curated layout (`ProviderCapability::HomeSections` is Silo-only), so its Home is unchanged.

The watchlist is a per-profile list separate from favorites. Movie and series pages show "Add to Watchlist" / "On Watchlist" when the provider reports `ProviderCapability::Watchlist` (native Silo only; Jellyfin has none). The state comes from the item's `user_state.in_watchlist` (canonical `inWatchlist`). `PUT` and `DELETE /api/v2/watchlist/{item_id}` change it with an empty body. Both are `natural_idempotent` in the contract, so the transport may replay them after a lost response. The button changes at once and reverts with a message when the server refuses.

Series and season collections omit `type`. The mapper treats an untyped card with `episode_number` as an episode, and one with `season_number` and `episode_count` as a season numbered by `season_number`.

Artwork URLs are signed links to one stored size variant per image. Each read picks the narrowest variant at least as wide as the request's `poster_width`, `backdrop_width`, `logo_width`, `still_width`, or `photo_width`, else the original. Without them Silo uses per-surface defaults: 300 px posters and backdrops on catalog pages and structured queries, 300 px stills on episode rows (season episode lists), and 500 px posters with 1920 px backdrops on Home and details. Those defaults upscale on a large screen. `SiloArtworkSizes` sends `poster_width=1000`, `backdrop_width=3840`, `logo_width=1280`, and `still_width=1280` (sizes for a 4K screen, matching Bloom's 2x `maxWidth` requests to Jellyfin) on every catalog, Home, library section, similar, and watchlist read, and on the artwork refresh read. Episode stills (the series page and Up Next episode cards) get the 1280 px rung; photos keep their 500 px default. Silo answers `422` to parameters a route does not declare, so library discovery, filters, theme reads, settings, and writes never carry them. The mapper tags each artwork reference with a hash of the URL path without its signed query. The image cache key therefore changes when the variant or the artwork changes, but not when the signature rotates.

Episode cards can also carry their parents' artwork: `series_poster_url`, `season_poster_url`, and `series_backdrop_url`, each with a `*_thumbhash` (`GET /api/v2/images/capabilities` reports `episode_parent_artwork`). They appear on Home and library sections, catalog browse and search, Watch Tonight, and episode detail. Each is optional; older servers send none, and the mapper then maps only the card's own artwork. The mapper fills the keys the Jellyfin mapper fills:

- `series_poster_url` becomes `seriesPrimaryArtwork`, owned by the series (`series_id`). Poster cards for episodes (Recently Added and the curated rows) therefore show the series poster, as on Jellyfin.
- `season_poster_url` becomes `parentPrimaryArtwork`. Cards carry no season ID, so the episode owns it. Its refresh re-reads the episode and returns `season_poster_url` when that field's path still matches the reference's tag, else the episode's own poster.
- `series_backdrop_url` becomes `backdropArtwork`, owned by the series, when `backdrop_url` is missing or `backdrop_is_episode_still` is true. This matches Jellyfin, whose episodes take their parent's backdrop. Otherwise the card's own `backdrop_url` stays first (the season or series backdrop in Continue Watching and Next Up), and the series backdrop follows it in the `artwork` list.

None of these fields has an `*_expires_at`. When the series backdrop replaces the still, the card's `backdrop_url_expires_at` is dropped from `artworkExpiresAt`.

Catalog cards (`CatalogItem`) carry `overlay_summary` but no unwatched episode count: their `user_state` holds only `played`, `is_favorite`, `in_watchlist`, and `not_interested`. Seasons and item details carry the full watch rollup, so Silo series posters show a watched check but no count. Poster badge preferences come from the profile-scoped effective `ui.card_overlays` and `ui.card_overlays_enabled` settings and `GET /api/v2/settings/overlay-config`; see [poster badges](poster-badges.md).

Silo has no series-wide episode route, so `LibraryService::getNextUnplayedEpisode` lists the seasons, reads each season's episodes, and runs `NextEpisodeResolver` over the whole series. The series page and autoplay therefore share the Jellyfin rules, including the excluded current episode. The catalog query source has no random order. For the Home backdrop, hero, screensaver and Search suggestions, `LibraryService::sendRandomSampleRequest` reads the count of matching movies and series, fetches a 100-item window at a random `seek` offset, and shuffles it locally. The full backdrop index pages through the same catalog query. A provider that implements these operations natively, such as Jellyfin, keeps its own request.

Conditional playback and theme capability reads return ETags. Bloom sends `If-None-Match` and reuses the cached representation on `304`. The watched, favorite, watchlist, authentication-session revoke, playback, and theme-grant operations Bloom uses do not declare guarded writes. Bloom does not invent `If-Match` for them. A future guarded write must preserve the resource ETag and reconcile `412` after a fresh read.

## Media requests and Discover

Native Silo exposes `ProviderCapability::MediaRequests`. Every request route is profile scoped. Bloom reads `GET /api/v2/requests/status` (`FeatureStatus`) for the selected profile and offers requests only when `requests_enabled`, `allowed`, and `state: available` hold. A configured Seerr API key takes priority, and Bloom then shows none of Silo's request surface. See [media requests](seerr.md).

- Search uses `GET /api/v2/requests/search` with a percent-encoded `q`: `QUrlQuery` would leave `+`, which the server reads as a space. Titles whose `availability` is `available` are left out, because the library shelves show them.
- `GET /api/v2/requests/detail/{media_type}/{tmdb_id}` supplies the dialog's seasons (`availability`, `requested`), `request.requestable` with its `reason`, and the `recommendations` Bloom shows as similar titles. Bloom maps its `tv` to Silo's `series`.
- `POST /api/v2/requests` is declared `non_retryable`, so Bloom never replays it. The body carries `media_type`, `tmdb_id`, and the required `title` from the prepared detail, plus `year`, `overview`, and artwork paths. `seasons` is sent only when the user picks seasons. Without it Silo requests every aired season the library lacks.
- `GET /api/v2/requests/discover` returns the rows with their first page. `GET /api/v2/requests/discover/{section}` pages one row by its enum key; Bloom sends only the six documented keys. When rating backfill consumed more than one provider page, `next_page` gives the next page.
- Poster and backdrop paths are TMDB image paths. Bloom resolves them against the TMDB image CDN, and request routes never carry the catalog artwork width parameters.
- A title in the library carries `library_content_id`. Discover opens its details page instead of the request dialog.
- Browsing lists `GET /api/v2/requests/discover/{genres|studios|networks}` and pages one by slug through `GET /api/v2/requests/discover/browse/{kind}/{slug}` with `sort` and `page`. Only genres take `media_type`: studios are movies and networks are series. A genre browses series when its brand is `series_supported`.
- The request list reads the first page of `GET /api/v2/requests/mine` (`limit=50`). Bloom offers withdrawing an `active` request in state `pending` or `approved`, through `POST /api/v2/requests/{id}/cancel` with an empty object. `cancelRequest` is `non_retryable`.
- When `follow_supported`, a title with someone else's active request can be followed with `PUT` and unfollowed with `DELETE /api/v2/requests/follows/{media_type}/{tmdb_id}`. Both are `natural_idempotent`, so Bloom retries them.
- When `watchlist_titles_supported`, a title the library lacks goes on and off the watchlist through `PUT` / `DELETE /api/v2/watchlist/titles/{media_type}/{tmdb_id}`. These writes are `non_retryable` because adding can request the title (`watchlist_requests`).

## Playback, themes, and seek previews

- `/api/v2/playback/capabilities` must allow protocol 3, `playback_plan_v3`, and original HTTP delivery. Its `installation_id` is required on start and reports.
- Start has ten required fields, including string `file_id`, `playback_attempt_id`, client capabilities, and client playback context. One attempt ID remains stable across retries. `PlaybackDecision.playback_plan` supplies the URL, selected tracks, source duration, and timeline.
- The `st` query is a reconstruction reference, not account authentication. For mpv's URL-only path, Bloom preserves the plan URL and appends the documented `token` account fallback only on the configured API origin. Other origins receive no account token. Header-authenticated delivery is not advertised. Tokens remain transient and redacted.
- Deliveries: Bloom offers `original_http` and the `hls` class, and the planner picks the original file whenever it can play, so direct play stays first. Silo falls back to `server_remux_hls` (Bloom `DirectStream`) or `server_transcode_hls` (`Transcode`) for codecs, containers, bitrate caps or subtitles the original cannot carry. The `hls` entry repeats the decode lists, claims HDR10, HDR10+ and HLG output because mpv tone-maps itself (no Dolby Vision), and lists text, ASS and PGS sidecars without the JSON font bundle. `progressive` is never offered, and a `server_remux_progressive` plan is rejected by name: its chunked response has no ranges and closes while paused.
- Timeline: positions in Bloom are source positions. A plan's `timeline_offset_seconds` is added to mpv's clock for the UI, progress, intro markers and chapters, and subtracted from seek targets. Duration comes from `source.duration_seconds`, never from mpv, whose HLS length grows while a remux runs. A transcode with a known runtime has a complete playlist (`can_seek_anywhere: true`, offset `0`) and mpv seeks it directly. A copy remux, or a transcode without a known runtime, starts at the resume keyframe and grows: mpv seeks locally only forward inside its cache, and every other seek is a `seek_reanchor` replan at the source position, which Bloom accepts only from servers that advertise `seek_reanchor_v1`. One replan runs at a time; seeks and track changes made meanwhile merge into the next one.
- Replan uses `POST /playback/{session_id}/replan` with `operation: track_change` (or `seek_reanchor`), the current plan as `failed_plan_id`, and a stable `replan_request_id`. The audio PATCH route is absent from the contract and returned `404` in the live test. Original-file audio and subtitle selection stays local to mpv. An HLS stream carries only the selected audio, so its audio menu switches through a replan; mpv's tracks are numbered from what the stream carries (`deliveredAudioTracks`, `deliveredSubtitleTracks`), not from the source. The new plan reloads in place with `loadfile replace` at its `player_start_seconds`; the replaced file's end is not playback end, and sidecars are added again once the new stream has loaded.
- HLS authentication: the master playlist URL keeps the `token` rule below, and Silo copies its query onto segment URIs. Playlists the server builds itself carry that query once through `EXT-X-DEFINE` once it saves 64 KiB, which FFmpeg's HLS demuxer does not expand, so mpv also sends `Authorization: Bearer` (`http-header-fields`) when the playlist is on the API origin. mpv sends `http-header-fields` with every request, so while a stream carries headers the player adds no sidecar from another origin, and a replan replaces the header list entry by entry (`change-list`). mpv cannot scope the header to an origin, so this relies on Silo's playlists using relative segment URIs. At the pin they do: every API-served media playlist, whether Silo builds it (`syntheticManifestQuery`) or rewrites it (`RewriteManifestPaths`, including transcode-node playlists relayed by the API), writes `segment/<name>` relative to the playlist. A node serving its stream directly has an absolute URL on another origin and gets no bearer. Recheck this when bumping the pin. Without `hls_variable_substitution_v1`, FFmpeg's own growing playlists stay literal. mpv opens growing (EVENT) playlists at their first segment (`live_start_index=0`), not FFmpeg's live edge.
- Subtitles are embedded-first. Start sends `subtitle_track_index` as Silo's combined ordinal (external files, then embedded streams, then downloads), which `SiloPlaybackProvider` derives from the `/watch` track lists; the ffmpeg stream index is never sent. Bloom negotiates `embedded_subtitles_v1` and declares `native_embedded` claims keyed by `ffmpeg_stream_index` for every container and codec mpv renders, so Silo answers `render` with `embedded` and extracts nothing. mpv selects embedded tracks from the original stream. On `original_http`, inventory entries with `source: embedded` are never mounted as sidecars. External inventory entries replace the `/watch` track they belong to, and only downloaded subtitles get a new menu entry. An HLS stream holds none of the file's embedded streams, so Bloom negotiates `hls_subtitle_renditions_v1` and `hls_remux_subtitle_renditions_v1` when the server advertises them: the selected embedded text track then arrives as the playlist's WebVTT rendition, which mpv (FFmpeg 9) exposes as the stream's subtitle track. Choosing another embedded track on an HLS stream is a `track_change` replan, which yields a rendition, the plan's sidecar `artifact`, or burn-in, in Silo's order; Bloom does not mount the whole-file embedded sidecars itself because extraction is slow on network storage. External and downloaded sidecars switch locally, shifted by the timeline offset because their cues are on the source clock. A burned-in subtitle pins the stream, so turning it off or choosing another track replans. Embedded tracks the plan lists nowhere are dropped from the menu. `PlaybackPolicy` lists embedded tracks before external files for both providers, so each language, forced and default rule picks an embedded match first.
- The provider tracks at most 64 sessions and forgets the oldest when full. A start response that arrives after its request was superseded is stopped at once, unless the account or profile changed meanwhile.
- Session lifecycle: a progress or pause report answered `404` means Silo expired the attempt, so the player recovers through its reconnect path with a new session at the current position. `410 playback_session_ended` means an administrator ended it; the player leaves playback with a message and never restarts it. A paused player sends a paused progress sample once a minute, well inside Silo's 30-minute paused grace.
- Silo expires a started session after about 45 seconds without stream reads or progress (`queuedSessionsExpire`). Multipart playback therefore starts only part 1's session up front; part N+1's session starts and is appended to mpv's playlist when part N begins playing.
- The stream URL carries the account token for the whole movie. Before a start request, `AuthenticationService::ensureAccessTokenValidFor` renews a token that expires within four hours, capped at half its issued lifetime, or whose lifetime is unknown after a session restore.
- A refused decision shows Silo's `terminal.message`. A playable plan Bloom cannot execute names the delivery and the reason (an unrequested delivery, a server-anchored stream without `seek_reanchor_v1`, or stream headers that need refreshing) instead of a generic error. A refused replan restores the previous track choice and keeps the current stream.
- Progress uses `installation_id`, a monotonic sequence, `position` in seconds, and `is_paused`. Stop has a stable `stop_id`. These requests have explicit replay safety. Rejected plans receive stop cleanup without an invented final position.
- `/api/v2/watch/{id}` supplies versions, tracks, source duration, chapters, markers, and per-file preview availability. Original HTTP plans need an unshifted seekable timeline; HLS remux and transcode plans may be anchored as described above. `plan_invalidated_v1`, quality rungs and HLS Dolby Vision are not negotiated: a withdrawn copy-remux plan ends the session, and Bloom's session-loss recovery starts a new one.
- Themes use `/catalog/themes/capabilities`, detail `themes`, and an owner/theme playback grant. The short-lived URL is never built from an item ID or persisted. Each loop gets a fresh grant. Silo grant audio is fetched once without ranges into a buffer with a 16 MiB limit and a 45-second deadline. Converted audio is progressive AAC in MP4 and has no range or length contract.
- Previews require `trickplay_v1` and the selected version's `trickplay_available`. `/watch/{id}/trickplay?file_id=...` returns the grid, interval, count, expiring signed sheet URLs, and file identity. Bloom validates these values and keeps a bounded cache scoped to server/account/profile/item. URLs are never reconstructed.

The native client implements theme and trickplay capability flags. A flag does not override server permission or establish live validation. The available server fixture validated theme grants/audio but had no published trickplay version in 100 episode and 100 movie checks. The operator confirmed that no previews exist and accepted unit-test coverage for this migration. Live preview delivery remains unverified.

## Validation and exclusions

Use `./scripts/dev-build.sh --tests` during development, then `nix flake check` and `nix build`. The live runner is separate because it needs a disposable account, server, and media. Its JSON report contains no credentials or grant URLs. Skipped live gates are inconclusive and do not pass.

```zsh
export BLOOM_CONTRACT_USERNAME=tester
read -rs 'BLOOM_CONTRACT_PASSWORD?Fixture password: '
export BLOOM_CONTRACT_PASSWORD
python3 tests/contracts/run_live_contracts.py --deployment silo-a103c2c-native --base-url http://silo --allow-mutations --output /tmp/opencode/silo-v2-report.json
unset BLOOM_CONTRACT_PASSWORD
```

Set `BLOOM_CONTRACT_TRICKPLAY_ITEM_ID` to a title with published previews to complete that gate. Live GPU playback, audible output, physical keyboard/gamepad navigation, Windows composition, and HDR/display transitions remain separate manual checks.

Ebooks, audiobooks, comics, watch-together, notifications, history imports, downloads/offline, admin operations, progressive remux, and Silo quality selection remain out of scope. Silo HLS remux and transcode playback have unit coverage only; they need live validation against a server (seek reanchor, rendition subtitles, audio replans, and long playlists that use `EXT-X-DEFINE`). The existing historical Jellyfin compatibility matrix retains its missing/stubbed/partial labels and is not evidence of native v2 behavior.

### Jellyfin 12 API policy

Bloom's Jellyfin production requests are audited against the pinned official
OpenAPI document above. The checked-in
`tests/contracts/jellyfin-12-openapi-manifest.json` is generated from that
exact artifact and is what offline contract tests consume. To reproduce it:

```bash
curl --fail --location \
  https://raw.githubusercontent.com/jellyfin/jellyfin-sdk-typescript/592747ce7add446b9a14ad56aba8a7441a2e2618/openapi.json \
  --output /tmp/jellyfin-12-openapi.json
printf '%s  %s\n' \
  f073a3e661f1c7249c8dc7df257b31f290c8931950f8563e9df537a46717f700 \
  /tmp/jellyfin-12-openapi.json | sha256sum --check
python3 tests/contracts/generate_jellyfin_openapi_manifest.py \
  /tmp/jellyfin-12-openapi.json
```

The generator rejects a digest or API-version mismatch before writing the
manifest. The validator derives route, parameter, deprecation, request-schema,
and `ItemFields` assertions from the generated surface. Client code must use
only listed operations and must not use operations or parameters marked
obsolete. The same replacements are present in Jellyfin 10.11, so Bloom does
not branch the core catalog/playback contract by server version. In particular:

- user-scoped catalog operations use `/UserViews`, `/Items`, `/Items/Latest`,
  `/Items?Ids={id}`, `/UserPlayedItems/{id}`, and `/UserFavoriteItems/{id}` with
  `UserId` where the operation declares it;
- item `Fields` values are limited to the OpenAPI `ItemFields` enum, and direct
  item reads do not send the unlisted `Fields` parameter;
- `PlaybackInfoDto.UserId` is sent in the POST body, playback progress omits
  the unlisted `EventName`, and stopped reports contain only
  `PlaybackStopInfo` fields;
- session restore validates the stored token with `GET /Users/{userId}` and
  accepts it only when the returned `UserDto.Id` matches the stored account.
  The login `AuthenticationResult.User` and that `UserDto` also supply
  `Policy.IsAdministrator`, which Bloom keeps as the account's administrator
  flag; the playback recovery ping uses the same route instead of
  `/System/Info`;
- sign-out sends `POST /Sessions/Logout` (`ReportSessionEnded`) once, best
  effort, before deleting the token. It deletes only that account's token;
  Jellyfin also ends the in-memory session of any other account using
  Bloom's device ID on that server, which reopens on its next request;
- Jellyfin media markers use `/MediaSegments/{id}`. Bloom does not call the
  excluded legacy Intro Skipper plugin endpoint;
- remote session removal resolves the session's device and uses
  `DELETE /Devices?id=...`; the excluded per-session logout endpoint is not
  used. Jellyfin allows that route only for administrators, so Bloom offers
  it only when the account is not reported as a non-administrator, and maps a
  `403` refusal to the same explanation (and hides the controls from then on);
- requests made through Bloom use the `Authorization: MediaBrowser ...`
  header with `Client="Bloom"`, the machine hostname as `Device` and the
  stable installation `DeviceId`. Jellyfin splits that header on commas
  outside quotes and URL-decodes each value, so the device name has control
  characters removed, is capped at 64 characters, and is percent-encoded. It
  falls back to `Bloom Device` when the hostname is empty. A new device name
  never creates a new Jellyfin device entry, because the entry is keyed by
  `DeviceId`. External consumers that cannot receive request headers use the
  current capitalized `ApiKey` query fallback; Bloom strips deprecated query
  aliases and the obsolete `deviceProfileId` from provider-supplied stream URLs;
  and
- external subtitle files use the `DeliveryUrl` that PlaybackInfo returns for
  the `External` subtitle profiles. On the server's origin its credential is
  replaced by the session's current `ApiKey` and the rest of the query is kept
  as sent; with `IsExternalUrl` only an http(s) URL is used. Without a usable
  one, Bloom uses the non-obsolete `GetSubtitle` route
  `/Videos/{itemId}/{mediaSourceId}/Subtitles/{index}/Stream.{format}`.

The checked-in live Jellyfin fixture remains the stable 10.11.10 control. On
2026-08-11, the pinned official Jellyfin 12 image above reported server version
`12.0.0`; unauthenticated probes of `/UserViews`, `/Items`, `/Items/Filters2`,
`/Items/Latest`, `/MediaSegments/{id}`, `/Sessions`, and POST PlaybackInfo all
reached registered routes and returned `401`, not `404`. The Jellyfin 12 target
was also validated structurally against the pinned OpenAPI. A credentialed
12.0 release-candidate/final payload run still requires the operator-owned
media fixture described below.


See [provider architecture](provider-architecture.md), [playback](playback.md), [media segments](media-segments.md), and the [manual validation ledger](manual-validation.md) for ownership and evidence.

## Local audio decode and encoded output

Both providers use the shared local decode codec list. Endpoint passthrough
support does not restrict that list. Jellyfin's pinned `PlaybackInfoDto.DeviceProfile`
accepts `DirectPlayProfiles.AudioCodec`; Bloom sends its local decode list there.
Silo's `original_http.audio_decode_codecs` carries the same decode semantics.
Jellyfin's `SampleRate`, `BitDepth`, `AudioSpatialFormat` and `SupportsDirectPlay`
map to canonical metadata. Silo's pinned audio tracks supply `sample_rate`,
`bit_depth`, `profile` and `title`. See [Audio output](audio.md) for limitations
and the manual provider/session checks.
