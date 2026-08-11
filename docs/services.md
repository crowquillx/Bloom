Service Locator & Initialization

Overview
- The repo uses a ServiceLocator pattern to centralize and manage application-wide services. Services are typically `QObject` derived and registered at app startup.
- Thread-safety: ServiceLocator accessors are protected with a mutex.

Provider selection, connection identity, and credential migration are described in [provider architecture](provider-architecture.md); the machine/source-grounded Silo matrix is in [provider compatibility](provider-compatibility.md).

Key services
- `ConfigManager` — Configuration, QML bindings, and provider-neutral connection metadata persistence.
- `BloomProfileRepository` — Bloom workspace profiles/memberships and request-context generation; depends on ConfigManager raw `settings.bloom_profiles` accessors. See [profiles](profiles.md).
- `CredentialStore` — Provider-neutral platform-keychain names, access/refresh/profile token storage, and verified legacy Jellyfin credential migration.
- `TrackPreferencesManager` — Versioned, connection-scoped persistence for explicit season/movie audio and subtitle preferences.
- `IPlayerBackend` — Playback backend abstraction registered in `ServiceLocator`.
- `ExternalMpvBackend` — External mpv process/IPC backend adapter (primary rollback path on Linux/non-Windows).
- `PlayerProcessManager` — Manages external mpv process & IPC (used by `ExternalMpvBackend`).
- `HttpTransport` — Owns the shared `QNetworkAccessManager`, bounded request/auth-recovery deadlines, explicit retry-safety policy, cancellation, separated transport/HTTP errors, redaction, and unauthorized-response handling.
- `IProviderAdapter` — Selected provider bundle consumed by stable application façades; detection keeps native Silo and MediaBrowser compatibility connections distinct.
- `JellyfinProviderAdapter` — Jellyfin authenticator, request factory, catalog, artwork, and playback boundaries for the MediaBrowser surface.
- `SiloProviderAdapter` — Native Silo `/api/v1` bundle; composes `SiloAuthenticator`, `SiloRequestFactory`, `SiloCatalogProvider`, `SiloArtworkProvider`, and `SiloPlaybackProvider`.
- `IProviderRequestFactory` / `JellyfinRequestFactory` / `SiloRequestFactory` — Provider-owned URL and authorization-header construction. Silo emits bearer, device, and selected profile headers; signed media/artwork URLs remain opaque.
- `IProviderAuthenticator` / `JellyfinAuthenticator` / `SiloAuthenticator` — Provider-owned login, refresh, profile-PIN, error-envelope, and validation wire contracts. `SiloProviderAdapter` supplies profile-list and auth-session route mapping/model parsing; tokens are read from `CredentialStore`.
- `ICatalogProvider` / `JellyfinCatalogProvider` / `SiloCatalogProvider` — Provider-owned catalog request and response mapping; Silo keeps `content_id` (catalog identity) separate from `file_id` (version/playback identity).
- `IArtworkProvider` / `JellyfinArtworkProvider` / `SiloArtworkProvider` — Provider-owned artwork URL resolution and expiry refetch; cache identities never include credentials.
- `IPlaybackProvider` / `JellyfinPlaybackProvider` / `SiloPlaybackProvider` — Provider-owned finalization of canonical playback descriptors and serialization of canonical playback reports into provider endpoints and payloads. Silo uses the pinned legacy envelope and omits protocol v3 for mpv.
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

