# Updates

## Overview

- Bloom supports first-party update checks on all builds.
- Automatic in-app install is Windows-only and only for registered NSIS
  installs. Portable Windows and non-Windows builds remain notify-only and
  expose direct download links.
- `Bloom::Updater` owns manifest trust, update transport, installer download,
  verification, cleanup, and Windows launch policy.

## Channels

- `stable`: latest non-prerelease tagged release.
- `dev`: rolling `dev-latest` prerelease built from `main`. It is never marked
  as GitHub's latest stable release and contains only the newest successful
  main build's assets.

Automatic startup checks remain opt-in through
`settings.updates.auto_check_enabled`. Manual checks are always available.

## Signed manifest format

Bloom reads `stable.json` or `dev.json` from the `update-manifests` branch.
Unsigned and schema-1 manifests are rejected. Schema 2 is an envelope around
the exact UTF-8 JSON bytes covered by an Ed25519 signature:

```json
{
  "schema": 2,
  "key_id": "bloom-update-2026-08",
  "payload": "<base64 UTF-8 JSON>",
  "signature": "<base64 64-byte Ed25519 signature>"
}
```

The decoded payload retains the existing channel/version/build, notes, asset,
checksum, and rollout fields. Encoding the exact signed payload avoids
cross-language JSON canonicalization ambiguity. CI signs with the private key
stored in `BLOOM_UPDATE_MANIFEST_SIGNING_KEY_B64`; only the corresponding
public key is embedded in Bloom.

The release manifest generator accepts only HTTPS asset URLs under
`github.com/crowquillx/Bloom/releases/download/`, validates every field, signs
the compact sorted payload with OpenSSL Ed25519, and atomically replaces the
output manifest.

Development build IDs continue to use `YYYYMMDDhhmmss-<short-sha>` so rolling
builds compare lexicographically. Stable notes come from `RELEASE_NOTES.md`;
development notes identify the main commit and build metadata.

## Network and storage policy

Manifest fetches:

- allow only HTTPS `raw.githubusercontent.com/crowquillx/Bloom/...` URLs;
- validate every redirect and allow at most three;
- have a 15-second total deadline and 10-second transfer timeout;
- reject envelopes over 1 MiB, decoded payloads over 768 KiB, and truncated
  `Content-Length` responses.

Installer downloads:

- start only at the official Bloom GitHub release path;
- follow at most five redirects to the explicit GitHub release-asset host
  allowlist;
- have a 10-minute total deadline and 30-second transfer timeout;
- reject files over 1 GiB and reject truncated responses;
- stream into `QSaveFile` while incrementally calculating SHA-256, then commit
  atomically only after the signed-manifest checksum matches;
- cancel and remove partial temporary files on every error, timeout,
  cancellation, or destruction;
- remove obsolete files from `%LOCALAPPDATA%/Bloom/updates/<channel>/` before a
  new download.

The verified installer is never launched from a partial path. Filename
sanitization and registered-install eligibility checks remain mandatory.

## Authenticode

Windows calls `WinVerifyTrust` before elevation. A present but invalid
Authenticode signature is rejected. Official Bloom installers are currently
checksum-and-manifest authenticated but may be unsigned, so unsigned files
remain eligible by default to preserve the existing release path.

When a Windows code-signing certificate is available, configure
`BLOOM_UPDATE_AUTHENTICODE_PUBLISHER` to its exact display name. That turns the
policy into fail-closed publisher verification: the installer must have a
valid Authenticode signature from that publisher before elevation.

## Key rotation and recovery

Routine rotation must preserve an authenticated bridge:

1. Add the new public key and key ID beside the old key in Bloom.
2. Release that multi-key build while manifests are still signed by the old
   key.
3. After the supported client population has received it, switch CI to the new
   private key and key ID.
4. Retain the old public key for the documented support/migration window, then
   remove it in a later release.

Never overwrite the signing secret and public key in one release. If the
private key is lost or suspected compromised, stop manifest publication and
revoke the Actions secret. Existing clients cannot safely learn an unrelated
replacement key from the compromised channel. Publish a manual recovery build
through the GitHub releases page, provide independent checksums/provenance, and
embed the replacement public key there before signed automatic updates resume.

## Validation

Automated tests cover valid and tampered signatures, unknown keys, insecure or
foreign origins, redirect rejection, timeouts, oversize/truncated responses,
partial cleanup, obsolete-file cleanup, incremental checksum verification, and
atomic finalization. The generator is independently checked with OpenSSL.

Before enabling strict Authenticode publisher policy, manually validate on a
registered Windows NSIS installation that a correctly signed installer is
accepted, a different publisher is rejected, UAC appears only after all
verification, and the silent installer targets the registered directory.
Non-Windows builds require no automatic-install validation because they remain
notify-only.
