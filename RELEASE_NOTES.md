## What's Changed in v0.8.0

Bloom 0.8.0 adds Spaces, which combine several servers into one Home, library,
and search. It also adds native Silo support, a new playback
overlay, a redesigned interface with a floating navigation rail, and audio
output control. On Windows, Scoop is now the recommended install method and the
in-app updater works again.

### Before you upgrade

- **Windows installer users must install 0.8.0 by hand once.** The 0.7.1
  installer registered Bloom where the in-app updater could not find it, so
  0.7.1 can tell you 0.8.0 is out but cannot install it. Download
  `Bloom-Setup-0.8.0.exe` from this release and run it over your existing
  install. Later updates install from inside Bloom.
- **Exclusive Mode is on by default on Windows, including for existing
  installs.** During playback, Bloom takes the audio device for itself. Windows
  volume and other apps, including Bloom's own menu sounds, are silent on that
  device until playback stops. Turn it off in Settings > Playback >
  Exclusive Mode if you need shared audio.
- Settings and saved sign-ins migrate automatically on first launch. Your
  current server becomes a space with one source.
- The login screen no longer asks for a server type. Bloom detects Jellyfin or
  Silo itself. An address without `http://` or `https://` tries HTTPS first and
  falls back to HTTP only if HTTPS gets no response.
- Native Silo support requires Silo's stable API v2 (Silo 1.0). The older
  alpha API is not supported.

### Spaces

- A space groups server connections. A space with several sources merges their
  Home rows, libraries, and search, and each item shows a badge for the server
  it came from. Opening, playing, or marking an item always uses the server that
  owns it.
- Switch spaces from the navigation rail. Manage them in Settings > Spaces.
- Choose libraries per source. A library can be hidden from browsing and,
  separately, left out of search. Libraries added to a server later show up
  automatically.
- Every source in a merged space now renews its own expired sign-in instead of
  showing as unavailable. Only a refresh the server refuses asks you to sign in
  again. Network errors keep your session.

### Playback

- New playback overlay with one control bar: title, scrub bar, transport
  buttons, a seek preview filmstrip, and a chapter rail. Subtitles, audio,
  quality, and output settings moved into one pop-out menu.
- Switch between versions of a movie or episode during playback, and jump to
  the previous or next episode from the overlay.
- New Preferred Version setting for items with several versions: highest
  quality (default), smallest file, or ask every time.
- Seek previews work without server trickplay images. Bloom generates frames
  from the stream for the part of the filmstrip you are looking at, after you
  stop scrubbing. It generates at most one frame per second and pauses while
  playback buffers, so it does not compete with slow or debrid-backed storage.
  Settings > Playback has separate toggles for server trickplay and local
  previews.
- Subtitle delay, adjustable in 1 ms steps from the subtitle menu. Bloom
  remembers it per movie or per season, like your subtitle choice.
- Volume keys show a small volume indicator instead of opening the full
  controls.
- Up Next has a new layout with backdrop, logo, countdown ring, and the season's
  episode row. It stays on screen until the next episode is actually playing,
  so the page underneath no longer flashes during the handoff.
- Dolby Vision Profile 7 files with a full enhancement layer (FEL) now play
  with the enhancement layer applied. Every release package ships an mpv build
  that supports it. If you point Bloom at your own mpv, that build needs FEL
  support too. The experimental embedded Linux player cannot apply it.
- Playback now rides out short stream drops. Bloom keeps buffering while the
  connection recovers instead of ending playback, which helps with debrid and
  proxy servers.
- Dolby Vision files with an HDR10 or HLG base layer are detected as HDR more
  reliably, so automatic HDR switching turns on for them.
- The mouse cursor hides after 2.5 seconds without movement during playback.
- Intro and credits skipping uses TheIntroDB's v4 API and matches episodes more
  accurately.

### Audio

- Choose the audio output device in Settings and during playback. The list
  shows only devices the current audio driver can use. If a saved device is
  unplugged, playback uses the system default until it comes back, and Bloom
  keeps your choice.
- Audio Passthrough has three modes: Auto, Disabled, and Custom. Custom lets you
  pick Dolby Digital, Dolby Digital Plus, TrueHD, DTS, and DTS-HD.
- Bloom now picks the audio output format before playback starts, based on the
  source and what the output device supports, and checks the result once mpv
  opens the device.

### Interface

- New floating navigation rail that expands over the page without moving it,
  and full-screen backdrops on every page.
- With Settings > Home > Sync page backdrop on (the default), the Home hero's
  backdrop fills the whole page. With it off, the artwork sits in a card. Hero
  focus shows as light along its edges instead of a focus ring.
- Layout size now follows the window size and no longer applies Windows display
  scaling a second time. Library grids add columns on ultrawide screens.
- On Windows, embedded video sizes correctly on displays with scaling above
  100%.
- Sign-out asks for confirmation, and Exit moved into the power menu.
- Keyboard and controller focus stays on the right item after dialogs close and
  while pages load. Search results no longer steal focus.
- Episode pages show the season poster again, and quality and HDR badges stay
  readable on bright artwork.

### Servers and sign-in

- Native Silo support covers sign-in, profiles, browsing, artwork, playback
  with progress reporting, theme songs, and trickplay. It uses Silo's own API,
  so the server's Jellyfin compatibility listener can stay off.
- Jellyfin requests follow the Jellyfin 12 API contract and avoid obsolete
  endpoints.
- Jellyfin streams request up to 1 Gbps, so local network playback no longer
  transcodes because of a low bitrate cap.
- Restored Jellyfin sessions are checked against the signed-in user.
- Saving and deleting credentials in the system keyring happens in the
  background, so sign-in and sign-out no longer freeze the interface.

### Updates and installation

- Scoop is the recommended way to install on Windows. Use
  `scoop install bloom/bloom` for stable releases or
  `scoop install bloom/bloom-dev` for builds from the latest `main` commit.
  See the README for setup. Bloom tells Scoop users which `scoop update`
  command to run instead of offering a separate download.
- The in-app updater installs updates again for installer-based Windows
  installs. Bloom closes, the installer replaces every file, and Bloom starts
  again on its own.
- Update information is signed, and Bloom verifies the signature and the
  installer checksum before installing anything.
- Switching update channels no longer offers an older version, for example a
  dev build switching to an older stable release.

### Reliability

- Settings files are written atomically and keep a backup of a damaged file
  instead of overwriting it.
- Network requests have deadlines and bounded retries.
- mpv startup, shutdown, HDR, and refresh-rate switching no longer block the
  interface.
- Artwork loads once per image and size even when several views request it.

**Full changelog:** [v0.7.1...v0.8.0](https://github.com/crowquillx/Bloom/compare/v0.7.1...v0.8.0)
