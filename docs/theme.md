Design System & Theme — Theme.qml

## Purpose
- `Theme.qml` is the single source of truth for visual tokens (colors, radii, spacing, durations, fonts, icons). Avoid hardcoding colors or sizes in QML components.

## Responsive Layout System

The new responsive system, driven by `ResponsiveLayoutManager`, replaces the old `dpiScale` approach. It unifies scaling for both content and UI chrome, ensuring a consistent look across all display sizes and densities, from standard 1080p monitors to 4K TVs and ultrawide displays.

### Core Concepts

**1. Layout Scale (`Theme.layoutScale`)**
- Logical pixels per design unit. Tokens are authored on a fixed 16:9 canvas,
  so every element covers the same fraction of the screen at any resolution
  or OS display scaling (10-foot / Kodi-style).
- Calculated from the logical window size only; Qt already applies
  `devicePixelRatio`, so Bloom never multiplies it a second time.
- Exception: native child windows (the Windows embedded mpv host) are placed in
  physical pixels. Convert with `ResponsiveLayoutPolicy::toNativePixelRect`
  instead of passing logical geometry to Win32.
- Replaces the removed `Theme.dpiScale` alias.
- Usage: every pixel size must be a Theme token or be multiplied by `Theme.layoutScale`.
  ```qml
  width: Math.round(100 * Theme.layoutScale)
  ```

**2. Ultrawide Support**
- `Theme.gridColumns` (7 at 16:9) grows with width beyond 16:9, so cards keep
  their on-screen size.

