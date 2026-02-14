# Responsive Layout System

This document describes Bloom's responsive layout system, which provides adaptive UI scaling based on viewport dimensions, high-DPI displays, and ultrawide aspect ratios.

## Overview

The responsive layout system replaces the previous `dpiScale` approach with a comprehensive `layoutScale` system that provides:

- **Unified scaling** for both content AND UI chrome
- **Breakpoint detection** and management
- **Aspect-ratio awareness** for ultrawide displays
- **Clean separation** from display hardware concerns (refresh rate, HDR)

## Core Component: ResponsiveLayoutManager

The [`ResponsiveLayoutManager`](../src/ui/ResponsiveLayoutManager.h) class is the single source of truth for all responsive layout calculations. It is registered with `ServiceLocator` and exposed to QML via `WindowManager`.

### Key Properties

| Property | Type | Description |
|----------|------|-------------|
| `breakpoint` | QString | Current breakpoint: "Small", "Medium", "Large", or "XL" |
| `layoutScale` | qreal | Continuous scaling factor (0.6 - 1.5) |
| `gridColumns` | int | Grid column count (4-10) |
| `homeRowVisibleItems` | int | Visible items per home row (default: 6) |
| `sidebarDefaultMode` | QString | Default sidebar mode: "overlay", "rail", or "expanded" |
| `aspectRatio` | qreal | Viewport aspect ratio (width / height) |
| `viewportWidth` | int | Current viewport width in logical pixels |
| `viewportHeight` | int | Current viewport height in logical pixels |

## Breakpoint Model

Height-first breakpoints using effective viewport height:

| Breakpoint | Height Range    | Base Columns | Sidebar Default | layoutScale Range |
|------------|-----------------|--------------|-----------------|-------------------|
| Small      | < 850px         | 4            | Overlay         | 0.6 - 0.8         |
| Medium     | 850-1150px      | 6            | Rail            | 0.8 - 1.0         |
| Large      | 1150-1700px     | 7            | Rail            | 1.0 - 1.25        |
| XL         | >= 1700px       | 8            | Expanded        | 1.25 - 1.5        |

### Why Height-First?

As a 10-foot HTPC client, Bloom prioritizes vertical space for content visibility. Height determines how much content fits on screen, while width is handled by the ultrawide adjustment system.

## High-DPI Handling

When `devicePixelRatio > 1.5`, the system uses physical height for breakpoint calculations:

```text
effectiveHeight = logicalHeight * devicePixelRatio
```

This ensures that 4K displays with 200-300% scaling are correctly identified as XL breakpoints rather than Small/Medium.

### Windows Behavior

Windows builds are configured as DPI-unaware at the process level. This means OS display scaling is intentionally ignored, and the app renders at 96 DPI behavior while Windows applies compositor scaling.

### Example

A 4K display (3840×2160) at 200% scaling:
- Logical height: 1080px
- devicePixelRatio: 2.0
- Effective height: 2160px → XL breakpoint

## Manual DPI Scale Override

Users can manually override the automatic layout scale calculation via `ConfigManager::manualDpiScaleOverride`. This setting allows fine-tuning of UI scale independent of automatic detection.

### How It Works

The final `layoutScale` is calculated as:

```text
finalLayoutScale = baseLayoutScale * manualDpiScaleOverride
```

Where:
- `baseLayoutScale` is the automatically calculated scale (0.6 - 1.5) based on viewport height
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

For displays with aspect ratio > 2.2 (e.g., 21:9 ultrawide monitors), the system adds extra grid columns:

```cpp
if (aspectRatio > 2.2) {
    qreal extraWidth = aspectRatio - 2.2;
    int extraColumns = qMin(2, static_cast<int>(extraWidth * 2));
    baseColumns += extraColumns;
}
```

### Example

A 34" ultrawide (3440×1440):
- Aspect ratio: 2.39
- Base columns (Large): 7
- Extra columns: min(2, (2.39 - 2.2) * 2) = min(2, 0.38) = 0
- Final columns: 7

