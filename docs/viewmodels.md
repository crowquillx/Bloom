# ViewModel Architecture

Guidance for standardizing ViewModels with shared loading/error state and consistent QML bindings.

## BaseViewModel (C++)
- Inherits `QAbstractListModel`; exposes `isLoading`, `hasError`, `errorMessage` (read-only, QML-visible).
- Signals: `isLoadingChanged`, `hasErrorChanged`, `errorMessageChanged`.
- Standard retry hook: `Q_INVOKABLE reload()` (no-op by default; override to call your load/refresh entry point).
- Helpers:
  - `setLoading(bool)` to flip busy state (emits change once).
  - `setError(QString)` / `clearError()` to manage error flags and text.
  - `emitModelReset(fn)` wraps `beginResetModel`/`endResetModel` around a mutator.
  - `mapNetworkError(endpoint, error)` maps common service failures to user-friendly strings.

## Migration guidance
- Derive list-style ViewModels from `BaseViewModel` (e.g., `LibraryViewModel`, `SeriesDetailsViewModel`).
- Remove bespoke `isLoading`/`errorMessage` flags and use the base helpers.
- Route network callbacks through `setError(mapNetworkError(...))` and clear errors before new loads.
- Use `emitModelReset([&]{ ... })` when replacing model contents; use `dataChanged` for partial updates.
- Keep role definitions and business logic localized; only loading/error plumbing moves to the base.

## QML usage
- Bind spinners/toasts to `isLoading`/`hasError`/`errorMessage` without custom glue per ViewModel.
- After model reloads that affect focusable delegates, use `Qt.callLater` to restore focus per `docs/developer_notes.md`.
- Retry pattern (with shared `StateMessage.qml`):
```qml
Column {
    BusyIndicator { running: model.isLoading; visible: running }
    StateMessage {
        visible: model.hasError
        kind: "error"
        message: model.errorMessage
        actionText: qsTr("Retry")
        onActionTriggered: model.reload()   // BaseViewModel::reload()
    }
}
```

