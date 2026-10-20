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
- Replaces `Theme.dpiScale`.
- Usage: every pixel size must be a Theme token or be multiplied by `Theme.layoutScale`.
  ```qml
  width: Math.round(100 * Theme.layoutScale)
  ```

**2. Ultrawide Support**
- `Theme.gridColumns` (7 at 16:9) and `Theme.homeRowVisibleItems` (6 at 16:9)
  grow with width beyond 16:9, so cards keep their on-screen size.

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
Sizes are design units multiplied by `layoutScale`.
- `fontSizeHero` (72): cardless hero titles when no logo exists
- `fontSizeDisplay` (48): detail-view titles
- `fontSizeHeader` (38): page and dialog headers
- `fontSizeTitle` (32): section headers and card titles
- `fontSizeMedium` (28): subtitles
- `fontSizeBody` (24): body text, controls and rail labels
- `fontSizeSmall` (21): metadata and secondary text
- `fontSizeCaption` (18): tooltips, eyebrow labels and sublabels
- Weights: `fontWeightRegular`, `fontWeightMedium`, `fontWeightStrong`, `fontWeightHeavy`. `fontMono` is for mpv arguments.

### 2. Spacing
- `spacingSmall` (8), `spacingMedium` (20), `spacingLarge` (32), `spacingXLarge` (40)
- `paddingLarge` / `pageMarginX` (56): horizontal page margin to the right of the rail
- `pageMarginTop` (48), `sectionSpacing` (56) between shelves, `sectionTitleSpacing` (18) between a header and its content
- `controlSpacing` (16) between adjacent buttons, `panelPadding` (40) inside glass panels, `dialogPadding` (40)

### 3. Dimensions
- Radii: `radiusSmall` (10), `radiusMedium` / `radiusControl` (16), `radiusLarge` (22), `radiusXLarge` / `radiusDialog` (30), `radiusArtwork` / `imageRadius` (20), `radiusPanel` (32)
- Buttons: `buttonHeightSmall` (52), `buttonHeightMedium` (60), `buttonHeightLarge` (68), `buttonIconSize` (60)
- Rail: `navRailInset` (20), `navRailCollapsedWidth` (96), `navRailExpandedWidth` (360), `navRailItemHeight` (72), `navRailReservedWidth` (inset + collapsed width), `iconSizeNav` (34)
- `posterWidth` / `posterHeight` (2:3), `cardWidthMedium` / `cardHeightMedium` (16:9)
- `detailViewLogoHeight`: hero logo height for movie and series detail views, with a fixed 1.14 boost
- `seriesLogoHeight` / `seriesLogoMaxWidth`: generic logo bounds for series and related logo usages

### 4. Semantic Color Tokens
Every theme resolves to the same semantic roles. New QML should use these roles rather than raw palette or accent colors.

| Role | Tokens | Notes |
|------|--------|-------|
| Text | `textPrimary`, `textSecondary`, `textTertiary`, `textOnAccent` | `textTertiary` is for eyebrow labels and hints |
| Surfaces | `surfaceGlass`, `surfaceGlassStrong`, `surfaceDialog`, `surfaceSunken`, `surfaceBorder`, `surfaceBorderStrong` | Soft glass tinted by the theme's `surfaceTint`; light flavors use higher opacity |
| Interactive fills | `surfaceInteractive`, `surfaceInteractiveHover`, `surfaceInteractivePressed`, `surfaceInteractiveDisabled` | Neutral fills for rows, chips and secondary controls |
| Actions | `actionPrimary*`, `actionSecondary*`, `actionDestructive*`, `disabledOpacity` | The primary action is an inverse fill (`textPrimary` on `backgroundPrimary`), so it reads on any artwork and in every theme |
| Focus | `focusRing`, `focusRingHalo`, `focusRingWidth`, `focusRingGap`, `focusScale` | White ring with a dark halo on dark themes; dark ring with a light halo on light flavors |
| Selection | `selectionBackground`, `selectionIndicator`, `selectionText`, `selectionIndicatorWidth` | Accent tint and bar; never used to show focus |
| Status | `errorColor`, `successColor`, `warningColor`, `favoriteColor` | |
| Progress | `progressTrack`, `progressFill`, `progressTrackOnArtwork`, `progressHeight` | |
| Scrims | `scrimModal`, `scrimStrong`, `scrimMedium`, `scrimSoft`, `scrimClear` | Derived from `backgroundPrimary`, so light flavors get light scrims |
| Elevation | `elevationShadow` | Blurred drop shadow under raised surfaces (focused hero card) |
| Artwork | `artworkPlaceholder`, `artworkPlaceholderIcon` | Missing or loading art |
| Rail | `navRailBackground`, `navRailBackgroundCollapsed` | |

Older tokens (`cardBackground`, `buttonPrimaryBackground`, `focusBorder`, `button*BorderFocused`, `chipBackground`, `hoverOverlay`) are kept as aliases of the semantic roles so older components inherit the new look. Do not use them in new code. `Theme.withAlpha(color, alpha)` and `Theme.mix(a, b, t)` derive variants without hardcoding colors.

### Focus vs. Selection
- **Focus** (where the keyboard or gamepad is) is always the shared `FocusRing`: a high-contrast outline separated by `focusRingGap`, with a halo, so it stays visible over bright or busy artwork. Artwork cards may also scale by `Theme.focusScale`.
- **Selection** (current page, active tab, chosen option, checked filter) uses `selectionBackground` plus a `selectionIndicator` bar, underline or check mark.
- A control can be both. Never signal focus with accent color alone.
- Inside clipped containers such as lists, popups and the rail, use `FocusRing { inset: true }` so the ring is not clipped.
- The Home hero carousel is the one exception. A ring cuts across its full-bleed artwork, so focus lights its side edges and shows Left/Right paddles instead (see `docs/hero-banner.md`). The paddles are a shape cue, so focus does not rely on color alone.

