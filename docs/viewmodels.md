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
  - `setBusyWhile(QFutureWatcherBase&)` for Qt Concurrent tasks (auto-clears loading on finish/cancel).

## Migration guidance
- Derive list-style ViewModels from `BaseViewModel` (e.g., `LibraryViewModel`, `SeriesDetailsViewModel`).
- Remove bespoke `isLoading`/`errorMessage` flags and use the base helpers.
- Route network callbacks through `setError(mapNetworkError(...))` and clear errors before new loads.
- Use `emitModelReset([&]{ ... })` when replacing model contents; use `dataChanged` for partial updates.
- Keep role definitions and business logic localized; only loading/error plumbing moves to the base.

## QML usage
- Bind spinners/toasts to `isLoading`/`hasError`/`errorMessage` without custom glue per ViewModel.
- After model reloads that affect focusable delegates, use `Qt.callLater` to restore focus per `docs/developer_notes.md`.
- Retry pattern (with shared `RetryButton.qml`):
```qml
Column {
    BusyIndicator { running: model.isLoading; visible: running }
    ToastNotification {
        visible: model.hasError
        text: model.errorMessage
    }
    RetryButton {
        target: model        // any BaseViewModel-derived instance
        visible: model.hasError
    }
}
```

## Current migrations
- `LibraryViewModel` now inherits `BaseViewModel`; loading/error wiring is centralized while pagination, caching, and signals (`loadComplete`, `loadMoreComplete`) remain intact.
- `SeriesDetailsViewModel` inherits `BaseViewModel` for standardized loading/error properties; nested `SeasonsModel`/`EpisodesModel` keep their role-focused implementations.

## Async work and setBusyWhile
Use `setBusyWhile` to keep loading flags accurate for Qt Concurrent tasks:
```cpp
class ArtworkViewModel : public BaseViewModel {
    Q_OBJECT
public:
    Q_INVOKABLE void reload() override {
        watcher.setFuture(QtConcurrent::run([=]{
            // expensive work here
            return 42;
        }));
        setBusyWhile(watcher);
    }
private:
    QFutureWatcher<int> watcher;
};
```


## MovieDetailsViewModel
ViewModel for the series-style movie details screen.

### Purpose
Acts as the data source for `MovieDetailsView.qml`. It aggregates Jellyfin movie metadata, playback progress, cast, similar-library recommendations, and external ratings from MDBList (IMDb, TMDB, Rotten Tomatoes, etc.) plus AniList when available.

### Public API
- **Key Properties**:
  - `title`, `overview`, `logoUrl`, `posterUrl`, `backdropUrl`: UI-ready metadata and artwork.
  - `officialRating`, `runtimeTicks`, `communityRating`, `premiereDate`, `genres`: movie metadata used by the hero chips and synopsis area.
  - `isWatched`, `playbackPositionTicks`: sync with Jellyfin `UserData` and drive `Play` vs `Resume` UI.
  - `people`: cast/crew entries used by the lower cast shelf.
  - `similarItems`, `similarItemsLoading`: recommendation shelf data and loading state for similar titles.
  - `mdbListRatings`: `QVariantMap` containing normalized external ratings, including AniList when merged in.
- **Methods**:
  - `loadMovieDetails(QString movieId)`: hydrates from cache when possible, then refreshes metadata and similar items from the library service.
  - `reload()`: retries the active movie load for error/retry flows.
  - `markAsWatched()` / `markAsUnwatched()`: sync played status to the server.
  - `clear(bool preserveArtwork)`: resets state. Set `preserveArtwork=true` to keep artwork visible during transitions.
- **Signals**:
  - `movieLoaded()`: metadata is ready for display.
  - `loadError(QString error)`: standardized error notification.
  - `peopleChanged()`, `similarItemsChanged()`, `similarItemsLoadingChanged()`: update the cast and recommendation shelves.

### Integration
- **Services**: Retrieves `LibraryService` via `ServiceLocator` for movie details, user-data updates, and similar-item requests.
- **Network**: Uses an internal `QNetworkAccessManager` for MDBList and Wikidata/AniList lookups.
- **Config**: Reads `ConfigManager` for API keys and cache storage paths.

### Lifecycle & Side Effects
- **Caching**: Uses separate in-memory and disk caches for movie details and similar items. Disk cache resides in `<configDir>/cache/movies/` with a 1-hour TTL.
- **Rating Fetching**: Automatically triggers external rating lookups after metadata loads when provider IDs (IMDb/TMDB) are present.
- **Recommendation Fetching**: Requests similar items after details load, while still allowing cached empty recommendation results to short-circuit redundant network fetches.
- **State Cleanup**: `clear()` should be called when navigating away from the view to prevent stale shelves or metadata from lingering.
