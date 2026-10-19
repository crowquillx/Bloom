# Responsive Layout System

This document describes Bloom's responsive layout system, which keeps every UI element the same fraction of the screen regardless of resolution or OS display scaling, and adapts item counts to ultrawide aspect ratios.

## Overview

The responsive layout system replaces the previous `dpiScale` approach with a comprehensive `layoutScale` system that provides:

- **Unified scaling** for both content AND UI chrome
- **Resolution-independent sizing** (10-foot, Kodi-style)
- **Aspect-ratio awareness** for ultrawide displays
- **Clean separation** from display hardware concerns (refresh rate, HDR)

## Core Component: ResponsiveLayoutManager

The [`ResponsiveLayoutManager`](../src/ui/ResponsiveLayoutManager.h) class is the single source of truth for all responsive layout calculations. It is registered with `ServiceLocator` and exposed to QML via `WindowManager`.

### Key Properties

| Property | Type | Description |
|----------|------|-------------|
| `layoutScale` | qreal | Logical pixels per Theme design unit (proportional to window size) |
| `gridColumns` | int | Grid column count (7 at 16:9, more on wider viewports) |
| `homeRowVisibleItems` | int | Visible items per home row (6 at 16:9, more on wider viewports) |
| `sidebarDefaultMode` | QString | Default sidebar mode (always "rail") |
| `aspectRatio` | qreal | Viewport aspect ratio (width / height) |
| `viewportWidth` | int | Current viewport width in logical pixels |
| `viewportHeight` | int | Current viewport height in logical pixels |

## Scaling Model

Bloom scales like a 10-foot app (similar to Kodi): every element covers the
same fraction of the screen at any resolution and any OS display scaling.

Theme tokens are design units on a fixed 16:9 canvas 1272 units tall.
`Bloom::ResponsiveLayoutPolicy` (a Qt-Core-only target) maps that canvas onto
the window:

```text
baseLayoutScale = min(logicalHeight / 1272, logicalWidth / (1272 * 16/9))
layoutScale     = baseLayoutScale * manualDpiScaleOverride
```

- **Logical pixels only.** Qt Quick already multiplies every logical pixel by
  the device pixel ratio (OS display scaling). The policy has no
  device-pixel-ratio input, so a 4K panel at 100%, 150% or 300% renders
  identically.
- **Reference height 1272** reproduces the layout that was tuned at
  2560×1440 logical (a 4K TV at 150%).
- **Narrower than 16:9** (for example a 4:3 or tall window): the canvas is
  fitted by width so nothing overflows.
- **Wider than 16:9**: the scale stays height-driven, and grid columns and
  home row items grow with the extra width (see Ultrawide Adjustment).

| Display | Logical viewport | layoutScale | Physical px per design unit | Fraction of screen height |
|---------|------------------|-------------|-----------------------------|---------------------------|
| 1080p @ 100% | 1920×1080 | 0.849 | 0.849 | 1/1272 |
| 4K @ 100%    | 3840×2160 | 1.698 | 1.698 | 1/1272 |
| 4K @ 150%    | 2560×1440 | 1.132 | 1.698 | 1/1272 |
| 4K @ 300%    | 1280×720  | 0.566 | 1.698 | 1/1272 |

### Rules for QML

- Every pixel size must come from a Theme token or be written as
  `Math.round(N * Theme.layoutScale)`. A bare number stays a fixed number of
  logical pixels and drifts out of proportion as the window or OS scale
  changes.
- Do not clamp scaled sizes to fixed pixel minimums/maximums; that breaks
  proportionality.
- Exceptions: image decode sizes (`sourceSize`, artwork request widths),
  animation timings, blur radii and hairline (1 px) borders.
- Server artwork requests should cover the physical size; for example
  `HomeScreen.homeCardImageRequestWidth` uses
  `Math.max(2, Screen.devicePixelRatio)` times the logical card width.

