# Provider and connection architecture

Bloom is migrating from one implicit Jellyfin session to provider-neutral, connection-scoped services. The current implementation keeps the existing QML-facing authentication, library, and playback APIs stable while the boundaries are introduced incrementally under issue #75.

Native Silo is implemented behind the provider adapter boundary and is released as **experimental native Silo support**. It is not first-class support: the pinned native contract and platform/runtime gates still leave optional capabilities unavailable. Use the support labels defined in [provider compatibility](provider-compatibility.md).

The native Silo path is selected only after deterministic health detection (`GET /api/v1/health` with `status: "ok"`). A compatibility connection remains a separate MediaBrowser/Jellyfin surface and never silently upgrades to native.

## Server connections

`ServerConnection` (`src/providers/ServerConnection.*`) is the persisted identity for one server/account/profile combination:

- `connectionId`: Bloom-generated stable namespace for server-owned IDs and preferences
- `providerKind`: `jellyfin` or `silo`
- `protocolMode`: `native` or `compatibility`
- `baseUrl`: normalized server URL without a trailing slash
- `serverId` / `serverName`: provider-reported server identity when available
- `accountId` / `profileId`: provider identities for the selected account and profile
- `username` / `displayName`: non-secret session metadata
- `capabilities`: provider-advertised feature flags
- `credentialReference`: opaque reference used by `CredentialStore`

No access, refresh, profile, PIN, API, or signed URL token may be serialized in a connection.

Config schema version 28 stores connections under `settings.connections`:

```json
{
  "settings": {
    "connections": {
      "version": 1,
      "active": "8ecb86b8-8cec-4c69-929f-ac4cbb0e76a1",
      "items": [
        {
          "id": "8ecb86b8-8cec-4c69-929f-ac4cbb0e76a1",
          "provider": "jellyfin",
          "protocol_mode": "native",
          "base_url": "https://media.example.test",
          "server_id": "",
          "server_name": "",
          "account_id": "user-id",
          "profile_id": "user-id",
          "username": "alice",
          "display_name": "Alice",
          "capabilities": {},
          "credential_reference": "connection:8ecb86b8-8cec-4c69-929f-ac4cbb0e76a1"
        }
      ]
    }
  }
}
```

`ConfigManager` provides connection persistence and active-connection access. Connection removal is intentionally deferred until an account-session service can delete credentials before dropping metadata. Its Jellyfin session methods remain temporary compatibility façades while existing callers move to provider-neutral services.

## Bloom profiles

Bloom profiles (`src/profiles/*`, documented in [`profiles.md`](profiles.md)) are Bloom-owned workspaces, not provider household profiles. A membership references a `connectionId` only. The same physical server may appear multiple times via distinct connections (different accounts/users); memberships are never keyed by `serverId` or `baseUrl`. Within one Bloom profile, at most one membership per `connectionId` so `MediaRef{connectionId,itemId}` remains unambiguous. Membership identity lives in request/source context (`BloomProfileRequestContext`), not in `MediaRef` or caches. Config v30 persists `settings.bloom_profiles`; migration seeds one default `single` profile from the active (or sole) connection without changing `settings.connections.active`.

## Credential storage and migration

`CredentialStore` (`src/security/CredentialStore.*`) centralizes credential key generation over the platform `ISecretStore` implementation.

- Service: `Bloom/Connections`
- Account: `<credentialReference>/<access-token|refresh-token|profile-token>`

The account key contains no server URL, username, remote item ID, or secret. Access, refresh, and profile tokens are separate entries so providers can rotate them independently.

Config v27 migration creates a connection from a valid `settings.jellyfin` record but temporarily retains that record as rollback metadata. At session restoration:

1. Bloom reads a provider-neutral access-token entry from `CredentialStore`.
2. If absent, `CredentialStore` reads the legacy `Bloom/Jellyfin` key for the old `serverUrl|username|deviceId` account.
3. `CredentialStore` may consume a legacy plaintext token from the old config only as a one-time migration fallback when neither secure entry exists; it writes the token to secure storage and reads it back.
4. Only after verification does Bloom delete the old keychain entry and remove `settings.jellyfin`.

If a write, verification, or deletion fails, the old config/keychain entry remains as rollback material and restoration continues with the legacy token when possible. Current credentials are never stored in `app.json`: access, refresh, and profile tokens are retained only by `CredentialStore`, and the plaintext fallback is removed after a verified secure-store copy. Logout removes provider-neutral credentials and any remaining legacy Jellyfin entry using the restored username, fixing cleanup after a restored session.

Provider-neutral credential keys do not include the rotating device ID. Device rotation first resolves any pending legacy entry and aborts if the credential cannot be preserved.

## Connection-scoped state

Config v29 stores server-owned preferences under `settings.connection_state.scopes.<connectionId>`. MPV library/series assignments and per-library startup-buffering overrides migrate from their former global maps into the active connection scope, or the sole saved connection when signed out. Ambiguous legacy state and new settings written before first activation are retained under `_pending` and adopted by the next activated connection; test-only values use `_local` only when no `ConfigManager` participates.

