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

## Incremental boundaries

The remaining issue #75 work should preserve these directions:

1. Extract provider request factories so MediaBrowser and Silo headers are created only inside their adapters.
2. Introduce a shared HTTP transport for execution, cancellation, redaction, retry policy, error mapping, and provider-directed `401` recovery.
3. Place Jellyfin authentication behind `IProviderAuthenticator` while retaining the current QML-facing façade.
4. Namespace persisted preferences and caches by `connectionId` before enabling multiple live providers.
5. Add Silo authentication only after the native adapter can own access/refresh/profile token behavior.

QML must not select protocol routes, construct provider headers, or read credentials.

## Verification

Connection and credential migration tests cover:

- v27 Jellyfin metadata migration
- token exclusion from `app.json`
- retained rollback metadata until secure migration completes
- verified legacy keychain copy and cleanup
- failed-copy recovery
- multiple persisted connections and active selection

Use the blessed project checks:

```fish
./scripts/dev-build.sh
nix flake check
nix build
```
