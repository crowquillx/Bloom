pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import BloomUI

/**
 * ChapterTimeline - compact chapter picker for chapters without images.
 *
 * Draws the runtime as a track with a tick at each chapter start and a marker
 * on the current chapter. Left/Right moves between chapters, Enter (or a click
 * on the current chapter) emits chapterActivated. Up/Down are left to the
 * owning view.
 *
 * Usage:
 *   ChapterTimeline {
 *       chapters: focusedEpisodeChapters
 *       durationMs: selectedEpisodeDurationMs
 *       positionMs: selectedEpisodePlaybackPosition
 *       onChapterActivated: (startMs) => startPlayback(false, this, startMs)
 *   }
 */
FocusScope {
    id: root

    property var chapters: []
    property real durationMs: 0
    property real positionMs: 0
    property int currentIndex: 0

    signal chapterActivated(real startMs)

    readonly property int count: chapters ? chapters.length : 0
    readonly property var currentChapter: count > 0 ? chapters[Math.max(0, Math.min(currentIndex, count - 1))] : null
    // Chapter starts can run past a missing or short runtime; keep every tick on the track.
    readonly property real totalMs: {
        var lastStart = count > 0 ? Number(chapters[count - 1].startMs || 0) : 0
        return Math.max(durationMs, lastStart + 1)
    }
    readonly property string currentLabel: currentChapter
                                           ? qsTr("%1 · %2").arg(currentChapter.name || qsTr("Chapter %1").arg(currentIndex + 1))
                                                            .arg(formatTime(currentChapter.startMs || 0))
                                           : ""

    readonly property int markerSize: Math.round(20 * Theme.layoutScale)
    readonly property int tickHeight: Math.round(16 * Theme.layoutScale)
    readonly property int tickWidth: Math.max(2, Math.round(3 * Theme.layoutScale))
    // Room at the track ends for the marker and its focus ring.
    readonly property int trackInset: Math.round(markerSize / 2) + Theme.focusRingGap + Theme.focusRingWidth

    implicitHeight: content.implicitHeight
    activeFocusOnTab: true

    Accessible.role: Accessible.Slider
    Accessible.name: qsTr("Chapters")
    Accessible.description: currentLabel

    function formatTime(ms) {
        var total = Math.max(0, Math.floor((ms || 0) / 1000))
        var hours = Math.floor(total / 3600)
        var minutes = Math.floor((total % 3600) / 60)
        var seconds = total % 60
        function pad(value) { return value < 10 ? "0" + value : "" + value }
        return hours > 0 ? hours + ":" + pad(minutes) + ":" + pad(seconds)
                         : minutes + ":" + pad(seconds)
    }

    function fractionAt(index) {
        if (index < 0 || index >= count) {
            return 0
        }
        return Math.max(0, Math.min(1, Number(chapters[index].startMs || 0) / totalMs))
    }

    // Start on the chapter that contains the resume point.
    function chapterIndexAt(ms) {
        var found = 0
        for (var i = 0; i < count; ++i) {
            if (Number(chapters[i].startMs || 0) <= ms) {
                found = i
            }
        }
        return found
    }

    function nearestChapterIndex(fraction) {
        var best = 0
        var bestDistance = Number.MAX_VALUE
        for (var i = 0; i < count; ++i) {
            var distance = Math.abs(fractionAt(i) - fraction)
            if (distance < bestDistance) {
                best = i
                bestDistance = distance
            }
        }
        return best
    }

    function activateCurrent() {
        if (currentChapter) {
            chapterActivated(Number(currentChapter.startMs || 0))
        }
    }

    onChaptersChanged: currentIndex = chapterIndexAt(positionMs)

    Keys.onLeftPressed: (event) => {
        if (currentIndex > 0) {
            currentIndex -= 1
            event.accepted = true
        } else {
            event.accepted = false
        }
    }

    Keys.onRightPressed: (event) => {
        if (currentIndex + 1 < count) {
            currentIndex += 1
            event.accepted = true
        } else {
            event.accepted = false
        }
    }

    Keys.onReturnPressed: (event) => {
        if (!event.isAutoRepeat) {
            activateCurrent()
        }
        event.accepted = true
    }

    Keys.onEnterPressed: (event) => {
        if (!event.isAutoRepeat) {
            activateCurrent()
        }
        event.accepted = true
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Text {
                text: qsTr("Chapters")
                font.pixelSize: Theme.fontSizeHeader
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightHeavy
                color: Theme.textPrimary
            }

            Item { Layout.fillWidth: true }

            Text {
                text: root.currentLabel
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                font.weight: root.activeFocus ? Theme.fontWeightStrong : Theme.fontWeightRegular
                color: root.activeFocus ? Theme.textPrimary : Theme.textSecondary
                elide: Text.ElideRight
                Layout.maximumWidth: content.width * 0.6
            }
        }

        Item {
            id: trackArea
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(44 * Theme.layoutScale)

            Rectangle {
                id: track
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: root.trackInset
                anchors.rightMargin: root.trackInset
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.progressHeight
                radius: height / 2
                color: Theme.progressTrack

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * Math.max(0, Math.min(1, root.positionMs / root.totalMs))
                    radius: parent.radius
                    color: Theme.progressFill
                    visible: root.positionMs > 0
                }

                Repeater {
                    model: root.count

                    Rectangle {
                        required property int index
                        x: Math.round(track.width * root.fractionAt(index) - width / 2)
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.tickWidth
                        height: root.tickHeight
                        radius: width / 2
                        color: Theme.textTertiary
                    }
                }

                Rectangle {
                    id: marker
                    visible: root.count > 0
                    x: Math.round(track.width * root.fractionAt(root.currentIndex) - width / 2)
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.markerSize
                    height: root.markerSize
                    radius: width / 2
                    color: root.activeFocus ? Theme.textPrimary : Theme.textSecondary

                    Behavior on x {
                        enabled: Theme.uiAnimationsEnabled
                        NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic }
                    }

                    FocusRing {
                        active: root.activeFocus
                        radius: marker.radius
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: (mouse) => {
                    var fraction = (mouse.x - track.x) / Math.max(1, track.width)
                    var index = root.nearestChapterIndex(fraction)
                    var wasCurrent = root.activeFocus && index === root.currentIndex
                    root.currentIndex = index
                    root.forceActiveFocus()
                    if (wasCurrent) {
                        root.activateCurrent()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: root.formatTime(0)
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontPrimary
                color: Theme.textSecondary
            }

            Item { Layout.fillWidth: true }

            Text {
                text: root.formatTime(root.totalMs)
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontPrimary
                color: Theme.textSecondary
            }
        }
    }
}