> **Migration note:** Earlier builds used height breakpoints
> (Small/Medium/Large/XL, scale 0.6–1.5) and, above a device pixel ratio of 1.5,
> treated physical height as logical height. That applied OS scaling twice: a
> 4K TV at 300% rendered the UI 2.3× larger than at 150%. The `breakpoint`
> property, `onBreakpointChanged` focus-restoration handlers, and fixed-pixel
> poster/episode clamps were removed. `sidebarDefaultMode` is now always
> `"rail"`. `ResponsiveLayoutPolicyTest` guards the resolution and OS-scaling
> independence.
>
> The navigation shell is a floating rail. Pages reserve a constant
> `Theme.navRailReservedWidth`, and the rail expands as an overlay. The old
> 960 px overlay threshold and the content-margin animation were removed.
> `NavigationRailTest` checks the rail metrics and fit at 720p, 1080p, 4K,
> 21:9, 4:3 and narrow windows.

## Manual DPI Scale Override

Users can manually override the automatic layout scale calculation via `ConfigManager::manualDpiScaleOverride`. This setting allows fine-tuning of UI scale independent of automatic detection.

### How It Works

The final `layoutScale` is calculated as:

```text
finalLayoutScale = baseLayoutScale * manualDpiScaleOverride
```

Where:
- `baseLayoutScale` is the automatically calculated, resolution-independent scale (see Scaling Model)
- `manualDpiScaleOverride` is the user setting (default: 1.0, range: 0.5 - 2.0)

### Use Cases

- **Small displays**: Increase scale (e.g., 1.2) for better readability on smaller screens
- **Large displays**: Decrease scale (e.g., 0.8) to fit more content on screen
- **Projectors**: Adjust for optimal viewing distance
- **Personal preference**: Fine-tune the automatic scaling

### Signal Flow

When the user changes `manualDpiScaleOverride`:

```text
ConfigManager::setManualDpiScaleOverride()
    ↓
ConfigManager::manualDpiScaleOverrideChanged signal
    ↓
ResponsiveLayoutManager::onManualDpiScaleOverrideChanged()
    ↓
ResponsiveLayoutManager::updateLayout()
    ↓
layoutScale recalculated with new override
    ↓
UI updates automatically
```

### Persistence

The `manualDpiScaleOverride` setting is persisted in the user's configuration file (`~/.config/Bloom/app.json`) and persists across sessions.

## Ultrawide Adjustment

For viewports wider than 16:9, `layoutScale` stays height-driven and item
counts grow with the extra width so cards keep the same on-screen size:

```text
widthInCanvases     = max(1, aspectRatio / (16/9))
gridColumns         = round(7 * widthInCanvases)
homeRowVisibleItems = round(6 * widthInCanvases)
```

| Display | Aspect ratio | gridColumns | homeRowVisibleItems |
|---------|--------------|-------------|---------------------|
| 16:9 (any resolution) | 1.78 | 7 | 6 |
| 3440×1440 | 2.39 | 9 | 8 |
| 5120×1440 | 3.56 | 14 | 12 |

## Usage in QML

### Via Theme.qml

The recommended approach is to access responsive tokens through `Theme.qml`:

```qml
import QtQuick 2.15

Item {
    width: 100 * Theme.layoutScale
    height: 50 * Theme.layoutScale
    
    // Responsive typography
    Text {
        font.pixelSize: Theme.fontSizeTitle
        color: Theme.textPrimary
    }
    
    // Responsive grid
    GridView {
        cellWidth: parent.width / Theme.gridColumns
        cellHeight: cellWidth * 1.5
    }
}
```

### Available Theme Tokens

| Token | Description |
|-------|-------------|
| `Theme.layoutScale` | Continuous scale factor for sizing |
| `Theme.gridColumns` | Number of grid columns |
| `Theme.fontSizeSmall` | Small text (20px base) |
| `Theme.fontSizeMedium` | Medium text (28px base) |
| `Theme.fontSizeTitle` | Title text (32px base) |
| `Theme.fontSizeDisplay` | Display text (42px base) |
| `Theme.spacingSmall` | Small spacing (4px base) |
| `Theme.spacingMedium` | Medium spacing (8px base) |
| `Theme.spacingLarge` | Large spacing (16px base) |
| `Theme.cardWidth` | Responsive card width |
| `Theme.cardHeight` | Responsive card height |
| `Theme.posterWidth` | Responsive poster width |
| `Theme.posterHeight` | Responsive poster height |
| `Theme.uiAnimationsEnabled` | Whether UI animations should play |

