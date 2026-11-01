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
| `expired`, `canceled`, or 404 | Shows "This code expired". |
| `consumed` without tokens | Fails; the user gets a new code. |
| 429 | Adds five seconds to the interval for the rest of the code (RFC 8628 `slow_down`). |
| No response, 408, or 5xx | Retries after a growing delay; fails after three in a row. |

A local deadline with no decision also ends as expired, after one last poll (or the answer to a poll already in flight), so an approval that lands just before the deadline is kept. Every ended state offers "Get a new code". Backing out, leaving the signed-out state, or starting another sign-in withdraws a live code with `cancelDeviceLogin` when the capability reports `cancel`. Cancel is naturally idempotent, so it is sent with one transient retry and its answer is ignored. Approved tokens go through the password-login path: provider profiles and PIN, `CredentialStore`, the refresh token, and the access-token lifetime from `expires_in`. Bloom never starts `remote_playback` requests and never approves devices.

`SiloRequestFactory` sends bearer authorization and selected `X-Profile-Id` and `X-Profile-Token` headers on same-origin requests. Login, refresh, discovery, device start/poll/cancel/capability, and PIN verification exclude stale authentication/profile headers as required. Profile-scoped catalog, capability, watch, marker, and playback requests require the selected profile. Credentials remain in `CredentialStore`; connection metadata contains no token.

Errors use RFC 9457 `application/problem+json` with `type`, `title`, `status`, `detail`, and `instance`. The problem-type suffix supplies the stable error code. Bloom ignores unknown response fields and treats unknown capability, decision, and delivery values as unavailable. HTTP 406 and 415 are terminal format errors. Bounded retries cover 408 and 429 only when the operation is explicitly safe to replay.


### Jellyfin Quick Connect

Jellyfin's code sign-in is Quick Connect: `GetQuickConnectEnabled`, `InitiateQuickConnect`, `GetQuickConnectState`, and `AuthenticateWithQuickConnect`, all non-obsolete in the pinned OpenAPI contract. When the address on the login screen is not native Silo, or Jellyfin is the selected provider, Bloom reads `GET /QuickConnect/Enabled` with the same 10-second deadline and one retry as the other discovery reads. Only a bare JSON `true` offers "Sign in with Quick Connect". `false`, a failed read, or any other answer leaves the password form alone. The password form always stays, because Jellyfin has no `password_login: false` equivalent. An explicit Silo selection never asks.

Every Quick Connect request carries the MediaBrowser authorization header with the stable Bloom `DeviceId` and no `Token`, the identity a password login uses. The resulting session is therefore the normal Bloom device.

1. `POST /QuickConnect/Initiate` has an empty body and is never replayed. Its `QuickConnectResult` supplies `Secret` (kept in memory only) and `Code`. A 401 means Quick Connect was turned off, which Bloom shows as "not offering sign-in with a code right now". A 401 from Initiate or from a status read (Quick Connect turned off mid-flow) also checks the address again, so the stale "Sign in with Quick Connect" offer disappears. Only Quick Connect treats 401 this way; Silo's device-login routes declare no 401. Changing the provider selection drops the sign-in options found under the old selection.
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

