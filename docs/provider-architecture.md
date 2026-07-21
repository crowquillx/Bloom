# Provider and connection architecture

Bloom is migrating from one implicit Jellyfin session to provider-neutral, connection-scoped services. The current implementation keeps the existing QML-facing authentication, library, and playback APIs stable while the boundaries are introduced incrementally under issue #75.

Native Silo is not enabled by the connection model alone. Until the native authentication, catalog, and playback adapters pass their release gates, use the support labels defined in [provider compatibility](provider-compatibility.md).

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

## Credential storage and migration

`CredentialStore` (`src/security/CredentialStore.*`) centralizes credential key generation over the platform `ISecretStore` implementation.

- Service: `Bloom/Connections`
- Account: `<credentialReference>/<access-token|refresh-token|profile-token>`

The account key contains no server URL, username, remote item ID, or secret. Access, refresh, and profile tokens are separate entries so providers can rotate them independently.

Config v27 migration creates a connection from a valid `settings.jellyfin` record but temporarily retains that record as rollback metadata. At session restoration:

1. Bloom reads a provider-neutral access-token entry.
2. If absent, Bloom reads the legacy `Bloom/Jellyfin` key for the old `serverUrl|username|deviceId` account.
3. Bloom writes and reads back the provider-neutral entry.
4. Only after verification does Bloom delete the old keychain entry and remove `settings.jellyfin`.

If a write, verification, or deletion fails, the old config/keychain entry remains and restoration continues with the legacy token when possible. A legacy token found directly in `app.json` is a final fallback only when neither a provider-neutral nor legacy keychain credential exists; it never overwrites an existing secure credential and is removed only after a verified secure-store copy. Logout removes provider-neutral credentials and any remaining legacy Jellyfin entry using the restored username, fixing cleanup after a restored session.

Provider-neutral credential keys do not include the rotating device ID. Device rotation first resolves any pending legacy entry and aborts if the credential cannot be preserved.

## Connection-scoped state

Config v29 stores server-owned preferences under `settings.connection_state.scopes.<connectionId>`. MPV library/series assignments and per-library startup-buffering overrides migrate from their former global maps into the active connection scope, or the sole saved connection when signed out. Ambiguous legacy state and new settings written before first activation are retained under `_pending` and adopted by the next activated connection; test-only values use `_local` only when no `ConfigManager` participates.

`track_preferences.json` schema v4 groups season/movie preferences by connection scope and applies the same pending-activation rule. Library SQLite caches and series/movie detail caches use SHA-256 connection-scope directory keys, while static in-memory cache keys include the connection ID and are cleared when the active scope changes. `LibraryViewModel` reopens its cache and clears displayed account state when the active connection changes. Logout cancels transport operations and clears library validation, remote-session, and detail-view state without deleting another connection's persisted preferences.

## Canonical model boundary

Bloom-owned media contracts live in `src/models/MediaModels.*` and are documented in [`canonical-models.md`](canonical-models.md). `MediaRef` always combines connection and remote item identity; canonical times use milliseconds; `ArtworkRef` cache identities contain no credentials; and `PlaybackDescriptor` carries a finalized provider-neutral stream request. Temporary `QVariantMap` projections use Bloom-defined camelCase fields.

Provider conversion belongs inside provider adapters. `JellyfinModelMapper` is the initial Jellyfin DTO boundary and owns tick-to-millisecond conversion for mapped items and chapters. Existing catalog, artwork, and player consumers migrate to these contracts in focused slices while stable QML-facing façade names remain available.

## Request, authentication, and transport boundaries

`IProviderAdapter` bundles the provider implementation consumed by stable application façades. `JellyfinProviderAdapter` exposes the Jellyfin authenticator, request factory, and playback provider while identifying its provider/protocol mode; login, restore, browse, playback, and remote-session traffic therefore share one selected provider boundary without changing QML APIs.

`IProviderRequestFactory` owns provider-specific URL and authorization-header construction. `JellyfinRequestFactory` is the only production source of the `MediaBrowser` header and also redacts token-bearing query parameters before URLs reach logs.

`IProviderAuthenticator` owns provider login payloads, response parsing, and validation routes. `JellyfinAuthenticator` implements the existing AuthenticateByName flow while `AuthenticationService` remains the stable QML-facing session façade.

`HttpTransport` owns the shared `QNetworkAccessManager` and centralizes retry/backoff, cancellation, error mapping, redacted request logging, and unauthorized policy. Catalog and remote-session `401` responses expire immediately; playback reads can defer expiry until playback stops. Canceled work is never retried. `SessionService` uses the shared transport instead of a private network manager.

`LibraryService`, `PlaybackService`, and `SessionService` remain the stable Jellyfin catalog/playback/remote-session adapters for their existing QML signals while all of their HTTP requests flow through the selected provider request factory and transport. Canonical JSON/model conversion continues with the provider model work without changing those façade contracts. QML must not select protocol routes, construct provider headers, or read credentials. Native Silo authentication remains deferred until its adapter can own access, refresh, and profile-token behavior.

## Verification

Connection, credential, request-factory, and transport tests cover:

- v27 Jellyfin metadata migration
- token exclusion from `app.json`
- retained rollback metadata until secure migration completes
- verified legacy keychain copy and cleanup
- failed-copy recovery
- multiple persisted connections and active selection
- Jellyfin header/authentication wire ownership and URL redaction
- transient retry, non-retryable cancellation/client failures, and unauthorized policy
- connection isolation for MPV assignments, buffering overrides, track preferences, and caches

Use the blessed project checks:

```fish
./scripts/dev-build.sh
nix flake check
nix build
```
