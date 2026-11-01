# Bloom Style Guide

How Bloom screens look and behave, and how to build a new one that fits. The
tokens live in `src/ui/Theme.qml` ([theme.md](theme.md)), the scaling math in
[responsive.md](responsive.md), and QML and focus conventions in
[developer_notes.md](developer_notes.md). This page covers how those pieces fit
together.

## Principles

1. **Artwork is the interface.** Every page sits on a full-bleed stage. Where
   there is art, it fills the screen; chrome stays light and floats over it.
2. **One grammar, everywhere.** A hero on Home reads exactly like a hero on a
   detail page or Up Next. A shelf on Home is the same shelf on a detail page.
   A card is a `MediaCard`. Reuse before inventing.
3. **Built for the couch.** Everything is readable from across a room and
   reachable with four arrows, Enter, and Back. Hover is a bonus, never a
   requirement.
4. **Same size on every screen.** Sizes are design units on a fixed 16:9
   canvas, so an element covers the same fraction of a 1080p monitor, a 4K TV
   at 300%, or a 720p window. Wider screens show more items; they never make
   items bigger or smaller.
5. **Quiet until focused.** Rest states are calm (secondary text, hairline
   borders). Focus is unmistakable: lift, ring, shadow.

## The canvas

- Tokens are design units on a 16:9 canvas 1272 units tall (about 2261 wide).
  Write every size as a Theme token or `Math.round(N * Theme.layoutScale)`.
  Never clamp a scaled size to fixed pixels.
- **Gutter:** `Theme.pageMarginX` (96) on both sides. Copy, shelf headers, the
  first card of every shelf, and grid edges align to it.
- **Top:** the navigation bar floats over every page. Pages without a hero
  start content at `Theme.pageMarginTop` (= `navBarReservedHeight`). Hero
  pages draw under the bar and keep their copy below `navBarReservedHeight`.
- **Bottom:** leave `Theme.pageMarginBottom` after the last row.
- Fixed-size items (cards, buttons) keep their size on ultrawide screens and
  repeat more. Grids derive columns from `Theme.gridColumns` (7 at 16:9).

## Page anatomy

Every page is a `FocusScope` with a `StageBackdrop` behind its content and
the shell's `NavigationBar` floating above it.

```text
┌───────────────────────────────────────────────────────────────────────┐
│  ◆ bloom   (⌂ Home) ▭ Anime  ▭ Movies  ▭ Shows  ⌕ Search     ⚙ (T) ⏻ │  NavigationBar
│                                                                       │
│  RECENTLY ADDED                                         [ artwork ]  │
│  Title or logo                                                        │  Hero (hero pages)
│  2024 │ S1 E4 │ 24m                                                   │
│  Synopsis, three lines at most, about half the width…                 │
│  [ ▶ Resume S1 E4 ]  [ Details ]                                      │
│                                                                       │
│  Continue Watching                                                    │  Shelf
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌──                      │
│  └────────┘ └────────┘ └────────┘ └────────┘ └──                      │
└───────────────────────────────────────────────────────────────────────┘
```

### Archetypes

| Archetype | Pages | Stage | Header | Body |
|-----------|-------|-------|--------|------|
| **Hero** | Home, movie/series/season/episode details, Up Next | `mode: "hero"`, the subject's backdrop | Hero block | Shelves below the hero |
| **Browse** | Library grids, Search, Discover and its Browse pages, collections, person pages | `mode: "ambient"`, optionally the focused item's backdrop | `PageHeader` + toolbar | Poster grid or shelves |
| **Panel** | Settings, Active Sessions, account pages | `mode: "ambient"`, no art | `PageHeader` | Category list + content column of rows |

Only hero pages carry a hero. Browse and panel pages do not invent one; they
get their warmth from the stage.

## Navigation bar

`NavigationBar.qml` is owned by `Main.qml`. Left to right: brand, page tabs
(Home, Movies, Series, Discover when requests have a backend, Search),
and a utility cluster (update when available, Settings, space avatar, power). Keep the tab row short: a new destination
earns a tab only if it is a top-level way to browse, and libraries never get
their own tabs.

- **Movies and Series** stand for every library of that kind (Bloom supports
  only movie and series libraries). With one library the tab opens it. With
  several, a small menu drops down: "All Movies" (the libraries combined on
  one page), then each library. Merged spaces group the menu by source, with
  one "All …" per source. A tab with a menu shows a chevron.

- **Selection** (the current page) uses `selectionBackground` with an accent
  hairline. **Focus** uses `FocusRing`. A tab can show both.
- The bar recedes (slides up and fades) while a page reports
  `navigationRecessed: true`, typically once its content scrolls past the
  first row or the hero. It returns when focus or the pointer comes back.