Rows load in the background with the other Home rows and refresh with them: after playback, on returning to Home after 30 seconds, on a library-visibility change, and after a watchlist or Not interested change. A new Home (sign-in, profile or space switch) starts without rows. Merged spaces keep Bloom's own Home rows only, because a layout belongs to one Silo profile. Jellyfin has no curated layout (`ProviderCapability::HomeSections` is Silo-only); its Home gets a Favorites row and Jellyfin's movie recommendation rows instead (see [Jellyfin Home](#jellyfin-home)).

Library pages offer the rows the profile curates for each library as shelves (`ProviderCapability::LibrarySections`, native Silo only). `LibraryService::getLibrarySections` reads `GET /api/v2/library/{id}/sections` (`listLibrarySections`) with the artwork widths and keeps the rows in the server's order. Every section type is kept, with only movie and series cards (`LibraryKinds`), once each per row; rows left empty are dropped. The read sends `item_limit=20` (or the server's smaller maximum) only when the Home capabilities report `section_item_limit_max`; it shares Home's cached capability read, so a connection reads the capabilities once for both, and a remembered cap the server rejects (`400`/`422`) is dropped and the rows are read once more without it. A row has more cards only when Bloom sent `item_limit`, the row came back with exactly that many items, and its `total_count` is larger (or it says `has_more`, which the pin sends only on Home reads). `total_count` alone is no such claim: it counts the section's whole set, past the section's own `item_limit`, which the items read does not exceed. Its other cards come from `GET /api/v2/library/{id}/sections/{section_id}/items` (`getLibrarySectionItems`), which takes no cap or cursor and answers the whole row, read once when focus nears the row's end. `getLibraryLayout` is not read: the sections read already carries each row's title, type and cards. Jellyfin has no curated library rows and shows only the grid.

The library grid's A–Z rail covers the whole library in title order. Native Silo reads `POST /api/v2/catalog/letters` (`queryCatalogLetters`) only when `GET /api/v2/catalog/search/capabilities` (`getCatalogSearchCapabilities`) reports `state: available`, `allowed` and `letter_index`. It is the same profile-scoped document that gates scoped people search: `LibraryService` reads it once per connection and profile for both (`CatalogSearchCapabilities`, one cache entry with both flags), callers that ask while the read is in flight wait for it, and a sign-out, account or profile switch clears it. A server without the route (`404`, `405`, `501`) is remembered as having neither feature, and another failed read is not repeated for five minutes. The body carries the grid query's `library_id`, `match: all` and rule `groups` (the same groups `queryCatalogItems` gets, including the movie or series type rule) and `order: desc` for descending title order; the index takes no search text, cursor or limit, so a search keeps the loaded-rows rail. The operation is a read-only `natural_idempotent` POST, so the transport may replay it. Bloom keeps `letters` (`{letter, count, position}`) and `runs`; a jump sends the letter's `position` as `seek` with the window cursor, and `runs` tell which letter a list position is under. Entries without a letter, with no titles or with a negative position, and runs out of list order, are dropped. Silo's index can lag the list by titles added during a scan for up to two minutes; a window that answers fewer rows shortens the list.

Continue Watching and Up Next cards can be hidden from Home (the card actions menu, see [view models](viewmodels.md#continue-watching-and-next-up)). `PUT /api/v2/home/dismissals/{surface}/{item_id}` (`dismissHomeItem`, `ProviderCatalogOperation::SetHomeDismissal`, `ProviderCapability::HomeDismissals`) hides a card for the acting profile, and `DELETE` on the same route (`undismissHomeItem`) backs the undo toast. The surface is the row the card shows in by its progress: `continue_watching` for a card with a position, whose body is `{"progress_updated_at": …}` with the card's own `progress_updated_at` (canonical `progressUpdatedAt`, sent verbatim because the server compares it with its stored stamp), and `next_up` otherwise, whose body is `{"series_id": …}`. A card without the field its surface needs offers the action disabled. Dismissing an episode drops the whole show from both rows until the profile plays it again, so Bloom hides every card of that series at once. The server also sends the drop to the profile's watch providers (`dispatchDropped` in `internal/api/handlers/home_dismissals.go`, for example Trakt), and an undo sends the undrop. Bloom therefore labels an episode's action "Stop watching ‹series›", asks before sending it, and says so in the toast; a movie keeps a plain remove with undo. Both routes are `natural_idempotent`, so the transport may replay them after a lost response. Bloom does not use `DELETE /api/v2/progress/{media_item_id}/position`: a dismissal keeps the resume point and can be undone.

The watchlist is a per-profile list separate from favorites. Movie and series pages show "Add to Watchlist" / "On Watchlist" when the provider reports `ProviderCapability::Watchlist` (native Silo only; Jellyfin has none). The state comes from the item's `user_state.in_watchlist` (canonical `inWatchlist`). `PUT` and `DELETE /api/v2/watchlist/{item_id}` change it with an empty body. Both are `natural_idempotent` in the contract, so the transport may replay them after a lost response. The button changes at once and reverts with a message when the server refuses.

Person pages read `GET /api/v2/catalog/people/{id}` (`getPerson`, without `prefetch`) for the name, biography, life dates, birthplace, photo and provider IDs, and the person's titles through `GET /api/v2/catalog?source=person&person_id={id}&type=video&sort=-year` (the order Silo's web app uses), with the artwork widths, paged by `page.next_cursor`. People search uses `GET /api/v2/catalog/people?q=…&limit=…&media_scope=video`, which `listPeople` accepts only when `GET /api/v2/catalog/search/capabilities` reports `people_media_scope`; it then returns only people with movie or series credits the viewer can see. Bloom reads that flag once per connection and profile, in the same read that gates the letter index (merged spaces read it once per member); a failed read is not repeated for five minutes, and until then Search shows no people from that connection. Without the flag Search shows no people, because an unscoped search also returns authors and people from libraries the profile cannot open, and older servers reject the parameter with `422`. `listPeople` and `getPerson` declare no artwork widths, so photos keep their 500 px default.

"Not interested" keeps a movie or series out of the profile's recommendation and discovery rows and ranks similar titles lower; library browsing, search, details and playback are unchanged. The movie and series pages' "More options" menu shows "Not Interested" / "Interested Again" when the provider reports `ProviderCapability::NotInterested` (native Silo only) and `GET /api/v2/capabilities/not-interested` reports `state: available` with `allowed` for the selected profile. That document is read once per connection and profile when a detail page opens; a 404, 405 or 501 hides the action for good, other failures hide it until the next page. A read cancelled by a session or profile change is forgotten, so the next page asks again. The state comes from `user_state.not_interested` (canonical `notInterested`). `PUT` and `DELETE /api/v2/not-interested/{item_id}` change it with an empty body; both are `natural_idempotent`, so the transport may replay them. Silo refuses episodes and seasons with `422`; Bloom offers the action on movies and series only. The menu item changes at once and reverts with a message when the server refuses. Silo filters marked titles out of recommendation rows as they are read, so the curated Home rows refresh after a change as after a watchlist change. Detail pages run on the live session, so in a merged space the mark goes to the owning connection after routing (see [profiles](profiles.md)).

The series page shows the next episode to air as a chip in its hero ("S2 E5 airs Thu, Nov 6"). Silo's `upcoming_event` is filled only on Home and Discover cards at the pin, never on item details, so the series page reads `GET /api/v2/calendar` instead (`ProviderCapability::AiringCalendar`): today through 30 days later (Silo accepts at most 31), with the system's IANA zone as `timezone`, so the days are the viewer's own. A system zone Qt does not know or that is not an IANA name is sent as its whole-hour `Etc/GMT` zone instead (with that zone's inverted sign: UTC-5 is `Etc/GMT+5`), and when Silo rejects the zone (a validation problem at `query.timezone`) the read is retried once that way. The calendar covers the whole household, so `LibraryService` reads it once per connection, profile, local day and zone, keeps the parsed airings for 12 minutes, and answers every series page asking meanwhile from that read or the kept copy; a session or profile change drops both. `library_id` is not sent: catalog details do not say which library holds a series. It keeps the episode and season premiere items whose `series_id` is the series, skips those whose `air_at` instant has passed (an item without one counts until its `local_air_date` is over), and shows the earliest. The chip uses the instant on the viewer's clock when the server knows it ("today at 9:00 PM"), else the local day. The read is skipped when the details already carry an `upcoming_event` still ahead or the series has ended or been cancelled. The hero keeps the chip's place only while the read is out on a provider with a calendar; an empty or failed answer releases it, and a failure never affects the rest of the page. A card's airing is shown only while it is still ahead, and the chip refreshes at the next local midnight or air time, asking the calendar for the following airing once one has passed. Jellyfin has no per-series next-airing read in the pinned contract (`/Shows/Upcoming` lists every series), so its series page shows none.

Title pages show the server's ratings first: details carry `ratings`, already in the order of `GET /api/v2/capabilities/ratings` and limited to the sources an administrator shows, each with a plain-text `name` and `display` text. Bloom renders them in that order with the display text (canonical `serverRatings`) and does not read the ratings capability itself. Without `ratings` (cards, older servers) the fixed `rating_imdb`, `rating_tmdb`, `rating_rt_critic`, and `rating_rt_audience` fields stand in. MDBList and AniList ratings follow for sources the server does not show, matched by `ExternalRatingsHelper::ratingSourceKey`, the one table of source spellings (it also gives `RatingMetadataChip` its logo key). A server rating of 0 still shows, and the movie page drops its ★ community-rating fact when the server's ratings include IMDb or TMDB, which that fact repeats.

Series and season collections omit `type`. The mapper treats an untyped card with `episode_number` as an episode, and one with `season_number` and `episode_count` as a season numbered by `season_number`.

Artwork URLs are signed links to one stored size variant per image. Each read picks the narrowest variant at least as wide as the request's `poster_width`, `backdrop_width`, `logo_width`, `still_width`, or `photo_width`, else the original. Without them Silo uses per-surface defaults: 300 px posters and backdrops on catalog pages and structured queries, 300 px stills on episode rows (season episode lists), and 500 px posters with 1920 px backdrops on Home and details. Those defaults upscale on a large screen. `SiloArtworkSizes` sends `poster_width=1000`, `backdrop_width=3840`, `logo_width=1280`, and `still_width=1280` (sizes for a 4K screen, matching Bloom's 2x `maxWidth` requests to Jellyfin) on every catalog, Home, library section, similar, and watchlist read, and on the artwork refresh read. Episode stills (the series page and Up Next episode cards) get the 1280 px rung; photos keep their 500 px default. Silo answers `422` to parameters a route does not declare, so library discovery, filters, theme reads, settings, and writes never carry them. The mapper tags each artwork reference with a hash of the URL path without its signed query. The image cache key therefore changes when the variant or the artwork changes, but not when the signature rotates.

