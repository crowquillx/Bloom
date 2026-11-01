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
  - `loadError(QString error)`: standardized error notification.
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

## HomeSectionsViewModel
The rows a Silo profile curates for its Home page, which `HomeScreen.qml` shows below its own shelves (see [provider compatibility](provider-compatibility.md#catalog-and-conditional-requests) for which rows).

- **Model**: one list row per server row, in layout order, with roles `sectionId`, `title`, and `items` (canonical camelCase item maps). `LibraryService::getHomeSections` chooses the rows and maps the items; the model owns only their lifecycle.
- **Loading and errors**: `refresh()` loads in the background with `isLoading`. A failure sets `hasError`/`errorMessage` and keeps the rows already shown; Home shows no error for these rows, like its other shelves. `reload()` retries.
- **Ownership**: only the answer to the newest request for the active connection is applied. Refreshing under another connection drops the old rows first. `clear()` drops the rows and the pending request; Home calls it for a new Home and in merged spaces, which have no curated rows.
- **Updates**: a refresh that returns the same rows in the same order updates titles and items in place (`dataChanged`), so focused shelves keep their delegates. Other changes reset the model. A confirmed watchlist change refreshes rows already loaded for the live connection.
- **Focus**: Home saves a focused row as `serverRow:<sectionId>` and restores it after returning. Up/Down move between these rows and Bloom's own shelves in visual order.
- **Tests**: `HomeRowsAndWatchlistTest` covers the model and both detail pages' watchlist toggles; `SiloCatalogServiceTest` covers section filtering, the Watchlist row, and the watchlist requests.
