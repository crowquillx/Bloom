import QtQuick
import QtQuick.Layouts

import BloomUI

Rectangle {
    id: ratingChip

    property var ratingData: ({})
    property var normalizedRatingSourceFn: null
    property var ratingLogoSourceFn: null
    property var ratingDisplayValueFn: null
    property var ratingFallbackLabelFn: null

    property string originalSource: ratingData && ratingData.source ? String(ratingData.source) : ""
    property var score: ratingData && ratingData.score !== undefined ? ratingData.score : ratingData.value

    readonly property string normalizedSource: normalizedRatingSourceFn ? normalizedRatingSourceFn(originalSource) : ""
    readonly property string logoSource: ratingLogoSourceFn ? ratingLogoSourceFn(normalizedSource, score) : ""
    readonly property string displayValue: ratingDisplayValueFn ? ratingDisplayValueFn(normalizedSource, score) : ""
    readonly property string fallbackText: ratingFallbackLabelFn ? ratingFallbackLabelFn(normalizedSource, originalSource) : ""

    implicitHeight: Math.round(38 * Theme.layoutScale)
    implicitWidth: ratingRow.implicitWidth + Math.round(20 * Theme.layoutScale)
    radius: implicitHeight / 2
    color: Qt.rgba(0, 0, 0, 0.28)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.12)
    visible: displayValue !== ""

    RowLayout {
        id: ratingRow
        anchors.centerIn: parent
        spacing: Math.round(6 * Theme.layoutScale)

        Item {
            Layout.preferredWidth: Math.round(42 * Theme.layoutScale)
            Layout.preferredHeight: Math.round(16 * Theme.layoutScale)
            Layout.alignment: Qt.AlignVCenter

            Image {
                anchors.fill: parent
                source: ratingChip.logoSource
                fillMode: Image.PreserveAspectFit
                visible: source !== ""
                asynchronous: true
                cache: true
            }

            Text {
                anchors.centerIn: parent
                visible: ratingChip.logoSource === ""
                text: ratingChip.fallbackText
                font.pixelSize: Math.round(13 * Theme.layoutScale)
                font.family: Theme.fontPrimary
                font.weight: Font.Black
                color: Theme.textSecondary
            }
        }

        Text {
            text: ratingChip.displayValue
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.fontPrimary
            font.weight: Font.Black
            color: Theme.textPrimary
            verticalAlignment: Text.AlignVCenter
            Layout.alignment: Qt.AlignVCenter
        }
    }
}