## Current migrations
- `LibraryViewModel` now inherits `BaseViewModel`; loading/error wiring is centralized while pagination, caching, and signals (`loadComplete`, `loadMoreComplete`) remain intact. It consumes connection- and request-aware canonical `LibraryService` results, exposes canonical camelCase `modelData`, resolves posters from `ArtworkRef` maps, compares complete canonical objects during SWR, and rejects provider wire-shaped disk-cache rows. When refreshed identities/order are stable, changed rows are replaced in place with role-specific `dataChanged` notifications; structural changes still reset the model.
- `LibraryViewModel.loadCombinedLibraries(libraryIds, collectionType, limit)` shows several libraries of one kind as one list ("All Movies"). It pages each library with the current sort and filters (recursive, flat, library order treated as title) and merges the pages with `CombinedLibraryCursor` (`Bloom::Models`), which releases an item only once every library still holding items has one buffered, so the merged order never changes as pages arrive. `loadMore`, reload and filter options (the union of each library's genres, tags and studios) work on the combined list; combined pages skip the SWR cache. A failed library is skipped; all failing is an error. `CombinedLibraryTest` covers the merge.
- `LibraryViewModel` owns per-library root search, sort, and broad filters. The UI currently exposes stable sort fields (library order, title, release date, date added, rating, year, random) and hides watched filtering, watched/runtime sort controls, and year-range inputs until those interactions are reliable. Series details, seasons, movie details, and playback-return contexts stay outside this root-library filter state.
- `LibraryViewModel.letterIndex` is where each letter starts in the whole title-ordered library (`{letter, count, position}` in list order). In title order, `loadLibrary` of a paged library asks `LibraryService::getLibraryLetters` for the index of the same query (filters and order); the answer is kept per dataset in memory for two minutes and shown at once on a return. Other sorts, unpaged loads (series children), combined pages and a failed or unsupported read leave it empty, and the rail falls back to the letters of the loaded rows. Only the answer for the current dataset is applied. `letterAtPosition(position)` reads the index's runs, which the rail uses for the focused card's letter.
- `jumpToLetter(letter)` and `seekToPosition(position)` make a row available and emit `positionReady(position)` once it is loaded (at once when it already is). A position past the loaded rows grows the model with placeholder rows (`modelData.bloomPlaceholder`) up to one window after it and reads that window (100 rows) starting at the position: Silo's `seek` with the window cursor, Jellyfin's `StartIndex`. `ensureLoaded(index)` reads the window holding a placeholder row when its card is shown, upward from it above a jump and downward below; windows in flight are not read twice. Window pages replace placeholders only, a page that was in flight when placeholders appeared fills its own rows, a shorter list drops placeholders past its new end, and a failed window leaves its placeholders to be read again. Window pages are not cached (the cache keeps the contiguous prefix); a jump drops a pending background refresh, which would reset the model under it. `LibraryScreen` focuses the row on `positionReady`, and a return to a title past the loaded pages seeks back to it. `LibraryViewModelCanonicalTest` covers the index ownership, jumps, windows, pages in flight, a shrunk list and failures.
- `LibraryViewModel.supportedSorts` and `supportedFilters` hold the canonical sorts and filters the active connection serves (`ProviderLibraryQueryCapabilities`, from `ICatalogProvider::libraryQueryCapabilities` through `LibraryService::libraryQueryCapabilities`). Jellyfin serves every one; Silo has no `random` sort and no `tags` filter. The view model reads them on every `loadViews`, `loadLibrary` and `loadCombinedLibraries`, before it builds a query. A sort or filter the connection does not serve, such as Random kept from a Jellyfin library, falls back to library order or no filter, and `setSortBy`, `toggleTag` and `setAddedSinceFilter` refuse unsupported values. `LibraryScreen` builds the sort menu and the Genres/Tags/Studios tabs from these lists and hides the "Added since" menu without `addedSince`, so focus never reaches a hidden control. Every provider serves the favorite and rating filters, which stay visible.
- Library loading separates dataset identity from request identity. `LibraryItemQuery::datasetKey()` contains connection-independent query semantics (parent, search, filters, sort, item types, and recursion) but excludes offset, limit, and field shape. Each operation gets a generation-scoped request token plus a typed load kind (`Views`, `Initial`, `LoadMore`, `BackgroundRefresh`, or `FilterOptions`). Only the request that currently owns a state flag may clear it or publish results/errors; late success and failure signals are rejected. Connection/profile changes start a new generation.
- Initial pages, later pages, and SWR refreshes share one connection-scoped cache snapshot. Empty snapshots are valid cache entries, while snapshots containing undecodable rows are rejected. Pagination replaces a requested position range only when its preceding range is contiguous; an isolated nonzero-offset result is never reused as page zero. A successful first-page SWR response invalidates unverified later pages because insertions, deletions, or sorting changes can shift page boundaries; a provider-level not-modified response retains the full snapshot. The process-wide memory tier is bounded to 48 datasets or approximately 32 MiB with LRU eviction. Persisted SQLite caches remain isolated by connection scope.
- Filter facets are loaded per library through request-aware `LibraryService` operations and cached separately from item result slices. Recursive filtered queries use collection-type item narrowing (`Movie` for movie libraries, `Series` for TV libraries, both for mixed/unknown libraries). A failed or superseded facet request cannot leave `filterOptionsLoading` active.
- `SeriesDetailsViewModel` inherits `BaseViewModel` for standardized loading/error properties and consumes only connection-aware canonical series/item-list/next-episode/similar-item/focused-item/chapter signals. Nested `SeasonsModel`/`EpisodesModel` expose canonical camelCase roles, including `durationMs`, `positionMs`, special-placement fields, and connection-scoped artwork URLs.
- `SeriesDetailsViewModel` has the same watchlist API as `MovieDetailsViewModel` (`watchlistSupported`, `inWatchlist`, `toggleWatchlist()`, `watchlistUpdateFailed`) for the series.
- `SeriesDetailsViewModel` and the QML series entry points load children through `LibraryService::getSeasons` and `getSeasonEpisodes`, never untyped `getItems(parentId)`. Silo treats an untyped parent as a library ID.
- `SeriesDetailsViewModel` owns canonical stale-while-revalidate caches for series metadata, seasons, episodes, and recommendations. Wire-shaped payloads are rejected. Request-start `connectionId` and focused-episode ownership tokens prevent late responses from another source or episode from changing the active view.
- Focused-episode chapters use canonical `name`, `startMs`, and `ArtworkRef` data, with a small in-memory per-episode cache for quick return navigation. `SeriesSeasonEpisodeView.qml` and the player request boundary keep playback and chapter timing in milliseconds.

## MovieDetailsViewModel
ViewModel for the series-style movie details screen.

### Purpose
Acts as the data source for `MovieDetailsView.qml`. It aggregates canonical movie metadata, playback progress, chapters, cast, similar-library recommendations, and external ratings from MDBList (IMDb, TMDB, Rotten Tomatoes, etc.) plus AniList when available. It consumes `LibraryService::canonicalItemLoaded` / `canonicalSimilarItemsLoaded` only; raw Jellyfin DTO signals remain for unmigrated flows.

### Public API
- **Key Properties**:
  - `title`, `overview`, `logoUrl`, `posterUrl`, `backdropUrl`: UI-ready metadata and artwork.
  - `officialRating`, `durationMs`, `communityRating`, `premiereDate`, `genres`: movie metadata used by the hero chips and synopsis area.
  - `isWatched`, `positionMs`: sync with canonical user state and drive `Play` vs `Resume` UI.
  - `watchlistSupported`, `inWatchlist`: the watchlist action, shown only when the active provider has a watchlist (`LibraryService::supportsWatchlist`, Silo). `inWatchlist` comes from canonical `inWatchlist`.
  - `people`: cast/crew entries used by the lower cast shelf (canonical person maps).
  - `chapters`, `chaptersLoading`: normalized movie chapter cards plus the rail loading state.
  - `similarItems`, `similarItemsLoading`: recommendation shelf data and loading state for similar titles (`itemId`, `primaryArtwork`, camelCase fields).
  - `extras`, `trailer`: the movie's canonical local extras and the one Play Trailer starts (empty without a trailer or teaser). Both change with `extrasChanged`. A detail that carries `extras` (Silo) supplies them; otherwise the view model calls `LibraryService::getItemExtras` (one request in flight per movie and connection) and applies only the matching answer. `ItemExtrasState` (`src/viewmodels/ItemExtrasState.h`) holds this logic and a small in-memory cache: a return visit shows the cached shelf at once and refreshes it in the background. A partial answer (one of Jellyfin's two reads failed) is shown but never cached. `extrasLoading` (`extrasLoadingChanged`) is true while a requested answer is outstanding, so a page restoring focus to the Extras shelf waits for it instead of falling back. `SeriesDetailsViewModel` exposes the same two properties for the series.
  - `mdbListRatings`: `QVariantMap` containing normalized external ratings, including AniList when merged in.