### Direct Access

For advanced use cases, access `ResponsiveLayoutManager` directly:

```qml
import QtQuick 2.15

Item {
    // Direct property access
    property int columns: ResponsiveLayoutManager.gridColumns

    // React to column changes (window resize or aspect ratio change)
    Connections {
        target: ResponsiveLayoutManager
        function onGridColumnsChanged() {
            console.log("Grid columns changed to:", ResponsiveLayoutManager.gridColumns)
        }
    }
}
```

## UI Animations

The `uiAnimationsEnabled` property in `ConfigManager` controls whether UI animations should play. This is exposed through `Theme.qml`:

```qml
Behavior on opacity {
    enabled: Theme.uiAnimationsEnabled
    NumberAnimation { duration: 200 }
}
```

Users can disable animations via the settings screen, which is useful for:
- Reducing motion sensitivity
- Lower-end hardware
- Personal preference

## Migration from dpiScale

The old `DisplayManager.dpiScale` has been removed. Replace any remaining references:

| Old | New |
|-----|-----|
| `DisplayManager.dpiScale` | `Theme.layoutScale` |
| `10 * DisplayManager.dpiScale` | `10 * Theme.layoutScale` |

The new system provides more consistent scaling across different display configurations.

## Implementation Details

### Initialization Order

1. `ApplicationInitializer` creates `ResponsiveLayoutManager`
2. `ApplicationInitializer` registers with `ServiceLocator`
3. `WindowManager` exposes as QML context property
4. `WindowManager` calls `setWindow()` after QML engine creates window
5. `ResponsiveLayoutManager` connects to window geometry signals
6. Initial layout calculation runs automatically

### Signal Flow

```text
Window geometry change
    ↓
ResponsiveLayoutManager::updateLayout()
    ↓
Calculate new values
    ↓
Emit changed signals
    ↓
QML bindings update
    ↓
UI reflows
```

### Thread Safety

All calculations run on the main thread. The `updateLayout()` slot is connected to Qt signals, ensuring thread-safe execution.

## Testing

When testing responsive behavior:

1. **Resize the window**: everything should scale uniformly, like resizing a picture
2. **Test high-DPI** by changing OS display scaling, or launch with `QT_SCALE_FACTOR=3`; the UI must look identical apart from pixel density
3. **Test ultrawide** by resizing wider than 16:9; cards keep their size and more columns appear
4. **Check console output** for debug messages showing layout calculations

### Debug Output

Enable verbose logging to see layout calculations:

```text
ResponsiveLayoutManager: Layout updated - viewport: 1280 x 720 DPR: 3 baseScale: 0.566 manualOverride: 1 finalLayoutScale: 0.566 gridColumns: 7 aspectRatio: 1.78
```

## Visual Regression Testing

The responsive layout system includes automated visual regression testing to catch UI regressions across different screen sizes.

### Running Tests Locally

1. Configure and build the quarantine targets (the `nix flake check` run builds
   inside the Nix store and does not create a local `build-quarantine`
   directory, so it must be configured explicitly first):
   ```bash
   nix develop --command bash -c '
     cmake -S . -B build-quarantine -G Ninja -DBUILD_TESTING=ON
     cmake --build build-quarantine --target VisualRegressionTest
   '
   ```

2. Run the visual regression tests:
   ```bash
   nix develop --command bash -c '
     QT_QPA_PLATFORM=offscreen ctest --test-dir build-quarantine \
       --output-on-failure --tests-regex "^VisualRegressionTest$"
   '
   ```

### Test Mode

The application supports a `--test-mode` flag for visual regression testing:

```bash
nix run . -- --test-mode --test-resolution 1920x1080
```

Options:
- `--test-mode` - Enable test mode (loads fixture data, bypasses network)
- `--test-fixture <path>` - Path to fixture JSON (default: tests/fixtures/test_library.json)
- `--test-resolution <WxH>` - Viewport resolution (default: 1920x1080)

### Test Fixtures

Test fixtures are located in [`tests/fixtures/`](../tests/fixtures/):

- **`test_library.json`** - Comprehensive test data including movies, series, episodes, and playback info
- **`test_images/placeholder.svg`** - Deterministic placeholder image for all test media