A super-ultrawide (5120×1440):
- Aspect ratio: 3.56
- Base columns (Large): 7
- Extra columns: min(2, (3.56 - 2.2) * 2) = min(2, 2.72) = 2
- Final columns: 9

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
    property string currentBreakpoint: ResponsiveLayoutManager.breakpoint
    property bool isLargeScreen: ResponsiveLayoutManager.breakpoint === "Large" || 
                                  ResponsiveLayoutManager.breakpoint === "XL"
    
    // React to breakpoint changes
    Connections {
        target: ResponsiveLayoutManager
        function onBreakpointChanged() {
            console.log("Breakpoint changed to:", ResponsiveLayoutManager.breakpoint)
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

1. **Resize the window** to different heights to trigger breakpoint changes
2. **Test high-DPI** by changing display scaling in OS settings
3. **Test ultrawide** by resizing to extreme aspect ratios (> 2.2)
4. **Check console output** for debug messages showing layout calculations

### Debug Output

Enable verbose logging to see layout calculations:

```text
ResponsiveLayoutManager: Layout updated - viewport: 1920 x 1080 effectiveHeight: 1080 DPR: 1.0 breakpoint: "Large" layoutScale: 1.12 gridColumns: 7 aspectRatio: 1.78
```

## Visual Regression Testing

The responsive layout system includes automated visual regression testing to catch UI regressions across different screen sizes.

### Running Tests Locally

1. Build the project using the approved wrapper (see [`docs/build.md`](./build.md)):
   ```bash
   ./scripts/build-docker.sh
   ```

2. Run the visual regression tests:
   ```bash
   ./build-docker/tests/VisualRegressionTest
   ```

### Test Mode

The application supports a `--test-mode` flag for visual regression testing:

```bash
./build-docker/src/Bloom --test-mode --test-resolution 1920x1080
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

2. Run the tests again:
   ```bash
   ./build-docker/tests/VisualRegressionTest
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

Column count comes directly from `Theme.gridColumns` (4/6/7/8 at Small/Medium/Large/XL breakpoints). Cell dimensions:

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

Button padding and the back-button width scale with `Theme.layoutScale`, keeping touch/focus targets proportional across breakpoints.

## SeriesDetailsView Responsive Layout

[`SeriesDetailsView.qml`](../src/ui/SeriesDetailsView.qml) uses the responsive layout system for its two-column series details layout.

### Layout structure

A 70%/30% `RowLayout` split (left content area + right sidebar). The sidebar narrows to 15% on wide screens (`root.width > 1200 * Theme.layoutScale`).

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

### Animated transitions

Sidebar width animates on resize, gated by `Theme.uiAnimationsEnabled`:

```qml
Behavior on Layout.preferredWidth {
    enabled: Theme.uiAnimationsEnabled
    NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
}
```

### Focus restoration on breakpoint change

A `Connections` block on `ResponsiveLayoutManager.onBreakpointChanged` saves the current season grid index and active focus item, then restores both via `Qt.callLater` after the layout reflows.

## SeriesSeasonEpisodeView Responsive Layout

[`SeriesSeasonEpisodeView.qml`](../src/ui/SeriesSeasonEpisodeView.qml) uses the responsive layout system for the episode browsing experience with seasons tabs, episode thumbnails, metadata, action buttons, and audio/subtitle track selection.

### Token usage

| Element | Tokens used |
|---------|-------------|
| Main content area margins | `Theme.paddingLarge`, responsive top margin via `Math.round(N * Theme.layoutScale)` |
| Season tabs | Height, width, padding all scale via `Math.round(N * Theme.layoutScale)` |
| Series logo / name | Max height uses `Math.round(180 * Theme.layoutScale)` with viewport-relative capping |
| Episode metadata typography | `Theme.fontSizeHeader`, `Theme.fontSizeBody`, `Theme.fontSizeCaption` |
| Episode thumbnail delegate | Margins/spacing via `Theme.spacingSmall`, `Math.round(2 * Theme.layoutScale)` |
| Focus border width | `Theme.buttonFocusBorderWidth` |
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

### Focus restoration on breakpoint change

A `Connections` block on `ResponsiveLayoutManager.onBreakpointChanged` saves the current episode list index and active focus item, then restores both via `Qt.callLater` after the layout reflows — matching the `SeriesDetailsView` pattern.


## Future Enhancements

Potential improvements for future versions:

- **Configurable breakpoints** via user settings
- **Per-screen handling** for multi-monitor setups
- **Orientation support** for portrait displays
- **Custom layout modes** for specific content types