## Initialization order (recommended)
1. ConfigManager — loads configs, path info, and active `ServerConnection` metadata.
1.1. BloomProfileRepository — loads/repairs `settings.bloom_profiles` after ConfigManager.
2. IPlayerBackend — created by `PlayerBackendFactory` (`win-libmpv` on Windows; platform-selected backend elsewhere).
3. HttpTransport and the selected `IProviderAdapter` — use deterministic health detection to choose `JellyfinProviderAdapter` for MediaBrowser or `SiloProviderAdapter` for native `/api/v1`; never infer Silo from compatibility `/System/Info`.
4. AuthenticationService — stable session façade; depends on `CredentialStore`, `HttpTransport`, and the selected provider adapter.
4.1. LibraryService — depends on AuthenticationService and uses the selected catalog/artwork provider through shared transport.
5. InputModeManager — depends on QGuiApplication.
5.1. InputBindingManager — depends on QGuiApplication + ConfigManager.
6. MediaSegmentProviderService — depends on AuthenticationService + ConfigManager.
7. PlaybackService — depends on AuthenticationService + ConfigManager + MediaSegmentProviderService.
8. SeerrService — depends on AuthenticationService + ConfigManager.
9. TrackPreferencesManager — depends on ConfigManager for the active connection scope and loads versioned track preference state.
10. PlayerController — depends on IPlayerBackend, ConfigManager, TrackPreferencesManager, DisplayManager, PlaybackService, LibraryService, AuthenticationService; provider-aware recovery refreshes through the selected `IPlaybackProvider` rather than assuming Jellyfin endpoints.
11. TrickplayProcessor — created by PlayerController; uses AuthenticationService + PlaybackService. Native Silo has no trickplay contract at the pinned revision, so the capability remains unavailable.
12. ThemeSongManager — depends on LibraryService, ConfigManager, PlayerController.
13. ViewModels — e.g., LibraryViewModel, SeriesDetailsViewModel (depend on LibraryService).
14. SidebarSettings — UI preference persistence for the shell/sidebar state.
15. UiSoundController — depends on ConfigManager for UI sound settings.
16. SystemPowerController — depends on ConfigManager for pre-action config saves and is exposed to QML.
17. SessionManager — shared provider-aware session reporting/coordination service.
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
- Platform default backend path is selected by `PlayerBackendFactory` (Wayland defaults to `external-mpv-ipc`; non-Wayland Linux prefers `linux-libmpv-opengl` when runtime-supported; Windows always uses `win-libmpv`; other platforms default to `external-mpv-ipc`).
- `PlayerBackendFactory` supports `createByName(...)`, env override via `BLOOM_PLAYER_BACKEND`, and config preference via `settings.playback.player_backend`.
- Selection precedence:
  - Windows: forced `win-libmpv` (override values are ignored).
  - Non-Windows: env override -> config preference -> platform default.
- Unknown backend names are logged and resolved safely (Windows forces `win-libmpv`; non-Windows fall back to external backend).
- Integration check: `VisualRegressionTest` asserts `IPlayerBackend` is registered in `ServiceLocator` after `ApplicationInitializer::registerServices()`.

Note: Avoid tightly coupling multiple services. Prefer small, single-purpose services and keep interface clear.

Series/Season detail caching (December 2025)
- `LibraryService::getSeriesDetails` and `getItems` honor `ETag/If-None-Match` and `If-Modified-Since` when `useCacheValidation=true` (SeriesDetailsViewModel uses this for series + season episode lists). 304 responses are surfaced through the connection-scoped `canonicalSeriesDetailsNotModified` / `canonicalItemsNotModifiedForConnection` signals.
- `LibraryService::getChapters(itemId)` fetches Jellyfin item `Chapters`, requests chapter image metadata, normalizes missing titles to `Chapter N`, and emits typed chapter data reusable outside playback. `getCachedChapterThumbnailUrl(...)` mirrors the Jellyfin client pattern of requesting `/Items/{itemId}/Images/Chapter/{chapterIndex}` with the chapter image tag when present; the UI keeps a neutral placeholder visible until the image provider reports a ready frame.
- Series details and season/episode lists are cached in-memory (≈5 min TTL) and on disk under `cache/series` (≈1 hour TTL). Cache is served immediately (SWR) and revalidated in the background; stale data stays visible until refresh completes.
- Prefetch: when navigating seasons, the view model prefetches the next two seasons' episodes (bounded, cancelable) to reduce focus-to-episodes latency.
