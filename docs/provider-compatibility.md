# Provider compatibility and contract baseline

Bloom's native Silo provider uses stable API v2 and playback protocol 3. Silo 1.0 returns `410 Gone` for the frozen alpha API. Bloom has no native v1 fallback. The separate Jellyfin compatibility listener retains its existing contract.

The release label remains **experimental native Silo support**. Implemented client code and a passing unit test do not establish a live platform pass. [Manual validation](manual-validation.md) records the available evidence and open gates.

## Pins and artifacts

| Component | Pin |
| --- | --- |
| Native Silo source | `35ef973bc64af293aff76bcd49fa2dcab82a7f74` |
| Silo API | `2`, 707 paths |
| Silo OpenAPI SHA-256 | `be30c2f6d2a4dc7fbfef2a9b1438d843b4c25239eb2d637ec09dfef12f1220be` |
| Silo migration SHA-256 | `2580fbf4222c5518605af3ff19ee3df634480dbf5226b22cf99bee6159516d61` |
| Historical Silo compatibility control | `8044eb84dd0cfa512ce8f2448cfd51cb7899a4c6`, image `ghcr.io/silo-server/silo-server@sha256:944ee9821de1d6a61876c9b7b06daa358118163d1e5f9b3aa9f5437856fd06e9` |
| Jellyfin control | `10.11.10`, image `docker.io/jellyfin/jellyfin@sha256:f66273e014b307e4ac46778845ebc1e9ee24b2e57c1fc17d5ec5ac3015649bfa` |
| Jellyfin API | `12.0.0`, SDK revision `592747ce7add446b9a14ad56aba8a7441a2e2618`, SHA-256 `f073a3e661f1c7249c8dc7df257b31f290c8931950f8563e9df537a46717f700` |

The native source and exact contract bytes are pinned. No new native container digest is asserted. The live server reported `35ef973b` and served contract bytes equal to the pinned OpenAPI artifact. The historical compatibility image remains a separate baseline; it is not a v2 image pin.

[`provider-contracts.json`](../tests/contracts/provider-contracts.json) records native operation references, behavior, capability conditions, and exclusions. [`silo-v2-openapi-manifest.json`](../tests/contracts/silo-v2-openapi-manifest.json) contains the operations and transitive schemas Bloom consumes. The generator checks both artifact hashes before writing the manifest. The offline validator rejects pin drift, absent operations, unsupported claims, and frozen native API calls.

To reproduce the manifest, use a zsh-compatible shell:

```sh
mkdir -p /tmp/opencode
revision=35ef973bc64af293aff76bcd49fa2dcab82a7f74
curl --fail --location "https://raw.githubusercontent.com/Silo-Server/silo-server/$revision/contracts/api/v2/openapi.json" --output /tmp/opencode/openapi.json
curl --fail --location "https://raw.githubusercontent.com/Silo-Server/silo-server/$revision/contracts/api/v2/migration.json" --output /tmp/opencode/migration.json
jq '.paths["/api/v2/playback/start"].post.requestBody' /tmp/opencode/openapi.json
python3 tests/contracts/generate_silo_openapi_manifest.py /tmp/opencode/openapi.json /tmp/opencode/migration.json
```

The full specification is about 6 MB. Query individual endpoints and schemas with `jq`.

## Native discovery and authentication

Discovery probes `/health` and `/ready`, then reads `/api/v2/system/info`. Only `api_major: 2` and the system document identify the native protocol. A reverse proxy can serve HTML at the neutral probe paths, so those probes establish reachability only. Compatibility `/System/Info` is not native detection. The system document has no installation identity, and Bloom no longer has a special deterministic-server-ID exception.

`server_version` and `contract_digest` are diagnostic fields. They never enable a feature. Playback, themes, and previews require explicit capability documents and resource availability.

Login and refresh use `/api/v2/auth/login` and `/api/v2/auth/refresh`. Session validation uses `/api/v2/account/me`. Profile and authentication-session lists use `items`. IDs remain opaque strings. Profile-PIN verification requires `valid: true` and a non-empty `profile_token`; `valid: false` is an incorrect PIN.

`SiloRequestFactory` sends bearer authorization and selected `X-Profile-Id` and `X-Profile-Token` headers on same-origin requests. Login, refresh, discovery, and PIN verification exclude stale authentication/profile headers as required. Profile-scoped catalog, capability, watch, marker, and playback requests require the selected profile. Credentials remain in `CredentialStore`; connection metadata contains no token.

Errors use RFC 9457 `application/problem+json` with `type`, `title`, `status`, `detail`, and `instance`. The problem-type suffix supplies the stable error code. Bloom ignores unknown response fields and treats unknown capability, decision, and delivery values as unavailable. HTTP 406 and 415 are terminal format errors. Bounded retries cover 408 and 429 only when the operation is explicitly safe to replay.

## Catalog and conditional requests

Catalog reads use `cursor`, `page.next_cursor`, `page.has_more`, and `window_cursor`. Bloom keeps bounded cursors per connection and query. Explicit window jumps use `seek` with the window cursor. There are no offset or snapshot parameters. GET pages allow up to 200; structured POST queries allow up to 100. Bloom currently requests at most 100 for both paths.

Library IDs are strings in query bodies and URLs. Hierarchy requests use explicit series/season context rather than inferring a resource kind from whether an ID is numeric. Numeric season numbers remain numbers. GET sort direction uses a minus prefix; grouped GET filters use one JSON `groups` parameter.

