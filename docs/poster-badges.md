# Poster badges

Silo profiles choose which badges their poster cards show (resolution, HDR,
audio, ratings, and so on) and in which corner. Bloom reads those choices and
draws the badges in its own theme. Preset shapes and accent colours from Silo's
web app are not carried over. Jellyfin has no server-side badge preferences,
so Jellyfin posters show none.

## Data

Silo catalog cards (`CatalogItem`, used by catalog pages, queries, search, and
Home and library sections) carry `overlay_summary`, the technical summary of
the item's best file. `SiloModelMapper::mediaItem` maps it to the canonical
`mediaSummary` map (`resolution`, `hdr`, `audio`, `audioChannels`,
`videoCodec`, `container`, `aspectRatio`, `releaseType`, `edition`,
`multiAudio`, `multiSub`). It leaves out empty and false values. Cards also
supply `advisoryAge`, `originalLanguage` and `showStatus`, plus the existing
`ratings`, `officialRating`, `productionYear`, `runtimeMinutes`, `studios`
and `networks`.

The badges need no extra item requests: the summary arrives with every card
Bloom already loads.

## Preferences

`PosterBadgeService` (QML `PosterBadges`) holds one `Bloom::PosterBadgeLayout`
per Silo connection of the active space: the live connection of a single space,
or each enabled member of a merged space. For each connection it reads:

- `GET /api/v2/settings/values/effective?keys=ui.card_overlays&keys=ui.card_overlays_enabled`,
  scoped to the selected profile.
- `GET /api/v2/settings/overlay-config`, the server-wide enabled flag and the
  administrator's defaults document.

The reads go through `MergedCatalogService::readConnection`, which uses the
member's own credentials and token renewal. They never change a source's
state, so a failed read raises no "unavailable source" toast. A failed
settings read keeps the previous layout. Without the overlay config, the
shipped defaults apply.

The service reads again when the space, its members, or the saved connections
change, after a sign-in or connection activation, and when a pooled member
session becomes ready. Home's content refresh also calls `PosterBadges.refresh()`.
That re-reads connections whose last attempt is older than 60 seconds, so
choices made in Silo's web app reach Bloom on the next Home refresh. Layouts
live in memory only.

`SiloModelMapper::posterBadgeLayout` mirrors the pinned web client
(`web/src/lib/overlays`):

- **Enabled.** An explicit `ui.card_overlays_enabled` wins. Otherwise the
  server-wide `enabled` flag applies, and overlays are on when the server sends
  none.
- **Document.** A null `ui.card_overlays` means no profile choice, so the
  admin `defaults` document applies. Either may be an object or the legacy JSON
  string. Version 2 documents nest `items` and may rank ids in `order`.
  Version 1 documents are a flat id map on the classic preset. Unreadable
  values read as no preference.
- **Items.** Each registry entry keeps its default enabled state and corner
  unless the document overrides it. Invalid positions keep the default.
- **Order.** Ranked ids come first. The rest keep registry order.
- **Suppression.** An enabled `resolution_hdr` hides the standalone
  `resolution` and `hdr` badges.
- **Icons.** A per-item `showIcon` wins. Otherwise the `vibrant` and `pill`
  presets show icons and the others do not. This is the only preset effect
  Bloom keeps.
- **Dropped ids.** `request_status` (watchlist titles outside the library) and
  `imdb_top_250`/`rt_certified_fresh` (no API data yet) are ignored.

