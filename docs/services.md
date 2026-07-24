Service Locator & Initialization

Overview
- The repo uses a ServiceLocator pattern to centralize and manage application-wide services. Services are typically `QObject` derived and registered at app startup.
- Thread-safety: ServiceLocator accessors are protected with a mutex.

Key services
- `ConfigManager` — Configuration, QML bindings, and provider-neutral connection metadata persistence.
- `BloomProfileRepository` — Bloom workspace profiles/memberships and request-context generation; depends on ConfigManager raw `settings.bloom_profiles` accessors. See [profiles](profiles.md).
- `CredentialStore` — Provider-neutral platform-keychain names, access/refresh/profile token storage, and verified legacy Jellyfin credential migration.
- `TrackPreferencesManager` — Versioned, connection-scoped persistence for explicit season/movie audio and subtitle preferences.
- `IPlayerBackend` — Playback backend abstraction registered in `ServiceLocator`.
- `ExternalMpvBackend` — External mpv process/IPC backend adapter (primary rollback path on Linux/non-Windows).
- `PlayerProcessManager` — Manages external mpv process & IPC (used by `ExternalMpvBackend`).
- `HttpTransport` — Owns the shared `QNetworkAccessManager` and central retry, cancellation, error, redaction, and unauthorized-response policy.
- `IProviderAdapter` / `JellyfinProviderAdapter` — Selected provider implementation bundle consumed by stable application façades.
- `IProviderRequestFactory` / `JellyfinRequestFactory` — Provider-owned URL and authorization-header construction.
- `IProviderAuthenticator` / `JellyfinAuthenticator` — Provider-owned login payload, response parsing, and validation routes.
- `IPlaybackProvider` / `JellyfinPlaybackProvider` — Provider-owned finalization of canonical playback descriptors and serialization of canonical playback reports into provider endpoints and payloads.
- `AuthenticationService` — Stable QML façade for login, logout, session persistence, and token validation; delegates provider wire details and HTTP execution.
- `LibraryService` — Library views/items, series details, search, reusable chapter metadata, image/theme-song URLs.
- `PlaybackService` — Millisecond-based playback reporting transport, stream info, media segments, trickplay URLs and info.
- `MediaSegmentProviderService` — Fetches and normalizes external intro/recap/credits/preview markers from configured providers; used by `PlaybackService` after server segment lookup.
- `SeerrService` — Seerr/Jellyseerr search integration, request-option loading, request submission, and similar-title provider endpoints.
- `PlayerController` — Orchestrates playback using `IPlayerBackend` and services; owns `TrickplayProcessor`.
- `TrickplayProcessor` — Uses `AuthenticationService` (network) + `PlaybackService` (tile URLs) to build trickplay binaries.
- `ThemeSongManager` — Uses `LibraryService` for theme songs plus `ConfigManager` and `PlayerController` for state.
- `InputModeManager` — Pointer/keyboard detection and cursor management.
- `InputBindingManager` — Configurable keyboard/controller action bindings and SDL-backed gamepad dispatch.
- `SystemPowerController` — QML-facing app/system power actions for the home power menu; saves config before quit/restart/shutdown requests.
- `UpdateService` — Fetches per-channel update manifests, determines whether an update is available, gates the startup-only popup, and exposes update actions/state to QML.