See [`tests/fixtures/README.md`](../tests/fixtures/README.md) for fixture documentation.

### Golden Screenshots

Golden screenshots are stored in `tests/golden/` and are generated automatically on first test run. To update golden screenshots:

1. Delete the existing golden files:
   ```bash
   rm tests/golden/*.png
   ```

2. Configure (if `build-quarantine` does not already exist) and run the tests again:
   ```bash
   nix develop --command bash -c '
     cmake -S . -B build-quarantine -G Ninja -DBUILD_TESTING=ON
     cmake --build build-quarantine --target VisualRegressionTest
     QT_QPA_PLATFORM=offscreen ctest --test-dir build-quarantine \
       --output-on-failure --tests-regex "^VisualRegressionTest$"
   '
   ```

3. New golden screenshots will be created

### CI Integration

Visual regression tests run automatically in CI on every pull request. On failure, diff images are uploaded as artifacts for review.

The CI workflow:
1. Builds the project using the approved build wrapper
2. Runs `VisualRegressionTest` at all supported resolutions
3. Uploads `tests/diffs/` as artifacts if tests fail
4. Fails the build if visual regressions are detected

### Test Coverage

| Screen | 720p | 1080p | 1440p | 4K |
|--------|------|-------|-------|-----|
| Home   | ✓    | ✓     | ✓     | ✓   |
| Library| ✓    | ✓     | ✓     | ✓   |
| Details| ✓    | ✓     | ✓     | ✓   |

### Test Architecture

The visual regression testing infrastructure consists of:

- **[`TestModeController`](../src/test/TestModeController.h)** - Manages test mode state and fixture loading
- **[`MockAuthenticationService`](../src/test/MockAuthenticationService.h)** - Mock authentication for isolated testing
- **[`MockLibraryService`](../src/test/MockLibraryService.h)** - Mock library data from fixtures
- **[`VisualRegressionTest`](../tests/VisualRegressionTest.cpp)** - Qt Test framework for screenshot capture and comparison

## LibraryScreen Responsive Grid

[`LibraryScreen.qml`](../src/ui/LibraryScreen.qml) uses the responsive grid system to lay out poster cards.

### Grid columns and cell sizing

Column count comes directly from `Theme.gridColumns` (7 at 16:9, more on ultrawide). Cell dimensions:

```qml
cellWidth:  width / Theme.gridColumns
cellHeight: cellWidth * 1.75   // poster aspect ratio
```

### Animated transitions

`Behavior on cellWidth` and `Behavior on cellHeight` animate grid reflows, gated by `Theme.animationsEnabled`:

```qml
Behavior on cellWidth  { enabled: Theme.uiAnimationsEnabled; NumberAnimation { duration: 200 } }
Behavior on cellHeight { enabled: Theme.uiAnimationsEnabled; NumberAnimation { duration: 200 } }
```

### Focus restoration on column change

A `Connections` block on `Theme.onGridColumnsChanged` saves `currentIndex` before the reflow and restores it via `Qt.callLater`, preventing focus loss when the grid re-lays out.

### Token usage

| Element | Tokens used |
|---------|-------------|
| Grid margins / padding | `Theme.paddingLarge`, `Theme.spacingMedium`, `Theme.spacingLarge` |
| Card corner radius | `Theme.radiusXLarge` |
| Scaled hard-coded values | `Math.round(N * Theme.layoutScale)` |

### Letter rail

The alphabetical letter rail width and spacing scale with `Theme.layoutScale` (e.g., `Math.round(28 * Theme.layoutScale)`).

### Filter tabs

Button padding and the back-button width scale with `Theme.layoutScale`, keeping touch/focus targets proportional at every window size.

## SeriesDetailsView Responsive Layout

[`SeriesDetailsView.qml`](../src/ui/SeriesDetailsView.qml) is a single scrolling column inside `Theme.pageMarginX` / `Theme.pageMarginTop` margins, over a full-bleed, sharp backdrop with scrims.

### Layout structure

