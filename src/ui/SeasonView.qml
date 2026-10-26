import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

/**
 * SeasonView - a single season: hero with the series logo and a Play action,
 * then the season's episodes as a landscape shelf.
 *
 * LibraryScreen browses seasons through SeriesSeasonEpisodeView; this page
 * keeps the same hero grammar for callers that open one season directly.
 */
FocusScope {
    id: root

    // Input properties
    property string seasonId: ""
    property string seasonName: ""
    property int seasonNumber: 0
    property string seriesId: ""
    property string seriesName: ""
    property string seriesLogoUrl: ""
    property string seasonPosterUrl: ""
    property string seriesPosterUrl: ""
    property string seriesBackdropUrl: ""
    property int pendingEpisodeIndex: -1  // Index to restore when returning from episode view

    // Signals for navigation and actions
    signal episodeSelected(var episodeData)
    signal playEpisode(string episodeId, var startPositionMs)
    signal backRequested()

    readonly property bool isLoading: SeriesDetailsViewModel.isLoading
    readonly property int episodeCount: episodesList.count
    // Theme.heroCopyMaxWidth
    readonly property int heroBlockWidth: Math.min(Math.round(1040 * Theme.layoutScale),
                                                   root.width - Theme.pageMarginX * 2)
    /// The navigation bar recedes once the page scrolls into the shelves.
    readonly property bool navigationRecessed: contentFlickable.contentY > Theme.navBarReservedHeight

    focus: true

    function formatRuntime(durationMs) {
        if (!durationMs || durationMs === 0) return ""
        var minutes = Math.round(durationMs / 60000)
        return minutes >= 60 ? Math.floor(minutes / 60) + "h " + (minutes % 60) + "m" : minutes + "m"
    }

    function episodeSubtitle(episodeNumber, durationMs) {
        var code = episodeNumber > 0 ? qsTr("S%1 E%2").arg(root.seasonNumber).arg(episodeNumber) : qsTr("Special")
        var runtime = formatRuntime(durationMs)
        return runtime !== "" ? code + "  ·  " + runtime : code
    }

    function restorePendingEpisode() {
        if (pendingEpisodeIndex < 0 || episodesList.count <= 0) {
            return false
        }
        episodesList.currentIndex = Math.min(pendingEpisodeIndex, episodesList.count - 1)
        episodesList.forceActiveFocus()
        pendingEpisodeIndex = -1
        return true
    }

    // Plays the first unplayed episode, or the first episode when all are watched.
    function playSeason() {
        for (var i = 0; i < episodeCount; i++) {
            var ep = SeriesDetailsViewModel.episodesModel.getItem(i)
            if (ep && !ep.isPlayed) {
                root.playEpisode(ep.itemId, ep.positionMs || 0)
                return
            }
        }
        if (episodeCount > 0) {
            var firstEp = SeriesDetailsViewModel.episodesModel.getItem(0)
            if (firstEp) {
                root.playEpisode(firstEp.itemId, 0)
            }
        }
    }

    function scrollTo(y) {
        var maxY = Math.max(0, contentFlickable.contentHeight - contentFlickable.height)
        scrollAnimation.to = Math.max(0, Math.min(maxY, y))
        scrollAnimation.restart()
    }

    onPendingEpisodeIndexChanged: Qt.callLater(restorePendingEpisode)

    onSeasonIdChanged: {
        if (seasonId !== "") {
            SeriesDetailsViewModel.loadSeasonEpisodes(seasonId)
        }
    }

    Keys.onPressed: (event) => {
        if (event.isAutoRepeat) {
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            root.backRequested()
            event.accepted = true
        }
    }

    onActiveFocusChanged: {
        if (activeFocus) {
            Qt.callLater(function() {
                if (root.restorePendingEpisode())
                    return
                if (playButton.enabled)
                    playButton.forceActiveFocus()
                else if (episodesList.count > 0)
                    episodesList.forceActiveFocus()
            })
        }
    }

    StageBackdrop {
        anchors.fill: parent
        z: -1
        mode: root.seriesBackdropUrl !== "" ? "hero" : "ambient"
        source: root.seriesBackdropUrl || root.seriesPosterUrl || root.seasonPosterUrl
        dim: Math.min(0.72, contentFlickable.contentY / Math.max(1, heroArea.height) * 0.72)
    }

    StateMessage {
        anchors.centerIn: parent
        visible: root.isLoading && root.episodeCount === 0
        z: 100
        kind: "loading"
        message: qsTr("Loading episodes…")
    }

    NumberAnimation {
        id: scrollAnimation
        target: contentFlickable
        property: "contentY"
        duration: Theme.durationLong
        easing.type: Theme.easingStandard
    }

    Flickable {
        id: contentFlickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        ColumnLayout {
            id: contentColumn
            width: contentFlickable.width
            spacing: Theme.sectionSpacing

            Item {
                id: heroArea
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Math.round(root.height * 0.60),
                                                 Theme.navBarReservedHeight + detailHero.height)

                DetailHero {
                    id: detailHero
                    x: Theme.pageMarginX
                    anchors.bottom: parent.bottom
                    width: root.heroBlockWidth
                    eyebrow: root.seasonNumber > 0 ? qsTr("Season %1").arg(root.seasonNumber) : qsTr("Specials")
                    logoSource: root.seriesLogoUrl
                    title: root.seriesName
                    subtitle: root.seasonName
                    metadataItems: [root.episodeCount === 1 ? qsTr("1 Episode")
                                    : (root.episodeCount > 0 ? qsTr("%1 Episodes").arg(root.episodeCount) : "")]
                    primaryAction: playButton

                    ActionButton {
                        id: playButton
                        variant: "primary"
                        text: qsTr("Play")
                        iconGlyph: Icons.playArrow
                        enabled: root.episodeCount > 0
                        implicitHeight: Theme.buttonHeightHero
                        labelPixelSize: Theme.fontSizeMedium
                        iconPixelSize: Theme.iconSizeLarge
                        sidePadding: Math.round(40 * Theme.layoutScale)

                        onActiveFocusChanged: if (activeFocus) root.scrollTo(0)
                        // Top-most control: Up goes on to the navigation bar.
                        Keys.onUpPressed: (event) => {
                            if (detailHero.readMoreButton.visible) {
                                detailHero.readMoreButton.forceActiveFocus()
                                event.accepted = true
                            } else {
                                event.accepted = false
                            }
                        }
                        Keys.onDownPressed: (event) => {
                            if (episodesList.count > 0)
                                episodesList.forceActiveFocus()
                            event.accepted = true
                        }
                        Keys.onReturnPressed: (event) => {
                            if (!event.isAutoRepeat && enabled)
                                clicked()
                            event.accepted = true
                        }
                        Keys.onEnterPressed: (event) => {
                            if (!event.isAutoRepeat && enabled)
                                clicked()
                            event.accepted = true
                        }
                        onClicked: root.playSeason()
                    }
                }
            }

            ColumnLayout {
                id: episodesSection
                Layout.fillWidth: true
                spacing: Theme.sectionTitleSpacing
                visible: episodesList.count > 0

                SectionHeader {
                    text: qsTr("Episodes")
                    Layout.leftMargin: Theme.pageMarginX
                    Layout.rightMargin: Theme.pageMarginX
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                ShelfList {
                    id: episodesList
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    cardHeight: Theme.landscapeCardHeight
                    model: SeriesDetailsViewModel.episodesModel

                    delegate: MediaCard {
                        required property int index
                        required property var name
                        required property var imageUrl
                        required property var indexNumber
                        required property var isPlayed
                        required property var model
                        variant: "landscape"
                        artworkSources: [imageUrl || ""]
                        title: name || ""
                        subtitle: root.episodeSubtitle(indexNumber, model.durationMs || 0)
                        placeholderIcon: Icons.tvShows
                        positionMs: isPlayed ? 0 : (model.positionMs || 0)
                        durationMs: model.durationMs || 0
                        showWatchState: !!isPlayed
                        watched: !!isPlayed
                        focused: ListView.isCurrentItem && ListView.view.activeFocus
                        onClicked: {
                            episodesList.currentIndex = index
                            episodesList.forceActiveFocus()
                            root.episodeSelected(model.modelData)
                        }
                    }

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            var top = episodesSection.mapToItem(contentColumn, 0, 0).y
                            root.scrollTo(top + episodesSection.height + Theme.spacingLarge - contentFlickable.height)
                        }
                    }
                    Keys.onUpPressed: (event) => {
                        playButton.forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onDownPressed: (event) => { event.accepted = true }
                    Keys.onReturnPressed: (event) => {
                        var item = currentIndex >= 0 ? SeriesDetailsViewModel.episodesModel.getItem(currentIndex) : null
                        if (!event.isAutoRepeat && item)
                            root.episodeSelected(item)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        var item = currentIndex >= 0 ? SeriesDetailsViewModel.episodesModel.getItem(currentIndex) : null
                        if (!event.isAutoRepeat && item)
                            root.episodeSelected(item)
                        event.accepted = true
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.pageMarginBottom }
        }
    }

    WheelStepScroller {
        anchors.fill: contentFlickable
        target: contentFlickable
        stepPx: Math.round(140 * Theme.layoutScale)
    }

    // Restore the episode index once the season's episodes arrive.
    Connections {
        target: SeriesDetailsViewModel.episodesModel
        function onModelReset() {
            Qt.callLater(root.restorePendingEpisode)
        }
    }
}