Episode cards can also carry their parents' artwork: `series_poster_url`, `season_poster_url`, and `series_backdrop_url`, each with a `*_thumbhash` (`GET /api/v2/images/capabilities` reports `episode_parent_artwork`). They appear on Home and library sections, catalog browse and search, Watch Tonight, and episode detail. Each is optional; older servers send none, and the mapper then maps only the card's own artwork. The mapper fills the keys the Jellyfin mapper fills:

- `series_poster_url` becomes `seriesPrimaryArtwork`, owned by the series (`series_id`). Poster cards for episodes (Recently Added and the curated rows) therefore show the series poster, as on Jellyfin.
- `season_poster_url` becomes `parentPrimaryArtwork`. Cards carry no season ID, so the episode owns it. Its refresh re-reads the episode and returns `season_poster_url` when that field's path still matches the reference's tag, else the episode's own poster.
- `series_backdrop_url` becomes `backdropArtwork`, owned by the series, when `backdrop_url` is missing or `backdrop_is_episode_still` is true. This matches Jellyfin, whose episodes take their parent's backdrop. Otherwise the card's own `backdrop_url` stays first (the season or series backdrop in Continue Watching and Next Up), and the series backdrop follows it in the `artwork` list.

None of these fields has an `*_expires_at`. When the series backdrop replaces the still, the card's `backdrop_url_expires_at` is dropped from `artworkExpiresAt`.