| Silo id | Bloom kind | Shown as | Default |
| --- | --- | --- | --- |
| `resolution` | `resolution` | `4K`, `8K`, `1080p` | top-left, on |
| `hdr` | `hdr` | value as sent (`DV HDR10`) | top-left, on |
| `resolution_hdr` | `resolutionHdr` | `4K DV`, `1080p HDR` | top-left, off |
| `audio` | `audio` | `TrueHD`, `Atmos` | top-left, on |
| `audio_channels` | `audioChannels` | `5.1` | top-left, off |
| `video_codec` | `videoCodec` | `H.265` | top-left, off |
| `container` | `container` | `MKV` | bottom-left, off |
| `aspect_ratio` | `aspectRatio` | `2.39:1` | bottom-right, off |
| `release_type` | `releaseType` | `REMUX` | bottom-left, on |
| `edition` | `edition` | `Director's Cut` | bottom-left, off |
| `multi_audio` | `multiAudio` | `Multi-Audio` | bottom-right, off |
| `multi_sub` | `multiSub` | `CC` | bottom-right, off |
| `rating_imdb` | `ratingImdb` | IMDb logo + `8.4` | top-right, off |
| `rating_tmdb` | `ratingTmdb` | TMDB logo + `8.4` | top-right, off |
| `rating_rt` | `ratingRtCritic` | Rotten Tomatoes logo + `93%` | top-right, off |
| `rating_rt_audience` | `ratingRtAudience` | Popcornmeter logo + `88%` | top-right, off |
| `content_rating` | `contentRating` | `PG-13` | bottom-right, off |
| `advisory_age` | `advisoryAge` | `13+` | bottom-right, off |
| `year` | `year` | `2024` | bottom-left, off |
| `runtime` | `runtime` | `2h 5m` | bottom-left, off |
| `original_language` | `originalLanguage` | `Japanese` | bottom-left, off |
| `studio` | `studio` | first studio | bottom-right, off |
| `network` | `network` | first network | bottom-right, off |
| `show_status` | `showStatus` | `Returning`, `Ended`, `Cancelled`, `Upcoming` | top-right, off |

Ratings of zero count as missing. Silo omits a Rotten Tomatoes score when an
administrator hides that source, so the badge does not render.

## Resolution and drawing

`Bloom::resolvePosterBadges` (`src/models/PosterBadges.*`, Qt Core only) turns
a layout and a canonical item into badge maps with `corner`, `kind`, `text`,
`icon`, and for ratings `ratingSource` and `score`. Badges whose value the item
lacks are skipped before the limit of three per corner applies.
`PosterBadges.badgesFor(item)` picks the layout of the item's `connectionId`.

`PosterBadgeOverlay.qml` draws the result on Home Recently Added posters, the
library grid, Search result cards (and Up Next suggestions that reuse them),
and the Movie and Series "More like this" shelves. Badges use the same
overlay surface, border, radius, and caption font as `SourceBadge`. Rating
badges use Bloom's rating logos; other badges use a Material Symbols glyph
when the layout asks for icons. A corner may use the poster width minus up to
half reserved for the opposite corner on the same edge, depending on how wide
that corner's badges are. Long labels elide. Hosts reserve space for other
poster chrome: the top-right stack starts below `UnwatchedBadge`, and on Search
cards the bottom-left stack sits above the merged-space `SourceBadge`.

## Setting

Settings > Display > Silo Poster Badges (`serverPosterBadgesEnabled`, default
on; see [config](config.md)) turns the badges off without forgetting layouts.
The row appears only when a saved connection is a Silo server
(`PosterBadges.hasBadgeConnections`).

## Unwatched counts

Silo list cards have no unwatched episode count: `CatalogItemUserState` holds
only `played`, `is_favorite`, `in_watchlist` and `not_interested`. Seasons and
item details carry the full watch rollup. Series cards from Silo therefore
show the watched check when every episode is played and nothing otherwise.
`UnwatchedBadge` takes an `applicable` flag for item-type gating. Call sites
must not override `visible`, which would draw an empty pill when there is no
count.

## Tests

`PosterBadgesTest` covers the Silo item mapping, preference parsing (defaults,
enabled precedence, version 1 and 2 documents, admin defaults, order,
suppression, icons, dropped ids), badge formatting and per-corner limits, and
the Silo and Jellyfin catalog requests. `WorkspaceRoutingTest` covers reads
through a pooled Silo member with its own token, items from a Jellyfin member,
the setting, and a failed read that leaves the source state alone.
`ConfigManagerThemeTest` covers the setting's default and persistence.
