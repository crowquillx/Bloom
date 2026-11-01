# Home Hero Banner

The Home screen hero is split into `HeroBannerProvider.qml` (source selection and filtering) and
`HeroBanner.qml` (presentation and keyboard/gamepad interaction). `HomeScreen.qml` supplies its
already-loaded Recently Added, Continue Watching, and Up Next models. Only the Library source
performs an additional request through `LibraryService::getHeroLibraryItems()`.

Sources are Recently Added, Continue Watching, Up Next, Library, and Mixed. The provider removes
duplicates by canonical `{connectionId, itemId}` identity, applies hidden media-type filters, and
caps output at 25 items. Empty sources hide the banner without changing the existing Home rows.

All Home-owned hero inputs are canonical media maps. `LibraryService` emits connection-aware
canonical results using the `connectionId` captured when each request starts, and the hero resolves
images only from connection-scoped `ArtworkRef` values. The raw Jellyfin compatibility signals stay
available for unmigrated consumers but are not used by Home or the hero. Hero runtime and resume
values use milliseconds; Home performs the temporary millisecond-to-tick conversion only when it
calls the existing player request API.

## Layout

The hero has two presentations. **Cinema mode** (`HeroBanner.cinematic`, used when Settings > Home > Sync page backdrop is on, the default) draws no card: the selected item's backdrop fills the page (`StageBackdrop` in `hero` mode) under the navigation bar, sharp and unblurred, and leading-edge and floor scrims keep the text legible. **Card mode** (sync off) shows the artwork inside a rounded card with its own scrims, over the normal rotating Home backdrop. Both modes use the same content: an uppercase accent eyebrow (source), the logo or title, the episode subtitle, a `MetadataLine`, the synopsis, and the actions (primary `ActionButton` reading "Play", "Resume" or "Resume S2 E3", plus a secondary "Details"). When the item has progress, a progress row shows the time left. Logos render through `ArtworkLogo`.

Logo and info (badge, metadata chips, action buttons) can be placed independently under
Settings > Home:

- Logo: `topLeft`, `topRight`, `topCenter`, `bottomLeft`, `bottomRight`, `bottomCenter`, `center`,
  or `centerLarge` (a larger centered logo).
- Info: same options except `centerLarge`.

When both use the same placement, they stack in one column (default: bottom-left — badge, logo or
title fallback, episode subtitle when relevant, chips, buttons). When placements differ, the logo
block and info block anchor independently inside the hero card. In card mode the card keeps a
30%-of-width aspect height but grows to fit the laid-out content (both blocks plus edge margins and
the carousel indicator inset), so tall content never spills above the card.

The hero shows the item logo image when available. If no logo exists, a text title is shown in the
logo slot (detail-view style). Episodes use the series name as the primary title and show the
episode name in smaller secondary text beneath when it differs from the series title.

Hero synopsis text is shown below the metadata chips. Movies and series use their canonical
`overview`. Episodes use the parent series overview by default. Settings > Home > Use episode
synopses opts into episode-level `overview` text when present; empty episode overviews still fall
back to the series overview. Series-overview responses are keyed by connection and series identity
before being attached to episode hero items. Overviews load through the live session, so episodes another
merged space member owns keep their own overview.

## Focus

The hero receives initial Home focus when enabled and populated. Hero items arrive after the shelves load, so on first activation the first shelf (Continue Watching, Up Next or Recently Added) holds provisional focus until the hero has content, and the hero then takes over. If the user moves within or off that shelf first (another card, another row, or the navigation bar), the hand-off is cancelled and late hero data never steals focus. Returning to Home restores the last focused section instead. Home has no library tiles; libraries live in the navigation bar.

In cinema mode the hero fills about the top 60% of Home, keeps its copy below `Theme.navBarReservedHeight`, and the first shelf sits fully in view below it. Up from carousel mode is not accepted, so it reaches the navigation bar.

Focus is two-tier:

- **Carousel mode** (hero card focused): Left/Right changes the hero item. Enter/Return enters
  Play/Details. Down moves to the first shelf. Up moves to the navigation bar.
- **Actions mode** (Play/Details focused): Left/Right moves between buttons. Enter/Return activates
  the focused button. Esc or Up returns to carousel mode. Down moves to the first shelf. The Home
  shelves recede to `Theme.recededOpacity` while actions mode lasts, so the hero reads as the
  focused surface.

The buttons claim Esc through `Keys.onShortcutOverride`, so in actions mode it returns to the
carousel instead of opening Home's power menu; from carousel mode Esc keeps its usual role.
Returning to the carousel clears the buttons' focus inside the hero's `FocusScope`, since
`forceActiveFocus()` on a scope would otherwise hand focus straight back to the button.

Leaving the hero resets to carousel mode. Auto-cycle pauses during focus, scrolling, and playback.

Carousel focus is not drawn with `FocusRing`, which cut across the full-bleed artwork. Instead, each side edge of the hero shows a half-ellipse of accent light (`HeroEdgeCue`) with a glass chevron paddle in the page margin. A paddle flashes when Left/Right cycles in its direction, and the current page dot takes the accent color. With a single item, only the edge light shows. In card mode the card also rises a few pixels on a blurred `Theme.elevationShadow`. Action buttons keep their own `ActionButton` focus ring.

When backdrop synchronization is enabled, the selected hero image drives Home's existing
cross-fading backdrop and pauses normal backdrop rotation. Home binds the displayed backdrop
(`displayedBackdropUrl`) to the hero's current URL rather than copying it on change, and the
sync stays active while another page covers Home, so the artwork always matches the hero text
on return. Settings are stored below
`settings.ui.hero_banner` and exposed on the Settings > Home page.

Normal backdrop rotation draws from `LibraryService.getHomeBackdropItems(limit)`, a single
lightweight random query over recursive media, rather than section-bound lists such as Next
Up or Latest. The screensaver uses its own `getScreensaverItems(limit)` query for
synopsis/logo metadata so this one stays light.

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