Catalog cards (`CatalogItem`) carry `overlay_summary` but no unwatched episode count: their `user_state` holds only `played`, `is_favorite`, `in_watchlist`, and `not_interested`. Seasons and item details carry the full watch rollup, so Silo series posters show a watched check but no count. Poster badge preferences come from the profile-scoped effective `ui.card_overlays` and `ui.card_overlays_enabled` settings and `GET /api/v2/settings/overlay-config`; see [poster badges](poster-badges.md).

Local extras (trailers, featurettes, deleted scenes and other files the scanner files under a movie or series) come with the detail: `GET /api/v2/catalog/items/{id}` returns `extras[]` (`ItemExtraInfo`: required `content_id` and `kind`, optional `title`, `duration_seconds`, `file_id`, `still_url`, `still_thumbhash`). `kind` is a free string; the server's vocabulary is `trailer`, `teaser`, `featurette`, `clip`, `behind_the_scenes`, `bloopers`, `deleted_scene` and `other`, and `SiloModelMapper::extras` reads anything else as `other`, as the server's own `NormalizeExtraKind` does. An entry without `content_id` is dropped. `still_url` is the 16:9 still at the `still_width` rung (1280 px). An extra has no catalog route, so its still is owned by the parent (`ArtworkOwnerKind::Extra`): a refresh re-reads the parent detail and takes the still whose path still matches the reference's tag, else the one at the same position. Each `content_id` plays through `/api/v2/watch/{id}` and protocol 3 like any item, and the server authorizes it through its parent. Remote provider videos (`item_videos`, YouTube) are not in `extras[]` and Bloom does not play them: its mpv runs without ytdl. There is no separate extras read, so `SpecialFeatures` and `LocalTrailers` are unsupported operations on native Silo.