`track_preferences.json` schema v4 groups season/movie preferences by connection scope and applies the same pending-activation rule. Library SQLite caches and series/movie detail caches use SHA-256 connection-scope directory keys, while static in-memory cache keys include the connection ID and are cleared when the active scope changes. `LibraryViewModel` reopens its cache and clears displayed account state when the active connection changes. Logout cancels transport operations and clears library validation, remote-session, and detail-view state without deleting another connection's persisted preferences.

## Canonical model boundary

Bloom-owned media contracts live in `src/models/MediaModels.*` and are documented in [`canonical-models.md`](canonical-models.md). `MediaRef` always combines connection and remote item identity; canonical times use milliseconds; `ArtworkRef` cache identities contain no credentials; and `PlaybackDescriptor` carries a finalized provider-neutral stream request. Temporary `QVariantMap` projections use Bloom-defined camelCase fields.

Provider conversion belongs inside provider adapters. `JellyfinModelMapper` is the Jellyfin DTO boundary and owns item-list envelopes, filter facets, ancestor identity, chapter extraction, tick-to-millisecond conversion, playback sources, streams, trickplay metadata, Intro Skipper segments, and remote sessions. Shared catalog, playback, and session projections expose camelCase fields and milliseconds only while stable QML-facing façade names remain available.

## Request, authentication, and transport boundaries

`SiloProviderAdapter` exposes `SiloAuthenticator`, `SiloRequestFactory`, `SiloCatalogProvider`, `SiloArtworkProvider`, and `SiloPlaybackProvider` for the native `/api/v1` surface. `SiloAuthenticator` owns login, refresh, `/auth/me`, and profile-PIN request/response wire contracts; the adapter's route mapping and model mapper supply provider discovery, caller logout, auth-session list/revoke, and profile listing. Access, refresh, and profile tokens are supplied from `CredentialStore` and never from QML or connection metadata.

`IProviderAdapter` bundles the provider implementation consumed by stable application façades. `JellyfinProviderAdapter` exposes the Jellyfin authenticator, request factory, catalog, artwork, and playback providers while identifying its provider/protocol mode; login, restore, browse, artwork, playback, and remote-session traffic therefore share one selected provider boundary without changing QML APIs.

`IProviderRequestFactory` owns provider-specific URL and authorization-header construction. `JellyfinRequestFactory` is the only production source of the `MediaBrowser` header. `SiloRequestFactory` emits bearer authorization plus `X-Silo-Client`, `X-Silo-Client-Version`, `X-Silo-Device-Id`, `X-Silo-Device-Name`, `X-Silo-Device-Platform`, and selected `X-Profile-Id`/`X-Profile-Token` headers on same-origin native requests. Both factories redact token-bearing query parameters before URLs reach logs.

`IProviderAuthenticator` owns provider login payloads, response parsing, and validation routes. `JellyfinAuthenticator` implements the existing AuthenticateByName flow while `SiloAuthenticator` validates access/refresh pairs, account identity, profile tokens, and typed error envelopes. `AuthenticationService` remains the stable QML-facing session façade.

`HttpTransport` owns the shared `QNetworkAccessManager` and centralizes retry/backoff, cancellation, error mapping, redacted request logging, and unauthorized policy. Catalog and remote-session `401` responses expire immediately; playback reads can defer expiry until playback stops. Canceled work is never retried. `SessionService` uses the shared transport instead of a private network manager.

`LibraryService`, `PlaybackService`, and `SessionService` remain stable application façades for catalog/playback/remote-session signals while requests flow through the selected provider adapter, request factory, and transport. `SiloCatalogProvider` maps `content_id` catalog identity separately from `file_id` playback/version identity; `SiloArtworkProvider` treats signed URLs as opaque and refetches the owning resource after expiry; `SiloPlaybackProvider` maps the pinned legacy envelope into `PlaybackDescriptor` and provider-native reports. QML must not select protocol routes, construct provider headers, or read credentials. Native Silo protocol v3 remains explicitly unavailable for Bloom/mpv because the pinned capability is `media3_only`, not an mpv contract.

## Verification

Connection, credential, request-factory, transport, and provider-boundary tests cover:

- `ConnectionPersistenceTest`: v27 Jellyfin metadata migration/rollback preservation, token exclusion from new `app.json` writes, verified legacy keychain/config copy, failed-copy recovery, multiple connections, Silo URL-scoped identity, and connection isolation.
- `SiloPlaybackProviderTest`: legacy start/progress/stop/audio-switch and recovery mapping, opaque signed URLs, multipart file identity, and conditional HLS/transcode behavior.
- `ProviderCatalogTest` and `SiloCatalogServiceTest`: provider-neutral catalog identity, snapshots, paging/filter limitations, marker/chapter mapping, state mutations, and unsupported-capability reporting.
- `ArtworkRefreshTest`: provider artwork expiry refetch and token-free cache identity.
- Provider/model and controller tests cover connection-scoped caches, MPV assignments, buffering overrides, track preferences, and provider-aware recovery.

The machine/source-grounded compatibility matrix and pinned revision are maintained in [provider compatibility](provider-compatibility.md). Native protocol v3/media3-only, trickplay, playable theme songs, and other unavailable capabilities must remain labeled unavailable rather than supported.

Use the blessed project checks:

```fish
./scripts/dev-build.sh
nix flake check
nix build
```
