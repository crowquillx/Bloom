# Bloom HTPC Client - Security

## Credential Storage

Bloom uses platform-native secure storage for server-connection access, refresh, and profile tokens:

- **Linux**: `libsecret` (GNOME Keyring / KWallet via Secret Service API)
- **Windows**: Windows Credential Manager

Current Bloom writes never persist tokens in `app.json`. The configuration file `~/.config/Bloom/app.json` stores only non-sensitive, provider-neutral connection metadata such as provider, protocol mode, normalized server URL, account/profile IDs, capabilities, display names, and an opaque credential reference. Older installations may retain legacy migration material temporarily until `CredentialStore` verifies a secure copy.

## Storage Schema

`CredentialStore` owns the provider-neutral key schema:

- **Service**: `"Bloom/Connections"`
- **Account**: `"<credential_reference>/<credential_kind>"`
- **Credential kinds**: `access-token`, `refresh-token`, and `profile-token`

The credential reference is an opaque connection-scoped identifier such as `connection:<uuid>`; it does not contain the server URL, username, remote item ID, or secret.

Legacy Jellyfin releases used service `"Bloom/Jellyfin"` with account `"<server_url>|<username>|<device_id>"`. These entries are read only for migration and cleanup.

## Migration from Legacy Config

When upgrading, Bloom creates provider-neutral connection metadata and lets `CredentialStore` migrate any legacy Jellyfin credential during session restoration. Provider-neutral and legacy keychain credentials take precedence. If both secure entries are absent, `CredentialStore` may consume a legacy plaintext token once, write it to the provider-neutral secure-store entry, and read it back for verification; the plaintext value is not retained as current configuration. Bloom removes the old keychain entry and legacy `settings.jellyfin` metadata only after verification. If secure storage is unavailable or verification fails, the old keychain/config material remains recoverable as rollback data and migration is retried later. The connection migration sequence is also summarized in [provider architecture](provider-architecture.md).

Logout removes all provider-neutral credential kinds for the active connection and any matching legacy Jellyfin entry. It does not delete another connection's credentials or metadata.

### Native Silo credential boundaries

Native Silo login, refresh, `/auth/me`, and profile PIN request/response wire contracts are owned by `SiloAuthenticator`; adapter route mapping/model parsing supplies provider discovery, caller logout, auth-session operations, and profile listing. Bearer access/refresh tokens and the optional profile token are stored only through `CredentialStore`; `SiloRequestFactory` adds device/profile headers to same-origin native requests and redacts token-bearing query parameters in logs. Signed media/artwork URLs are opaque fetch locations, not credentials to copy into config or reconstruct.

Native and compatibility connections are separate. A Silo compatibility listener uses the MediaBrowser/Jellyfin credential flow and profile suffix limitations; the native listener uses `/api/v1` and must pass health detection. A `401` is handled by the selected provider's refresh-once policy, then provider-aware session expiry/logout; Bloom never retries native requests by switching to a Jellyfin endpoint.

**Requirements**:
- On Linux, a Secret Service-compatible keyring must be available (GNOME Keyring or KWallet).
- If the keyring is locked, you may be prompted to unlock it during migration or app startup.

## Troubleshooting

### Linux: Keyring Locked

If your keyring is locked, Bloom cannot access stored credentials. You will need to unlock it manually or configure your desktop environment to unlock it at login.

**Symptom**: App logs show "Failed to retrieve secret" or migration fails.

**Solution**:
1. Install and run `seahorse` (GNOME Keyring manager):
   ```bash
   sudo pacman -S seahorse  # Arch
   sudo apt install seahorse  # Debian/Ubuntu
   ```
2. Open Seahorse → Login keyring → Unlock it.
3. Optionally, configure your keyring to unlock at login.

### Native Silo login or profile failures

- Confirm the URL is the native listener and that `GET /api/v1/health` returns `{"status":"ok"}`. A refusal or `404` on `/api/v1` usually means the native listener is disabled or the connection points at the compatibility port.
- Native profile selection requires `X-Profile-Id`; a PIN-protected profile additionally requires a successful `/api/v1/profiles/{id}/verify-pin` response before Bloom stores/uses `X-Profile-Token`.
- Do not paste bearer, refresh, profile, or signed URL values into `app.json` or bug reports. Inspect only redacted request logs and the provider-neutral connection metadata.
- If refresh fails or the session is revoked, sign out and sign in again for that connection. Bloom does not fall back to compatibility or Jellyfin routes.

### Viewing Stored Credentials

#### Linux (Seahorse)
1. Open Seahorse (Passwords and Keys).
2. Navigate to "Passwords" → "Login".
3. Search for "Bloom/Connections". During migration, an older "Bloom/Jellyfin" entry may also be present.

#### Windows (Credential Manager)
1. Open Control Panel → Credential Manager.
2. Click "Windows Credentials".
3. Look for entries starting with `Bloom:Bloom/Connections:`. During migration, an older `Bloom:Bloom/Jellyfin:` entry may also be present.

### Manual Cleanup

To remove stored credentials:
- **Linux**: Use Seahorse to delete the relevant "Bloom/Connections" entries and any remaining legacy "Bloom/Jellyfin" entry.
- **Windows**: Remove the corresponding credentials from Credential Manager.

Alternatively, use the "Sign Out" button in Bloom's settings, which deletes all credential kinds for the active connection and any matching legacy Jellyfin entry.

## Privacy & Security

- **No Logging**: Connection access, refresh, and profile tokens are never logged or written to disk outside the secure keychain by current config writes.
- **Encrypted at Rest**: Platform keychains encrypt credentials using OS-level encryption (e.g., user's login password on Linux, Windows Data Protection API on Windows).
- **Automatic Cleanup**: Signing out deletes the credential from the keychain immediately.

## Developer Notes

### Building with Secure Storage

Secure storage is enabled by default and requires:
- **Linux**: `libsecret-1-dev` (or `libsecret` on Arch)
- **Windows**: No additional dependencies (advapi32 is linked automatically)

To build:
```bash
nix build
```

Or locally:
```bash
nix flake check
```

### Disabling Secure Storage (Dev Only)

Not recommended for production. If you need to disable secure storage for testing:
1. Edit `src/security/SecretStoreFactory.cpp` to return `nullptr`.
2. Rebuild.

This will cause tokens to **not** be persisted between sessions (you must log in every time).