Silo has no series-wide episode route, so `LibraryService::getNextUnplayedEpisode` lists the seasons, reads each season's episodes, and runs `NextEpisodeResolver` over the whole series. The series page and autoplay therefore share the Jellyfin rules, including the excluded current episode. The catalog query source has no random order. For the Home backdrop, hero, screensaver and Search suggestions, `LibraryService::sendRandomSampleRequest` reads the count of matching movies and series, fetches a 100-item window at a random `seek` offset, and shuffles it locally. The full backdrop index pages through the same catalog query. A provider that implements these operations natively, such as Jellyfin, keeps its own request.

Conditional playback and theme capability reads return ETags. Bloom sends `If-None-Match` and reuses the cached representation on `304`. The watched, favorite, watchlist, authentication-session revoke, playback, and theme-grant operations Bloom uses do not declare guarded writes. Bloom does not invent `If-Match` for them. A future guarded write must preserve the resource ETag and reconcile `412` after a fresh read.

## Media requests and Discover

Native Silo exposes `ProviderCapability::MediaRequests`. Every request route is profile scoped. Bloom reads `GET /api/v2/requests/status` (`FeatureStatus`) for the selected profile and offers requests only when `requests_enabled`, `allowed`, and `state: available` hold. A configured Seerr API key takes priority, and Bloom then shows none of Silo's request surface. See [media requests](seerr.md).

