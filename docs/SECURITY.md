# Bloom HTPC Client - Security

## Credential Storage

Bloom uses platform-native secure storage for Jellyfin access tokens:

- **Linux**: `libsecret` (GNOME Keyring / KWallet via Secret Service API)
- **Windows**: Windows Credential Manager

Tokens are **never** stored in plain text. The configuration file `~/.config/Bloom/app.json` stores only non-sensitive session metadata (server URL, user ID, username).

## Storage Schema

- **Service**: `"Bloom/Jellyfin"`
- **Account**: `"<server_url>|<username>"` (e.g., `"https://jellyfin.example.com|user123"`)
- **Secret**: Access token

## Migration from Legacy Config

If you are upgrading from a version that stored tokens in `app.json`, Bloom will automatically migrate your credentials to the secure keychain on first launch. The legacy token will be removed from the config file once migration is complete.

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
3. Search for "Bloom/Jellyfin".

#### Windows (Credential Manager)
1. Open Control Panel → Credential Manager.
2. Click "Windows Credentials".
3. Look for entries starting with `Bloom:Bloom/Jellyfin:`.

### Manual Cleanup

To remove stored credentials:
- **Linux**: Use Seahorse to delete the "Bloom/Jellyfin" entry.
- **Windows**: Remove the credential from Credential Manager.

Alternatively, use the "Sign Out" button in Bloom's settings, which will delete the credential.

## Privacy & Security

- **No Logging**: Access tokens are never logged or written to disk outside the secure keychain.
- **Encrypted at Rest**: Platform keychains encrypt credentials using OS-level encryption (e.g., user's login password on Linux, Windows Data Protection API on Windows).
- **Automatic Cleanup**: Signing out deletes the credential from the keychain immediately.

## Developer Notes

### Building with Secure Storage

Secure storage is enabled by default and requires:
- **Linux**: `libsecret-1-dev` (or `libsecret` on Arch)
- **Windows**: No additional dependencies (advapi32 is linked automatically)

To build:
```bash
./scripts/build-docker.sh  # Uses Docker with libsecret pre-installed
```

Or locally:
```bash
cmake -B build -G Ninja
ninja -C build
```

### Disabling Secure Storage (Dev Only)

Not recommended for production. If you need to disable secure storage for testing:
1. Edit `src/security/SecretStoreFactory.cpp` to return `nullptr`.
2. Rebuild.

This will cause tokens to **not** be persisted between sessions (you must log in every time).
