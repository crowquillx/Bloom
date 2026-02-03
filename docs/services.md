Service Locator & Initialization

Overview
- The repo uses a ServiceLocator pattern to centralize and manage application-wide services. Services are typically `QObject` derived and registered at app startup.
- Thread-safety: ServiceLocator accessors are protected with a mutex.

Key services
- `ConfigManager` — Configuration, QML bindings, session persistence.
- `PlayerProcessManager` — Manages mpv process & IPC.
- `AuthenticationService` — Login, logout, session persistence, token validation; owns shared `QNetworkAccessManager`.
- `LibraryService` — Library views/items, series details, search, image/theme-song URLs.
- `PlaybackService` — Playback reporting, stream info, media segments, trickplay URLs and info.
- `PlayerController` — Orchestrates playback using `PlayerProcessManager` and services; owns `TrickplayProcessor`.
- `TrickplayProcessor` — Uses `AuthenticationService` (network) + `PlaybackService` (tile URLs) to build trickplay binaries.
- `ThemeSongManager` — Uses `LibraryService` for theme songs plus `ConfigManager` and `PlayerController` for state.
- `InputModeManager` — Pointer/keyboard detection and cursor management.

Initialization order (recommended)
1. ConfigManager — loads configs and path info.
2. PlayerProcessManager — mpv startup behavior and lifecycle.
3. AuthenticationService — handles session management; provides shared `QNetworkAccessManager`.
4. LibraryService — depends on AuthenticationService.
5. PlaybackService — depends on AuthenticationService.
6. PlayerController — depends on ProcessManager, ConfigManager, TrackPreferencesManager, DisplayManager, PlaybackService, LibraryService, AuthenticationService.
7. TrickplayProcessor — created by PlayerController; uses AuthenticationService + PlaybackService.
8. ThemeSongManager — depends on LibraryService, ConfigManager, PlayerController.
9. InputModeManager — depends on QGuiApplication.
10. ViewModels — e.g., LibraryViewModel, SeriesDetailsViewModel (depend on LibraryService).

Usage
- Register services at main startup using `ServiceLocator::registerService<T>(&instance);`.
- Use `ServiceLocator::get<T>()` to fetch global service pointers; `tryGet<T>()` returns nullptr if not registered.
- Document dependencies for each service to ensure registration order.

Adding a new service
1. Derive from QObject.
2. Register the service in `main.cpp` after its dependencies are registered.
3. Update `AGENTS.md` (or `docs/services.md`) to document the dependency and purpose.

Note: Avoid tightly coupling multiple services. Prefer small, single-purpose services and keep interface clear.

Series/Season detail caching (December 2025)
- `LibraryService::getSeriesDetails` and `getItems` honor `ETag/If-None-Match` and `If-Modified-Since` when `useCacheValidation=true` (SeriesDetailsViewModel uses this for series + season episode lists). 304 responses are surfaced via `seriesDetailsNotModified` / `itemsNotModified`.
- Series details and season/episode lists are cached in-memory (≈5 min TTL) and on disk under `cache/series` (≈1 hour TTL). Cache is served immediately (SWR) and revalidated in the background; stale data stays visible until refresh completes.
- Prefetch: when navigating seasons, the view model prefetches the next two seasons' episodes (bounded, cancelable) to reduce focus-to-episodes latency.