- Search uses `GET /api/v2/requests/search` with a percent-encoded `q`: `QUrlQuery` would leave `+`, which the server reads as a space. Titles whose `availability` is `available` are left out, because the library shelves show them.
- `GET /api/v2/requests/detail/{media_type}/{tmdb_id}` supplies the dialog's seasons (`availability`, `requested`), `request.requestable` with its `reason`, and the `recommendations` Bloom shows as similar titles. Bloom maps its `tv` to Silo's `series`.
- `POST /api/v2/requests` is declared `non_retryable`, so Bloom never replays it. The body carries `media_type`, `tmdb_id`, and the required `title` from the prepared detail, plus `year`, `overview`, and artwork paths. `seasons` is sent only when the user picks seasons. Without it Silo requests every aired season the library lacks.
- `GET /api/v2/requests/discover` returns the rows with their first page. `GET /api/v2/requests/discover/{section}` pages a built-in row by its enum key (the six documented keys). When rating backfill consumed more than one provider page, `next_page` gives the next page.
- When `FeatureStatus.discover_rows_supported` holds, the page follows the admin's layout: `row_order` (`DiscoverRowRef` `{key, type, title}`) lists every row in order, `items` holds the title rows that have titles, and rows beyond the six built-in ones carry opaque keys. Bloom shows `row_order`'s rows in order under their titles, skips types it does not know, and fills a brand row (`studios`, `networks`, `genres`, keyed by its type) from the matching brand list operation. A title row in `row_order` without an `items` entry is left out. Rows that are not built-in page only through `getDiscoverRow` (`GET /api/v2/requests/discover/rows/{row_id}`, a profile-scoped read retried as `Idempotent`); Bloom sends it only keys matching its `^[a-z0-9_]{1,40}$` pattern, never a brand row. A layout row trimmed to its limit (`firstRowPage`: up to 100 titles from `ceil(limit / 20)` source pages, one more with a rating ceiling or `hide_available`) loses `next_page`, and its titles past the limit are reachable only by reading those source pages again. A first page without a cursor that shows fewer titles than `total_results` therefore continues at page `shown / 20 + 1`, bounded by `total_pages`; Bloom drops the titles it repeats. A layout without brand rows shows no browse categories, and an empty `row_order` shows an empty page. Without the capability Bloom ignores `row_order` and keeps the response order of the built-in rows, with genres, studios, and networks after them.
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
- person pages read the person by ID with `GET /Items/{itemId}?userId=`
  (`GetItem`), not the name-keyed `/Persons/{name}`, since names are not
  unique, and their titles with `GET /Items?PersonIds={id}` limited to
  `Movie,Series`, recursive, sorted by `PremiereDate,SortName` descending then
  ascending, with `StartIndex`/`Limit` paging. People search uses
  `GET /Persons` (`GetPersons`) with `searchTerm`, `limit` and Primary images
  instead of `/Search/Hints`. `GetPersons` has no media or library scope, so
  it sends `personTypes=Actor,Director,Writer,Producer,GuestStar` to leave out
  music artists, composers and book authors (someone with both kinds of
  credit still matches);
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
- local extras come from `GET /Items/{itemId}/LocalTrailers` and
  `GET /Items/{itemId}/SpecialFeatures` (`GetLocalTrailers`,
  `GetSpecialFeatures`) with `userId`; both answer a bare `BaseItemDto` array.
  Every local trailer is a trailer, whatever its `ExtraType`. Special
  features map `ExtraType` (`Featurette`, `Clip`, `BehindTheScenes`,
  `DeletedScene`, `Interview`, `Scene`, `Short`; `Unknown` or none is
  `other`) and drop `Sample`, `ThemeSong` and `ThemeVideo`. The obsolete
  `GET /Trailers` and remote `RemoteTrailers` are never used. Silo's
  compatibility surface serves both routes at the pinned compat revision;
- the library grid's A–Z rail in title order first counts the list: `GET
  /Items` with the grid query's filters, `EnableImages=false`,
  `EnableUserData=false` and `Limit=0`, read for `TotalRecordCount` only.
  Above 5,000 titles no index is built and the rail keeps the letters of the
  loaded rows. Otherwise the same query with `SortBy=SortName` and its
  `SortOrder`, `Fields=SortName`, and no `StartIndex` or `Limit` returns the
  whole list's sort names, in one attempt with a 60-second deadline (a retry
  would download it again); it is parsed off the UI thread and the index is
  kept per query for 15 minutes. Each item's index is its position,
  and its letter is the first character of `SortName` (else `Name`), folded
  to A–Z or `#`. `NameLessThan` is not used: Jellyfin 10.11 compares only the
  first character of `SortName` or `Name` (so "The Matrix", sorted under M,
  counts as T), which cannot locate a letter in `SortName` order. An answer
  with fewer items than `TotalRecordCount` (Silo's compatibility listener
  defaults `Limit` to 24) yields no index, and the rail keeps the letters of
  the loaded rows. A jump reads the window at the letter's position with
  `StartIndex`;
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

#### Jellyfin Home

Jellyfin answers in-progress items and Next Up separately, so Bloom reads
both for Continue Watching and Next Up:

- `GET /UserItems/Resume` (`GetResumeItems`, `ProviderCatalogOperation::ResumeItems`)
  sends `UserId`, `Limit=20`, `IncludeItemTypes=Movie,Episode`,
  `MediaTypes=Video`, and the Next Up card `Fields`, `EnableUserData`, and
  `EnableImageTypes`, so its cards have the same shape. Jellyfin orders it by
  last played, newest first. In-progress movies, which `/Shows/NextUp` never
  returns, therefore reach Continue Watching.