See [responsive.md](responsive.md#scaling-model) for the formula, the 4K
scaling table, and the migration note for the removed breakpoints.

### Animation Control
- `Theme.uiAnimationsEnabled`: Boolean flag controlling all UI animations.
- Respects user preference ("Reduce Motion").
- **Best Practice**: Bind animation `enabled` or `duration` properties to this flag.
  ```qml
  Behavior on x { 
      NumberAnimation { 
          duration: Theme.durationShort 
          enabled: Theme.uiAnimationsEnabled 
      } 
  }
  ```

## Tokens

### Theme Catalog & Variants
- `Theme.qml` stores built-in themes as catalog entries with a stable ID, display label, optional `flavors`, optional `colorSchemes`, defaults, and token resolver data.
- `ConfigManager.theme` selects the base theme. `ConfigManager.themeFlavor` and `ConfigManager.themeColorScheme` store the active variant IDs in lowercase.
- Themes only show variant controls they declare. A theme may expose `Flavor`, `Color Scheme`, both, or neither.
- Invalid stored variant IDs fall back to the selected theme defaults, keeping older configs and future theme changes safe.
- Keep catalog entries data-shaped: stable IDs, labels, option lists, defaults, and token maps. Avoid theme-specific UI branches so future user-provided themes can use the same shape.

Built-in variants:
- `Bloom` (default): flavors `Dusk`, `Midnight`, `Dawn` (light); color schemes `Lavender`, `Orchid`, `Rose`, `Peach`, `Sky`, `Mint`; default `Dusk` + `Lavender`. A restrained plum/lavender palette authored for the floating-cinema design. It uses the Catppuccin palette shape, so its resolver is shared.
- `Jellyfin`: color schemes `Blue`, `Purple`, `Blend`; default `Blue`.
- `Rosé Pine`: flavors `Main`, `Moon`, `Dawn`; color schemes `Love`, `Gold`, `Rose`, `Pine`, `Foam`, `Iris`; default `Main` + `Foam`.
- `Catppuccin`: flavors `Latte`, `Frappé`, `Macchiato`, `Mocha`; color schemes `Rosewater`, `Flamingo`, `Pink`, `Mauve`, `Red`, `Maroon`, `Peach`, `Yellow`, `Green`, `Teal`, `Sky`, `Sapphire`, `Blue`, `Lavender`; default `Mocha` + `Mauve`.

### 1. Typography
Inter (`fontPrimary`) and Inter Display (`fontDisplay`) are bundled under
`src/resources/fonts/inter` (SIL OFL) and registered by `FontLoader`, so type
renders identically on every platform. Sizes are design units multiplied by
`layoutScale`; roles are listed in `docs/style-guide.md#typography`.
- `fontSizeHero` (84): hero titles when no logo exists (`fontDisplay`)
- `fontSizeDisplay` (60): page titles (`fontDisplay`, via `PageHeader`)
- `fontSizeHeader` (36): shelf and section headers
- `fontSizeTitle` (32): dialog titles and the brand
- `fontSizeMedium` (28): hero subtitles, navigation tabs, hero buttons
- `fontSizeBody` (24): body text, synopsis, controls and row labels
- `fontSizeSmall` (21): metadata, descriptions and poster titles
- `fontSizeCaption` (18): captions, eyebrow labels and badges; nothing is smaller
- `displayLetterSpacing` tightens display type; `eyebrowLetterSpacing` spaces uppercase eyebrows.
- Weights: `fontWeightRegular`, `fontWeightMedium`, `fontWeightStrong`, `fontWeightHeavy`. `fontMono` is for mpv arguments.

### 2. Spacing
- `spacingSmall` (8), `spacingMedium` (20), `spacingLarge` (32), `spacingXLarge` (40)
- `pageMarginX` (96): left and right page gutter; copy, shelf headers, first cards and grids align to it
- `pageMarginTop` (= `navBarReservedHeight`): where non-hero pages start, below the navigation bar; `pageMarginBottom` (64) after the last row
- `paddingLarge` (56), `sectionSpacing` (56) between shelves, `sectionTitleSpacing` (18) between a header and its content
- `controlSpacing` (16) between adjacent buttons, `dialogPadding` (40)

### 3. Dimensions
- Radii: `radiusSmall` (10), `radiusMedium` / `radiusControl` (16), `radiusLarge` (22), `radiusXLarge` / `radiusDialog` (30), `radiusArtwork` / `imageRadius` (20), `radiusPanel` (32)
- Buttons: `buttonHeightSmall` (52), `buttonHeightMedium` (60), `buttonHeightLarge` (68), `buttonHeightHero` (84, hero actions)
- Navigation bar: `navBarInsetTop` (34), `navBarHeight` (76), `navBarItemHeight` (64), `navBarReservedHeight` (inset + height + 30), `navBarScrimHeight` (260), `iconSizeNav` (30)
- Cards: `posterCardWidth` × `posterCardHeight` (268 × 402, 2:3), `landscapeCardWidth` × `landscapeCardHeight` (456 × 256, 16:9), `cardCaptionHeight` (84), `cardGap` (28), `shelfFocusMargin` (22, room for the focus lift). Fixed design units: wider screens show more cards, never bigger ones.
- `detailViewLogoHeight`: hero logo height for movie and series detail views, with a fixed 1.14 boost
- `seriesLogoHeight` / `seriesLogoMaxWidth`: generic logo bounds for series and related logo usages

### 4. Semantic Color Tokens
Every theme resolves to the same semantic roles. New QML should use these roles rather than raw palette or accent colors.

| Role | Tokens | Notes |
|------|--------|-------|
| Text | `textPrimary`, `textSecondary`, `textTertiary`, `textOnAccent` | `textTertiary` is for eyebrow labels and hints |
| Surfaces | `surfaceGlassStrong`, `surfaceDialog`, `surfaceBorder`, `surfaceBorderStrong` | Soft glass tinted by the theme's `surfaceTint`; light flavors use higher opacity |
| Interactive fills | `surfaceInteractive`, `surfaceInteractiveHover`, `surfaceInteractivePressed`, `surfaceInteractiveDisabled` | Neutral fills for rows, chips and secondary controls |
| Actions | `actionPrimary*`, `actionSecondary*`, `actionDestructive*`, `disabledOpacity`, `recededOpacity` | The primary action is an inverse fill (`textPrimary` on `backgroundPrimary`), so it reads on any artwork and in every theme |
| Focus | `focusRing`, `focusRingHalo`, `focusRingWidth`, `focusRingGap`, `focusScale` | White ring with a dark halo on dark themes; dark ring with a light halo on light flavors |
| Selection | `selectionBackground`, `selectionIndicator`, `selectionText`, `selectionIndicatorWidth` | Accent tint and bar; never used to show focus |
| Status | `errorColor`, `successColor`, `warningColor`, `favoriteColor` | |
| Progress | `progressTrack`, `progressFill`, `progressTrackOnArtwork`, `progressHeight` | |
| Scrims | `scrimModal` | Tinted to the theme, so light flavors get a light scrim |
| Elevation | `elevationShadow` | Blurred drop shadow under raised surfaces (focused hero card) |
| Artwork | `artworkPlaceholder`, `artworkPlaceholderIcon` | Missing or loading art |
| Stage | `stageGlowPrimary`, `stageGlowSecondary`, `stageAmbientArtOpacity` | `StageBackdrop`'s ambient glow and muted art |

`Theme.withAlpha(color, alpha)` and `Theme.mix(a, b, t)` derive variants without hardcoding colors.

### Focus vs. Selection
- **Focus** (where the keyboard or gamepad is) is always the shared `FocusRing`: a high-contrast outline separated by `focusRingGap`, with a halo, so it stays visible over bright or busy artwork. Artwork cards may also scale by `Theme.focusScale`.
- **Selection** (current page, active tab, chosen option, checked filter) uses `selectionBackground` plus a `selectionIndicator` bar, underline or check mark.
- A control can be both. Never signal focus with accent color alone.
- Inside clipped containers such as lists and popups, use `FocusRing { inset: true }` so the ring is not clipped.
- The Home hero carousel is the one exception. A ring cuts across its full-bleed artwork, so focus lights its side edges and shows Left/Right paddles instead (see `docs/hero-banner.md`). The paddles are a shape cue, so focus does not rely on color alone.

### Shared Components
Shared building blocks live in `src/ui/` and are registered in the `BloomUI` module:

| Component | Use |
|-----------|-----|
| `ActionButton` | Every text/icon button. `variant`: `primary`, `secondary`, `ghost`, `destructive`; `selected` for toggles; `busy` swaps the icon for a spinner while keeping the label; includes its own `FocusRing`. Callers wire Enter/Return. |
| `FocusRing` | Focus indicator; place it inside a focusable item's background. |
| `NavigationBar` | The shell's top navigation (owned by `Main.qml`). |
| `StageBackdrop` | Full-bleed page background: artwork in `hero` or `ambient` mode with the standard scrims, or the ambient accent glow without art. |
| `MediaCard` | The one media card: `poster` (caption below) or `landscape` (title on the art, progress), with focus lift, badges and hybrid rounded artwork. `menuEnabled` makes right-click and press-and-hold emit `menuRequested` (an actions menu); without it a long press still clicks. |
| `ShelfList` | Horizontal shelf `ListView` aligned to the gutter with focus room and displacement transitions (no fade-in: ListView can leave an interrupted fade at opacity 0). |
| `PageHeader` | Eyebrow, display title and detail line for non-hero pages, with a trailing slot for toolbars. |
| `DialogSurface` / `ModalScrim` | `Dialog`/`Popup` background and `Overlay.modal`. |
| `ModalFocusGuard` | Keeps focus inside an open modal `Dialog` when the page behind it restores focus late (`popup`, optional `fallbackItem`). |
| `BloomMenu` / `BloomMenuItem` | Context menus and submenus (`iconGlyph`, `destructive`, `marked`). |
| `ToastNotification` | Brief message at the bottom of its parent (`show(text)`); `showWithAction(text, actionText, hint)` adds a pointer-only button (`actionTriggered`) and a second line (`hint`, for example the key that reaches the same action), and `dismissed` fires when it goes away. `paused` holds the timeout (while a menu offering the action is open). Offer the same action somewhere focusable. |
| `BloomCheckIndicator` | Check box indicator for `CheckBox`/`CheckDelegate`. |
| `FieldBackground` | Text field and combo box backgrounds with focus and error states. |
| `SectionHeader` | Page/section/group headers with optional detail text. |
| `MetadataLine` | Year, runtime and rating text joined by a separator. |
| `DetailHero` | Hero block of detail pages: eyebrow, logo or display title, subtitle, metadata, status/quality/rating chips (`statusBadges`, `qualityBadges`, `ratings`), synopsis with Read More, the action row (its children) and progress. Read More leaves Up unhandled so it reaches the navigation bar. |
| `ChapterTimeline` | Compact chapter picker for chapters without images: a runtime track with a tick per chapter; Left/Right moves, Enter emits `chapterActivated(startMs)`. The owning view wires Up/Down. |
| `StateMessage` | Loading, empty and error states with an optional focusable action. `titleColor`, `messageColor` and `iconColor` default to the theme tokens; the always-dark playback pop-out sets the `playbackTime*` ones. |
| `ArtworkPlaceholder` | Missing or loading artwork with an icon and title. |
| `ArtworkLogo` | Provider title logos; adds a legibility shadow on light themes (white logos over light scrims). |
| `settings/SettingsRowBackground` | Settings row hover and focus treatment. |
| `settings/SettingsRow` | One settings row: strong label, optional description, control slot on the right edge, row focus (`highlighted`) and `activated()` for focusable disclosure/navigation rows. Toggle, slider, spin box and text rows build on it. |
| `settings/SettingsScrollView` | Settings rows column: animated `ensureFocusVisible(item)`, edge fades, `scrolled` for receding the navigation bar. |

### Navigation Bar
`NavigationBar.qml` floats over the top of every signed-in page on its own soft scrim, inset `navBarInsetTop` from the top and aligned to the page gutters. Pages fill the window and reserve no width; non-hero pages start at `pageMarginTop`, hero pages draw under the bar.

Entry order (left to right): Home, Movies, Series, Discover (only while `MediaRequests.discoverAvailable`: a Seerr key, or a Silo server that takes requests from the profile), Search, then the utility cluster: Update (only when available), Settings, the space avatar (space picker and sign out), and Exit. Tabs that overflow scroll horizontally.

Movies and Series appear when the space has a library of that kind. With one library the tab opens it (`libraryRequested`). With several it opens a menu inside the bar's focus scope (not a `Popup`, so the bar keeps focus and stays shown): "All Movies"/"All Series" (`combinedLibraryRequested`, which opens `LibraryScreen` on `combined:movies` or `combined:series`), then each library in the saved order. Merged spaces group the menu by source with one "All …" per source; a combined page never spans sources. In the menu Up/Down move, Enter opens, Esc/Back/Left/Right close it (Left/Right also move along the bar), and Shift+Enter starts reordering a library among libraries of the same kind (Up/Down move it, Enter finishes). Single spaces only; merged spaces keep member order.

Focus behavior: Up that no page item accepts reaches `Main.qml`, which calls `NavigationBar.focusBar()`; focus lands on the current page's entry. Left/Right move through entries, Down or Esc/Back return focus to the page (`restoreFocusFromNavigation()`), and Enter navigates. `M` also toggles focus into the bar. Pages expose `navigationRecessed` to slide the bar away while scrolled into content; it returns when focused or when the pointer rests at the top edge. `NavigationBarTest` covers focus order, focus restoration, the library menu and combined pages, reordering, recess and metrics across window shapes.


### Playback Overlay Tokens
- `themeIsLight`
- `playbackOverlayShadowTint`
- `playbackControlGlassBackground`
- `playbackControlGlassBackgroundHover`
- `playbackControlGlassBorder`
- `playbackControlGlassBorderStrong`
- `playbackProgressTrack`
- `playbackProgressFill`
- `playbackTimePrimary`
- `playbackTimeSecondary`
- `playbackIconColor`
- `playbackAccent`, `playbackAccentGlow` (lavender accent lifted for light themes)
- `playbackChromeSurface`, `playbackChromeTray`, `playbackChromeDivider` (unified control bar, transport tray, group dividers)
- `playbackHeroBackground`, `playbackHeroIcon` (play/pause button)
- `playbackSkipBackground`, `playbackSkipText` (Skip Intro/Credits control)

Use these instead of hardcoded playback overlay colors so built-in themes remain consistent. `playbackOverlayShadowTint` resolves to black for light themes and the theme background base for dark themes.

Artwork overlays (poster badges/labels drawn on top of images) should use `overlayTextBackground`, `overlayTextBorder`, and `textOnDarkOverlay` rather than theme `surfaceInteractive*` / `textPrimary` tokens. Poster artwork can be bright or busy in any theme, so translucent theme fills (especially Rosé Pine / light flavors) lose contrast. `MetadataChip { onArtwork: true }` applies these tokens.

Page backdrops are drawn by `StageBackdrop`; don't hand-roll page scrims. Note that QML reads 8-digit hex colors as `#AARRGGBB`, not CSS `#RRGGBBAA`; prefer `Qt.rgba()` when a token needs alpha.

## Layout Patterns

### Proportional Sizing
Avoid fixed pixel values. Always use Theme tokens or scale base values.
**Bad:**
```qml
width: 250 // Fixed size, won't scale
```
**Good:**
```qml
width: Theme.posterCardWidth // Responsive token
// OR
width: 250 * Theme.layoutScale // Scaled base value
```

### Responsive Grids
Use `Theme.gridColumns` to determine cell width.
**Good:**
```qml
GridView {
    cellWidth: width / Theme.gridColumns
    cellHeight: cellWidth * (16/9)
}
```

### Smooth Transitions
Ensure layout changes are smooth but respect accessibility settings.
**Good:**
```qml
Behavior on width {
    NumberAnimation { 
        duration: Theme.durationNormal
        easing.type: Easing.OutQuad
    }
}
```

## Migration Guide

Migrating from old `dpiScale` system:

1.  **Replace Scaling Factor**: Search for `Theme.dpiScale` and replace with `Theme.layoutScale`.
2.  **Remove Manual Math**: If you were manually calculating responsive sizes, check if a Theme token exists (e.g., `Theme.posterCardWidth`).
3.  **Update Grids**: Replace hardcoded column counts with `Theme.gridColumns`.
4.  **Verify Animations**: Ensure all generic animations use `Theme.duration*` tokens.

## Visual Principles
See `docs/style-guide.md` for the full page anatomy.
- **Artwork is the interface**: every page sits on a full-bleed `StageBackdrop`; sharp behind heroes, softened behind dense content.
- **Open layouts**: content sits on the stage; `surfaceGlassStrong` is for floating chrome and controls, not for boxing page content. Avoid heavy borders.
- **Rounded artwork**: `radiusArtwork` for posters and thumbnails, `radiusPanel` for panels.
- **Restrained color**: accent is for selection, progress and small highlights. Primary actions use the inverse fill.
- **Clear focus**: the `FocusRing` is the focus signal everywhere except the Home hero carousel, and it must remain visible on every surface.
- **High contrast and large text**: designed for 10-foot viewing; every theme, including light flavors, must stay readable.
