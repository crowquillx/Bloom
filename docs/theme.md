Design System & Theme — Theme.qml

## Purpose
- `Theme.qml` is the single source of truth for visual tokens (colors, radii, spacing, durations, fonts, icons). Avoid hardcoding colors or sizes in QML components.

## Responsive Layout System

The new responsive system, driven by `ResponsiveLayoutManager`, replaces the old `dpiScale` approach. It unifies scaling for both content and UI chrome, ensuring a consistent look across all display sizes and densities, from standard 1080p monitors to 4K TVs and ultrawide displays.

### Core Concepts

**1. Layout Scale (`Theme.layoutScale`)**
- A continuous scaling factor (0.6 - 1.5) that adapts the UI size.
- Calculated based on viewport height and `devicePixelRatio`.
- Replaces `Theme.dpiScale`.
- Usage: Multiply base pixel values by `Theme.layoutScale`.
  ```qml
  width: 100 * Theme.layoutScale
  ```

**2. Breakpoints (`Theme.breakpoint`)**
The system categorizes the viewport into four breakpoints based on effective height:

| Breakpoint | Height Range (px) | Grid Cols | Use Case |
|------------|-------------------|-----------|----------|
| **Small**  | < 850             | 4         | Laptops, small windows |
| **Medium** | 850 - 1150        | 6         | Standard 1080p |
| **Large**  | 1150 - 1700       | 7         | 1440p, QHD |
| **XL**     | >= 1700           | 8         | 4K TV |

**3. Ultrawide Support**
- Automatically detects aspect ratios > 2.2.
- Adds extra grid columns (up to +2) to utilize horizontal space effectively.

**4. High-DPI Support**
- Automatically detects high-density displays (e.g., 4K @ 200% scale).
- Uses physical pixels for calculation to prevent UI from becoming too small.

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

### 1. Typography
Font sizes scale automatically with `layoutScale`.
- `fontSizeDisplay`: 42pt (Base) - Hero titles
- `fontSizeHeader`: 36pt (Base) - Section headers
- `fontSizeTitle`: 32pt (Base) - Card titles
- `fontSizeMedium`: 28pt (Base) - Subtitles
- `fontSizeBody`: 24pt (Base) - Standard text
- `fontSizeSmall`: 20pt (Base) - Metadata, secondary text
- `fontSizeCaption`: 16pt (Base) - Tooltips, subtle labels

### 2. Spacing
Consistent spacing that scales with the UI.
- `spacingSmall` (8px): Inside cards, tight groups
- `spacingMedium` (20px): Between cards, list items
- `spacingLarge` (32px): Section headers to content
- `spacingXLarge` (40px): Section separators
- `paddingLarge` (40px): Screen margins

### 3. Dimensions
Standard component sizes.
- `radiusSmall` (8px), `radiusMedium` (12px), `radiusLarge` (16px)
- `buttonHeightSmall` (48px), `buttonHeightMedium` (56px), `buttonHeightLarge` (64px)
- `posterWidth` / `posterHeight` (standard 2:3 ratio)
- `cardWidthMedium` / `cardHeightMedium` (standard 16:9 ratio)

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
4.  **Check Breakpoints**: If you had custom logic for screen sizes, use `Theme.breakpoint` ("Small", "Medium", etc.) instead.
5.  **Verify Animations**: Ensure all generic animations use `Theme.duration*` tokens.

## Visual Principles
- **Glassmorphism**: Translucent backgrounds (`backgroundGlass`) with blur (`blurRadius`).
- **Consistent Radius**: Use `radius*` tokens everywhere.
- **High Contrast**: Designed for 10-foot experience; text must remain legible at distance.