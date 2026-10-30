# Updates

## Overview

- Bloom supports first-party update checks on all builds.
- Automatic in-app install is Windows-only and only for registered NSIS
  installs. Portable Windows and non-Windows builds remain notify-only and
  expose direct download links.
- Scoop installs (recommended on Windows) are notify-only. Bloom detects the
  Scoop layout (`<root>/apps/<app>/<version>/` with Scoop's `install.json` and
  `manifest.json`), reports `scoop update <app>` as its status text, and
  hides the installer/ZIP links so users do not create a second, unmanaged
  copy. `UpdateService.packageManaged` exposes this to QML.
- `Bloom::Updater` owns manifest trust, update transport, installer download,
  verification, cleanup, and Windows launch policy.

## Channels

- `stable`: latest non-prerelease tagged release.
- `dev`: rolling `dev-latest` prerelease built from `main`. It is never marked
  as GitHub's latest stable release and contains only the newest successful
  main build's assets.

Automatic startup checks remain opt-in through
`settings.updates.auto_check_enabled`. Manual checks are always available.

Within a build's own channel, stable compares versions and dev compares build
IDs. When the selected channel differs from the build channel, Bloom offers the
other channel's build only when its base version is not older than the current
one, so a dev build never downgrades to an older stable release.

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
checksum, and rollout fields.

Published manifests also repeat the payload fields, unsigned, at the top level
of the envelope. Pre-0.8 clients (0.7.x and older) parse only plain top-level
JSON and would otherwise never see an update. Current clients ignore every
top-level field except `schema`, `key_id`, `payload`, and `signature`, so the
unsigned copy cannot influence them. Drop it once 0.7.x clients no longer need
a path to a signed-manifest build. Encoding the exact signed payload avoids
cross-language JSON canonicalization ambiguity. CI signs with the private key
stored in `BLOOM_UPDATE_MANIFEST_SIGNING_KEY_B64`; only the corresponding
public key is embedded in Bloom.

The release manifest generator accepts only HTTPS asset URLs under
`github.com/crowquillx/Bloom/releases/download/`, validates every field, signs
the compact sorted payload with OpenSSL Ed25519, and atomically replaces the
output manifest. Contributor and release environments require OpenSSL 3.0 or
newer because older versions do not support this Ed25519 `pkeyutl -rawin` flow.

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
sanitization and registered-install eligibility checks remain mandatory. On
Windows, Bloom holds a read handle that denies write and delete sharing from
Authenticode verification until `ShellExecuteExW` has created the elevated
process, preventing the verified pathname from being swapped before launch.

## Install handoff

Install eligibility requires the NSIS uninstall key (`DisplayName` `Bloom`,
`InstallLocation`, `UninstallString`) to match the running executable's
directory, with `Uninstall.exe` present. The installer writes this key to the
64-bit registry view (`SetRegView 64`). Installers before 0.8 wrote it to the
32-bit `WOW6432Node` view, which the 64-bit app did not read, so those installs
were always notify-only. Bloom now reads both views, and the current installer
removes the stale 32-bit key.

The verified installer runs as `/S /RELAUNCH /D=<registered dir>`, and Bloom
quits right after the elevated process starts, even during playback. The
installer then:

1. waits up to 60 seconds for `Bloom.exe` to become writable, then aborts with
   exit code 2 before copying anything (silent NSIS would otherwise skip locked
   files and leave a mixed install);
2. removes the previous version's files as listed in its
   `bloom-install-manifest.txt` (only Bloom's own, never the whole folder; see
   [Windows build](WINDOWS-BUILD.md#installer)), installs, and removes the legacy 32-bit uninstall key;
3. with `/RELAUNCH`, starts Bloom through `explorer.exe` so the new process
   runs unelevated as the signed-in user.

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
partial cleanup, obsolete-file cleanup, incremental checksum verification,
atomic finalization, ignored unsigned legacy fields, Scoop layout detection,
and cross-channel downgrade suppression. The generator is independently
checked with OpenSSL, including that its legacy fields match the signed
payload.

The registry view, installer wait, and relaunch are Windows-only and need
manual validation on a registered NSIS install: from an install made by an
older installer (key under `WOW6432Node`), Bloom offers **Download and
Install**; after UAC, Bloom exits, every installed file is replaced, and Bloom
restarts unelevated; only the 64-bit uninstall key remains.

Before enabling strict Authenticode publisher policy, manually validate on a
registered Windows NSIS installation that a correctly signed installer is
accepted, a different publisher is rejected, UAC appears only after all
verification, and the silent installer targets the registered directory.
Non-Windows builds require no automatic-install validation because they remain
notify-only.