- Keys: Up from the top of any page enters the bar on the current tab.
  Left/Right walk the entries, Enter activates, Down or Escape return to the
  page. In the library menu Up/Down move, Enter opens, Escape/Left/Right
  close it; Shift+Enter on a library starts reordering and Up/Down move it.

## Stage

`StageBackdrop` draws the page background:

- `mode: "hero"`: sharp art behind a leading-edge scrim (left side darkened
  for copy) and a floor gradient (the bottom settles into the theme
  background for shelves). Use `dim` to quiet the art as the page scrolls.
- `mode: "ambient"`: the art softened, blurred, and at
  `Theme.stageAmbientArtOpacity` behind dense content.
- With no art, two soft accent glows over the theme background. This is the
  default stage for panel pages and while art loads.

Never draw page backgrounds by hand. If a page needs a new scrim, add it to
`StageBackdrop` so every page gets it.

## Hero

The hero block is left-aligned at the gutter and never wider than
`Theme.heroCopyMaxWidth` (about half a 16:9 screen, fixed so ultrawide
screens don't stretch the reading measure). Top to bottom:

1. **Eyebrow** (optional): why this is here ("Recently Added", "Season 2").
   `fontSizeCaption`, heavy, uppercase, `eyebrowLetterSpacing`,
   `accentPrimary`.
2. **Title**: the provider logo through `ArtworkLogo`, at most
   `detailViewLogoHeight` tall. Without a logo, the title in `fontDisplay` at
   `fontSizeHero`, heavy, `displayLetterSpacing`, at most two lines.
3. **Subtitle** (optional): episode name, `fontSizeMedium`, strong.
4. **Metadata**: `MetadataLine` (year │ rating │ runtime │ ends at), then
   `MetadataChip`s: status first (a series' next airing, "S2 E5 airs
   Thu, Nov 6", through `DetailHero.statusBadges`), then quality (4K, Dolby
   Vision) and ratings (the media server's first, in its order).
5. **Synopsis**: `fontSizeBody`, `textPrimary` at 0.86 opacity, three lines
   at most, with Read More when the page supports it.
6. **Actions**: one primary `ActionButton` (`variant: "primary"`, height
   `buttonHeightHero`, label `fontSizeMedium`), then secondary buttons, then
   icon-only buttons. One primary per hero. Occasional actions (watched
   state, "Not Interested", the mpv profile) go in the trailing "More
   options" `BloomMenu`; an action the provider does not offer is hidden,
   not disabled.
7. **Progress** (optional): a slim bar under the primary action with
   "18 min left".

Detail pages (movie, series, season, episode) build this block with
`DetailHero`; Home's carousel and Up Next lay it out themselves.

Nothing in the hero moves unless the user moves it. Anything that loads after
the page's details keeps its slot reserved from the start: quality badges
(playback info), external ratings (MDBList, AniList), a series' next airing,
and late footnotes such as a series' up-next episode. Set `reserveChipRow`
and `reserveFootnote` on `DetailHero` for those, and `stableSynopsisHeight`
where browsing changes the synopsis. Reserve only for an answer that can
arrive: a provider without the read reserves nothing, and an empty or failed
answer releases the slot.

The hero occupies about the top 60% of the screen. The first shelf sits fully
in view below it, and the next shelf's header peeks in.

## Shelves and cards

A shelf is a `SectionHeader` (aligned to the gutter) over a `ShelfList`
(horizontal `ListView` preconfigured for gutters, focus room, scrolling and
transitions), spaced by `sectionTitleSpacing`. Shelves stack with
`sectionSpacing`.

Cards are always `MediaCard`:

| Variant | Size (design units) | Use | Caption |
|---------|---------------------|-----|---------|
| `poster` | `posterCardWidth` 268 × 402 (2:3) | Movies, series, seasons, recommendations, search results, library grids | Title + subtitle below the art |
| `landscape` | `landscapeCardWidth` 456 × 256 (16:9) | Continue Watching, Up Next, episodes, chapters, extras | Title + subtitle on a scrim inside the art, progress along the bottom |

- Cards are separated by `cardGap`.
- Landscape cards prefer landscape art: series, parent and item thumbs, then
  the backdrop, and only then the primary image. Art that does not match the
  card's shape is centre-cropped, never stretched. A card without a thumb
  passes the title logo as `logoSource`, which replaces the title text above
  the subtitle; the text returns when the logo is missing or fails to load.
  A 16:9 "logo" also keeps the text: that is fanart.tv ClearArt or a TV thumb
  (1000×562 canvas, with characters) that the server filed as the logo.
- Subtitles follow one format: movies show the year; series show
  `2019 – Present` or `2019 – 2023`; episodes show `S1 E4 · Title`, or
  `S1 E4 · 18 min left` when resuming.
- Badges come from the card: unwatched count or watched check
  (`showWatchState`), Silo poster badges (`item`), and the merged-space source
  (`sourceLabel`). Do not stack extra badges on top of a card.
- Continue Watching and Up Next cards have an actions menu: the actions key
  (`nav.menu`: Menu, Shift+F10, controller Start) opens a `BloomMenu` under
  the focused card. Actions come from C++ (`ContinueWatchingViewModel`), and
  an action the card cannot take shows disabled rather than missing. A
  removed card leaves the shelf at once, focus moves to its neighbour, and a
  `ToastNotification` reports failures or offers Undo. Other shelves have no
  card menu yet; reuse this pattern when one needs it.
- People use `PersonCard` (cast and crew on detail pages, People in Search).
  Enter or a click opens the person's page when the card has a `personId`.
  Don't introduce other card shapes without adding them to this table.

## Person page

`PersonScreen.qml` is a browse page for one person, opened from a cast or
crew card or a People result in Search. It has no hero: below
`pageMarginTop`, the person's photo (poster size, `imageRadius`) sits beside a
`PageHeader` with their name and a life line ("Born 25 April 1940 in New York
City", or the birth and death dates), then the biography at `fontSizeBody`,
five lines with Read More, which shows all of it. Poster shelves of their Movies and Shows follow,
newest first, each a `MediaCard` with the usual year or run subtitle. The
stage picks up the focused title's backdrop.

Read More, when the biography is long, is the top-most control, so Up from it
(or from the first shelf when there is none) reaches the navigation bar. When
the expanded biography is taller than the screen, Down and Up on Read More
first page through it (keeping Read More in view at the end) before focus moves
on.
Enter opens a title through Main's details navigation, and Back returns to
the same card. Back from the person page returns to the card that opened it.
Shelves read the next page as focus nears their end; new titles are added
without moving the focused card. Loading, error (with
Retry) and "no titles" states use `StateMessage`.

### Extras shelf

Movie and series pages show an "Extras" shelf of the title's local extras
(trailers, featurettes, deleted scenes, ...) as landscape cards: the still,
the extra's title (the kind label when it has none), and `Kind · 2:31` as
the subtitle. It follows Chapters on a movie and Seasons on a series, and is
absent when there are no extras. Activating a card plays the extra. When one
of the extras is a trailer (or else a teaser), the hero adds a secondary
"Play Trailer" button right after the primary action. Both return focus to
themselves when the extra ends. Remote (YouTube) trailers are never shown.
Labels and the playback request come from `src/ui/ExtrasUtils.js`, shared by
both pages.

## Grids

Browse grids fill the content width between gutters with `Theme.gridColumns`
columns of poster `MediaCard`s (`artworkWidth` = cell width − `cardGap`). Rows
are separated by `cardGap` plus the caption. The focused row scrolls to a
stable position and never under the navigation bar.

### Letter rail

A library grid has an A–Z rail on its right edge. Right from the last column
enters it; Up/Down move, Enter jumps, Left or Escape return to the grid. In
title order the rail covers the whole library when the server can say where
each letter starts (Silo's letter index; on Jellyfin, from the list's sort
names): it lists every letter with titles, and a jump lands focus on that
letter's first title even when it is far past the loaded rows. Rows skipped
on the way are placeholder cards that load as they scroll into view; the
header's count leaves them out. If a jump's titles fail to load, a toast says
so and choosing the letter again retries. Other sorts, searches on Silo,
combined pages ("All Movies"), Jellyfin libraries of more than 5,000 matching
titles and servers without an index keep a rail of the letters already
loaded. The highlighted letter follows the focused card.

### Library shelves

A library whose server curates rows for it (Silo) offers a **Shelves** toggle
first in the toolbar. Selected, it shows the library's rows as shelves in the
server's order instead of the grid, and hides search, sort and filters, which
belong to the grid. The grid is the default; each library then opens on the
view last chosen for it. The toggle is absent on Jellyfin and while the
library has no rows. Up from the first shelf reaches the toggle, Up again the
navigation bar. A shelf that has more cards than it shows reads the rest as
focus nears its end. Returning from a title puts focus back on its shelf and
card, found by ID when the shelves changed meanwhile.

## Panels and rows

Panel pages show a `PageHeader`, then a category list on the left (about a
quarter of the width) and a content column. Rows:

- are at least `navBarItemHeight` tall, with the label at `fontSizeBody`
  (strong) and the description at `fontSizeSmall` (`textSecondary`);
- show focus with a `surfaceInteractiveHover` fill plus `FocusRing` (inset
  inside clipped lists);
- put their control (toggle, value, chevron) on the right edge.

Group rows under a `SectionHeader` with `level: "group"`. Settings builds its
rows from `settings/SettingsRow` and scrolls them with
`settings/SettingsScrollView`; reuse them for other panel pages.

## Typography

Inter (text) and Inter Display (titles) are bundled and loaded by
`FontLoader`, so type is identical on every platform.

| Role | Family | Size token | Weight |
|------|--------|-----------|--------|
| Hero title (no logo) | `fontDisplay` | `fontSizeHero` (84) | Heavy |
| Page title | `fontDisplay` | `fontSizeDisplay` (60) | Heavy |
| Shelf / section header | `fontPrimary` | `fontSizeHeader` (36) | Heavy |
| Dialog title, card title in panels | `fontPrimary` | `fontSizeTitle` (32) | Strong |
| Hero subtitle, navigation tabs, hero buttons | `fontPrimary` | `fontSizeMedium` (28) | Strong / Medium |
| Body, synopsis, row labels, buttons | `fontPrimary` | `fontSizeBody` (24) | Regular / Strong |
| Metadata, descriptions, poster titles | `fontPrimary` | `fontSizeSmall` (21) | Medium |
| Captions, eyebrows, badges | `fontPrimary` | `fontSizeCaption` (18) | Medium / Heavy |

Nothing is smaller than `fontSizeCaption`. Display type uses
`displayLetterSpacing`; eyebrows use `eyebrowLetterSpacing` and uppercase.

## Color and material

- Text: `textPrimary` for titles and body, `textSecondary` for metadata and
  descriptions, `textTertiary` for hints. `textDisabled` is only for
  unavailable controls.
- Accent (`accentPrimary`) is for eyebrows, selection indicators, progress
  and the glow. It is never the only signal of state.
- Surfaces: `surfaceInteractive*` for controls and rows,
  `surfaceGlassStrong` for floating chrome, `surfaceDialog` for modals.
  Prefer open layouts on the stage over boxing content in glass panels.
- Scrims are tinted from `backgroundPrimary`. Over artwork, text uses
  `textOnDarkOverlay` on `overlayTextBackground`.
- Every theme and flavor (including light Dawn and Latte) must stay legible.
  Check one light theme before shipping.

## Focus, selection, motion

- **Focus** = lift (`Theme.focusScale`) + `FocusRing` + soft shadow for cards;
  `FocusRing` + hover fill for controls. The hero carousel uses edge light
  cues instead of a ring ([hero-banner.md](hero-banner.md)).
- **Selection** = `selectionBackground` + `selectionIndicator`. Never a ring.
- Motion uses `durationShort/Normal/Long/Fade` with `easingStandard` or
  `easingEmphasized`, and every animation honours `Theme.uiAnimationsEnabled`
  (Reduce Motion collapses durations to 0).
- Scroll to the focused row with an animation, not a jump. Focused rows land
  in a stable position.

## Keyboard and remote contract

Every screen must be fully keyboard and gamepad navigable.

- Up at the top edge of a page must not be accepted, so it reaches
  `Main.qml`, which moves focus into the navigation bar.
- Implement `saveFocusForNavigation()` and `restoreFocusFromNavigation()` on
  the page root when focus needs more than `forceActiveFocus()` to come back
  to the right place.
- Expose `readonly property bool navigationRecessed` when the page scrolls.
- Expose `navigationId` (`"home"`, `"search"`, `"settings"`, `"library:<id>"`)
  so the bar shows the current page.
- Back (Escape) pops the page unless it sets `handlesOwnBackNavigation`.
- The actions key (`nav.menu`) opens the focused card's actions menu where a
  card has one. Menu items accept `ShortcutOverride` for Escape, so Back
  closes the menu instead of reaching `Main.qml`'s shortcuts.
- Restore focus with `Qt.callLater` after async model updates.

## Adding a screen

1. Pick the archetype (hero, browse, panel). If none fits, extend this guide
   first.
2. Root is a `FocusScope`; add a `StageBackdrop` filling it.
3. Lay out from the shared components: `PageHeader`, `SectionHeader`,
   `ShelfList`, `MediaCard`, `PersonCard`, `ActionButton`, `MetadataLine`,
   `MetadataChip`, `StateMessage`, `BloomMenu`, `DialogSurface`.
4. Size everything with Theme tokens or `Math.round(N * Theme.layoutScale)`.
   Align to `pageMarginX`; start below `pageMarginTop` unless it is a hero.
5. Wire the keyboard contract above, including Up to the navigation bar.
6. Use `StateMessage` for loading, empty and error states.
7. Check it at 1280×720, 1920×1080, 3840×2160 and 3440×1440, with a dark and
   a light theme, using only the keyboard.
8. Add accessible roles and names (`CONTRIBUTING.md`).

## Don'ts

- Don't hardcode pixels, colors or font families.
- Don't draw a custom focus indicator; use `FocusRing`.
- Don't box page content in glass panels; let it sit on the stage.
- Don't add a hero to browse or panel pages.
- Don't build a one-off card; extend `MediaCard`.
- Don't size cards from the window width on shelves; use the card tokens.
