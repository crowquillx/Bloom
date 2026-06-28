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

## Transitions

When the hero item changes (manual cycle via Left/Right, or auto-cycle), the banner
animates rather than snapping:

- **In-card backdrop cross-fade**: `heroCard` carries two stacked `Image`s
  (`heroBackdropA`/`heroBackdropB`) and a `showBackdropA` toggle. On
  `currentBackdropUrlChanged`, the new URL is loaded into the inactive image and the
  toggle flips when that image reaches `Image.Ready`, so the in-card backdrop cross-fades
  over `Theme.durationFade` in lockstep with Home's full-screen backdrop cross-fade.
- **Content transition**: `contentLoader` (logo/title/badge/metadata/buttons) is driven
  by a `SequentialAnimation` (`heroTransition`) wrapping a `transitionToIndex(newIndex,
  direction)` helper. Each half runs `Theme.durationFade / 2`:
  1. Fade `contentOpacity 1 → 0` and slide `contentSlideX → -contentSlideOffset *
     direction` (old content exits in the navigation direction).
  2. `ScriptAction` commits `currentIndex = pendingIndex` and parks the slide offset on
     the opposite side.
  3. Fade `contentOpacity 0 → 1` and slide `contentSlideX → 0` (new content enters from
     the navigation direction).
  The total `Theme.durationFade` matches the backdrop cross-fade, so the content swap
  lands at the backdrop's midpoint.
- **Rapid cycling**: an in-flight transition is force-completed (the pending index is
  committed and visual state is reset) before the next transition starts, so no hero item
  is ever skipped or left stuck invisible.
- **Page indicator dots** animate their `width` and `color` over `Theme.durationNormal`.

All three animations are gated by `Theme.uiAnimationsEnabled`: when animations are
disabled, `Theme.durationFade`/`Theme.durationNormal` resolve to `0`, so all transitions
collapse to an instantaneous swap and the behavior matches the previous instant change.
Initial load and model refresh (`onHeroModelChanged`) bypass the transition and assign
`currentIndex` directly.
