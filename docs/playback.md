Playback — mpv & Jellyfin Integration

Overview
- mpv runs as an external, top-level process (avoid --wid embedding or transparent Qt overlays). Prefer `vo=gpu-next` on supported platforms.
- Player is controlled via JSON IPC (Unix domain sockets on Linux; named pipes on Windows). The repo uses a PlayerProcessManager that launches mpv and exposes JSON IPC to the C++ controller.

Backend architecture (Milestone A)
- Playback now routes through `IPlayerBackend` (`src/player/backend/IPlayerBackend.h`).
- `PlayerController` depends on the backend interface (not directly on `PlayerProcessManager`).
- Current default backend is `ExternalMpvBackend` via `PlayerBackendFactory`.
- Active backend is logged at startup from `ApplicationInitializer`.
- Optional environment override for backend selection: `BLOOM_PLAYER_BACKEND`.
- Unknown backend names safely fall back to `external-mpv-ipc`.

Key components
- IPlayerBackend: playback backend contract used by `PlayerController`.
- ExternalMpvBackend: adapter that delegates to `PlayerProcessManager`.
- PlayerProcessManager: manages external mpv process lifetime, sockets/pipes, scripts and config dir. Observes `time-pos`, `duration`, `pause`, `aid`, and `sid` properties.
- PlayerController: state machine that handles play/pause/resume, listens for backend updates, manages track selection, and reports playback state to the Jellyfin server.
- JellyfinClient: handles API communication for reporting playback events, track selections, and sessions.
- TrackPreferencesManager: persists audio/subtitle track preferences to a separate JSON file for fast lookup and persistence across sessions.

Audio/Subtitle Track Selection
- Call `JellyfinClient::getPlaybackInfo(itemId)` to fetch `PlaybackInfoResponse` containing `MediaSources` with all available streams.
- Each `MediaSourceInfo` contains `mediaStreams` array with `MediaStreamInfo` objects describing video, audio, and subtitle tracks.
- The server provides `defaultAudioStreamIndex` and `defaultSubtitleStreamIndex` which reflect the user's preferences set on the Jellyfin server.
- Use `PlayerController::setSelectedAudioTrack(index)` and `setSelectedSubtitleTrack(index)` to change tracks during playback via mpv IPC (`aid`, `sid` properties).
- Track selections are persisted per-season for TV shows and per-movie for films (see Track Preference Persistence below).
- All playback reporting methods include `mediaSourceId`, `audioStreamIndex`, `subtitleStreamIndex`, and `playSessionId` for proper server sync.

Track Preference Persistence
- Track preferences are stored separately from the main config in `~/.config/Bloom/track_preferences.json`.
- This allows fast lookup without loading the main config, and keeps preferences organized separately.
- Preferences are loaded at startup and saved with a 1-second delay to batch multiple changes.

### TV Episodes (Per-Season)
- Preferences are stored by season ID, not series ID, because:
  - Different seasons may have different audio tracks available (e.g., added dubs in later seasons)
  - Subtitle indexing can vary between seasons
- When the user changes audio/subtitle track in `SeriesSeasonEpisodeView`, the preference is immediately saved.
- When navigating to a new episode in the same season, saved preferences are restored.
- Use `PlayerController.getLastAudioTrackForSeason(seasonId)` and `getLastSubtitleTrackForSeason(seasonId)` to retrieve.
- Use `PlayerController.saveAudioTrackPreference(seasonId, index)` and `saveSubtitleTrackPreference(seasonId, index)` to save.

### Movies (Per-Movie)
- Preferences are stored by movie ID for rewatches.
- When the user changes audio/subtitle track in `MovieDetailsView`, the preference is immediately saved.
- When returning to the same movie, saved preferences are restored instead of server defaults.
- Use `PlayerController.getLastAudioTrackForMovie(movieId)` and `getLastSubtitleTrackForMovie(movieId)` to retrieve.
- Use `PlayerController.saveMovieAudioTrackPreference(movieId, index)` and `saveMovieSubtitleTrackPreference(movieId, index)` to save.

### JSON Format
```json
{
  "seasonId1": { "audio": 1, "subtitle": 2 },
  "seasonId2": { "audio": 0, "subtitle": -1 },
  "movies": {
    "movieId1": { "audio": 1, "subtitle": 0 },
    "movieId2": { "audio": 0, "subtitle": -1 }
  }
}
```
- Season preferences are stored at the top level (for backwards compatibility)
- Movie preferences are stored under the "movies" key
- A subtitle index of -1 means "off" (no subtitles)

### Navigation Path Considerations
- **HomeScreen "Next Up"**: Episode data includes `ParentId` (seasonId). `SeriesSeasonEpisodeView.qml` uses `initialSeasonId` to load the correct season.
- **Library browsing**: `currentSeasonId` is set when the user selects a season.
- **Search → Series → Season → Episode**: Season ID is captured when entering the season view.
- **Post-playback next episode**: `showEpisodeDetails` extracts seasonId from episode data if not already set.

UI Components for Track Selection
- `TrackSelector.qml`: Reusable dropdown component for selecting audio/subtitle tracks with keyboard navigation.
- `MediaInfoPanel.qml`: Displays video info (resolution, codec, HDR) and contains audio/subtitle `TrackSelector` components.
- `SeriesSeasonEpisodeView.qml` and `MovieDetailsView.qml`: Integrate `MediaInfoPanel` to show track selection before playback.

Playback Reporting
- Report sequence: Start -> Periodic Progress -> Pause/Resume -> Stop.
- Use `POST /Sessions/Playing` when playback begins (includes track selection).
- Periodic progress: `POST /Sessions/Playing/Progress` (throttled, e.g., every 10s) to update resume position and track state.
- Stop: `POST /Sessions/Playing/Stopped` when playback ends.
- Mark Watched: when the playback position reaches a configurable threshold (default 90%), call the watch/mark endpoint.

Important design notes
- Avoid heavy UI work during playback or IPC handling on main thread.
- Use PlayerController as a single source for playback state; it should be responsible for reporting state and keeping user-level playback logic (resume, next episode/autoplay, track selection).
- PlayerProcessManager should accept an mpv config dir and script directory (e.g., `~/.config/Bloom/mpv`) and allow adding `extra_flags` via `app.json`.

mpv config hints
- Create `mpv.conf` and `input.conf` when you need custom behavior (e.g., enable vo, hardware settings) in `~/.config/Bloom/mpv/`.
- Always pass mpv arguments from `ConfigManager` so users can override behavior at runtime.

Jellyfin integration
- Key endpoints used frequently:
  - `/Users/{userId}/Items`
  - `/Shows/NextUp`
  - `/Items/{itemId}/PlaybackInfo` - Get media streams and track info
  - `/Sessions/Playing` - Start, progress, stop reporting with track selection
- The client must persist session information via `ConfigManager` and restore sessions during startup.
- On 401 responses, emit a sessionExpired event and trigger a logout/restore flow.

Security & privacy
- Session tokens should be persisted securely in `app.json` and cleared on logout.
- Avoid logging sensitive tokens; prefer obfuscated logging when necessary.

Troubleshooting
- Ensure proper owner and permissions for socket/pipes when using Unix sockets.
- On Wayland, avoid `--wid`; run mpv as a top-level window and use the compositor for overlays.
- Test playback flows with typical server and client device combinations to ensure direct play vs transcode logic works.
- If track selection doesn't sync, verify that `playSessionId` is being passed correctly in all reporting calls.
- If track preferences aren't being restored, check that `track_preferences.json` exists and contains the expected season/movie IDs.