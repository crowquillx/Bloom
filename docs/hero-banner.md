# Home Hero Banner

The Home screen hero is split into `HeroBannerProvider.qml` (source selection and filtering) and
`HeroBanner.qml` (presentation and keyboard/gamepad interaction). `HomeScreen.qml` supplies its
already-loaded Recently Added, Continue Watching, and Up Next models. Only the Library source
performs an additional request through `LibraryService::getHeroLibraryItems()`.

Sources are Recently Added, Continue Watching, Up Next, Library, and Mixed. The provider removes
duplicates, applies hidden media-type filters, and caps output at 25 items. Empty sources hide the
banner without changing the existing Home rows.

## Layout

Logo and info (badge, metadata chips, action buttons) can be placed independently under
Settings > Home:

- Logo: `topLeft`, `topRight`, `topCenter`, `bottomLeft`, `bottomRight`, `bottomCenter`, `center`,
  or `centerLarge` (a larger centered logo).
- Info: same options except `centerLarge`.

When both use the same placement, they stack in one column (default: bottom-left — badge, logo or
title fallback, episode subtitle when relevant, chips, buttons). When placements differ, the logo
block and info block anchor independently inside the hero card.

The hero shows the item logo image when available. If no logo exists, a text title is shown in the
logo slot (detail-view style). Episodes use the series name as the primary title and show the
episode name in smaller secondary text beneath when it differs from the series title.

## Focus

The hero card receives initial Home focus when enabled and populated. Focus is two-tier:

- **Carousel mode** (hero card focused): Left/Right changes the hero item. Enter/Return enters
  Play/Details. Down moves to My Media.
- **Actions mode** (Play/Details focused): Left/Right moves between buttons. Enter/Return activates
  the focused button. Esc or Up returns to carousel mode. Down moves to My Media.

Leaving the hero resets to carousel mode. Auto-cycle pauses during focus, scrolling, and playback.

When backdrop synchronization is enabled, the selected hero image drives Home's existing
cross-fading backdrop and pauses normal backdrop rotation. Settings are stored below
`settings.ui.hero_banner` and exposed on the Settings > Home page.