- **Methods**:
  - `loadMovieDetails(QString movieId)`: hydrates from cache when possible, then refreshes metadata and similar items from the library service.
  - `reload()`: retries the active movie load for error/retry flows.
  - `markAsWatched()` / `markAsUnwatched()`: sync played status to the server.
  - `toggleWatchlist()`: adds or removes the movie. `inWatchlist` changes at once; while the write is in flight further presses and refreshed details are ignored. A refusal restores the previous state and emits `watchlistUpdateFailed(message)` for a toast. A confirmed change updates the details cache.
  - `clear(bool preserveArtwork)`: resets state. Set `preserveArtwork=true` to keep artwork visible during transitions.
  - `loadMovieChapters(QString movieId)` / `clearMovieChapters()`: load or reset the active movie chapter rail.
- **Signals**:
  - `movieLoaded()`: metadata is ready for display.
  - `loadError(QString error)`: standardized error notification. Only a failed movie read (`getItem`) fails a load; errors of other `LibraryService` reads, such as Home's background rows, are not the page's.
  - `peopleChanged()`, `chaptersChanged()`, `chaptersLoadingChanged()`, `similarItemsChanged()`, `similarItemsLoadingChanged()`: update the lower metadata shelves.

### Integration
- **Services**: Retrieves `LibraryService` via `ServiceLocator` for movie details, user-data updates, and similar-item requests.
- **Network**: Uses an internal `QNetworkAccessManager` for MDBList and Wikidata/AniList lookups through `ExternalRatingsHelper`. Each request has a 15 s transfer timeout, and error text is passed through `redactNetworkLogText` because the MDBList request URL carries the API key.
- **Config**: Reads `ConfigManager` for API keys and cache storage paths.

