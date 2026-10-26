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
| **Browse** | Library grids, Search, collections | `mode: "ambient"`, optionally the focused item's backdrop | `PageHeader` + toolbar | Poster grid or shelves |
| **Panel** | Settings, Active Sessions, account pages | `mode: "ambient"`, no art | `PageHeader` | Category list + content column of rows |

Only hero pages carry a hero. Browse and panel pages do not invent one; they
get their warmth from the stage.

## Navigation bar

`NavigationBar.qml` is owned by `Main.qml`. Left to right: brand, page tabs
(Home, each library in the saved order, Search), and a utility cluster (update
when available, Settings, space avatar, power).

- **Selection** (the current page) uses `selectionBackground` with an accent
  hairline. **Focus** uses `FocusRing`. A tab can show both.
- The bar recedes (slides up and fades) while a page reports
  `navigationRecessed: true`, typically once its content scrolls past the
  first row or the hero. It returns when focus or the pointer comes back.
- Keys: Up from the top of any page enters the bar on the current tab.
  Left/Right walk the entries, Enter activates, Down or Escape return to the
  page. Shift+Enter on a library starts reordering; Left/Right move it.

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
   `MetadataChip`s for quality (4K, Dolby Vision) and ratings.
5. **Synopsis**: `fontSizeBody`, `textPrimary` at 0.86 opacity, three lines
   at most, with Read More when the page supports it.
6. **Actions**: one primary `ActionButton` (`variant: "primary"`, height
   `buttonHeightHero`, label `fontSizeMedium`), then secondary buttons, then
   icon-only buttons. One primary per hero.
7. **Progress** (optional): a slim bar under the primary action with
   "18 min left".

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
| `landscape` | `landscapeCardWidth` 456 × 256 (16:9) | Continue Watching, Up Next, episodes, chapters | Title + subtitle on a scrim inside the art, progress along the bottom |

- Cards are separated by `cardGap`.
- Subtitles follow one format: movies show the year; series show
  `2019 – Present` or `2019 – 2023`; episodes show `S1 E4 · Title`, or
  `S1 E4 · 18 min left` when resuming.
- Badges come from the card: unwatched count or watched check
  (`showWatchState`), Silo poster badges (`item`), and the merged-space source
  (`sourceLabel`). Do not stack extra badges on top of a card.
- People use `PersonCard`. Don't introduce other card shapes without adding
  them to this table.

## Grids

Browse grids fill the content width between gutters with `Theme.gridColumns`
columns of poster `MediaCard`s (`artworkWidth` = cell width − `cardGap`). Rows
are separated by `cardGap` plus the caption. The focused row scrolls to a
stable position and never under the navigation bar.

## Panels and rows

Panel pages show a `PageHeader`, then a category list on the left (about a
quarter of the width) and a content column. Rows:

- are at least `navBarItemHeight` tall, with the label at `fontSizeBody`
  (strong) and the description at `fontSizeSmall` (`textSecondary`);
- show focus with a `surfaceInteractiveHover` fill plus `FocusRing` (inset
  inside clipped lists);
- put their control (toggle, value, chevron) on the right edge.

Group rows under a `SectionHeader` with `level: "group"`.

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
- Surfaces: `surfaceInteractive*` for controls and rows, `surfaceGlass` /
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