### Shared Components
Shared building blocks live in `src/ui/` and are registered in the `BloomUI` module:

| Component | Use |
|-----------|-----|
| `ActionButton` | Every text/icon button. `variant`: `primary`, `secondary`, `ghost`, `destructive`; `selected` for toggles; `busy` swaps the icon for a spinner while keeping the label; includes its own `FocusRing`. Callers wire Enter/Return. |
| `FocusRing` | Focus indicator; place it inside a focusable item's background. |
| `GlassPanel` | Rounded glass surface (`strong`, `bordered`). |
| `DialogSurface` / `ModalScrim` | `Dialog`/`Popup` background and `Overlay.modal`. |
| `BloomMenu` / `BloomMenuItem` | Context menus and submenus (`iconGlyph`, `destructive`, `marked`). |
| `BloomCheckIndicator` | Check box indicator for `CheckBox`/`CheckDelegate`. |
| `FieldBackground` | Text field and combo box backgrounds with focus and error states. |
| `SectionHeader` | Page/section/group headers with optional detail text. |
| `MetadataLine` | Year, runtime and rating text joined by a separator. |
| `StateMessage` | Loading, empty and error states with an optional focusable action. |
| `ArtworkPlaceholder` | Missing or loading artwork with an icon and title. |
| `ArtworkLogo` | Provider title logos; adds a legibility shadow on light themes (white logos over light scrims). |
| `settings/SettingsRowBackground` | Settings row hover and focus treatment. |

### Navigation Rail
`Sidebar.qml` is a floating rail inset by `navRailInset` from the window edges. Pages reserve only `navRailReservedWidth` on the left. Full-bleed backdrops extend under the rail with `anchors.leftMargin: -Theme.navRailReservedWidth`.

The rail shows large icons when collapsed. It expands to `navRailExpandedWidth` with labels while keyboard or gamepad focus is inside it, or while pointer-pinned via the logo toggle. Expansion overlays the page with a scrim and never moves, resizes or recrops it.

Entry order: Home, Search, the "My Media" libraries (in the saved order; Shift+Enter reorders), Update available (only when present), Settings, the account entry (sign out), and Exit.

Focus behavior: Left at a page's edge calls `Sidebar.focusRail()`, which lands on the current page's entry. Up/Down move through entries, Right returns focus to the page, Esc/Back closes the rail, and Enter navigates. `NavigationRailTest` covers focus order, expansion without layout shift, focus restoration, reordering, pointer pinning and rail metrics across window shapes.


### Playback Overlay Tokens
- `playbackOverlayTopTint`
- `playbackOverlayBottomTint`
- `themeIsLight`
- `playbackOverlayShadowTint`
- `playbackOverlayAccentShadowTint`
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

Use these instead of hardcoded playback overlay colors so built-in themes remain consistent. `playbackOverlayShadowTint` resolves to black for light themes and the theme background base for dark themes; `playbackOverlayAccentShadowTint` adds a subtle themed accent to active playback chrome shadows.

Artwork overlays (poster badges/labels drawn on top of images) should use `overlayTextBackground`, `overlayTextBorder`, and `textOnDarkOverlay` rather than theme `chip*` / `textPrimary` tokens. Poster artwork can be bright or busy in any theme, so translucent theme chips (especially Rosé Pine / light flavors) lose contrast. `MetadataChip { onArtwork: true }` applies these tokens.

Full-page blurred backdrops (Home, Library) are covered by `gradientOverlayStart` → `gradientOverlayMiddle` → `gradientOverlayEnd`. These are derived from `backgroundPrimary` at 65–88% opacity so page text stays legible over bright artwork and the scrim follows the active theme (light flavors get a light scrim). Note that QML reads 8-digit hex colors as `#AARRGGBB`, not CSS `#RRGGBBAA`; prefer `Qt.rgba()` when a token needs alpha.

## Layout Patterns

### Proportional Sizing
Avoid fixed pixel values. Always use Theme tokens or scale base values.
**Bad:**
```qml
width: 250 // Fixed size, won't scale
```
**Good:**
```qml
width: Theme.posterWidth // Responsive token
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
2.  **Remove Manual Math**: If you were manually calculating responsive sizes, check if a Theme token exists (e.g., `Theme.posterWidth`).
3.  **Update Grids**: Replace hardcoded column counts with `Theme.gridColumns`.
4.  **Verify Animations**: Ensure all generic animations use `Theme.duration*` tokens.

## Visual Principles
- **Floating cinema**: expansive, sharp backdrops behind content, with scrims (`scrim*`) for legibility. Blur is reserved for pages without a hero.
- **Soft glass**: `GlassPanel` / `surfaceGlass*` for panels and controls; avoid heavy borders.
- **Rounded artwork**: `radiusArtwork` for posters and thumbnails, `radiusPanel` for panels.
- **Restrained color**: accent is for selection, progress and small highlights. Primary actions use the inverse fill.
- **Clear focus**: the `FocusRing` is the focus signal everywhere except the Home hero carousel, and it must remain visible on every surface.
- **High contrast and large text**: designed for 10-foot viewing; every theme, including light flavors, must stay readable.