Conditional playback and theme capability reads return ETags. Bloom sends `If-None-Match` and reuses the cached representation on `304`. The watched, favorite, authentication-session revoke, playback, and theme-grant operations Bloom uses do not declare guarded writes. Bloom does not invent `If-Match` for them. A future guarded write must preserve the resource ETag and reconcile `412` after a fresh read.

## Playback, themes, and seek previews

- `/api/v2/playback/capabilities` must allow protocol 3, `playback_plan_v3`, and original HTTP delivery. Its `installation_id` is required on start and reports.
- Start has ten required fields, including string `file_id`, `playback_attempt_id`, client capabilities, and client playback context. One attempt ID remains stable across retries. `PlaybackDecision.playback_plan` supplies the URL, selected tracks, source duration, and timeline.
- The `st` query is a reconstruction reference, not account authentication. For mpv's URL-only path, Bloom preserves the plan URL and appends the documented `token` account fallback only on the configured API origin. Other origins receive no account token. Header-authenticated delivery is not advertised. Tokens remain transient and redacted.
- Audio replan uses `POST /playback/{session_id}/replan` with `operation: track_change`. The audio PATCH route is absent from the contract and returned `404` in the live test. Original-file audio selection can remain local to mpv.
- Progress uses `installation_id`, a monotonic sequence, `position` in seconds, and `is_paused`. Stop has a stable `stop_id`. These requests have explicit replay safety. Rejected plans receive stop cleanup without an invented final position.
- `/api/v2/watch/{id}` supplies versions, tracks, source duration, chapters, markers, and per-file preview availability. Original HTTP plans with an unshifted seekable timeline are accepted. Forward-only remux and HLS/transcode plans require separate timeline and reanchor work and remain unavailable.
- Themes use `/catalog/themes/capabilities`, detail `themes`, and an owner/theme playback grant. The short-lived URL is never built from an item ID or persisted. Each loop gets a fresh grant. Silo grant audio is fetched once without ranges into a buffer with a 16 MiB limit and a 45-second deadline. Converted audio is progressive AAC in MP4 and has no range or length contract.
- Previews require `trickplay_v1` and the selected version's `trickplay_available`. `/watch/{id}/trickplay?file_id=...` returns the grid, interval, count, expiring signed sheet URLs, and file identity. Bloom validates these values and keeps a bounded cache scoped to server/account/profile/item. URLs are never reconstructed.

The native client implements theme and trickplay capability flags. A flag does not override server permission or establish live validation. The available server fixture validated theme grants/audio but had no published trickplay version in 100 episode and 100 movie checks. The operator confirmed that no previews exist and accepted unit-test coverage for this migration. Live preview delivery remains unverified.

## Validation and exclusions

Use `./scripts/dev-build.sh --tests` during development, then `nix flake check` and `nix build`. The live runner is separate because it needs a disposable account, server, and media. Its JSON report contains no credentials or grant URLs. Skipped live gates are inconclusive and do not pass.

```zsh
export BLOOM_CONTRACT_USERNAME=tester
read -rs 'BLOOM_CONTRACT_PASSWORD?Fixture password: '
export BLOOM_CONTRACT_PASSWORD
python3 tests/contracts/run_live_contracts.py --deployment silo-35ef973-native --base-url http://silo --allow-mutations --output /tmp/opencode/silo-v2-report.json
unset BLOOM_CONTRACT_PASSWORD
```

Set `BLOOM_CONTRACT_TRICKPLAY_ITEM_ID` to a title with published previews to complete that gate. Live GPU playback, audible output, physical keyboard/gamepad navigation, Windows composition, and HDR/display transitions remain separate manual checks.

Ebooks, audiobooks, comics, watch-together, notifications, history imports, downloads/offline, admin operations, and expanded HLS/transcode support remain out of scope. The existing historical Jellyfin compatibility matrix retains its missing/stubbed/partial labels and is not evidence of native v2 behavior.

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
- Jellyfin media markers use `/MediaSegments/{id}`. Bloom does not call the
  excluded legacy Intro Skipper plugin endpoint;
- remote session removal resolves the session's device and uses
  `DELETE /Devices?id=...`; the excluded per-session logout endpoint is not
  used; and
- requests made through Bloom use the `Authorization: MediaBrowser ...`
  header. External consumers that cannot receive request headers use the
  current capitalized `ApiKey` query fallback; Bloom strips deprecated query
  aliases and the obsolete `deviceProfileId` from provider-supplied stream URLs.

The checked-in live Jellyfin fixture remains the stable 10.11.10 control. On
2026-08-11, the pinned official Jellyfin 12 image above reported server version
`12.0.0`; unauthenticated probes of `/UserViews`, `/Items`, `/Items/Filters2`,
`/Items/Latest`, `/MediaSegments/{id}`, `/Sessions`, and POST PlaybackInfo all
reached registered routes and returned `401`, not `404`. The Jellyfin 12 target
was also validated structurally against the pinned OpenAPI. A credentialed
12.0 release-candidate/final payload run still requires the operator-owned
media fixture described below.


See [provider architecture](provider-architecture.md), [playback](playback.md), [media segments](media-segments.md), and the [manual validation ledger](manual-validation.md) for ownership and evidence.