- `GET /Shows/NextUp` sends `Limit=20` and `EnableResumable=true`, so a
  part-watched next episode stays in Next Up and still shows when the
  resume read fails. Both lists name it then, and the resume copy wins the
  dedupe; the page is twice Bloom's former 10 so those episodes leave room
  for the ones still to start. `EnableResumable` is sent explicitly because
  Silo's compatibility listener defaults it to false. `EnableRewatching=true`
  and `NextUpDateCutoff` (midnight UTC, the chosen number of days ago) are
  sent only when the viewer turns on the Next Up options
  (`ConfigManager::nextUpRewatching`, `nextUpCutoffDays`).
- `LibraryService::getNextUp` reads both side by side and publishes one list
  through `ContinueWatching::combine`: each item once (by `MediaRef`, the
  resume copy first), in-progress items first, most recently played first,
  then Next Up in its own order. Home splits that list by `positionMs` into
  the two rows. A failed resume read leaves Next Up alone; a failed Next Up
  read keeps the rows already shown. A newer refresh supersedes an older
  one, whose answers are dropped (also in merged spaces). Providers report
  the resume list and the options through `ProviderCapability::ResumeItems`
  and `NextUpOptions`; native Silo has neither, keeps its single Home-layout
  read in the server's order, and answers `ResumeItems` unsupported.
- Jellyfin has no watchlist, so `LibraryService::getHomeSections` adds a
  "Favorites" row (`bloom:favorites`) for providers without
  `ProviderCapability::Watchlist` when `ConfigManager::homeFavoritesRowEnabled`
  is on (the default). It reuses the `/Items` operation with
  `IsFavorite=true`, `IncludeItemTypes=Movie,Series`, `Recursive=true`,
  `SortBy=DatePlayed,SortName`, `SortOrder=Descending,Ascending`, and
  `Limit=20`, so only movies and series appear (`LibraryKinds`). Jellyfin
  applies one order per `SortBy` key and reuses the first for keys without
  one, so the name tie-break gets its own `Ascending`: never-played
  favorites follow A to Z. No favorites adds no row.
  A favorite toggled on a details page refreshes it, as a watchlist change
  refreshes Silo's rows. Merged spaces show no Favorites row, like Silo's
  curated rows.
