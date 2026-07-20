# Bloom HTPC Client - Security

## Credential Storage

Bloom uses platform-native secure storage for server-connection access, refresh, and profile tokens, including Jellyfin access tokens:

- **Linux**: `libsecret` (GNOME Keyring / KWallet via Secret Service API)
- **Windows**: Windows Credential Manager

Current Bloom releases never write tokens to plain-text configuration. The configuration file `~/.config/Bloom/app.json` stores only non-sensitive connection metadata such as provider, server URL, account/profile IDs, and display names. Older installations may retain a legacy token temporarily until Bloom verifies that it has been copied to secure storage.

## Storage Schema

`CredentialStore` owns the provider-neutral key schema:

- **Service**: `"Bloom/Connections"`
- **Account**: `"<credential_reference>/<credential_kind>"`
- **Credential kinds**: `access-token`, `refresh-token`, and `profile-token`

The credential reference is an opaque connection-scoped identifier such as `connection:<uuid>`; it does not contain the server URL, username, remote item ID, or secret.

Legacy Jellyfin releases used service `"Bloom/Jellyfin"` with account `"<server_url>|<username>|<device_id>"`. These entries are read only for migration and cleanup.

## Migration from Legacy Config

When upgrading, Bloom creates provider-neutral connection metadata and migrates any legacy Jellyfin credential on session restoration. Existing provider-neutral and legacy keychain credentials take precedence over a plaintext config fallback, so stale rollback metadata cannot overwrite a newer secure credential. Bloom writes and reads back the provider-neutral credential before removing either the old keychain entry or legacy `settings.jellyfin` metadata. If secure storage is unavailable or verification fails, the legacy data remains recoverable and migration is retried later.

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