A `GlassPanel` hero holds the poster (or `ArtworkPlaceholder`) next to the logo (`ArtworkLogo`) or title. Beside them sit a `MetadataLine`, ratings, genres, creators, a four-line overview with Read More, and the actions: the primary "Play/Resume S#E#" or "Start Series" `ActionButton`, mark watched, favorite and more. Seasons, Next Up, cast and recommendations follow below the hero as shelves.

### Token usage

| Element | Tokens used |
|---------|-------------|
| Root margins | `Theme.spacingLarge` |
| Action button sizing | `Theme.buttonHeightLarge`, `Math.round(200 * Theme.layoutScale)` |
| Typography | `Theme.fontSizeBody`, `Theme.fontSizeSmall`, `Theme.fontSizeHeader`, `Theme.fontSizeDisplay`, `Theme.fontSizeIcon` |
| Spacing | `Theme.spacingSmall`, `Theme.spacingMedium`, `Theme.spacingLarge`, `Theme.spacingXLarge` |
| Card radii | `Theme.radiusMedium`, `Theme.radiusLarge`, `Theme.radiusSmall` |
| Season poster dimensions | `Theme.seasonPosterWidth`, `Theme.seasonPosterHeight` (with responsive capping) |
| Next Up card | `Theme.nextUpHeight`, `Theme.nextUpImageHeight` |
| Series logo | `Theme.seriesLogoHeight`, `Theme.seriesLogoMaxWidth` |
| Overview max height | `Theme.seriesOverviewMaxHeight` |

### Seasons grid

The seasons grid uses `Theme.seasonPosterWidth`/`Theme.seasonPosterHeight` for cell sizing with responsive capping against viewport height (`root.height * 0.45`). Grid margins use `Theme.spacingMedium`. Custom keyboard navigation handles row/column traversal within the grid.

## SeriesSeasonEpisodeView Responsive Layout

[`SeriesSeasonEpisodeView.qml`](../src/ui/SeriesSeasonEpisodeView.qml) uses the responsive layout system for the episode browsing experience with seasons tabs, episode thumbnails, the selected-episode `Chapters` shelf, cast metadata, action buttons (Play, Mark Watched, Favorite, **Go to Series**, and the audio/subtitle context menu), and audio/subtitle track selection. **Go to Series** opens the parent series details view and returns to the same season/episode context via the library navigation stack. The portrait hero frame shows the season poster when available, then the series poster; landscape episode thumbs are only used as a last-resort fill.

### Token usage

| Element | Tokens used |
|---------|-------------|
| Main content area margins | `Theme.paddingLarge`, responsive top margin via `Math.round(N * Theme.layoutScale)` |
| Season tabs | Height, width, padding all scale via `Math.round(N * Theme.layoutScale)` |
| Series logo / name | Max height uses `Math.round(180 * Theme.layoutScale)` with viewport-relative capping |
| Episode metadata typography | `Theme.fontSizeHeader`, `Theme.fontSizeBody`, `Theme.fontSizeCaption` |
| Episode thumbnail delegate | Margins/spacing via `Theme.spacingSmall`, `Math.round(2 * Theme.layoutScale)` |
| Chapter rail cards | Surface/radius tokens, `Theme.spacingSmall`/`Theme.spacingMedium`, and the shared `FocusRing` |
| Focus indicator | `FocusRing` (`Theme.focusRing*` tokens) |
| Played indicator icon | `Theme.fontSizeIcon` |
| Read More button | Height/width via `Math.round(N * Theme.layoutScale)` |
| Action buttons | `Theme.buttonHeightLarge`, `Math.round(N * Theme.layoutScale)` for widths |
| Context menu | All dimensions scaled via `Math.round(N * Theme.layoutScale)` |
| Spacing throughout | `Theme.spacingSmall`, `Theme.spacingMedium`, `Theme.spacingLarge` |

### Animated transitions

The Flickable `contentY` animation is gated by `Theme.uiAnimationsEnabled`:

```qml
Behavior on contentY {
    enabled: Theme.uiAnimationsEnabled
    NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
}
```

All animation durations use Theme duration tokens (`Theme.durationNormal`, `Theme.durationLong`, `Theme.durationShort`).


## Future Enhancements

Potential improvements for future versions:

- **Per-screen handling** for multi-monitor setups
- **Orientation support** for portrait displays
- **Custom layout modes** for specific content types
