Media Requests (Seerr and Silo)

Overview
- Bloom can request movies and series through one of two backends:
  - Seerr/Jellyseerr, with the user's own URL and API key.
  - The active Silo server's own requests (`/api/v2/requests`), when the server lets the signed-in profile request.
- A configured Seerr takes priority. While a Seerr URL and API key are both set, every request goes to Seerr and Bloom shows none of Silo's request surface; Discover then shows Seerr's rows. Removing the key hands requests back to Silo at once.
- Jellyfin has no request backend of its own; on Jellyfin only Seerr applies.
- Surfaces:
  - Search shows request results in their own shelf after the library shelves.
  - Selecting a request result opens the request dialog.
  - Series details show a "More to Request" shelf of similar titles when the series has a TMDB id.
  - The post-playback no-next screen appends similar series to request after the local recommendations.
  - Discover, a navigation bar tab whenever requests have a backend: the request list, discovery rows, and categories to browse.

Configuration
- Seerr: `Settings > Third Party`, `Seerr URL` (example: `http://localhost:5055`) and `Seerr API Key` (sent as `X-Api-Key`).
- Silo: nothing to configure in Bloom. An administrator enables requests and sets who may request in Silo. Bloom reads `GET /api/v2/requests/status` for the selected profile and offers requests only when `requests_enabled`, `allowed`, and `state: available` all hold. The same document says whether following (`follow_supported`) and watchlist titles (`watchlist_titles_supported`, `watchlist_requests`) are on, and whether Discover follows an admin-defined layout (`discover_rows_supported`). Bloom reads it again after sign-in, a connection or profile switch, and whenever Discover opens.

What each backend offers

