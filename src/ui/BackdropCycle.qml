import QtQuick

import BloomUI

/**
 * BackdropCycle - steps a detail page's StageBackdrop through the title's
 * backdrops (docs/style-guide.md, Stage).
 *
 * `sources` lists the backdrop URLs, the page's main backdrop first (the
 * view model's `backdropUrls`). Bind the stage's `source` to `currentSource`
 * and its `fadeDuration` to `fadeDuration`. With one backdrop, or none,
 * `currentSource` is that backdrop or empty and nothing changes.
 *
 * The next backdrop loads in a hidden Image through the image cache before
 * the stage changes, so the stage only ever fades to art that is ready; one
 * that is still loading at the tick waits for the next one, and one that
 * fails is skipped. Cycling stops while `active` is false (page hidden,
 * playback, screensaver) and with Reduce Motion, which goes back to and
 * keeps the main backdrop. The interval is the user's backdrop rotation setting.
 */
Item {
    id: root

    property var sources: []
    property bool active: true
    property int interval: ConfigManager.backdropRotationInterval

    readonly property int count: sources ? sources.length : 0
    property int currentIndex: 0
    readonly property string currentSource: count > 0 ? sources[Math.min(currentIndex, count - 1)] : ""
    /// The cycle has moved off the main backdrop at least once; the stage
    /// then fades slowly instead of at the page-load speed.
    property bool cycled: false
    readonly property int fadeDuration: cycled ? Theme.durationBackdropCycle : Theme.durationFade

    readonly property bool running: active && Theme.uiAnimationsEnabled && count > 1
    // Backdrops that failed to load, skipped until the list changes.
    property var failed: ({})
    readonly property int nextIndex: nextUsableIndex(currentIndex)
    // The backdrop last handed to the stage.
    property string shownSource: ""

    visible: false
    width: 0
    height: 0

    function nextUsableIndex(from) {
        for (var step = 1; step < count; ++step) {
            var candidate = (from + step) % count
            if (!failed[sources[candidate]])
                return candidate
        }
        return -1
    }

    // A new detail keeps the backdrop on screen when it is still listed, so
    // a refresh of the same title does not jump back to the first one.
    onSourcesChanged: {
        failed = ({})
        var kept = count > 0 ? sources.indexOf(shownSource) : -1
        if (kept >= 0) {
            currentIndex = kept
        } else {
            currentIndex = 0
            cycled = false
        }
        shownSource = currentSource
    }

    // Reduce Motion turned on mid-cycle goes back to the main backdrop.
    Connections {
        target: Theme
        function onUiAnimationsEnabledChanged() {
            if (!Theme.uiAnimationsEnabled && root.currentIndex !== 0) {
                root.currentIndex = 0
                root.cycled = false
                root.shownSource = root.currentSource
            }
        }
    }

    Image {
        id: preload
        asynchronous: true
        cache: true
        source: root.running && root.nextIndex >= 0 ? root.sources[root.nextIndex] : ""
        onStatusChanged: {
            if (status === Image.Error) {
                var marked = Object.assign({}, root.failed)
                marked[source.toString()] = true
                root.failed = marked
            }
        }
    }

    Timer {
        interval: Math.max(1000, root.interval)
        repeat: true
        running: root.running
        onTriggered: {
            if (root.nextIndex >= 0 && preload.status === Image.Ready
                    && preload.source.toString() === root.sources[root.nextIndex]) {
                root.cycled = true
                root.currentIndex = root.nextIndex
                root.shownSource = root.currentSource
            }
        }
    }
}