### Lifecycle & Side Effects
- **Caching**: Uses separate in-memory and disk caches for canonical movie details and similar items. Disk cache resides under `<configDir>/cache/connections/<scope>/movies/` with `*_details_canonical.json` / `*_similar_items_canonical.json` filenames and a 1-hour TTL; wire-shaped caches are rejected. Movie chapters keep a small in-memory per-movie cache so returning to a movie does not immediately re-show loading skeletons.
- **Rating Fetching**: Automatically triggers external rating lookups after metadata loads when provider IDs (IMDb/TMDB) are present.
- **Recommendation Fetching**: Requests similar items after details load, while still allowing cached empty recommendation results to short-circuit redundant network fetches.
- **State Cleanup**: `clear()` should be called when navigating away from the view to prevent stale shelves or metadata from lingering.
- **Stacked detail pages**: `MovieDetailsViewModel` and `SeriesDetailsViewModel` are app-wide, but a person page can stack a second details page over the first (movie A → cast → person → movie B). Each details view therefore acts on the shared model only while it holds the view's own `movieId`/`seriesId` (`sharedModelCurrent`): load handlers, focus timers and toasts of a hidden page do nothing, and Mark Watched, Watchlist, Favorite and Play, which take no ID, are ignored otherwise. When a `LibraryScreen` becomes the current page again, `reloadSharedDetails()` reloads its own title if the model holds another (the detail cache serves it), and focus returns to the cast card that opened the person once the cast is back. The season view also puts back the season and episode that were on screen, with focus on that episode.