| | Seerr | Silo |
| --- | --- | --- |
| Search, similar titles, request dialog | yes | yes |
| Discover rows | Trending, Popular Movies, Upcoming Movies, Popular Shows, Upcoming Shows | the server's rows: the admin's layout (built-in, chart, discover, and list rows, in any order) when the server has one, else trending, popular, upcoming, and on the air |
| Browse | genres | genres, studios, networks (where the admin's layout places them) |
| Request list | the server's 20 most recent requests ("Recent Requests"; the API key sees every user) | the profile's requests ("My Requests") |
| Withdraw a request | pending requests | pending or approved requests |
| Notify me when available | no | when someone else requested the title |
| Watchlist titles the library lacks | no | yes |

Routing (`MediaRequestService`)
- `src/network/MediaRequestService.{h,cpp}` is the QML context property `MediaRequests`. QML never calls `SeerrService` directly.
- Properties: `available`, `backend` (`seerr`, `silo`, or empty), `backendName` (`Seerr`, or the Silo server's name), `discoverAvailable`, and the Silo-only `followSupported`, `watchlistTitlesSupported`, `watchlistRequests`.
- `discoverLoaded` lists the page's shelves in order: title rows `{key, title, items, page, nextPage, hasMore}` and brand rows `{key, title, browseKind}`, which the browse categories of that kind (`genre`, `studio`, or `network`) fill. When Silo advertises `discover_rows_supported`, the rows come in the layout's `row_order` under its titles, and the brand rows stand where the layout puts genres, studios, or networks; a category the layout leaves out gets no row, and an empty `row_order` gives no rows at all. Otherwise (Seerr, older Silo) the service appends three untitled brand rows, `browse:genre`, `browse:studio`, and `browse:network`, after the title rows.
- Methods: `search`, `getSimilar`, `prepareRequest`, `createRequest`, `loadDiscover`, `loadDiscoverRow`, `loadBrowseCategories`, `browse` with `browseKey`, `loadMyRequests`, `cancelRequest`, `setFollowing`, `setWatchlisted`, `refreshAvailability`, and `statusLabel`.
- Silo calls run through `LibraryService::sendMediaRequestOperation`, which uses the active provider's catalog boundary and shared transport. A response for a connection, account, or profile that is no longer active is dropped. `MediaRequestService` then reports the work as abandoned (an empty search, a `*Failed` signal, or an `errorOccurred`), so no loading state stays stuck.
- Seerr's Discover loads its five rows in parallel and answers once with the rows that returned titles, in a fixed order. Each Seerr call has a 20-second transfer timeout.
- When the backend changes (a Seerr key is set or cleared, or Silo's access changes), work started on the old backend is abandoned and late replies from it are dropped; Seerr replies are forwarded only while Seerr is the backend. The Silo capability read survives the switch so Silo can take over again.
- Silo search and similar titles leave out titles already in the library; the library shelves show those.

Discovery items
- Both backends produce the same map, so cards and the dialog do not care which one answered:
  - `source: "request"`, `requestBackend` (`seerr` or `silo`), `itemId` (`request:{type}:{tmdbId}`)
  - `requestMediaType` (`movie` or `tv`) and `requestTmdbId`
  - `name`, `mediaType` (`Movie` or `Series`), `productionYear`, `overview`
  - `posterPath`, `backdropPath`, and their TMDB CDN URLs `imageUrl` (w342) and `backdropUrl` (w1280)
  - `requestStatus`: `pending`, `approved`, `processing`, `partial`, `available`, `deleted`, `declined`, `cancelled`, `failed`, or empty
  - Silo only: `requestable` and, for titles in the library, `libraryItemId` (the catalog `content_id`)
- Request list items add `requestId`, `requestState`, `requestedSeasons`, `createdAt`, and `canCancel`.
- Browse categories are `{kind, id, name, logoUrl, movieId, tvId}`; an empty `movieId` or `tvId` means the category has no list of that type. Studios browse movies and networks browse series. A Silo genre browses series when the server marks it `series_supported`; a Seerr genre merges TMDB's movie and series genres of the same name.
- `MediaRequests.statusLabel(status)` gives the card subtitle text.

Request dialog (`src/ui/MediaRequestDialog.qml`)
- Seerr: the user picks a server, quality profile, and root folder (`SeerrComboBox`), then all seasons or specific ones.
- Silo: the server routes requests, so there is nothing to pick. Seasons already in the library or requested are listed as covered and left out of the season grid; "All Missing Seasons" sends no season list, and Silo requests every aired season the library lacks. When the profile may not request the title (`requestable: false`), the dialog says why (`already_requested`, `already_available`, or a general message) and disables Request.
- Below the options, extra actions appear when they apply:
  - "Notify Me When Available" (Silo): someone else's active request exists and the server supports following.
  - "Add to Watchlist" / "On Watchlist" (Silo): the title is not in the library. When `watchlist_requests` is on, the dialog warns that adding it also requests it.
  - "Withdraw Request": the dialog opened from the request list on a request that can be withdrawn, or Silo says the viewer made the title's active pending or approved request.
- A created request reports its state: pending approval, approved, or submitted. A withdrawn request closes the dialog.

Discover (`src/ui/DiscoverScreen.qml`) and Browse (`src/ui/BrowseScreen.qml`)
- Discover, top to bottom: the request list, the discovery rows, then Genres, Studios, and Networks as landscape cards (with the category logo when Silo has one).
- A Silo server with an admin-defined layout (`discover_rows_supported`) decides the rows below the request list instead: its rows in its order and under its titles, including rows an admin added (TMDB charts and discover queries, TMDB and MDBList lists). A brand row (studios, networks, genres) shows that browse category's cards at its place, and a category the layout leaves out is not shown. Row types Bloom does not know are skipped, and a title row with no titles is left out.
- The page shows its loading state until the rows arrive, and, when it has brand rows only, until the browse categories do. With nothing to show it says so: the error state when Discover failed, or when the browse categories failed and no title row came; the empty state otherwise. Focus lands on the top shelf. Until the viewer moves, a shelf that arrives later above the focused one (the request list, a brand row) takes focus.
- Each discovery row loads its next page when focus comes within six cards of its end. Silo's `next_page` wins over `page + 1` when rating backfill consumed several provider pages. Silo trims a layout row to its limit (1 to 100 titles, read from several 20-title source pages) on the Discover page and drops its cursor. A layout row's first page without `next_page` that shows fewer titles than `total_results` therefore continues at page `shown / 20 + 1`: every title on the pages before it is already shown, so nothing is skipped (10 of an 18-title list re-reads page 1; 90 titles continue at page 5). A row that shows all its titles reads no more. Repeated titles are skipped; a page of nothing new loads the next one at once, up to three in a row, and a page number that does not advance ends the row. `DiscoveryListModel` keeps that paging state. Browse pages the same way.
- The request list shows one card per request, so several requests for one title (different seasons, or other Seerr users) stay separate.
- A title already in the library opens its details page; any other title opens the request dialog. Requests made or withdrawn update card statuses in place and reload the request list.
- A category card opens Browse: a poster grid with Movies/Shows (when the category has both) and Popular, Top Rated, and Newest. Seerr's Top Rated needs at least 200 votes, and Newest stops at today's date. The grid loads the next page when focus is within two rows of the end or the pointer scrolls near it.
- `DiscoveryCard.qml` (the poster card) and `DiscoveryListModel.qml` (a de-duplicating, normalized `ListModel` that keeps focus as pages append) are shared by both pages.
- The stage shows the focused title's TMDB backdrop.

Seerr API endpoints
- Search: `GET /api/v1/search?query=...&page=...`
- Request options: `GET /api/v1/service/radarr`, `GET /api/v1/service/radarr/{id}`, `GET /api/v1/service/sonarr`, `GET /api/v1/service/sonarr/{id}`, `GET /api/v1/tv/{tvId}`
- Request submit: `POST /api/v1/request` with `mediaType`, `mediaId` (TMDB id), `seasons` (`"all"` or season numbers), and the optional `serverId`, `profileId`, `rootFolder`
- Similar titles: `GET /api/v1/movie/{movieId}/similar`, `GET /api/v1/tv/{tvId}/similar`
- Discover: `GET /api/v1/discover/trending?mediaType=all&timeWindow=week`, `GET /api/v1/discover/movies?sortBy=popularity.desc`, `GET /api/v1/discover/movies/upcoming`, `GET /api/v1/discover/tv?sortBy=popularity.desc`, `GET /api/v1/discover/tv/upcoming`, each with `page`
- Genres: `GET /api/v1/genres/movie`, `GET /api/v1/genres/tv`; browsing uses `/discover/movies` or `/discover/tv` with `genre`, `sortBy`, and for Top Rated `voteCountGte=200`, for Newest a release or first-air date up to today
- Request list: `GET /api/v1/request?take=20&skip=0&filter=all&sort=added`, then `GET /api/v1/movie/{id}` or `GET /api/v1/tv/{id}` per request for its title and artwork
- Withdraw: `DELETE /api/v1/request/{requestId}` (Seerr lets requesters remove only pending requests)

Silo API endpoints (all profile scoped; see [provider compatibility](provider-compatibility.md#media-requests-and-discover))
- Capability: `GET /api/v2/requests/status`
- Search: `GET /api/v2/requests/search?q=...&page=...`
- Title detail, seasons, follow and watchlist state, and recommendations (used for similar titles): `GET /api/v2/requests/detail/{media_type}/{tmdb_id}`
- Request submit: `POST /api/v2/requests` with `media_type` (`movie` or `series`), `tmdb_id`, `title`, the optional `year`, `overview`, `poster_path`, `backdrop_path`, and `seasons` only when chosen. Never retried.
- Discover: `GET /api/v2/requests/discover`; later pages of a built-in row through `GET /api/v2/requests/discover/{section}?page=...`, of any other title row through `GET /api/v2/requests/discover/rows/{row_id}?page=...`
- Browse: `GET /api/v2/requests/discover/{genres|studios|networks}`, `GET /api/v2/requests/discover/browse/{genre|studio|network}/{slug}?media_type=...&sort=...&page=...` (`media_type` for genres only)
- Request list: `GET /api/v2/requests/mine?limit=50`
- Withdraw: `POST /api/v2/requests/{id}/cancel` with `{}`. Never retried.
- Follow: `PUT` / `DELETE /api/v2/requests/follows/{media_type}/{tmdb_id}`
- Watchlist titles: `PUT` / `DELETE /api/v2/watchlist/titles/{media_type}/{tmdb_id}`. Never retried, because adding can request the title.

Code map
- `src/network/MediaRequestService.{h,cpp}`: backend choice and the QML-facing API.
- `src/network/SeerrService.{h,cpp}`: Seerr API calls and mapping to discovery items.
- `src/providers/silo/SiloCatalogProvider.h`: Silo request routes.
- `src/providers/silo/SiloMediaRequestMapper.{h,cpp}`: Silo wire bodies, discovery items, requests, and browse categories.
- `src/network/LibraryService.{h,cpp}`: `supportsMediaRequests` and `sendMediaRequestOperation`.
- `src/ui/SearchScreen.qml`, `src/ui/SeriesDetailsView.qml`, `src/viewmodels/UpNextRecommendationsViewModel.{h,cpp}` with `src/ui/UpNextScreen.qml`, `src/ui/DiscoverScreen.qml`, `src/ui/BrowseScreen.qml`, `src/ui/DiscoveryCard.qml`, `src/ui/DiscoveryListModel.qml`, `src/ui/MediaRequestDialog.qml`.
- Tests: `tests/MediaRequestServiceTest.cpp` (Silo routes and mapping, the admin Discover layout and its row paging, Seerr priority, browse categories, request list and withdrawal, follow and watchlist gating, abandoned work), `tests/UpNextRecommendationsViewModelTest.cpp`.