- Personalised movie rows follow the Favorites row when
  `ConfigManager::homeRecommendationsEnabled` is on (the default) and the
  provider reports `ProviderCapability::MovieRecommendations` (Jellyfin
  only). `GET /Movies/Recommendations` (`GetMovieRecommendations`,
  `ProviderCatalogOperation::MovieRecommendations`) is read once for each
  movie library Home shows. It sends `UserId`, `ParentId`,
  `CategoryLimit=4`, `ItemLimit=15` and
  `Fields=Overview,ParentId,PrimaryImageAspectRatio`; the operation has no
  image or user data switches, and the server adds user data for the user.
  The answer is a bare `RecommendationDto` array. The adapter keeps each
  category's `RecommendationType`, `BaselineItemName` and `CategoryId` with
  its wire `Items`, and `LibraryService` maps the items. Home titles a row
  by its type: `SimilarToRecentlyPlayed` "Because you watched %1",
  `SimilarToLikedItem` "More like %1", `HasDirectorFromRecentlyPlayed` and
  `HasLikedDirector` "Directed by %1", `HasActorFromRecentlyPlayed` and
  `HasLikedActor` "Starring %1", with the baseline name for `%1`. Only
  movie cards are kept; a row that repeats an earlier row's type and
  baseline (from another library) is merged into it, and empty rows are
  dropped ([view models](viewmodels.md#homesectionsviewmodel)). Each read
  can fail alone and leaves the other rows; a failed library shows the
  categories it answered last. Merged spaces show none, like the Favorites
  row: the rows are per user and per server, and Bloom does not merge them
  across members.
- `ParentId` scopes only the seed titles (the played and liked movies).
  Similar titles and people's films come from every library the user may
  open, in 10.11 (`MoviesController.GetSimilarTo`, `GetWithDirector`,
  `GetWithActor`) and in 12.x (`SimilarItemsManager`) alike, so a hidden
  library's movies can be recommended. Bloom then reads, per shown movie
  library, `GET /Items` (`GetItems`) with `UserId`, `ParentId`, `Ids` (at
  most 60 recommended items per read), `Recursive=true`, `Limit`,
  `EnableImages=false`, `EnableUserData=false` and
  `EnableTotalRecordCount=false`, and keeps only the items found
  (`ProviderCatalogOperation::LibraryMembership`). When that read fails,
  only items an earlier read found in the same libraries stay.
- Jellyfin 10.11.0 to 10.11.11 build the `SimilarTo*` categories without
  the baseline (`GetSimilarTo` sets no `SimilarTo = item`, sort or
  `ParentId`), so every "Because you watched" and "More like" row lists
  the same arbitrary movies. 10.10 still set it, and jellyfin/jellyfin
  `97c20e6ac` ("Fix movie recommendations", first in v12.0) fixed it. Bloom
  reads `GET /System/Info/Public` (`GetPublicSystemInfo`,
  `ProviderCatalogOperation::ServerInfo`) once per connection and keeps
  those categories only for `Version` 10.10 and older or 12.0 and newer
  (`HomeRecommendations::similarRowsWork`). A failed read hides them until
  it succeeds. As a second guard, a `SimilarTo*` category whose items equal
  those of a category with another title is dropped. Person rows always
  stay.
- The server picks its seeds at random, so Bloom keeps a read per
  connection, user and set of shown movie libraries for 45 minutes and
  reads again only on a new Home, after playback, and when the setting is
  turned on. A favorite change re-reads the Favorites row only.
- Silo's compatibility listener (pinned `8044eb8`) registers
  `GET /Movies/Recommendations` but ignores `ParentId` (each library read
  answers the same rows, which Bloom merges), approximates
  `RecommendationType`, and puts its whole row label ("Top Rated", "For
  You") in `BaselineItemName`, so Bloom's titles read oddly there. Its
  `/System/Info/Public` answers `ProductName` "Jellyfin Server" and the
  emulated version (12.1.0 by default), so Bloom cannot tell it from
  Jellyfin and shows these rows there too. Latest Silo (`d31eecb`) scopes
  `Ids` reads to `ParentId`; the pinned build was not checked. Native Silo
  reports the operation unsupported; its own recommendation rows come with
  the Home layout.
- "Remove from Continue Watching" sends `POST /UserItems/{itemId}/UserData`
  (`UpdateItemUserData`, `ProviderCatalogOperation::ClearResumePosition`)
  with `UserId` and the body `{"PlaybackPositionTicks":0}`. The server applies
  only the fields the body carries, so the played state, play count and
  favorite stay as they are, and the item leaves `/UserItems/Resume`. A
  next episode may then show in Next Up. The write declares no replay
  semantics and is never retried, and there is no undo. Jellyfin has no way
  to hide a Next Up card, so Up Next cards offer "Mark as watched" (the
  existing `/UserPlayedItems` write) and no hide action. The compatibility
  listener below does not route this `POST` (only the legacy
  `/Users/{userId}/Items/{itemId}/UserData`), so there the card comes back
  with an error. The listener reports itself as "Jellyfin Server", so Bloom
  cannot tell beforehand; after the first 404, 405 or 501 it stops offering
  the action on that connection for the session.
- Silo's Jellyfin compatibility listener (pinned `8044eb8`) serves the same
  reads: it registers `GET /UserItems/Resume`, honors `enableResumable`
  and `nextUpDateCutoff` on Next Up, and filters `IsFavorite`. It honors
  only the first `SortBy` key and maps `DatePlayed` to `created_at`, so
  there the Favorites row lists the newest-added favorites first.

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