Initialization order (recommended)
1. ConfigManager — loads configs, path info, and active `ServerConnection` metadata.
1.1. BloomProfileRepository — loads/repairs `settings.bloom_profiles` after ConfigManager.
2. IPlayerBackend — created by `PlayerBackendFactory` (`win-libmpv` on Windows; platform-selected backend elsewhere).
3. HttpTransport and `JellyfinProviderAdapter` — own shared HTTP execution and the selected provider wire implementation.
4. AuthenticationService — stable session façade; depends on `CredentialStore`, `HttpTransport`, and `IProviderAdapter`.
4.1. LibraryService — depends on AuthenticationService and uses its shared transport.
5. InputModeManager — depends on QGuiApplication.
5.1. InputBindingManager — depends on QGuiApplication + ConfigManager.
6. MediaSegmentProviderService — depends on AuthenticationService + ConfigManager.
7. PlaybackService — depends on AuthenticationService + ConfigManager + MediaSegmentProviderService.
8. SeerrService — depends on AuthenticationService + ConfigManager.
9. TrackPreferencesManager — depends on ConfigManager for the active connection scope and loads versioned track preference state.
10. PlayerController — depends on IPlayerBackend, ConfigManager, TrackPreferencesManager, DisplayManager, PlaybackService, LibraryService, AuthenticationService.
11. TrickplayProcessor — created by PlayerController; uses AuthenticationService + PlaybackService.
12. ThemeSongManager — depends on LibraryService, ConfigManager, PlayerController.
13. ViewModels — e.g., LibraryViewModel, SeriesDetailsViewModel (depend on LibraryService).
14. SidebarSettings — UI preference persistence for the shell/sidebar state.
15. UiSoundController — depends on ConfigManager for UI sound settings.
16. SystemPowerController — depends on ConfigManager for pre-action config saves and is exposed to QML.
17. SessionManager — shared session reporting/coordination service.
18. UpdateService — depends on ConfigManager and PlayerController, and owns the current update provider/applier wiring.

Usage
- Register services at main startup using `ServiceLocator::registerService<T>(&instance);`.
- Use `ServiceLocator::get<T>()` to fetch global service pointers; `tryGet<T>()` returns nullptr if not registered.
- Document dependencies for each service to ensure registration order.

Adding a new service
1. Derive from QObject.
2. Register the service in `main.cpp` after its dependencies are registered.
3. Update `AGENTS.md` (or `docs/services.md`) to document the dependency and purpose.

Backend selection notes
- Platform default backend path is selected by `PlayerBackendFactory` (Linux prefers `linux-libmpv-opengl` when runtime-supported; Windows always uses `win-libmpv`; others default `external-mpv-ipc`).
- `PlayerBackendFactory` supports `createByName(...)`, env override via `BLOOM_PLAYER_BACKEND`, and config preference via `settings.playback.player_backend`.
- Selection precedence:
  - Windows: forced `win-libmpv` (override values are ignored).
  - Non-Windows: env override -> config preference -> platform default.
- Unknown backend names are logged and resolved safely (Windows forces `win-libmpv`; non-Windows fall back to external backend).
- Integration check: `VisualRegressionTest` asserts `IPlayerBackend` is registered in `ServiceLocator` after `ApplicationInitializer::registerServices()`.

Note: Avoid tightly coupling multiple services. Prefer small, single-purpose services and keep interface clear.

Series/Season detail caching (December 2025)
- `LibraryService::getSeriesDetails` and `getItems` honor `ETag/If-None-Match` and `If-Modified-Since` when `useCacheValidation=true` (SeriesDetailsViewModel uses this for series + season episode lists). 304 responses are surfaced via `seriesDetailsNotModified` / `itemsNotModified`.
- `LibraryService::getChapters(itemId)` fetches Jellyfin item `Chapters`, requests chapter image metadata, normalizes missing titles to `Chapter N`, and emits typed chapter data reusable outside playback. `getCachedChapterThumbnailUrl(...)` mirrors the Jellyfin client pattern of requesting `/Items/{itemId}/Images/Chapter/{chapterIndex}` with the chapter image tag when present; the UI keeps a neutral placeholder visible until the image provider reports a ready frame.
- Series details and season/episode lists are cached in-memory (≈5 min TTL) and on disk under `cache/series` (≈1 hour TTL). Cache is served immediately (SWR) and revalidated in the background; stale data stays visible until refresh completes.
- Prefetch: when navigating seasons, the view model prefetches the next two seasons' episodes (bounded, cancelable) to reduce focus-to-episodes latency.
