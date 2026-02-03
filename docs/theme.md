Design System & Theme — Theme.qml

Purpose
- `Theme.qml` is the single source of truth for visual tokens (colors, radii, spacing, durations, fonts, icons). Avoid hardcoding colors or sizes in QML components.

DPI-Aware Scaling
- Content sizes (posters, cards, images) automatically scale based on screen DPI via `Theme.dpiScale`.
- Baseline is 2K (1440p) resolution; 4K displays will show proportionally smaller content to maintain consistent visible size.
- All poster/card dimensions (`posterWidthLarge`, `seasonPosterWidth`, `episodeCardHeight`, etc.) are scaled inversely with DPI.
- UI chrome (buttons, spacing, borders) remains fixed and does not scale with DPI.
- This ensures that library screens and series view look appropriately sized on both 2K monitors and 4K TVs from a standard viewing distance.

Tokens (examples)
- Colors: `backgroundPrimary`, `backgroundSecondary`, `cardBackground`, `textPrimary`, `textSecondary`, `accentPrimary`, `accentSecondary`.
- Radii & sizing: `radiusSmall`, `radiusMedium`, `radiusLarge`; `imageRadius`, `buttonHeightMedium`, `buttonIconSize`.
- Typography: `fontPrimary`, `fontSizeDisplay`, `fontSizeHeader`, `fontSizeBody`.
- Durations & animations: `durationShort`, `durationMedium`, `durationLong`.
- DPI-scaled content: `posterWidthLarge`, `seasonPosterWidth`, `episodeCardHeight`, `nextUpHeight`, etc.

Visual principles
- Glassmorphism: translucent backgrounds with subtle blur and rounded corners; use tokens for opacity and blur radii.
- Consistent radius values across UI: small(8), medium(12), large(16) as a suggestion.
- High contrast typography suitable for the 10-foot UX: large font sizes and readable color contrast.
- Responsive content sizing: use DPI-scaled dimension tokens for media displays to adapt across different resolutions.

Best practices
- Expose Theme as a singleton QML object and import it in components.
- Use color tokens for all backgrounds, borders, and overlays.
- Use DPI-scaled dimension tokens (`Theme.posterWidthLarge`, etc.) for media cards and content displays.
- Keep UI chrome sizes fixed (buttons, spacing) independent of DPI.
- Provide a light/dark theme variant and allow runtime switching.
- Keep glass effects subtle to avoid performance issues on lower-end hardware.

Examples
- Buttons should use Theme tokens for `color`, `border.color`, `radius`, and `focus` state styling.
- All components should use `Theme.fontSize*` for consistency and accessibility.
- Grid views and media displays should use DPI-scaled dimension tokens: `Theme.posterWidthLarge`, `Theme.seasonPosterWidth`, etc.