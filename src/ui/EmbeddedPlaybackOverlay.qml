import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Shapes
import BloomUI

FocusScope {
    id: root
    focus: visible

    readonly property bool overlayActive: PlayerController.supportsEmbeddedVideo && PlayerController.isPlaybackActive
    readonly property bool paused: PlayerController.isPaused
    readonly property bool waitingForFirstFrame: overlayActive
                                                && !paused
                                                && PlayerController.currentPositionSeconds <= 0
                                                && PlayerController.durationSeconds <= 0
    readonly property bool startupBuffering: overlayActive
                                             && (PlayerController.isStartupBuffering || waitingForFirstFrame)
    readonly property bool playbackBuffering: overlayActive
                                             && !startupBuffering
                                             && (PlayerController.isBuffering || PlayerController.isRecovering)
    readonly property bool buffering: startupBuffering || playbackBuffering
    readonly property string mediaTitle: (PlayerController.overlayTitle && PlayerController.overlayTitle.length > 0)
                                        ? PlayerController.overlayTitle
                                        : qsTr("Now Playing")
    readonly property string mediaSubtitle: (PlayerController.overlaySubtitle && PlayerController.overlaySubtitle.length > 0)
                                           ? PlayerController.overlaySubtitle
                                           : (paused ? qsTr("Paused") : qsTr("Playing"))
    // "S1 E6 - Name" reads as "S1 E6  •  Name" in the header.
    readonly property string headerSubtitle: mediaSubtitle.replace(/^(S\d+\s*E\d+)\s+-\s+/, "$1  •  ")
    readonly property string overlayLogoSource: (PlayerController.overlayLogoUrl && PlayerController.overlayLogoUrl.length > 0)
                                                ? PlayerController.overlayLogoUrl
                                                : ""
    readonly property string overlayMetadataFontFamily: Qt.platform.os === "windows" ? "Corbel" : "Noto Sans"
    readonly property int overlaySubtitlePixelSize: Math.round(26 * Theme.layoutScale)
    readonly property int bufferingSubtitlePixelSize: Math.round(26 * Theme.layoutScale)

    property bool controlsVisible: false
    property bool seekPreviewActive: false
    property bool seekOnlyMode: false
    property bool chapterMode: false
    property int focusedChapterIndex: -1

    // One settings pop-out holds subtitles, audio, quality and audio output.
    // Pages: "root", "subtitles", "audio", "quality", "output".
    property bool settingsMenuOpen: false
    property string settingsPage: "root"
    // True when the current page was entered from the root list, so Back
    // returns there instead of closing the menu.
    property bool settingsPageFromRoot: false
    property int settingsCursor: 0
    readonly property bool audioSelectorOpen: settingsMenuOpen && (settingsPage === "audio" || settingsPage === "output")
    readonly property bool subtitleSelectorOpen: settingsMenuOpen && settingsPage === "subtitles"

    // Interactive volume adjustment on the transport bar's slider.
    property bool volumeSelectorOpen: false
    property bool volumeOsdVisible: false
    property string audioNoticeText: ""
    readonly property bool selectorOpen: settingsMenuOpen || volumeSelectorOpen
    readonly property bool fullControlsVisible: controlsVisible && !seekOnlyMode && !chapterMode
    // The floating volume pill shows the OSD, and hosts adjustment when the
    // bar is too narrow for its inline slider.
    readonly property bool volumePanelVisible: (volumeOsdVisible && !controlsVisible)
                                               || (volumeSelectorOpen && !inlineVolumeSliderAvailable)
    readonly property bool inlineVolumeSliderAvailable: transportTray.roomForVolumeSlider
    property bool hasPrimedPointerPosition: false
    property real lastPointerY: -1

    // Pointer scrubbing on the progress bar.
    property bool scrubHovered: false
    property bool scrubPressed: false
    property real scrubSeconds: 0
    readonly property bool chromeHovered: bottomPanelHover.hovered || headerHover.hovered || settingsMenuHover.hovered

    readonly property bool introSegmentActive: PlayerController.isInIntroSegment
    readonly property bool outroSegmentActive: PlayerController.isInOutroSegment
    readonly property string activeSkipSegmentType: introSegmentActive ? "intro" : (outroSegmentActive ? "outro" : "")
    readonly property string activeSkipLabel: introSegmentActive ? qsTr("Skip Intro") : qsTr("Skip Credits")
    readonly property int skipPopupDurationMs: Math.max(0, ConfigManager.skipButtonAutoHideSeconds * 1000)
    // The skip button stays reachable from the transport bar while the
    // segment is active and the chrome is up.
    readonly property bool persistentSkipVisible: controlsVisible
                                                 && !chapterMode
                                                 && !skipPopupVisible
                                                 && !buffering
                                                 && activeSkipSegmentType.length > 0
    readonly property bool skipButtonShown: !buffering
                                            && activeSkipSegmentType.length > 0
                                            && (skipPopupVisible || persistentSkipVisible)
    property bool skipPopupVisible: false
    /// When true, `showSkipPopupIfEligible` is a no-op (e.g. after Esc dismisses full chrome).
    property bool suppressSkipPopupReshow: false
    property string skipPopupSegmentType: ""
    property double skipPopupWindowEndEpochMs: 0

    property int controlsAutoHideMs: 2500
    property int controlsHideAnimMs: 180
    property int seekPreviewHoldMs: 1800
    property int volumeOsdHoldMs: 1800
    property real controlsSlideDistance: Math.round(28 * Theme.layoutScale)
    property real topControlsOffset: controlsVisible && !seekOnlyMode ? 0 : -controlsSlideDistance
    property real bottomControlsOffset: controlsVisible ? 0 : controlsSlideDistance

    // Trickplay filmstrip: previews appear only while seeking or scrubbing.
    readonly property real previewSeconds: (scrubHovered || scrubPressed)
                                           ? scrubSeconds
                                           : PlayerController.currentPositionSeconds
    readonly property bool trickplayStripVisible: PlayerController.hasTrickplay
                                                  && controlsVisible
                                                  && !chapterMode
                                                  && (seekPreviewActive || scrubHovered || scrubPressed)
    // Neighbouring frames sit one seek step apart, so each 10 s seek slides
    // the strip by exactly one thumbnail.
    readonly property int trickplayStepMs: Math.max(PlayerController.trickplayIntervalMs, 10000)
    // Frame-aligned preview position; neighbour sources re-resolve only
    // when the previewed frame changes.
    readonly property real trickplayAnchorSeconds: {
        var interval = PlayerController.trickplayIntervalMs
        if (interval <= 0) return 0
        return Math.floor(Math.max(0, previewSeconds) * 1000 / interval) * interval / 1000
    }

    default property alias overlayContent: overlayRoot.data

    anchors.fill: parent
    visible: overlayActive
    z: 200
    Behavior on topControlsOffset {
        NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic }
    }
    Behavior on bottomControlsOffset {
        NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic }
    }

    function formatTime(seconds) {
        var total = Math.max(0, Math.floor(seconds))
        var hours = Math.floor(total / 3600)
        var minutes = Math.floor((total % 3600) / 60)
        var secs = total % 60
        if (hours > 0) {
            return hours + ":" + String(minutes).padStart(2, "0") + ":" + String(secs).padStart(2, "0")
        }
        return String(minutes).padStart(2, "0") + ":" + String(secs).padStart(2, "0")
    }

    function overlayHotzoneContains(yValue) {
        var topZone = Math.round(180 * Theme.layoutScale)
        var bottomZone = Math.max(Math.round(250 * Theme.layoutScale),
                                  bottomPanel.height + Math.round(80 * Theme.layoutScale))
        return yValue <= topZone || yValue >= (root.height - bottomZone)
    }

    function showSkipPopupIfEligible() {
        if (!overlayActive || controlsVisible || suppressSkipPopupReshow
                || activeSkipSegmentType.length === 0 || skipPopupDurationMs <= 0) {
            skipPopupVisible = false
            skipPopupTimer.stop()
            return
        }
        var remainingMs = skipPopupWindowEndEpochMs > 0
                        ? Math.max(0, Math.floor(skipPopupWindowEndEpochMs - Date.now()))
                        : skipPopupDurationMs
        if (remainingMs <= 0) {
            skipPopupVisible = false
            skipPopupTimer.stop()
            return
        }
        skipPopupSegmentType = activeSkipSegmentType
        skipPopupVisible = true
        skipPopupTimer.interval = remainingMs
        skipPopupTimer.restart()
        root.forceActiveFocus(Qt.ActiveWindowFocusReason)
        Qt.callLater(function() { skipButton.forceActiveFocus(Qt.ActiveWindowFocusReason) })
    }

    function triggerActiveSkip() {
        if (!overlayActive || activeSkipSegmentType.length === 0) {
            return
        }
        var skipHadFocus = skipButton.activeFocus
        PlayerController.skipActiveSegment()
        skipPopupVisible = false
        skipPopupTimer.stop()
        if (skipHadFocus && fullControlsVisible) {
            Qt.callLater(function() { playPauseButton.forceActiveFocus() })
        }
    }

    function showControls() {
        if (!overlayActive) {
            return
        }
        // Full chrome replaces the transient volume OSD.
        hideVolumeOsd()
        controlsVisible = true
        seekOnlyMode = false
        chapterMode = false
        if (paused || seekPreviewActive || selectorOpen) {
            hideTimer.stop()
        } else {
            hideTimer.restart()
        }
    }

    /// Restarts the auto-hide countdown without changing the chrome mode.
    function keepChromeAlive() {
        if (controlsVisible && !paused && !seekPreviewActive && !selectorOpen) {
            hideTimer.restart()
        }
    }

    function enterChapterMode() {
        if (!overlayActive || !PlayerController.hasPlaybackChapters) {
            return false
        }
        hideVolumeOsd()
        controlsVisible = true
        seekOnlyMode = false
        chapterMode = true
        focusedChapterIndex = PlayerController.currentPlaybackChapterIndex >= 0
                            ? PlayerController.currentPlaybackChapterIndex
                            : 0
        hideTimer.restart()
        Qt.callLater(function() {
            chapterList.currentIndex = focusedChapterIndex
            chapterList.positionViewAtIndex(focusedChapterIndex, ListView.Center)
            chapterList.forceActiveFocus(Qt.ShortcutFocusReason)
            if (chapterList.currentItem) {
                chapterList.currentItem.forceActiveFocus(Qt.ShortcutFocusReason)
            }
        })
        return true
    }

    function activateOverlayFocus() {
        if (!overlayActive) {
            return
        }
        showControls()
        root.forceActiveFocus(Qt.ShortcutFocusReason)
        Qt.callLater(function() { playPauseButton.forceActiveFocus(Qt.ShortcutFocusReason) })
    }

    function showSeekPreview(keepFullControls) {
        if (!overlayActive) {
            return
        }
        hideVolumeOsd()
        controlsVisible = true
        seekOnlyMode = keepFullControls === true ? false : true
        chapterMode = false
        hideTimer.stop()
        seekPreviewActive = true
        seekPreviewTimer.restart()
    }

    function seekBy(seconds, keepFullControls) {
        PlayerController.seekRelative(seconds)
        showSeekPreview(keepFullControls)
    }

    function focusControl(target) {
        if (target) {
            target.forceActiveFocus()
        }
    }

    /// Focusable transport controls in left-to-right order. Hidden or
    /// disabled controls (no adjacent episode, no active segment) are skipped.
    function transportControls() {
        var list = [volumeButton]
        if (previousEpisodeButton.visible && previousEpisodeButton.enabled) list.push(previousEpisodeButton)
        list.push(previousChapterButton, skipBackButton, playPauseButton, skipForwardButton, nextChapterButton)
        if (nextEpisodeButton.visible && nextEpisodeButton.enabled) list.push(nextEpisodeButton)
        list.push(settingsButton)
        if (persistentSkipVisible) list.push(skipButton)
        return list
    }

    function moveTransportFocus(active, step) {
        var list = transportControls()
        var index = list.indexOf(active)
        if (index < 0) {
            focusControl(playPauseButton)
            return
        }
        focusControl(list[(index + step + list.length) % list.length])
    }

    function trackPrimaryLabel(track) {
        if (!track) return ""
        return track.displayTitle || track.title || qsTr("Track")
    }

    function trackSecondaryLabel(track, isSubtitle) {
        if (!track) return ""
        if (track.index === -1) return qsTr("Disabled")
        if (track.title && track.title.length > 0 && track.title !== track.displayTitle) return track.title
        if (isSubtitle) {
            if (track.isHearingImpaired) return qsTr("Subtitles for the Deaf and Hard of Hearing")
            if (track.isForced) return qsTr("Forced")
            return qsTr("Full")
        }
        if (track.channelLayout && track.channelLayout.length > 0) return track.channelLayout
        if (track.channels && track.channels > 0) return qsTr("%1.0").arg(track.channels)
        return ""
    }

    readonly property var subtitleTrackOptions: {
        var options = [{ index: -1, displayTitle: qsTr("Off"), title: qsTr("No subtitles") }]
        var tracks = PlayerController.availableSubtitleTracks
        for (var i = 0; i < tracks.length; i++) {
            options.push(tracks[i])
        }
        return options
    }

    function audioOutputPrimaryLabel(entry) {
        if (!entry) return ""
        if (entry.kind === "default") return qsTr("System Default")
        return (entry.description && entry.description.length > 0) ? entry.description : entry.id
    }

    function audioOutputSecondaryLabel(entry) {
        if (!entry) return ""
        var status = PlayerController.audioOutputStatus
        if (!entry.available) return qsTr("Not connected")
        if (entry.selected && !entry.active) return qsTr("Unavailable, using System Default")
        if (!entry.active) return ""
        if (status.exclusiveActive) return qsTr("Playing in exclusive mode")
        if (status.exclusiveFallback) return qsTr("Playing in shared mode (exclusive unavailable)")
        return qsTr("Playing")
    }

    function audioOutputSummary() {
        var status = PlayerController.audioOutputStatus
        if (!status.device || status.device === "auto") return qsTr("System Default")
        return (status.description && status.description.length > 0) ? status.description : status.device
    }

    function audioOutputDetail() {
        var status = PlayerController.audioOutputStatus
        if (status.usingFallback) {
            return qsTr("%1 unavailable").arg(status.preferredDescription || status.preferredDevice)
        }
        if (status.exclusiveActive) return qsTr("Exclusive")
        if (status.exclusiveFallback) return qsTr("Shared, exclusive unavailable")
        return ""
    }

    function currentSubtitleLabel() {
        var selected = PlayerController.selectedSubtitleTrack
        var tracks = PlayerController.availableSubtitleTracks
        for (var i = 0; i < tracks.length; i++) {
            if (tracks[i].index === selected) return trackPrimaryLabel(tracks[i])
        }
        return qsTr("Off")
    }

    function currentAudioLabel() {
        var selected = PlayerController.selectedAudioTrack
        var tracks = PlayerController.availableAudioTracks
        for (var i = 0; i < tracks.length; i++) {
            if (tracks[i].index === selected) return trackPrimaryLabel(tracks[i])
        }
        return ""
    }

    function currentVersionLabel() {
        var versions = PlayerController.playbackVersions
        for (var i = 0; i < versions.length; i++) {
            if (versions[i].selected) return versions[i].title || versions[i].subtitle
        }
        return ""
    }

    function settingsPageTitle(page) {
        if (page === "subtitles") return qsTr("Subtitles")
        if (page === "audio") return qsTr("Audio Track")
        if (page === "quality") return qsTr("Quality")
        if (page === "output") return qsTr("Audio Output")
        return qsTr("Playback Settings")
    }

    function settingsPageValue(page) {
        if (page === "subtitles") return currentSubtitleLabel()
        if (page === "audio") return currentAudioLabel()
        if (page === "quality") return currentVersionLabel()
        if (page === "output") return audioOutputSummary()
        return ""
    }

    // Rows shown on the current settings page. Selection flags are resolved
    // per delegate, so a track change does not rebuild the list.
    readonly property var settingsRows: {
        var rows = []
        var i
        if (settingsPage === "subtitles") {
            var subtitles = subtitleTrackOptions
            for (i = 0; i < subtitles.length; i++) rows.push({ kind: "subtitle", track: subtitles[i] })
            rows.push({ kind: "delay" })
        } else if (settingsPage === "audio") {
            var audioTracks = PlayerController.availableAudioTracks
            for (i = 0; i < audioTracks.length; i++) rows.push({ kind: "audio", track: audioTracks[i] })
        } else if (settingsPage === "quality") {
            var versions = PlayerController.playbackVersions
            for (i = 0; i < versions.length; i++) rows.push({ kind: "version", version: versions[i] })
        } else if (settingsPage === "output") {
            var outputs = PlayerController.audioOutputOptions
            for (i = 0; i < outputs.length; i++) rows.push({ kind: "output", entry: outputs[i] })
        } else {
            rows.push({ kind: "page", page: "subtitles", icon: Icons.subtitles })
            if (PlayerController.availableAudioTracks.length > 0) {
                rows.push({ kind: "page", page: "audio", icon: Icons.audiotrack })
            }
            if (PlayerController.playbackVersions.length > 1) {
                rows.push({ kind: "page", page: "quality", icon: Icons.hd })
            }
            rows.push({ kind: "page", page: "output", icon: Icons.speaker })
        }
        return rows
    }

    onSettingsRowsChanged: {
        settingsCursor = Math.max(0, Math.min(settingsRows.length - 1, settingsCursor))
    }

    function isSettingsRowSelected(row) {
        if (!row) return false
        if (row.kind === "subtitle") return row.track.index === PlayerController.selectedSubtitleTrack
        if (row.kind === "audio") return row.track.index === PlayerController.selectedAudioTrack
        if (row.kind === "version") return row.version.selected === true
        if (row.kind === "output") return row.entry.selected === true
        return false
    }

    function settingsRowIndexForPage(page) {
        for (var i = 0; i < settingsRows.length; i++) {
            if (settingsRows[i].kind === "page" && settingsRows[i].page === page) return i
        }
        return 0
    }

    function placeSettingsCursor(index) {
        settingsCursor = Math.max(0, Math.min(settingsRows.length - 1, index))
        settingsList.positionViewAtIndex(settingsCursor, ListView.Contain)
    }

    function placeSettingsCursorOnSelection() {
        var rows = settingsRows
        for (var i = 0; i < rows.length; i++) {
            if (isSettingsRowSelected(rows[i])) {
                placeSettingsCursor(i)
                return
            }
        }
        placeSettingsCursor(0)
    }

    function openSettingsMenu(page) {
        var target = page || "root"
        if (settingsMenuOpen && settingsPage === target) {
            closeSettingsMenu()
            return
        }
        volumeSelectorOpen = false
        hideVolumeOsd()
        showControls()
        settingsPageFromRoot = false
        settingsPage = target
        settingsMenuOpen = true
        if (target === "output") {
            PlayerController.refreshAudioOutputDevices()
        }
        Qt.callLater(function() {
            root.placeSettingsCursorOnSelection()
            settingsList.forceActiveFocus()
        })
    }

    function openSettingsPage(page) {
        settingsPageFromRoot = true
        settingsPage = page
        if (page === "output") {
            PlayerController.refreshAudioOutputDevices()
        }
        placeSettingsCursorOnSelection()
    }

    function showSettingsRoot() {
        var from = settingsPage
        settingsPage = "root"
        settingsPageFromRoot = false
        placeSettingsCursor(settingsRowIndexForPage(from))
    }

    function closeSettingsMenu() {
        if (!settingsMenuOpen) {
            return false
        }
        settingsMenuOpen = false
        settingsPageFromRoot = false
        Qt.callLater(function() { settingsButton.forceActiveFocus() })
        return true
    }

    /// Back/Escape inside the menu: a page entered from the root list returns
    /// there; anything else closes the menu.
    function settingsBack() {
        if (settingsPage !== "root" && settingsPageFromRoot) {
            showSettingsRoot()
            return true
        }
        return closeSettingsMenu()
    }

    function activateSettingsRow(index) {
        var row = settingsRows[index]
        if (!row) {
            return
        }
        settingsCursor = index
        if (row.kind === "page") {
            openSettingsPage(row.page)
        } else if (row.kind === "subtitle") {
            PlayerController.setSelectedSubtitleTrack(row.track.index)
            closeSettingsMenu()
        } else if (row.kind === "audio") {
            PlayerController.setSelectedAudioTrack(row.track.index)
            closeSettingsMenu()
        } else if (row.kind === "version") {
            PlayerController.switchPlaybackVersion(row.version.id)
            closeSettingsMenu()
        } else if (row.kind === "output") {
            PlayerController.selectAudioOutputDevice(row.entry.id)
            closeSettingsMenu()
        } else if (row.kind === "delay") {
            PlayerController.resetSubtitleDelay()
        }
    }

    function settingsDirection(direction) {
        var row = settingsRows[settingsCursor]
        if (direction === "up") {
            placeSettingsCursor(settingsCursor - 1)
        } else if (direction === "down") {
            placeSettingsCursor(settingsCursor + 1)
        } else if (direction === "left") {
            if (row && row.kind === "delay") {
                adjustSubtitleDelayBy(-PlayerController.subtitleDelayStepMs)
            } else if (settingsPage !== "root") {
                showSettingsRoot()
            }
        } else if (direction === "right") {
            if (row && row.kind === "delay") {
                adjustSubtitleDelayBy(PlayerController.subtitleDelayStepMs)
            } else if (row && row.kind === "page") {
                openSettingsPage(row.page)
            }
        }
        return true
    }

    function openAudioSelector() {
        openSettingsMenu(PlayerController.availableAudioTracks.length > 0 ? "audio" : "output")
    }

    function openSubtitleSelector() {
        openSettingsMenu("subtitles")
    }

    function closeSelectors() {
        if (settingsMenuOpen) {
            return closeSettingsMenu()
        }
        if (volumeSelectorOpen) {
            closeVolumeSelector()
            return true
        }
        return false
    }

    /// Returns true if a dismissal occurred (caller should not stop playback).
    function tryDismissPlaybackOverlayBeforeStop() {
        if (!overlayActive) {
            return false
        }
        if (settingsMenuOpen) {
            settingsBack()
            return true
        }
        if (volumeSelectorOpen) {
            closeVolumeSelector()
            return true
        }
        if (volumeOsdVisible) {
            hideVolumeOsd()
            return true
        }
        if (skipPopupVisible) {
            skipPopupVisible = false
            skipPopupTimer.stop()
            return true
        }
        if (seekPreviewActive || controlsVisible) {
            suppressSkipPopupReshow = true
            seekPreviewTimer.stop()
            hideTimer.stop()
            seekPreviewActive = false
            parkFocus()
            controlsVisible = false
            seekOnlyMode = false
            chapterMode = false
            Qt.callLater(function() { root.suppressSkipPopupReshow = false })
            return true
        }
        return false
    }

    /// Keeps key focus on the overlay while its chrome fades out.
    function parkFocus() {
        if (hasFocusedControl() || !root.Window.activeFocusItem) {
            root.forceActiveFocus()
        }
    }

    function hideVolumeOsd() {
        volumeOsdTimer.stop()
        volumeOsdVisible = false
    }

    function showVolumeOsd() {
        if (!overlayActive || volumeSelectorOpen) {
            return
        }
        volumeOsdVisible = true
        volumeOsdTimer.restart()
    }

    function openVolumeSelector() {
        if (volumeSelectorOpen) {
            closeVolumeSelector()
            return
        }
        settingsMenuOpen = false
        volumeSelectorOpen = true
        hideVolumeOsd()
        showControls()
        Qt.callLater(function() {
            if (root.inlineVolumeSliderAvailable) {
                volumeSliderFocus.forceActiveFocus()
            } else {
                volumePanelFocus.forceActiveFocus()
            }
        })
    }

    function closeVolumeSelector() {
        volumeSelectorOpen = false
        Qt.callLater(function() { volumeButton.forceActiveFocus() })
    }

    function adjustVolumeBy(delta) {
        PlayerController.adjustVolume(delta)
        if (volumeSelectorOpen) {
            return
        }
        // Any visible chrome (full, seek-only, chapter) should not stack a floating OSD.
        if (controlsVisible) {
            if (fullControlsVisible) {
                showControls()
            } else if (chapterMode) {
                hideTimer.restart()
            }
            return
        }
        // Volume up/down should not open the entire overlay — show the panel alone.
        showVolumeOsd()
    }

    function handleVolumeKey(event) {
        if (event.key === Qt.Key_Left) {
            root.adjustVolumeBy(-5)
        } else if (event.key === Qt.Key_Right) {
            root.adjustVolumeBy(5)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                   || event.key === Qt.Key_Escape || event.key === Qt.Key_Back
                   || event.key === Qt.Key_Backspace) {
            root.closeVolumeSelector()
        } else if (event.key === Qt.Key_Space) {
            PlayerController.toggleMute()
        } else if (event.key !== Qt.Key_Up && event.key !== Qt.Key_Down) {
            return
        }
        event.accepted = true
    }

    function volumeIcon() {
        if (PlayerController.muted || PlayerController.volume <= 0) return Icons.volumeOff
        return PlayerController.volume < 50 ? Icons.volumeDown : Icons.volumeUp
    }

    function formatSubtitleDelay(ms) {
        if (ms === 0) {
            return qsTr("0 ms")
        }
        var sign = ms > 0 ? "+" : "-"
        return qsTr("%1%2 ms").arg(sign).arg(Math.abs(ms))
    }

    function adjustSubtitleDelayBy(delta) {
        PlayerController.adjustSubtitleDelayMs(delta)
        showControls()
    }

    function stopPlaybackFromBackAction() {
        if (tryDismissPlaybackOverlayBeforeStop()) {
            return
        }
        PlayerController.stop()
    }

    function handleSelectorDirection(direction) {
        if (settingsMenuOpen) {
            return settingsDirection(direction)
        }
        if (volumeSelectorOpen) {
            if (direction === "left") {
                adjustVolumeBy(-5)
            } else if (direction === "right") {
                adjustVolumeBy(5)
            }
            return true
        }
        return false
    }

    function activateFocusedPlaybackControl() {
        if (!overlayActive) {
            return false
        }
        if (skipPopupVisible) {
            triggerActiveSkip()
            return true
        }
        if (settingsMenuOpen) {
            activateSettingsRow(settingsCursor)
            return true
        }
        if (volumeSelectorOpen) {
            closeVolumeSelector()
            return true
        }
        if (chapterMode) {
            if (focusedChapterIndex >= 0) {
                PlayerController.seekToPlaybackChapter(focusedChapterIndex)
                hideTimer.restart()
            }
            return true
        }
        if (!fullControlsVisible) {
            showControls()
            focusControl(playPauseButton)
            return true
        }

        var active = root.Window.activeFocusItem
        if (!active || !hasFocusedControl()) {
            focusControl(playPauseButton)
            return true
        }
        if (active === progressFocus) {
            return true
        }
        var controls = transportControls().concat([backButton])
        for (var i = 0; i < controls.length; i++) {
            if (controls[i] === active) {
                controls[i].trigger()
                return true
            }
        }
        focusControl(playPauseButton)
        return true
    }

    function handlePlaybackAction(actionId) {
        if (!overlayActive || !actionId || actionId.length === 0) {
            return false
        }
        if (actionId === "playback.navigateUp") {
            if (handleSelectorDirection("up")) return true
            return handleDirectionalKey("up")
        }
        if (actionId === "playback.navigateDown") {
            if (handleSelectorDirection("down")) return true
            return handleDirectionalKey("down")
        }
        if (actionId === "playback.navigateLeft") {
            if (handleSelectorDirection("left")) return true
            return handleDirectionalKey("left")
        }
        if (actionId === "playback.navigateRight") {
            if (handleSelectorDirection("right")) return true
            return handleDirectionalKey("right")
        }
        if (actionId === "playback.select") {
            return activateFocusedPlaybackControl()
        }
        if (actionId === "playback.back") {
            stopPlaybackFromBackAction()
            return true
        }
        if (actionId === "playback.volumeUp") {
            adjustVolumeBy(5)
            return true
        }
        if (actionId === "playback.volumeDown") {
            adjustVolumeBy(-5)
            return true
        }
        if (actionId === "playback.volumePanel") {
            openVolumeSelector()
            return true
        }
        if (actionId === "playback.audioSelector") {
            openAudioSelector()
            return true
        }
        if (actionId === "playback.subtitleSelector") {
            openSubtitleSelector()
            return true
        }
        if (actionId === "playback.audioCycle") {
            PlayerController.cycleAudioTrack()
            showControls()
            return true
        }
        if (actionId === "playback.subtitleCycle") {
            PlayerController.cycleSubtitleTrack()
            showControls()
            return true
        }
        if (actionId === "playback.subtitleOverride") {
            PlayerController.toggleSubtitleAssOverride()
            return true
        }
        if (actionId === "playback.deband") {
            PlayerController.toggleDeband()
            return true
        }
        if (actionId === "playback.playPause") {
            PlayerController.togglePause()
            showControls()
            return true
        }
        if (actionId === "playback.seekBack") {
            seekBy(-10, true)
            return true
        }
        if (actionId === "playback.seekForward") {
            seekBy(10, true)
            return true
        }
        if (actionId === "playback.previousChapter") {
            PlayerController.previousChapter()
            showControls()
            return true
        }
        if (actionId === "playback.nextChapter") {
            PlayerController.nextChapter()
            showControls()
            return true
        }
        if (actionId === "playback.skipSegment") {
            triggerActiveSkip()
            return true
        }
        if (actionId === "playback.mute") {
            PlayerController.toggleMute()
            showControls()
            return true
        }
        if (actionId === "playback.stats") {
            PlayerController.toggleMpvStats()
            return true
        }
        if (actionId === "playback.statsOnce") {
            PlayerController.showMpvStatsOnce()
            return true
        }
        return false
    }

    function handlePlaybackShortcut(event) {
        if (!overlayActive) {
            return false
        }

        var actionId = InputBindingManager.actionForKeyboardEvent(event.key, event.modifiers, "playback")
        if (actionId && actionId.length > 0) {
            if (event.isAutoRepeat && !isRepeatablePlaybackAction(actionId)) {
                return true
            }
            return handlePlaybackAction(actionId)
        }

        return false
    }

    function isRepeatablePlaybackAction(actionId) {
        return actionId === "playback.volumeUp"
                || actionId === "playback.volumeDown"
                || actionId === "playback.seekBack"
                || actionId === "playback.seekForward"
    }

    function moveChapterFocus(step) {
        var chapters = PlayerController.playbackChapters
        if (!chapters || !chapters.length) {
            return
        }
        var next = Math.max(0, Math.min(chapters.length - 1, focusedChapterIndex + step))
        if (next === focusedChapterIndex) {
            return
        }
        focusedChapterIndex = next
        chapterList.currentIndex = next
        chapterList.positionViewAtIndex(next, ListView.Contain)
        hideTimer.restart()
    }

    function handleDirectionalKey(direction) {
        if (!overlayActive) {
            return false
        }

        if (selectorOpen) {
            return false
        }

        if (seekOnlyMode) {
            if (direction === "left") {
                seekBy(-10, false)
                return true
            }
            if (direction === "right") {
                seekBy(10, false)
                return true
            }
            showControls()
            focusControl(playPauseButton)
            return true
        }

        if (chapterMode) {
            if (direction === "up") {
                showControls()
                focusControl(playPauseButton)
            } else if (direction === "left") {
                moveChapterFocus(-1)
            } else if (direction === "right") {
                moveChapterFocus(1)
            }
            return true
        }

        if (!fullControlsVisible) {
            if (direction === "down" && PlayerController.hasPlaybackChapters) {
                return enterChapterMode()
            }
            showControls()
            focusControl(playPauseButton)
            return true
        }

        // Keep full overlay alive while navigating, but still allow forced auto-hide timer.
        showControls()

        var active = root.Window.activeFocusItem
        if (!active || !hasFocusedControl()) {
            focusControl(playPauseButton)
            return true
        }

        if (direction === "up") {
            if (active === progressFocus) {
                focusControl(backButton)
            } else if (active !== backButton) {
                focusControl(progressFocus)
            }
            return true
        }

        if (direction === "down") {
            if (active === backButton) {
                focusControl(progressFocus)
            } else if (active === progressFocus) {
                focusControl(playPauseButton)
            } else if (PlayerController.hasPlaybackChapters) {
                return enterChapterMode()
            }
            return true
        }

        if (active === progressFocus) {
            seekBy(direction === "left" ? -10 : 10, true)
            return true
        }
        if (active === backButton) {
            if (direction === "right") focusControl(progressFocus)
            return true
        }
        moveTransportFocus(active, direction === "left" ? -1 : 1)
        return true
    }

    function hasFocusedControl() {
        var active = root.Window.activeFocusItem
        while (active) {
            if (active === root) {
                return true
            }
            active = active.parent
        }
        return false
    }

    function hideControlsIfAllowed() {
        if (!overlayActive || paused || seekPreviewActive || selectorOpen) {
            return
        }
        // Never pull the chrome out from under the pointer.
        if (chromeHovered || scrubPressed) {
            hideTimer.restart()
            return
        }
        parkFocus()
        controlsVisible = false
        seekOnlyMode = false
        chapterMode = false
    }

    onOverlayActiveChanged: {
        if (overlayActive) {
            hideTimer.stop()
            seekPreviewTimer.stop()
            controlsVisible = false
            seekOnlyMode = false
            chapterMode = false
            hasPrimedPointerPosition = false
            lastPointerY = -1
            showSkipPopupIfEligible()
        } else {
            hideTimer.stop()
            seekPreviewTimer.stop()
            skipPopupTimer.stop()
            settingsMenuOpen = false
            settingsPage = "root"
            settingsPageFromRoot = false
            volumeSelectorOpen = false
            hideVolumeOsd()
            controlsVisible = false
            seekPreviewActive = false
            seekOnlyMode = false
            chapterMode = false
            scrubHovered = false
            scrubPressed = false
            skipPopupVisible = false
            skipPopupSegmentType = ""
            skipPopupWindowEndEpochMs = 0
            suppressSkipPopupReshow = false
        }
    }

    onPausedChanged: {
        if (!overlayActive) {
            return
        }
        // Avoid opening overlay during startup state churn; only refocus if controls are already shown.
        if (paused && controlsVisible) {
            activateOverlayFocus()
        } else if (!paused && controlsVisible && !seekPreviewActive && !selectorOpen) {
            hideTimer.restart()
        }
    }

    onControlsVisibleChanged: {
        if (controlsVisible) {
            skipPopupVisible = false
            skipPopupTimer.stop()
        } else {
            parkFocus()
            settingsMenuOpen = false
            volumeSelectorOpen = false
            showSkipPopupIfEligible()
        }
        if (controlsVisible && !seekOnlyMode && overlayActive && !buffering && !selectorOpen) {
            if (root.Window.window) {
                root.Window.window.requestActivate()
            }
            root.forceActiveFocus(Qt.ActiveWindowFocusReason)
            Qt.callLater(function() { playPauseButton.forceActiveFocus(Qt.ActiveWindowFocusReason) })
        }
    }

    onSelectorOpenChanged: {
        if (selectorOpen) {
            hideTimer.stop()
        } else if (controlsVisible) {
            showControls()
        }
    }

    onActiveSkipSegmentTypeChanged: {
        if (activeSkipSegmentType.length === 0) {
            if (skipButton.activeFocus && fullControlsVisible) {
                playPauseButton.forceActiveFocus()
            }
            skipPopupVisible = false
            skipPopupTimer.stop()
            skipPopupSegmentType = ""
            skipPopupWindowEndEpochMs = 0
            return
        }
        skipPopupWindowEndEpochMs = Date.now() + skipPopupDurationMs
        if (activeSkipSegmentType !== skipPopupSegmentType) {
            showSkipPopupIfEligible()
        }
    }

    Timer {
        id: hideTimer
        interval: root.controlsAutoHideMs
        repeat: false
        onTriggered: root.hideControlsIfAllowed()
    }

    Timer {
        id: seekPreviewTimer
        interval: root.seekPreviewHoldMs
        repeat: false
        onTriggered: {
            root.seekPreviewActive = false
            if (root.seekOnlyMode && !root.paused) {
                root.parkFocus()
                root.controlsVisible = false
                root.seekOnlyMode = false
            } else if (root.controlsVisible && !root.chapterMode) {
                root.showControls()
            }
        }
    }

    Timer {
        id: volumeOsdTimer
        interval: root.volumeOsdHoldMs
        repeat: false
        onTriggered: root.volumeOsdVisible = false
    }

    Timer {
        id: skipPopupTimer
        interval: root.skipPopupDurationMs
        repeat: false
        onTriggered: {
            root.skipPopupVisible = false
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        propagateComposedEvents: true
        cursorShape: InputModeManager.pointerActive ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: function(mouse) {
            if (!root.overlayActive) {
                return
            }
            if (!root.hasPrimedPointerPosition) {
                root.hasPrimedPointerPosition = true
                root.lastPointerY = mouse.y
                return
            }
            if (Math.abs(mouse.y - root.lastPointerY) < 0.5) {
                return
            }
            root.lastPointerY = mouse.y
            if (!root.controlsVisible && root.overlayHotzoneContains(mouse.y)) {
                root.showControls()
            } else {
                root.keepChromeAlive()
            }
        }
    }

    Connections {
        target: InputBindingManager
        function onActionTriggeredWithContext(actionId, runtimeContext) {
            if (runtimeContext === "playback" && root.visible) {
                root.handlePlaybackAction(actionId)
            }
        }
    }

    Keys.onPressed: function(event) {
        if (root.skipPopupVisible
                && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space)) {
            event.accepted = true
            root.triggerActiveSkip()
            return
        }
        if (root.handlePlaybackShortcut(event)) {
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Left) {
            event.accepted = root.handleDirectionalKey("left")
        } else if (event.key === Qt.Key_Right) {
            event.accepted = root.handleDirectionalKey("right")
        } else if (event.key === Qt.Key_Up) {
            event.accepted = root.handleDirectionalKey("up")
        } else if (event.key === Qt.Key_Down) {
            event.accepted = root.handleDirectionalKey("down")
        } else {
            event.accepted = false
        }
    }

    // Icon-only transport control. `text` is the accessible/tooltip label;
    // `glyph` is what is drawn.
    component ChromeIconButton: Button {
        id: chromeButton
        property string glyph: ""
        property int diameter: Math.round(64 * Theme.layoutScale)
        property int iconSize: Math.round(34 * Theme.layoutScale)
        /// Keep a glass fill at rest (controls floating directly over video).
        property bool filled: false
        function trigger() { chromeButton.clicked() }
        width: diameter
        height: diameter
        focusPolicy: Qt.StrongFocus
        activeFocusOnTab: true
        opacity: enabled ? 1.0 : 0.36
        scale: down ? 0.92 : 1.0
        onPressed: root.showControls()
        Keys.onReturnPressed: function(event) { event.accepted = true; chromeButton.animateClick() }
        Keys.onEnterPressed: function(event) { event.accepted = true; chromeButton.animateClick() }
        Keys.onSpacePressed: function(event) { event.accepted = true; chromeButton.animateClick() }
        ToolTip.visible: hovered && InputModeManager.pointerActive && text.length > 0
        ToolTip.text: text
        ToolTip.delay: 600
        background: Rectangle {
            radius: width / 2
            color: chromeButton.down || chromeButton.activeFocus
                   ? Theme.playbackControlGlassBackgroundHover
                   : (chromeButton.hovered || chromeButton.filled
                      ? Theme.playbackControlGlassBackground
                      : "transparent")
            border.width: chromeButton.filled ? Theme.borderWidth : 0
            border.color: Theme.playbackControlGlassBorder
            Behavior on color { ColorAnimation { duration: Theme.durationShort } }

            FocusRing {
                active: chromeButton.activeFocus
                radius: parent.radius
                color: Theme.playbackFocusRing
                haloColor: Qt.rgba(0, 0, 0, 0.55)
            }
        }
        contentItem: Text {
            text: chromeButton.glyph
            font.family: Theme.fontIcon
            font.pixelSize: chromeButton.iconSize
            color: Theme.playbackIconColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: Theme.durationShort } }
    }

    component HeroPlayButton: Button {
        id: heroButton
        property bool showsPlay: false
        function trigger() { heroButton.clicked() }
        width: Math.round(92 * Theme.layoutScale)
        height: width
        focusPolicy: Qt.StrongFocus
        activeFocusOnTab: true
        scale: down ? 0.94 : (hovered ? 1.04 : 1.0)
        onPressed: root.showControls()
        Keys.onReturnPressed: function(event) { event.accepted = true; heroButton.animateClick() }
        Keys.onEnterPressed: function(event) { event.accepted = true; heroButton.animateClick() }
        Keys.onSpacePressed: function(event) { event.accepted = true; heroButton.animateClick() }
        ToolTip.visible: hovered && InputModeManager.pointerActive
        ToolTip.text: text
        ToolTip.delay: 600
        background: Item {
            Rectangle {
                id: heroFill
                anchors.fill: parent
                radius: parent.width / 2
                color: Theme.playbackHeroBackground
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: Theme.playbackAccentGlow
                    shadowBlur: 1.0
                    shadowScale: heroButton.activeFocus || heroButton.hovered ? 1.16 : 1.08
                    shadowOpacity: heroButton.activeFocus || heroButton.hovered ? 1.0 : 0.7
                    shadowHorizontalOffset: 0
                    shadowVerticalOffset: 0
                }
            }
            FocusRing {
                active: heroButton.activeFocus
                radius: parent.width / 2
                color: Theme.playbackFocusRing
                haloColor: Qt.rgba(0, 0, 0, 0.55)
            }
        }
        // Solid shapes: the bundled outlined symbol font has no filled
        // play/pause variants.
        contentItem: Item {
            id: heroGlyph
            readonly property real glyphSize: Math.round(36 * Theme.layoutScale)
            Row {
                anchors.centerIn: parent
                spacing: Math.round(heroGlyph.glyphSize * 0.28)
                visible: !heroButton.showsPlay
                Repeater {
                    model: 2
                    Rectangle {
                        width: Math.round(heroGlyph.glyphSize * 0.30)
                        height: heroGlyph.glyphSize
                        radius: Math.max(1, Math.round(3 * Theme.layoutScale))
                        color: Theme.playbackHeroIcon
                    }
                }
            }
            Shape {
                id: playShape
                visible: heroButton.showsPlay
                readonly property real s: heroGlyph.glyphSize
                width: s
                height: s
                // Optical centring: shift right of the bounding-box centre.
                x: Math.round((parent.width - s) / 2 + s * 0.08)
                y: Math.round((parent.height - s) / 2)
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    fillColor: Theme.playbackHeroIcon
                    strokeColor: "transparent"
                    joinStyle: ShapePath.RoundJoin
                    startX: 0; startY: 0
                    PathLine { x: playShape.s * 0.95; y: playShape.s / 2 }
                    PathLine { x: 0; y: playShape.s }
                    PathLine { x: 0; y: 0 }
                }
            }
        }
        Behavior on scale { NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic } }
    }

    component SkipPillButton: Button {
        id: skipPill
        property string labelText: ""
        property bool emphasized: false
        function trigger() { skipPill.clicked() }
        focusPolicy: Qt.StrongFocus
        activeFocusOnTab: true
        text: labelText
        implicitHeight: Math.round(64 * Theme.layoutScale)
        implicitWidth: skipPillRow.implicitWidth + leftPadding + rightPadding
        leftPadding: Math.round(30 * Theme.layoutScale)
        rightPadding: Math.round(26 * Theme.layoutScale)
        scale: down ? 0.95 : 1.0
        onPressed: root.showControls()
        Keys.onReturnPressed: function(event) { event.accepted = true; skipPill.animateClick() }
        Keys.onEnterPressed: function(event) { event.accepted = true; skipPill.animateClick() }
        Keys.onSpacePressed: function(event) { event.accepted = true; skipPill.animateClick() }
        background: Rectangle {
            radius: height / 2
            color: skipPill.hovered || skipPill.activeFocus
                   ? Theme.playbackHeroBackground
                   : Theme.playbackSkipBackground
            border.width: Math.max(1, Math.round(2 * Theme.layoutScale))
            border.color: Theme.withAlpha(Theme.playbackHeroBackground, 0.75)
            Behavior on color { ColorAnimation { duration: Theme.durationShort } }

            FocusRing {
                active: skipPill.activeFocus
                radius: parent.radius
                color: Theme.playbackFocusRing
                haloColor: Qt.rgba(0, 0, 0, 0.55)
            }
            SequentialAnimation on opacity {
                running: skipPill.emphasized && skipPill.visible && !skipPill.activeFocus
                loops: Animation.Infinite
                NumberAnimation { to: 0.86; duration: 750; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 1.0; duration: 750; easing.type: Easing.InOutQuad }
            }
        }
        contentItem: Item {
            implicitWidth: skipPillRow.implicitWidth
            implicitHeight: skipPillRow.implicitHeight
            Row {
                id: skipPillRow
                anchors.centerIn: parent
                spacing: Math.round(12 * Theme.layoutScale)
                Text {
                    text: skipPill.labelText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Math.round(24 * Theme.layoutScale)
                    font.weight: Font.DemiBold
                    color: Theme.playbackSkipText
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: Icons.fastForward
                    font.family: Theme.fontIcon
                    font.pixelSize: Math.round(28 * Theme.layoutScale)
                    color: Theme.playbackSkipText
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
    }

    component VolumeMeter: Item {
        id: meter
        property bool emphasized: false
        implicitHeight: Math.round(28 * Theme.layoutScale)
        readonly property real level: PlayerController.muted ? 0 : Math.min(1, PlayerController.volume / 100)
        Rectangle {
            id: meterTrack
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: Math.round((meter.emphasized ? 8 : 6) * Theme.layoutScale)
            radius: height / 2
            color: Theme.playbackProgressTrack
            Rectangle {
                width: parent.width * meter.level
                height: parent.height
                radius: parent.radius
                color: Theme.playbackProgressFill
                Behavior on width { NumberAnimation { duration: 90 } }
            }
        }
        Rectangle {
            visible: meter.emphasized
            width: Math.round(18 * Theme.layoutScale)
            height: width
            radius: width / 2
            color: Theme.playbackHeroBackground
            x: Math.max(0, Math.min(meter.width - width, meter.width * meter.level - width / 2))
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // Light scrims keep chrome legible without dimming the whole picture.
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: header.y + header.height + Math.round(120 * Theme.layoutScale)
        visible: !root.buffering && opacity > 0.001
        opacity: root.controlsVisible && !root.seekOnlyMode ? 1.0 : 0.0
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.withAlpha(Theme.playbackOverlayShadowTint, 0.62) }
            GradientStop { position: 0.55; color: Theme.withAlpha(Theme.playbackOverlayShadowTint, 0.22) }
            GradientStop { position: 1.0; color: "transparent" }
        }
        Behavior on opacity {
            NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: bottomPanel.height + Math.round(180 * Theme.layoutScale)
        visible: !root.buffering && opacity > 0.001
        opacity: root.controlsVisible ? 1.0 : 0.0
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.45; color: Theme.withAlpha(Theme.playbackOverlayShadowTint, root.chapterMode ? 0.36 : 0.26) }
            GradientStop { position: 1.0; color: Theme.withAlpha(Theme.playbackOverlayShadowTint, root.chapterMode ? 0.72 : 0.62) }
        }
        Behavior on opacity {
            NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic }
        }
    }

    Item {
        id: startupBufferingOverlay
        anchors.fill: parent
        visible: root.startupBuffering
        z: 500

        Image {
            id: loadingBackdrop
            anchors.fill: parent
            source: PlayerController.overlayBackdropUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            smooth: true
            visible: source.toString().length > 0
            scale: 1.02

            SequentialAnimation on scale {
                running: startupBufferingOverlay.visible && loadingBackdrop.visible
                loops: Animation.Infinite
                NumberAnimation { to: 1.07; duration: 7000; easing.type: Easing.InOutCubic }
                NumberAnimation { to: 1.02; duration: 7000; easing.type: Easing.InOutCubic }
            }
        }

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(Theme.playbackOverlayShadowTint.r, Theme.playbackOverlayShadowTint.g, Theme.playbackOverlayShadowTint.b, loadingBackdrop.visible ? 0.58 : 0.78)
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.22) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.68) }
            }
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.30) }
                GradientStop { position: 0.24; color: "transparent" }
                GradientStop { position: 0.76; color: "transparent" }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.44) }
            }
        }

        Rectangle {
            id: loadingCard
            anchors.centerIn: parent
            width: Math.round(Math.min(parent.width * 0.56, 640 * Theme.layoutScale))
            height: Math.round(360 * Theme.layoutScale)
            radius: Theme.radiusXLarge
            color: Theme.playbackPanelBackground
            border.width: 1
            border.color: Theme.playbackControlGlassBorder
            layer.enabled: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blurMax: 44
                blur: 0.34
                shadowEnabled: true
                shadowColor: Qt.rgba(0, 0, 0, 0.56)
                shadowBlur: 0.95
                shadowVerticalOffset: 12
            }
        }

        Column {
            anchors.centerIn: parent
            width: loadingCard.width - Math.round(72 * Theme.layoutScale)
            spacing: Math.round(16 * Theme.layoutScale)

            Item {
                id: spinner
                width: Math.round(80 * Theme.layoutScale)
                height: width
                anchors.horizontalCenter: parent.horizontalCenter

                Rectangle {
                    anchors.centerIn: parent
                    width: Math.round(70 * Theme.layoutScale)
                    height: width
                    radius: width / 2
                    color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.18)
                    border.width: 1
                    border.color: Theme.playbackControlGlassBorder
                    opacity: 0.8

                        SequentialAnimation on opacity {
                            running: startupBufferingOverlay.visible
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.38; duration: 760; easing.type: Easing.InOutQuad }
                        NumberAnimation { to: 0.86; duration: 760; easing.type: Easing.InOutQuad }
                    }
                }

                Repeater {
                    model: 12
                    Item {
                        width: spinner.width
                        height: spinner.height
                        rotation: index * 30

                        Rectangle {
                            width: Math.round(6 * Theme.layoutScale)
                            height: Math.round(16 * Theme.layoutScale)
                            radius: width / 2
                            color: Theme.accentPrimary
                            opacity: (index + 1) / 12
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: Math.round(6 * Theme.layoutScale)
                        }
                    }
                }

                    RotationAnimator on rotation {
                        from: 0
                        to: 360
                        duration: 900
                        loops: Animation.Infinite
                        running: startupBufferingOverlay.visible
                    }
            }

            Text {
                text: qsTr("Buffering...")
                anchors.horizontalCenter: parent.horizontalCenter
                color: Theme.playbackTimePrimary
                font.family: root.overlayMetadataFontFamily
                font.pixelSize: Math.round(32 * Theme.layoutScale)
                font.weight: Font.Bold
                font.letterSpacing: 0.2
            }

            Text {
                text: qsTr("Connecting to server")
                anchors.horizontalCenter: parent.horizontalCenter
                color: Theme.playbackTimeSecondary
                font.family: root.overlayMetadataFontFamily
                font.pixelSize: Math.round(20 * Theme.layoutScale)
                font.weight: Font.DemiBold
                font.letterSpacing: 0.15
            }

            Column {
                width: parent.width
                spacing: Math.round(8 * Theme.layoutScale)

                Item {
                    width: parent.width
                    height: Math.max(bufferingTitleLogo.height, bufferingTitleText.implicitHeight)

                    Image {
                        id: bufferingTitleLogo
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        height: Math.round(92 * Theme.layoutScale)
                        width: Math.min(Math.round(500 * Theme.layoutScale), parent.width)
                        source: root.overlayLogoSource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: true
                        visible: root.overlayLogoSource.length > 0 && status !== Image.Error
                        opacity: status === Image.Ready ? 1.0 : 0.0
                        Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }
                    }

                    Text {
                        id: bufferingTitleText
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: root.mediaTitle
                        visible: root.overlayLogoSource.length === 0
                                 || bufferingTitleLogo.status === Image.Error
                                 || bufferingTitleLogo.status === Image.Loading
                                 || bufferingTitleLogo.status === Image.Null
                        color: Theme.withAlpha(Theme.playbackTimePrimary, 0.82)
                        font.family: root.overlayMetadataFontFamily
                        font.pixelSize: Math.round(18 * Theme.layoutScale)
                        font.weight: Font.Bold
                        font.letterSpacing: 0.2
                        style: Text.Outline
                        styleColor: Qt.rgba(0, 0, 0, 0.9)
                        elide: Text.ElideRight
                    }
                }

                Item {
                    readonly property bool subtitleUnderLogo: bufferingTitleLogo.visible
                    readonly property real preferredWidth: Math.round(560 * Theme.layoutScale)
                    readonly property real logoWidth: bufferingTitleLogo.paintedWidth > 0 ? bufferingTitleLogo.paintedWidth : bufferingTitleLogo.width
                    readonly property real subtitleRailWidth: subtitleUnderLogo
                                                               ? Math.min(parent.width,
                                                                          Math.max(preferredWidth,
                                                                                   logoWidth + Math.round(120 * Theme.layoutScale)))
                                                               : parent.width
                    width: parent.width
                    height: bufferingSubtitleLabel.implicitHeight

                    ScrollingCardLabel {
                        id: bufferingSubtitleLabel
                        x: parent.subtitleUnderLogo ? (parent.width - width) / 2 : 0
                        width: parent.subtitleRailWidth
                        text: root.mediaSubtitle
                        textColor: Theme.withAlpha(Theme.playbackTimePrimary, 0.9)
                        fontFamily: root.overlayMetadataFontFamily
                        fontPixelSize: root.bufferingSubtitlePixelSize
                        fontWeight: Font.Bold
                        fontLetterSpacing: 0.15
                        textStyle: Text.Outline
                            textStyleColor: Qt.rgba(0, 0, 0, 0.9)
                            active: startupBufferingOverlay.visible
                        }
                    }
                }
            }
        }

        Item {
            id: playbackBufferingOverlay
            anchors.fill: parent
            visible: root.playbackBuffering
            z: 505

            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.18)
            }

            Rectangle {
                id: compactSpinnerShell
                anchors.centerIn: parent
                width: Math.round(96 * Theme.layoutScale)
                height: width
                radius: width / 2
                color: Theme.playbackPanelBackground
                border.width: 1
                border.color: Theme.playbackControlGlassBorder

                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: Qt.rgba(0, 0, 0, 0.48)
                    shadowBlur: 0.85
                    shadowVerticalOffset: 8
                }
            }

            Item {
                id: compactSpinner
                anchors.centerIn: compactSpinnerShell
                width: Math.round(64 * Theme.layoutScale)
                height: width

                Repeater {
                    model: 12
                    Item {
                        width: compactSpinner.width
                        height: compactSpinner.height
                        rotation: index * 30

                        Rectangle {
                            width: Math.round(5 * Theme.layoutScale)
                            height: Math.round(14 * Theme.layoutScale)
                            radius: width / 2
                            color: Theme.accentPrimary
                            opacity: (index + 1) / 12
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: Math.round(4 * Theme.layoutScale)
                        }
                    }
                }

                RotationAnimator on rotation {
                    from: 0
                    to: 360
                    duration: 900
                    loops: Animation.Infinite
                    running: playbackBufferingOverlay.visible
                }
            }
        }

    Item {
        id: chromeLayer
        anchors.fill: parent
        visible: !root.buffering
        enabled: root.controlsVisible

        Item {
            id: header
            readonly property int edgeMargin: Math.round(56 * Theme.layoutScale)
            x: edgeMargin
            y: Math.round(44 * Theme.layoutScale) + root.topControlsOffset
            width: Math.min(root.width - 2 * edgeMargin, Math.round(760 * Theme.layoutScale))
            height: headerRow.implicitHeight
            opacity: root.controlsVisible && !root.seekOnlyMode ? 1.0 : 0.0
            enabled: root.controlsVisible && !root.seekOnlyMode
            Behavior on opacity {
                NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic }
            }

            HoverHandler { id: headerHover }

            Row {
                id: headerRow
                spacing: Math.round(20 * Theme.layoutScale)

                ChromeIconButton {
                    id: backButton
                    anchors.verticalCenter: titleBlock.verticalCenter
                    diameter: Math.round(56 * Theme.layoutScale)
                    iconSize: Math.round(30 * Theme.layoutScale)
                    filled: true
                    glyph: Icons.arrowBack
                    text: qsTr("Exit playback")
                    onClicked: PlayerController.stop()
                }

                Column {
                    id: titleBlock
                    width: header.width - backButton.width - headerRow.spacing
                    spacing: Math.round(8 * Theme.layoutScale)
                    layer.enabled: header.opacity > 0
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowColor: Qt.rgba(0, 0, 0, 0.75)
                        shadowBlur: 0.55
                        shadowVerticalOffset: Math.round(2 * Theme.layoutScale)
                        shadowHorizontalOffset: 0
                    }

                    Item {
                        readonly property bool showsLogo: chromeTitleLogo.visible
                        readonly property bool showsText: chromeTitleText.visible
                        width: parent.width
                        height: Math.max(showsLogo ? chromeTitleLogo.height : 0,
                                         showsText ? chromeTitleText.implicitHeight : 0)

                        // Compact wordmark: bounded on both axes, pinned left.
                        Image {
                            id: chromeTitleLogo
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            height: Math.round(92 * Theme.layoutScale)
                            width: Math.min(Math.round(420 * Theme.layoutScale), parent.width)
                            source: root.overlayLogoSource
                            fillMode: Image.PreserveAspectFit
                            horizontalAlignment: Image.AlignLeft
                            asynchronous: true
                            cache: true
                            visible: root.overlayLogoSource.length > 0 && status !== Image.Error
                            opacity: status === Image.Ready ? 1.0 : 0.0
                            Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }
                        }
                        Text {
                            id: chromeTitleText
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            text: root.mediaTitle
                            visible: root.overlayLogoSource.length === 0
                                     || chromeTitleLogo.status === Image.Error
                                     || chromeTitleLogo.status === Image.Loading
                                     || chromeTitleLogo.status === Image.Null
                            color: Theme.playbackTimePrimary
                            font.family: root.overlayMetadataFontFamily
                            font.pixelSize: Math.round(38 * Theme.layoutScale)
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                    }

                    ScrollingCardLabel {
                        id: chromeSubtitleLabel
                        width: parent.width
                        horizontalAlignment: Text.AlignLeft
                        text: root.headerSubtitle
                        textColor: Theme.playbackTimePrimary
                        fontFamily: root.overlayMetadataFontFamily
                        fontPixelSize: root.overlaySubtitlePixelSize
                        fontWeight: Font.DemiBold
                        fontLetterSpacing: 0.15
                        active: root.controlsVisible && !root.seekOnlyMode
                    }
                }
            }
        }

        // One glass bar: optional preview strip (trickplay or chapters), the
        // scrub row, and the transport tray.
        Rectangle {
            id: bottomPanel
            readonly property int sideMargin: Math.round(48 * Theme.layoutScale)
            readonly property int padding: Math.round(20 * Theme.layoutScale)
            width: Math.min(root.width - 2 * sideMargin, Math.round(1824 * Theme.layoutScale))
            height: panelColumn.height + 2 * padding
            x: Math.round((root.width - width) / 2)
            y: root.height - height - Math.round(40 * Theme.layoutScale) + root.bottomControlsOffset
            radius: Theme.radiusXLarge
            color: Theme.playbackChromeSurface
            border.width: Theme.borderWidth
            border.color: Theme.playbackControlGlassBorder
            opacity: root.controlsVisible ? 1.0 : 0.0
            visible: opacity > 0.001
            Behavior on opacity {
                NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic }
            }

            HoverHandler { id: bottomPanelHover }

            // Glass sheen along the top edge.
            Rectangle {
                anchors.fill: parent
                anchors.margins: parent.border.width
                radius: parent.radius - parent.border.width
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.06) }
                    GradientStop { position: 0.35; color: "transparent" }
                }
            }

            Column {
                id: panelColumn
                x: bottomPanel.padding
                y: bottomPanel.padding
                width: bottomPanel.width - 2 * bottomPanel.padding

                Item {
                    id: previewArea
                    width: parent.width
                    height: root.chapterMode
                            ? chapterList.height + Math.round(16 * Theme.layoutScale)
                            : (root.trickplayStripVisible ? filmstrip.height + Math.round(12 * Theme.layoutScale) : 0)
                    clip: true
                    Behavior on height {
                        NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
                    }

                    Item {
                        id: filmstrip
                        readonly property int gap: Math.round(14 * Theme.layoutScale)
                        readonly property int thumbWidth: Math.round(204 * Theme.layoutScale)
                        readonly property int thumbHeight: Math.round(thumbWidth * 9 / 16)
                        readonly property int centerWidth: Math.round(thumbWidth * 1.22)
                        readonly property int centerHeight: Math.round(centerWidth * 9 / 16)
                        // Odd count so the previewed frame sits in the middle.
                        readonly property int count: {
                            var sides = Math.floor((width - centerWidth) / (thumbWidth + gap))
                            sides = Math.max(0, Math.min(8, sides - sides % 2))
                            return sides + 1
                        }
                        readonly property int half: (count - 1) / 2
                        width: parent.width
                        height: centerHeight + Math.round(timeChip.height / 2)
                        opacity: root.trickplayStripVisible ? 1.0 : 0.0
                        visible: opacity > 0.001
                        Behavior on opacity { NumberAnimation { duration: Theme.durationShort } }

                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: filmstrip.gap

                            Repeater {
                                model: filmstrip.count

                                delegate: Item {
                                    id: frame
                                    required property int index
                                    readonly property int offset: index - filmstrip.half
                                    readonly property bool isCenter: offset === 0
                                    readonly property real frameSeconds: root.trickplayAnchorSeconds
                                                                         + offset * root.trickplayStepMs / 1000
                                    readonly property bool inRange: frameSeconds >= 0
                                                                    && frameSeconds <= PlayerController.durationSeconds
                                    width: isCenter ? filmstrip.centerWidth : filmstrip.thumbWidth
                                    height: isCenter ? filmstrip.centerHeight : filmstrip.thumbHeight
                                    y: Math.round((filmstrip.centerHeight - height) / 2)
                                    opacity: inRange ? (isCenter ? 1.0 : Math.max(0.4, 0.86 - 0.12 * Math.abs(offset))) : 0.0

                                    RoundedImage {
                                        anchors.fill: parent
                                        radius: Math.round(12 * Theme.layoutScale)
                                        source: root.trickplayStripVisible && frame.inRange
                                                ? PlayerController.trickplayPreviewUrlAt(frame.frameSeconds)
                                                : ""
                                        mode: "shader"
                                        preferPreRounded: false
                                        cache: false
                                        mipmap: false
                                        sourceWidth: 320
                                        sourceHeight: 180
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: Math.round(12 * Theme.layoutScale)
                                        color: "transparent"
                                        border.width: frame.isCenter
                                                      ? Math.max(2, Math.round(3 * Theme.layoutScale))
                                                      : Theme.borderWidth
                                        border.color: frame.isCenter ? Theme.playbackAccent : Theme.playbackControlGlassBorder
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: frame.inRange
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            PlayerController.seek(frame.frameSeconds)
                                            root.showSeekPreview(true)
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            id: timeChip
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: filmstrip.centerHeight - Math.round(height / 2)
                            width: timeChipLabel.implicitWidth + Math.round(28 * Theme.layoutScale)
                            height: Math.round(36 * Theme.layoutScale)
                            radius: height / 2
                            color: Theme.playbackPanelBackground
                            border.width: Theme.borderWidth
                            border.color: Theme.playbackControlGlassBorderStrong

                            Text {
                                id: timeChipLabel
                                anchors.centerIn: parent
                                text: root.formatTime(root.previewSeconds)
                                color: Theme.playbackTimePrimary
                                font.family: Theme.fontPrimary
                                font.pixelSize: Math.round(19 * Theme.layoutScale)
                                font.weight: Font.DemiBold
                            }
                        }
                    }

                    ListView {
                        id: chapterList
                        y: Math.round(4 * Theme.layoutScale)
                        width: parent.width
                        height: Math.round(204 * Theme.layoutScale)
                        orientation: ListView.Horizontal
                        spacing: Math.round(14 * Theme.layoutScale)
                        clip: true
                        model: PlayerController.playbackChapters
                        currentIndex: root.focusedChapterIndex
                        activeFocusOnTab: true
                        focus: root.chapterMode
                        highlightMoveDuration: Theme.durationNormal
                        opacity: root.chapterMode ? 1.0 : 0.0
                        visible: opacity > 0.001
                        Accessible.role: Accessible.List
                        Accessible.name: qsTr("Chapters")
                        Behavior on opacity { NumberAnimation { duration: Theme.durationShort } }
                        Keys.onUpPressed: function(event) {
                            event.accepted = true
                            root.handleDirectionalKey("up")
                        }
                        Keys.onDownPressed: function(event) { event.accepted = true }
                        Keys.onReturnPressed: function(event) { event.accepted = true; PlayerController.seekToPlaybackChapter(root.focusedChapterIndex); hideTimer.restart() }
                        Keys.onEnterPressed: function(event) { event.accepted = true; PlayerController.seekToPlaybackChapter(root.focusedChapterIndex); hideTimer.restart() }
                        Keys.onSpacePressed: function(event) { event.accepted = true; PlayerController.seekToPlaybackChapter(root.focusedChapterIndex); hideTimer.restart() }
                        onCurrentIndexChanged: {
                            if (activeFocus && currentIndex >= 0) {
                                root.focusedChapterIndex = currentIndex
                                positionViewAtIndex(currentIndex, ListView.Contain)
                            }
                        }

                        delegate: FocusScope {
                            id: chapterDelegate
                            required property var modelData
                            required property int index
                            readonly property bool isFocusedChapter: index === root.focusedChapterIndex
                            width: Math.round(248 * Theme.layoutScale)
                            height: chapterList.height
                            focus: chapterList.activeFocus && chapterList.currentIndex === index
                            Accessible.role: Accessible.ListItem
                            Accessible.name: modelData.name || ""

                            function seekHere() {
                                PlayerController.seekToPlaybackChapter(index)
                                hideTimer.restart()
                            }

                            Keys.onLeftPressed: function(event) {
                                event.accepted = true
                                root.moveChapterFocus(-1)
                            }
                            Keys.onRightPressed: function(event) {
                                event.accepted = true
                                root.moveChapterFocus(1)
                            }
                            Keys.onUpPressed: function(event) {
                                event.accepted = true
                                root.handleDirectionalKey("up")
                            }
                            Keys.onDownPressed: function(event) { event.accepted = true }
                            Keys.onReturnPressed: function(event) { event.accepted = true; chapterDelegate.seekHere() }
                            Keys.onEnterPressed: function(event) { event.accepted = true; chapterDelegate.seekHere() }
                            Keys.onSpacePressed: function(event) { event.accepted = true; chapterDelegate.seekHere() }

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    chapterList.currentIndex = index
                                    root.focusedChapterIndex = index
                                }
                            }

                            Rectangle {
                                id: chapterCard
                                anchors.fill: parent
                                radius: Theme.radiusLarge
                                color: chapterDelegate.isFocusedChapter
                                       ? Theme.playbackControlGlassBackgroundHover
                                       : Theme.playbackControlGlassBackground
                                border.width: Theme.borderWidth
                                border.color: Theme.playbackControlGlassBorder

                                FocusRing {
                                    inset: true
                                    active: chapterDelegate.isFocusedChapter && chapterList.activeFocus
                                    radius: parent.radius
                                    color: Theme.playbackFocusRing
                                }

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: Math.round(10 * Theme.layoutScale)
                                    spacing: Math.round(6 * Theme.layoutScale)

                                    Item {
                                        width: parent.width
                                        height: Math.round(width * 9 / 16)

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: Theme.radiusMedium
                                            color: Qt.rgba(0, 0, 0, 0.30)
                                            visible: !chapterThumbnail.ready
                                            Text {
                                                anchors.centerIn: parent
                                                text: Icons.movie
                                                font.family: Theme.fontIcon
                                                font.pixelSize: Math.round(36 * Theme.layoutScale)
                                                color: Theme.playbackTimeSecondary
                                            }
                                        }
                                        RoundedImage {
                                            id: chapterThumbnail
                                            anchors.fill: parent
                                            radius: Theme.radiusMedium
                                            source: chapterDelegate.modelData.thumbnailUrl || ""
                                            mode: "shader"
                                            preferPreRounded: false
                                            sourceWidth: 480
                                            sourceHeight: 270
                                            visible: ready
                                        }
                                        Rectangle {
                                            anchors.top: parent.top
                                            anchors.right: parent.right
                                            anchors.margins: Math.round(8 * Theme.layoutScale)
                                            width: Math.round(12 * Theme.layoutScale)
                                            height: width
                                            radius: width / 2
                                            color: Theme.playbackAccent
                                            visible: chapterDelegate.index === PlayerController.currentPlaybackChapterIndex
                                        }
                                    }

                                    Text {
                                        width: parent.width
                                        text: chapterDelegate.modelData.name || ""
                                        color: Theme.playbackTimePrimary
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: Math.round(18 * Theme.layoutScale)
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: root.formatTime((chapterDelegate.modelData.startMs || 0) / 1000)
                                        color: Theme.playbackTimeSecondary
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: Math.round(15 * Theme.layoutScale)
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.focusedChapterIndex = chapterDelegate.index
                                        chapterList.currentIndex = chapterDelegate.index
                                        chapterDelegate.forceActiveFocus()
                                        chapterDelegate.seekHere()
                                    }
                                }
                            }
                        }
                    }
                }

                FocusScope {
                    id: progressFocus
                    width: parent.width
                    height: Math.round(52 * Theme.layoutScale)
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Slider
                    Accessible.name: qsTr("Playback position")
                    Accessible.description: qsTr("%1 of %2")
                                            .arg(root.formatTime(PlayerController.currentPositionSeconds))
                                            .arg(root.formatTime(PlayerController.durationSeconds))
                    Keys.onLeftPressed: function(event) {
                        event.accepted = true
                        root.seekBy(-10, true)
                    }
                    Keys.onRightPressed: function(event) {
                        event.accepted = true
                        root.seekBy(10, true)
                    }

                    readonly property bool emphasized: activeFocus || progressMouse.containsMouse || root.scrubPressed

                    Text {
                        id: elapsedLabel
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.formatTime(PlayerController.currentPositionSeconds)
                        color: Theme.playbackTimePrimary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Math.round(21 * Theme.layoutScale)
                        font.weight: Font.Medium
                    }

                    Text {
                        id: remainingLabel
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: "-" + root.formatTime(Math.max(0, PlayerController.durationSeconds
                                                               - PlayerController.currentPositionSeconds))
                        color: Theme.playbackTimeSecondary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Math.round(21 * Theme.layoutScale)
                        font.weight: Font.Medium
                    }

                    Item {
                        id: progressTrackArea
                        anchors.left: elapsedLabel.right
                        anchors.right: remainingLabel.left
                        anchors.leftMargin: Math.round(22 * Theme.layoutScale)
                        anchors.rightMargin: Math.round(22 * Theme.layoutScale)
                        height: parent.height

                        Rectangle {
                            id: progressTrack
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: Math.round((progressFocus.emphasized ? 10 : 6) * Theme.layoutScale)
                            radius: height / 2
                            color: Theme.playbackProgressTrack
                            Behavior on height { NumberAnimation { duration: Theme.durationShort } }

                            Rectangle {
                                width: parent.width * PlayerController.cacheProgressRatio
                                height: parent.height
                                radius: parent.radius
                                color: Theme.playbackProgressCacheFill
                                visible: width > progressFill.width
                                Behavior on width {
                                    NumberAnimation { duration: 500; easing.type: Easing.OutCubic }
                                }
                            }

                            Rectangle {
                                id: progressFill
                                width: parent.width * PlayerController.progressRatio
                                height: parent.height
                                radius: parent.radius
                                color: Theme.playbackProgressFill
                            }

                            // Chapter boundaries from the provider chapter list.
                            Repeater {
                                model: PlayerController.durationSeconds > 0 ? PlayerController.playbackChapters : []
                                delegate: Rectangle {
                                    required property var modelData
                                    readonly property real startRatio: (modelData.startMs || 0) / 1000
                                                                       / PlayerController.durationSeconds
                                    visible: startRatio > 0.002 && startRatio < 0.998
                                    x: Math.round(progressTrack.width * startRatio - width / 2)
                                    width: Math.max(2, Math.round(3 * Theme.layoutScale))
                                    height: progressTrack.height
                                    color: Theme.withAlpha(Theme.playbackOverlayShadowTint, 0.85)
                                }
                            }

                            // Pointer scrub target.
                            Rectangle {
                                visible: root.scrubHovered || root.scrubPressed
                                x: Math.min(progressFill.width, scrubKnobX.value)
                                width: Math.abs(scrubKnobX.value - progressFill.width)
                                height: parent.height
                                radius: parent.radius
                                color: Theme.withAlpha(Theme.playbackHeroBackground, 0.28)
                            }
                        }

                        QtObject {
                            id: scrubKnobX
                            readonly property real value: PlayerController.durationSeconds > 0
                                                          ? progressTrack.width * Math.max(0, Math.min(1, root.scrubSeconds / PlayerController.durationSeconds))
                                                          : 0
                        }

                        Rectangle {
                            id: progressKnob
                            readonly property real centerX: root.scrubPressed ? scrubKnobX.value : progressFill.width
                            width: Math.round((progressFocus.emphasized ? 26 : 20) * Theme.layoutScale)
                            height: width
                            radius: width / 2
                            x: Math.round(centerX - width / 2)
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.playbackHeroBackground
                            border.width: Math.max(2, Math.round(3 * Theme.layoutScale))
                            border.color: Theme.playbackAccent
                            Behavior on width { NumberAnimation { duration: Theme.durationShort } }

                            FocusRing {
                                active: progressFocus.activeFocus
                                radius: parent.radius
                                color: Theme.playbackFocusRing
                                haloColor: Qt.rgba(0, 0, 0, 0.55)
                            }
                        }

                        // Hover time when there are no trickplay frames to show it.
                        Rectangle {
                            visible: (root.scrubHovered || root.scrubPressed) && !root.trickplayStripVisible
                            x: Math.max(-elapsedLabel.width,
                                        Math.min(progressTrackArea.width - width + remainingLabel.width,
                                                 scrubKnobX.value - width / 2))
                            y: -height + Math.round(4 * Theme.layoutScale)
                            width: hoverTimeLabel.implicitWidth + Math.round(24 * Theme.layoutScale)
                            height: Math.round(34 * Theme.layoutScale)
                            radius: height / 2
                            color: Theme.playbackPanelBackground
                            border.width: Theme.borderWidth
                            border.color: Theme.playbackControlGlassBorderStrong
                            Text {
                                id: hoverTimeLabel
                                anchors.centerIn: parent
                                text: root.formatTime(root.scrubSeconds)
                                color: Theme.playbackTimePrimary
                                font.family: Theme.fontPrimary
                                font.pixelSize: Math.round(18 * Theme.layoutScale)
                                font.weight: Font.DemiBold
                            }
                        }

                        MouseArea {
                            id: progressMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            function secondsAt(xValue) {
                                if (progressTrack.width <= 0 || PlayerController.durationSeconds <= 0) return 0
                                return PlayerController.durationSeconds
                                        * Math.max(0, Math.min(1, xValue / progressTrack.width))
                            }
                            onEntered: {
                                root.scrubSeconds = secondsAt(mouseX)
                                root.scrubHovered = true
                            }
                            onExited: root.scrubHovered = false
                            onPositionChanged: function(mouse) { root.scrubSeconds = secondsAt(mouse.x) }
                            onPressed: function(mouse) {
                                root.scrubSeconds = secondsAt(mouse.x)
                                root.scrubPressed = true
                            }
                            onReleased: {
                                if (!root.scrubPressed) return
                                root.scrubPressed = false
                                if (PlayerController.durationSeconds > 0) {
                                    PlayerController.seek(root.scrubSeconds)
                                    root.showSeekPreview(true)
                                }
                            }
                            onCanceled: root.scrubPressed = false
                        }
                    }
                }

                Item {
                    id: trayContainer
                    readonly property bool shown: !root.seekOnlyMode && !root.chapterMode
                    width: parent.width
                    height: shown ? transportTray.height + Math.round(10 * Theme.layoutScale) : 0
                    opacity: shown ? 1.0 : 0.0
                    visible: opacity > 0.001
                    Behavior on height { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
                    Behavior on opacity { NumberAnimation { duration: Theme.durationShort } }

                    Rectangle {
                        id: transportTray
                        readonly property int sidePadding: Math.round(26 * Theme.layoutScale)
                        readonly property int groupGap: Math.round(28 * Theme.layoutScale)
                        readonly property int volumeSliderWidth: Math.round(170 * Theme.layoutScale)
                        readonly property real sideSpace: (width - trayCenter.width) / 2
                        readonly property bool roomForVolumeSlider: sideSpace >= sidePadding + volumeButton.width
                                                                    + trayLeft.spacing + volumeSliderWidth + 2 * groupGap
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: Math.round(112 * Theme.layoutScale)
                        radius: Theme.radiusLarge
                        color: Theme.playbackChromeTray
                        border.width: Theme.borderWidth
                        border.color: Theme.playbackControlGlassBorder

                        Row {
                            id: trayLeft
                            anchors.left: parent.left
                            anchors.leftMargin: transportTray.sidePadding
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Math.round(12 * Theme.layoutScale)

                            ChromeIconButton {
                                id: volumeButton
                                glyph: root.volumeIcon()
                                text: qsTr("Volume")
                                onClicked: root.openVolumeSelector()
                            }

                            FocusScope {
                                id: volumeSliderFocus
                                anchors.verticalCenter: parent.verticalCenter
                                width: transportTray.volumeSliderWidth
                                height: Math.round(44 * Theme.layoutScale)
                                visible: transportTray.roomForVolumeSlider
                                // Display-only until the volume control is activated.
                                enabled: visible
                                Accessible.role: Accessible.Slider
                                Accessible.name: qsTr("Volume")
                                Accessible.description: PlayerController.muted ? qsTr("Muted") : qsTr("%1%").arg(PlayerController.volume)
                                Keys.onPressed: function(event) { root.handleVolumeKey(event) }

                                VolumeMeter {
                                    anchors.fill: parent
                                    emphasized: volumeSliderFocus.activeFocus || volumeSliderMouse.containsMouse
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.leftMargin: -Math.round(10 * Theme.layoutScale)
                                    anchors.rightMargin: -Math.round(10 * Theme.layoutScale)
                                    radius: height / 2
                                    color: "transparent"
                                    FocusRing {
                                        active: volumeSliderFocus.activeFocus
                                        radius: parent.radius
                                        color: Theme.playbackFocusRing
                                        haloColor: Qt.rgba(0, 0, 0, 0.55)
                                    }
                                }

                                // Level readout while adjusting (also shows boost above 100%).
                                Rectangle {
                                    visible: volumeSliderFocus.activeFocus || volumeSliderMouse.pressed
                                             || (volumeSliderMouse.containsMouse && PlayerController.volume > 100)
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.bottom: parent.top
                                    anchors.bottomMargin: Math.round(14 * Theme.layoutScale)
                                    width: volumeReadout.implicitWidth + Math.round(22 * Theme.layoutScale)
                                    height: Math.round(32 * Theme.layoutScale)
                                    radius: height / 2
                                    color: Theme.playbackPanelBackground
                                    border.width: Theme.borderWidth
                                    border.color: Theme.playbackControlGlassBorderStrong
                                    Text {
                                        id: volumeReadout
                                        anchors.centerIn: parent
                                        text: PlayerController.muted ? qsTr("Muted") : qsTr("%1%").arg(PlayerController.volume)
                                        color: Theme.playbackTimePrimary
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: Math.round(16 * Theme.layoutScale)
                                        font.weight: Font.DemiBold
                                    }
                                }

                                MouseArea {
                                    id: volumeSliderMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    function apply(xValue) {
                                        PlayerController.setVolume(Math.round(100 * Math.max(0, Math.min(1, xValue / width))))
                                        if (PlayerController.muted && xValue > 0) {
                                            PlayerController.setMuted(false)
                                        }
                                    }
                                    onPressed: function(mouse) { apply(mouse.x) }
                                    onPositionChanged: function(mouse) { if (pressed) apply(mouse.x) }
                                    onWheel: function(wheel) {
                                        root.adjustVolumeBy(wheel.angleDelta.y > 0 ? 5 : -5)
                                    }
                                }
                            }
                        }

                        Rectangle {
                            readonly property real targetX: trayCenter.x - transportTray.groupGap
                            visible: targetX > trayLeft.x + trayLeft.width + Math.round(8 * Theme.layoutScale)
                            x: Math.round(targetX)
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.borderWidth
                            height: Math.round(48 * Theme.layoutScale)
                            color: Theme.playbackChromeDivider
                        }

                        Row {
                            id: trayCenter
                            anchors.centerIn: parent
                            spacing: Math.round(14 * Theme.layoutScale)

                            ChromeIconButton {
                                id: previousEpisodeButton
                                anchors.verticalCenter: parent.verticalCenter
                                visible: PlayerController.hasEpisodeNavigation
                                enabled: PlayerController.hasPreviousEpisode
                                glyph: Icons.skipPrevious
                                text: qsTr("Previous episode")
                                onClicked: PlayerController.skipToPreviousEpisode()
                            }
                            ChromeIconButton {
                                id: previousChapterButton
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: Icons.fastRewind
                                text: qsTr("Previous chapter")
                                onClicked: PlayerController.previousChapter()
                            }
                            ChromeIconButton {
                                id: skipBackButton
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: Icons.replay10
                                text: qsTr("Back 10 seconds")
                                onClicked: root.seekBy(-10, true)
                            }
                            HeroPlayButton {
                                id: playPauseButton
                                anchors.verticalCenter: parent.verticalCenter
                                showsPlay: root.paused
                                text: root.paused ? qsTr("Play") : qsTr("Pause")
                                onClicked: PlayerController.togglePause()
                            }
                            ChromeIconButton {
                                id: skipForwardButton
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: Icons.forward10
                                text: qsTr("Forward 10 seconds")
                                onClicked: root.seekBy(10, true)
                            }
                            ChromeIconButton {
                                id: nextChapterButton
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: Icons.fastForward
                                text: qsTr("Next chapter")
                                onClicked: PlayerController.nextChapter()
                            }
                            ChromeIconButton {
                                id: nextEpisodeButton
                                anchors.verticalCenter: parent.verticalCenter
                                visible: PlayerController.hasEpisodeNavigation
                                enabled: PlayerController.hasNextEpisode
                                glyph: Icons.skipNext
                                text: qsTr("Next episode")
                                onClicked: PlayerController.skipToNextEpisode()
                            }
                        }

                        Rectangle {
                            readonly property real targetX: trayCenter.x + trayCenter.width + transportTray.groupGap
                            visible: targetX < trayRight.x - Math.round(8 * Theme.layoutScale)
                            x: Math.round(targetX)
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.borderWidth
                            height: Math.round(48 * Theme.layoutScale)
                            color: Theme.playbackChromeDivider
                        }

                        Row {
                            id: trayRight
                            anchors.right: parent.right
                            anchors.rightMargin: transportTray.sidePadding
                            anchors.verticalCenter: parent.verticalCenter

                            Button {
                                id: settingsButton
                                function trigger() { settingsButton.clicked() }
                                width: Math.round(104 * Theme.layoutScale)
                                height: Math.round(64 * Theme.layoutScale)
                                focusPolicy: Qt.StrongFocus
                                activeFocusOnTab: true
                                text: qsTr("Playback settings")
                                scale: down ? 0.95 : 1.0
                                onPressed: root.showControls()
                                onClicked: root.openSettingsMenu("root")
                                Keys.onReturnPressed: function(event) { event.accepted = true; settingsButton.animateClick() }
                                Keys.onEnterPressed: function(event) { event.accepted = true; settingsButton.animateClick() }
                                Keys.onSpacePressed: function(event) { event.accepted = true; settingsButton.animateClick() }
                                ToolTip.visible: hovered && InputModeManager.pointerActive && !root.settingsMenuOpen
                                ToolTip.text: text
                                ToolTip.delay: 600
                                background: Rectangle {
                                    radius: Math.round(18 * Theme.layoutScale)
                                    color: settingsButton.activeFocus || settingsButton.down || root.settingsMenuOpen
                                           ? Theme.playbackControlGlassBackgroundHover
                                           : Theme.playbackControlGlassBackground
                                    border.width: Theme.borderWidth
                                    border.color: root.settingsMenuOpen ? Theme.playbackAccent : Theme.playbackControlGlassBorderStrong
                                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                                    FocusRing {
                                        active: settingsButton.activeFocus
                                        radius: parent.radius
                                        color: Theme.playbackFocusRing
                                        haloColor: Qt.rgba(0, 0, 0, 0.55)
                                    }
                                }
                                contentItem: Text {
                                    text: Icons.tune
                                    font.family: Theme.fontIcon
                                    font.pixelSize: Math.round(34 * Theme.layoutScale)
                                    color: Theme.playbackIconColor
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                Behavior on scale { NumberAnimation { duration: 90 } }
                            }
                        }
                    }
                }
            }
        }
    }

    // Single skip control: an auto-focused prompt while the chrome is hidden,
    // then a persistent target above the bar until the segment ends.
    SkipPillButton {
        id: skipButton
        z: 640
        x: Math.round((root.controlsVisible ? bottomPanel.x + bottomPanel.width : root.width - Math.round(72 * Theme.layoutScale)) - width)
        y: Math.round(root.controlsVisible
                      ? bottomPanel.y - height - Math.round(18 * Theme.layoutScale)
                      : root.height - height - Math.round(72 * Theme.layoutScale))
        emphasized: root.skipPopupVisible
        labelText: root.activeSkipLabel
        enabled: root.skipButtonShown
        opacity: root.skipButtonShown ? 1.0 : 0.0
        visible: opacity > 0.001
        onClicked: root.triggerActiveSkip()
        Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
        Behavior on x { NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic } }
        Behavior on y { NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic } }
    }

    // Volume OSD while the chrome is hidden; also the adjustment surface when
    // the bar is too narrow for its inline slider.
    Rectangle {
        id: volumePanel
        z: 1200
        visible: opacity > 0.001
        opacity: root.volumePanelVisible ? 1.0 : 0.0
        width: Math.round(380 * Theme.layoutScale)
        height: Math.round(72 * Theme.layoutScale)
        x: Math.round((root.width - width) / 2)
        y: Math.round(root.controlsVisible
                      ? bottomPanel.y - height - Math.round(18 * Theme.layoutScale)
                      : root.height - height - Math.round(80 * Theme.layoutScale))
        radius: height / 2
        color: Theme.playbackChromeSurface
        border.width: Theme.borderWidth
        border.color: Theme.playbackControlGlassBorder
        Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

        FocusScope {
            id: volumePanelFocus
            anchors.fill: parent
            enabled: root.volumeSelectorOpen && !root.inlineVolumeSliderAvailable
            Accessible.role: Accessible.Slider
            Accessible.name: qsTr("Volume")
            Keys.onPressed: function(event) { root.handleVolumeKey(event) }

            FocusRing {
                active: volumePanelFocus.activeFocus
                radius: volumePanel.radius
                color: Theme.playbackFocusRing
                haloColor: Qt.rgba(0, 0, 0, 0.55)
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Math.round(24 * Theme.layoutScale)
                anchors.rightMargin: Math.round(24 * Theme.layoutScale)
                spacing: Math.round(16 * Theme.layoutScale)

                Text {
                    text: root.volumeIcon()
                    font.family: Theme.fontIcon
                    font.pixelSize: Math.round(30 * Theme.layoutScale)
                    color: Theme.playbackIconColor
                    Layout.alignment: Qt.AlignVCenter
                }
                VolumeMeter {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    emphasized: volumePanelFocus.activeFocus || volumePanelMouse.containsMouse

                    MouseArea {
                        id: volumePanelMouse
                        anchors.fill: parent
                        anchors.topMargin: -Math.round(12 * Theme.layoutScale)
                        anchors.bottomMargin: -Math.round(12 * Theme.layoutScale)
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        function apply(xValue) {
                            PlayerController.setVolume(Math.round(100 * Math.max(0, Math.min(1, xValue / width))))
                            if (PlayerController.muted && xValue > 0) {
                                PlayerController.setMuted(false)
                            }
                            // Keep the OSD up while the pointer drives it.
                            if (root.volumeOsdVisible) {
                                volumeOsdTimer.restart()
                            }
                        }
                        onPressed: function(mouse) { apply(mouse.x) }
                        onPositionChanged: function(mouse) { if (pressed) apply(mouse.x) }
                        onWheel: function(wheel) {
                            root.adjustVolumeBy(wheel.angleDelta.y > 0 ? 5 : -5)
                        }
                    }
                }
                Text {
                    text: PlayerController.muted ? qsTr("Muted") : qsTr("%1%").arg(PlayerController.volume)
                    color: Theme.playbackTimePrimary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Math.round(19 * Theme.layoutScale)
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignRight
                    Layout.preferredWidth: Math.round(72 * Theme.layoutScale)
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }
    }

    // Settings pop-out: subtitles (with delay), audio track, quality and
    // audio output, opened from the bar's settings control or the direct
    // audio/subtitle shortcuts.
    Rectangle {
        id: settingsMenu
        readonly property int menuPadding: Math.round(16 * Theme.layoutScale)
        readonly property int rowHeight: Math.round(76 * Theme.layoutScale)
        readonly property int delayRowHeight: Math.round(96 * Theme.layoutScale)
        readonly property int rowSpacing: Math.round(4 * Theme.layoutScale)
        readonly property real rowsHeight: {
            var rows = root.settingsRows
            var total = 0
            for (var i = 0; i < rows.length; i++) {
                total += rows[i].kind === "delay" ? delayRowHeight : rowHeight
            }
            return total + Math.max(0, rows.length - 1) * rowSpacing
        }
        readonly property real maxHeight: Math.max(rowHeight * 3, bottomPanel.y - Math.round(56 * Theme.layoutScale))
        z: 1200
        width: Math.min(Math.round(580 * Theme.layoutScale), root.width - 2 * bottomPanel.sideMargin)
        height: Math.min(maxHeight, menuHeader.height + rowsHeight + 2 * menuPadding + Math.round(8 * Theme.layoutScale))
        x: Math.round(bottomPanel.x + bottomPanel.width - width)
        y: Math.round(Math.max(Math.round(24 * Theme.layoutScale),
                               bottomPanel.y - height - Math.round(16 * Theme.layoutScale)))
        radius: Theme.radiusXLarge
        color: Theme.playbackPanelBackground
        border.width: Theme.borderWidth
        border.color: Theme.playbackControlGlassBorder
        opacity: root.settingsMenuOpen ? 1.0 : 0.0
        scale: root.settingsMenuOpen ? 1.0 : 0.96
        transformOrigin: Item.BottomRight
        visible: opacity > 0.001
        enabled: root.settingsMenuOpen
        Behavior on opacity { NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
        Behavior on height { NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic } }

        HoverHandler { id: settingsMenuHover }

        Item {
            id: menuHeader
            x: settingsMenu.menuPadding
            y: settingsMenu.menuPadding
            width: parent.width - 2 * settingsMenu.menuPadding
            height: Math.round(56 * Theme.layoutScale)

            ChromeIconButton {
                id: settingsBackButton
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                visible: root.settingsPage !== "root"
                diameter: Math.round(48 * Theme.layoutScale)
                iconSize: Math.round(28 * Theme.layoutScale)
                focusPolicy: Qt.NoFocus
                glyph: Icons.chevronLeft
                text: qsTr("Back")
                onClicked: root.showSettingsRoot()
            }

            Text {
                anchors.left: settingsBackButton.visible ? settingsBackButton.right : parent.left
                anchors.leftMargin: settingsBackButton.visible ? Math.round(8 * Theme.layoutScale) : Math.round(12 * Theme.layoutScale)
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.settingsPageTitle(root.settingsPage)
                color: Theme.playbackTimePrimary
                font.family: Theme.fontPrimary
                font.pixelSize: Math.round(25 * Theme.layoutScale)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }

        ListView {
            id: settingsList
            x: settingsMenu.menuPadding
            anchors.top: menuHeader.bottom
            anchors.topMargin: Math.round(8 * Theme.layoutScale)
            anchors.bottom: parent.bottom
            anchors.bottomMargin: settingsMenu.menuPadding
            width: parent.width - 2 * settingsMenu.menuPadding
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: settingsMenu.rowSpacing
            model: root.settingsRows
            focus: root.settingsMenuOpen
            Accessible.role: Accessible.List
            Accessible.name: root.settingsPageTitle(root.settingsPage)
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Keys.onPressed: function(event) {
                var handled = true
                if (event.key === Qt.Key_Up) {
                    root.settingsDirection("up")
                } else if (event.key === Qt.Key_Down) {
                    root.settingsDirection("down")
                } else if (event.key === Qt.Key_Left) {
                    root.settingsDirection("left")
                } else if (event.key === Qt.Key_Right) {
                    root.settingsDirection("right")
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    root.activateSettingsRow(root.settingsCursor)
                } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
                    root.settingsBack()
                } else {
                    handled = false
                }
                event.accepted = handled
            }

            delegate: Item {
                id: settingsRow
                required property int index
                required property var modelData
                readonly property string kind: modelData.kind
                readonly property bool isCurrent: index === root.settingsCursor
                readonly property bool isSelected: root.isSettingsRowSelected(modelData)
                width: settingsList.width
                height: kind === "delay" ? settingsMenu.delayRowHeight : settingsMenu.rowHeight
                Accessible.role: kind === "page" ? Accessible.Button : (kind === "delay" ? Accessible.Slider : Accessible.ListItem)
                Accessible.name: rowPrimary.text
                Accessible.description: rowSecondary.text

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusMedium
                    color: settingsRow.isCurrent
                           ? Theme.playbackControlGlassBackgroundHover
                           : (settingsRow.isSelected ? Theme.withAlpha(Theme.playbackAccent, 0.12) : "transparent")
                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }

                    FocusRing {
                        inset: true
                        active: settingsRow.isCurrent && settingsList.activeFocus && !InputModeManager.pointerActive
                        radius: parent.radius
                        color: Theme.playbackFocusRing
                    }
                }

                // Selected option indicator.
                Rectangle {
                    visible: settingsRow.isSelected
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(6 * Theme.layoutScale)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.selectionIndicatorWidth
                    height: Math.round(parent.height * 0.46)
                    radius: width / 2
                    color: Theme.playbackAccent
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: {
                        if (InputModeManager.pointerActive) root.settingsCursor = settingsRow.index
                    }
                    onClicked: {
                        settingsList.forceActiveFocus()
                        if (settingsRow.kind !== "delay") root.activateSettingsRow(settingsRow.index)
                        else root.settingsCursor = settingsRow.index
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Math.round(22 * Theme.layoutScale)
                    anchors.rightMargin: Math.round(14 * Theme.layoutScale)
                    spacing: Math.round(14 * Theme.layoutScale)

                    Text {
                        visible: settingsRow.kind === "page"
                        text: settingsRow.modelData.icon || ""
                        font.family: Theme.fontIcon
                        font.pixelSize: Math.round(28 * Theme.layoutScale)
                        color: Theme.playbackTimeSecondary
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Column {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: Math.round(3 * Theme.layoutScale)

                        Text {
                            id: rowPrimary
                            width: parent.width
                            text: {
                                var row = settingsRow.modelData
                                if (row.kind === "page") return root.settingsPageTitle(row.page)
                                if (row.kind === "subtitle" || row.kind === "audio") return root.trackPrimaryLabel(row.track)
                                if (row.kind === "version") return row.version.title || row.version.subtitle || qsTr("Version")
                                if (row.kind === "output") return root.audioOutputPrimaryLabel(row.entry)
                                if (row.kind === "delay") return qsTr("Subtitle Delay")
                                return ""
                            }
                            color: settingsRow.kind === "output" && !settingsRow.modelData.entry.available
                                   ? Theme.playbackTimeSecondary
                                   : Theme.playbackTimePrimary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Math.round(21 * Theme.layoutScale)
                            font.weight: settingsRow.isSelected ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                        }
                        Text {
                            id: rowSecondary
                            width: parent.width
                            text: {
                                var row = settingsRow.modelData
                                if (row.kind === "subtitle") return root.trackSecondaryLabel(row.track, true)
                                if (row.kind === "audio") return root.trackSecondaryLabel(row.track, false)
                                if (row.kind === "version") return row.version.title ? (row.version.subtitle || "") : ""
                                if (row.kind === "output") return root.audioOutputSecondaryLabel(row.entry)
                                if (row.kind === "page" && row.page === "output") return root.audioOutputDetail()
                                if (row.kind === "delay") return InputModeManager.pointerActive
                                                                ? qsTr("%1 ms steps").arg(PlayerController.subtitleDelayStepMs)
                                                                : qsTr("Left/Right to adjust")
                                return ""
                            }
                            visible: text.length > 0
                            color: Theme.playbackTimeSecondary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Math.round(16 * Theme.layoutScale)
                            elide: Text.ElideRight
                        }
                    }

                    // Current value of a category row.
                    Text {
                        visible: settingsRow.kind === "page" && text.length > 0
                        text: settingsRow.kind === "page" ? root.settingsPageValue(settingsRow.modelData.page) : ""
                        color: Theme.playbackTimeSecondary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Math.round(18 * Theme.layoutScale)
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignRight
                        Layout.maximumWidth: Math.round(settingsRow.width * 0.42)
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Text {
                        visible: settingsRow.kind === "page" || settingsRow.isSelected
                        text: settingsRow.kind === "page" ? Icons.chevronRight : Icons.check
                        font.family: Theme.fontIcon
                        font.pixelSize: Math.round(26 * Theme.layoutScale)
                        color: settingsRow.kind === "page" ? Theme.playbackTimeSecondary : Theme.playbackAccent
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Row {
                        visible: settingsRow.kind === "delay"
                        spacing: Math.round(8 * Theme.layoutScale)
                        Layout.alignment: Qt.AlignVCenter

                        ChromeIconButton {
                            id: subtitleDelayDecreaseButton
                            anchors.verticalCenter: parent.verticalCenter
                            diameter: Math.round(44 * Theme.layoutScale)
                            iconSize: Math.round(24 * Theme.layoutScale)
                            filled: true
                            focusPolicy: Qt.NoFocus
                            glyph: Icons.remove
                            text: qsTr("Earlier")
                            autoRepeat: true
                            autoRepeatDelay: 260
                            autoRepeatInterval: 35
                            onClicked: root.adjustSubtitleDelayBy(-PlayerController.subtitleDelayStepMs)
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.round(104 * Theme.layoutScale)
                            text: root.formatSubtitleDelay(PlayerController.subtitleDelayMs)
                            color: Theme.playbackTimePrimary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Math.round(21 * Theme.layoutScale)
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                        ChromeIconButton {
                            id: subtitleDelayIncreaseButton
                            anchors.verticalCenter: parent.verticalCenter
                            diameter: Math.round(44 * Theme.layoutScale)
                            iconSize: Math.round(24 * Theme.layoutScale)
                            filled: true
                            focusPolicy: Qt.NoFocus
                            glyph: Icons.add
                            text: qsTr("Later")
                            autoRepeat: true
                            autoRepeatDelay: 260
                            autoRepeatInterval: 35
                            onClicked: root.adjustSubtitleDelayBy(PlayerController.subtitleDelayStepMs)
                        }
                        ChromeIconButton {
                            id: subtitleDelayResetButton
                            anchors.verticalCenter: parent.verticalCenter
                            diameter: Math.round(44 * Theme.layoutScale)
                            iconSize: Math.round(22 * Theme.layoutScale)
                            filled: true
                            focusPolicy: Qt.NoFocus
                            enabled: PlayerController.subtitleDelayMs !== 0
                            glyph: Icons.replay
                            text: qsTr("Reset")
                            onClicked: PlayerController.resetSubtitleDelay()
                        }
                    }
                }
            }
        }
    }

    // Transient notice for output fallbacks (device missing, exclusive refused).
    Rectangle {
        id: audioNotice
        z: 1300
        visible: opacity > 0
        opacity: root.overlayActive && root.audioNoticeText.length > 0 ? 1.0 : 0.0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.round(48 * Theme.layoutScale)
        width: Math.min(audioNoticeLabel.implicitWidth + Math.round(48 * Theme.layoutScale),
                        root.width - Math.round(96 * Theme.layoutScale))
        height: audioNoticeLabel.implicitHeight + Math.round(28 * Theme.layoutScale)
        radius: Theme.radiusMedium
        color: Theme.playbackPanelBackground
        border.width: 1
        border.color: Theme.playbackControlGlassBorder
        Accessible.role: Accessible.AlertMessage
        Accessible.name: root.audioNoticeText
        Behavior on opacity { NumberAnimation { duration: Theme.durationShort } }

        Text {
            id: audioNoticeLabel
            anchors.centerIn: parent
            width: Math.min(implicitWidth, root.width - Math.round(144 * Theme.layoutScale))
            text: root.audioNoticeText
            color: Theme.playbackTimePrimary
            font.family: Theme.fontPrimary
            font.pixelSize: Math.round(22 * Theme.layoutScale)
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        Timer {
            id: audioNoticeTimer
            interval: 5000
            onTriggered: root.audioNoticeText = ""
        }

        Connections {
            target: PlayerController
            function onAudioOutputNotice(message) {
                root.audioNoticeText = message
                audioNoticeTimer.restart()
            }
        }
    }

    Item {
        id: overlayRoot
        visible: !root.buffering
        enabled: root.controlsVisible
        anchors.fill: parent
        opacity: root.controlsVisible ? 1.0 : 0.0
        Behavior on opacity {
            NumberAnimation { duration: root.controlsHideAnimMs; easing.type: Easing.OutCubic }
        }
    }
}