## HomeSectionsViewModel
The rows a Silo profile curates for its Home page, which `HomeScreen.qml` shows below its own shelves (see [provider compatibility](provider-compatibility.md#catalog-and-conditional-requests) for which rows), and the Favorites row for providers without a watchlist (Jellyfin, see [Jellyfin Home](provider-compatibility.md#jellyfin-home)).

- **Model**: one list row per server row, in layout order, with roles `sectionId`, `title`, and `items` (canonical camelCase item maps). `LibraryService::getHomeSections` chooses the rows and maps the items; the model owns only their lifecycle.
- **Loading and errors**: `refresh()` loads in the background with `isLoading`. A failure sets `hasError`/`errorMessage` and keeps the rows already shown; Home shows no error for these rows, like its other shelves. `reload()` retries.
- **Ownership**: only the answer to the newest request for the active connection is applied. Refreshing under another connection drops the old rows first. `clear()` drops the rows and the pending request; Home calls it for a new Home and in merged spaces, which have no curated rows.
- **Updates**: a refresh that returns the same rows in the same order updates titles and items in place (`dataChanged`), so focused shelves keep their delegates. Other changes reset the model. A confirmed watchlist change refreshes rows loaded or loading for the live connection (a read in flight may predate the change), and so does a confirmed favorite change when the provider has the Favorites row (`LibraryService::supportsHomeFavoritesRow`); Silo's rows ignore favorites.
- **Focus**: Home saves a focused row as `serverRow:<sectionId>` and restores it after returning. Up/Down move between these rows and Bloom's own shelves in visual order.
- **Tests**: `HomeRowsAndWatchlistTest` covers the model, the favorite-change refresh, and both detail pages' watchlist toggles; `SiloCatalogServiceTest` covers section filtering, the Watchlist row, and the watchlist requests; `WorkspaceRoutingTest` covers the Jellyfin Favorites row and its setting.

## LibrarySectionsViewModel
The shelves a Silo profile curates for one library, which `LibraryScreen.qml` shows instead of the grid when the viewer picks Shelves (see [provider compatibility](provider-compatibility.md#catalog-and-conditional-requests)).

- **Model**: one list row per server row, in the server's order, with roles `sectionId`, `title`, `sectionType`, `featured`, `items` (canonical camelCase movie and series maps), `hasMore` and `loadingMore`; `count`, `libraryId` and `available` are properties. `LibraryService::getLibrarySections` picks the rows and maps the items.
- **Availability**: `available` follows `LibraryService::supportsLibrarySections` (`ProviderCapability::LibrarySections`, native Silo only). Without it `load` clears the model and sends nothing, so Jellyfin libraries never show the toggle.
- **Ownership**: `load(libraryId)` drops rows of another library or connection first; only the answer to the newest request for that library and connection is applied. A failure sets `hasError` and keeps the rows. `clear()` drops everything; the library list and combined pages call it, and so does a sign-out.
- **Updates**: a reload with the same row IDs in the same order updates rows in place (`dataChanged`); other changes reset the model. The grid's filters and search do not reload the shelves.
- **Paging**: `loadMore(row)` reads the whole row (`getLibrarySectionItems`) once, when focus nears the end of a row with `hasMore`. The answer replaces the row's cards and the row then has no more; a failure keeps the row as it was, ready to try again. A reload that brings only the first page again keeps the cards past it (with the first page refreshed), so focus returning to a card there finds it.
- **Tests**: `LibrarySectionsViewModelTest` covers ownership, in-place updates, paging and availability; `SiloCatalogServiceTest` covers the reads and the shared item cap; `ProviderCatalogTest` covers the requests and parsing.

## Continue Watching and Next Up
Home's two first shelves come from one list, `LibraryService::canonicalNextUpLoaded` (or `MergedCatalog.nextUpLoaded` in merged spaces). The order is decided in C++: for providers with a resume list (Jellyfin), `ContinueWatching::combine` (`src/models/ContinueWatching.*`, `Bloom::Models`) puts each item once (an episode in both lists keeps its resume copy), in-progress items first by last played, then Next Up in its order; Silo's list keeps the server's order. Only the newest refresh publishes; an older one that answers later is dropped. `HomeScreen.qml` only splits it by `positionMs` into Continue Watching and Next Up, or shows it whole when `mergeContinueWatchingWithNextUp` is on, and restores focus with `Qt.callLater` after each update. `ContinueWatchingTest` covers the ordering; `WorkspaceRoutingTest` covers the single and merged reads and stale refreshes.

## PersonViewModel
The data behind `PersonScreen.qml`: one person and the movies and series they appear in on the live connection. It is a creatable QML type (`PersonViewModel`, registered through `ui/PersonViewModelRegistration.h`), so each person page owns one and stacked person pages keep their own state.

- **Load**: `load(person)` takes the canonical person map a cast card or a people search result carries. Its name and photo show at once; a credit's `role` and `kind` are dropped. The page then reads `LibraryService::getPerson` and the first page of `getPersonItems` on the live connection. A person whose `connectionId` is another source's is refused with an error (merged spaces route to the owning connection before the page opens).
- **Properties**: `person` (the seed, replaced key by key by the read; a seed photo stays when the read has none), `personId`, `connectionId`, `movies`, `series`, `hasMore`, `loadingMore`. The list rows are the filmography in server order, with roles `itemData`, `itemId`, `mediaType` and `name`. `movies` and `series` are one `PersonTitlesModel` per shelf (same roles, a `count` property and `get(row)`).
- **Filtering**: only `Movie` and `Series` items are kept (LibraryKinds), each item ID once across pages.
- **Paging**: pages of 60. `loadMore()` reads the next page while `hasMore`, one at a time, continuing from the server's cursor (Silo) or the number of items the server has sent, before filtering (Jellyfin). New titles are appended (`rowsInserted`) to the list and to their shelf's model; neither is reset until the next load, so a shelf keeps its focused card and scroll position while more titles arrive. An empty page ends paging.
- **Loading and errors**: `isLoading` covers the details and the first page. A failed first page sets `hasError`/`errorMessage`; failed details keep the seed with no error; a failed later page keeps the titles and `hasMore`, so the next `loadMore()` tries again. `reload()` starts over.
- **Ownership**: each page request has its own request key, unique across every `PersonViewModel` (two pages may show the same person); answers for another connection, person or an older load are ignored.
- **Tests**: `PersonViewModelTest` covers loading, the seed, filtering and de-duplication, paging by count and cursor, errors and retries, stale answers, refusing another source's person, shelves that only grow by inserts, and request keys of two instances. `ProviderCatalogTest` and `SiloCatalogServiceTest` cover the provider requests and mapping.

