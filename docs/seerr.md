Seerr Integration

Overview
- Bloom supports Seerr/Jellyseerr integration for discovery-style search and request submission.
- Current scope:
  - Search screen shows Seerr results in a dedicated section.
  - Selecting a Seerr result opens a request dialog.
  - Request dialog supports server/profile/root-folder selection and TV season selection.
  - Series details can render a Seerr recommendations shelf when the current series has a TMDB id and Seerr is configured.
  - The post-playback no-next screen can append Seerr TV recommendations to local Jellyfin recommendations.
- Future scope:
  - Similar-title provider hook is now rendered in series details; movie details can reuse the same service later.

Configuration
- Settings path: `Settings > Third Party`
- Required fields:
  - `Seerr URL` (base server URL, example: `http://localhost:5055`)
  - `Seerr API Key`
- Auth model:
  - API key only (`X-Api-Key` request header).

Runtime behavior
- Search requests are split:
  - Jellyfin results render first.
  - Seerr search runs independently and appears later when available.
- Seerr cards include source/status metadata and open the Seerr request dialog.
- On the post-playback no-next screen, Seerr similar-title failures are ignored and the screen remains focused on Back to Series/Home actions.

API endpoints used
- Validation:
  - `GET /api/v1/auth/me`
- Search:
  - `GET /api/v1/search?query=...&page=...`
- Request options:
  - Movies: `GET /api/v1/service/radarr`, `GET /api/v1/service/radarr/{id}`
  - TV: `GET /api/v1/service/sonarr`, `GET /api/v1/service/sonarr/{id}`, `GET /api/v1/tv/{tvId}`
- Request submit:
  - `POST /api/v1/request`

Request payload mapping
- Always sent:
  - `mediaType`: `movie` or `tv`
  - `mediaId`: TMDB ID
- TV:
  - `seasons`: `"all"` or an explicit season-number array
- Advanced options (when selected):
  - `serverId`
  - `profileId`
  - `rootFolder` (path string)

Code map
- `src/network/SeerrService.{h,cpp}`: API integration, result mapping, request preparation/submission.
- `src/ui/SearchScreen.qml`: independent Seerr search flow + Seerr section rendering.
- `src/ui/SeerrRequestDialog.qml`: keyboard-first request dialog.
- `src/ui/SearchResultCard.qml`: Seerr-aware card rendering (TMDB posters/source-status badge).
- `src/ui/SeriesDetailsView.qml`: Seerr recommendations shelf for series details, reusing the request dialog.
- `src/viewmodels/UpNextRecommendationsViewModel.{h,cpp}` and `src/ui/UpNextScreen.qml`: no-next recommendation shelf that merges Jellyfin similar series with Seerr TV results.

Similar Titles
- `SeerrService::getSimilar(mediaType, tmdbId, page)` and `similarResultsLoaded(...)` drive recommendations in series details and the no-next Up Next screen.
- Endpoints:
  - `GET /api/v1/movie/{movieId}/similar`
  - `GET /api/v1/tv/{tvId}/similar`
