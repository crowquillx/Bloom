import QtQuick
import QtQuick.Layouts
import BloomUI

// Positions a ColumnLayout content block within a hero card placement region.
Item {
    id: root

    property string placement: "bottomLeft"
    property bool centerLarge: false
    property real edgeMargin: Theme.spacingXLarge
    property real extraBottomInset: 0
    property real contentWidthRatio: 0.58
    property Item positionRoot: parent
    default property alias content: contentColumn.data

    readonly property string resolvedPlacement: centerLarge ? "center" : placement
    readonly property real cardWidth: positionRoot ? positionRoot.width : 0
    readonly property real cardHeight: positionRoot ? positionRoot.height : 0
    readonly property real maxContentWidth: Math.max(0, cardWidth - (2 * edgeMargin))
    readonly property real laidOutContentWidth: {
        if (maxContentWidth <= 0)
            return 0
        if (centerLarge)
            return maxContentWidth * 0.62
        return Math.min(Theme.seriesLogoMaxWidth, maxContentWidth * contentWidthRatio)
    }
    readonly property bool contentAlignRight: placement === "bottomRight" || placement === "topRight"
    readonly property bool contentAlignCenter: placement === "bottomCenter" || placement === "topCenter"
            || placement === "center" || placement === "centerLarge"
    readonly property int contentLayoutAlignment: contentAlignCenter ? Qt.AlignHCenter
                                                  : (contentAlignRight ? Qt.AlignRight : Qt.AlignLeft)

    width: laidOutContentWidth
    height: contentColumn.implicitHeight

    ColumnLayout {
        id: contentColumn
        width: root.laidOutContentWidth
        spacing: Theme.spacingSmall
        onImplicitHeightChanged: Qt.callLater(root.applyPlacement)
    }

    function applyPlacement() {
        var region = positionRoot || parent
        if (!region)
            return

        var regionWidth = region.width
        var regionHeight = region.height
        if (regionWidth <= 0 || regionHeight <= 0)
            return

        var margin = edgeMargin
        var blockWidth = laidOutContentWidth
        var blockHeight = height
        if (blockWidth <= 0 || blockHeight <= 0)
            return

        anchors.left = undefined
        anchors.right = undefined
        anchors.top = undefined
        anchors.bottom = undefined
        anchors.horizontalCenter = undefined
        anchors.verticalCenter = undefined
        anchors.fill = undefined
        anchors.leftMargin = 0
        anchors.rightMargin = 0
        anchors.topMargin = 0
        anchors.bottomMargin = 0

        switch (resolvedPlacement) {
        case "topLeft":
            x = margin
            y = margin
            break
        case "topRight":
            x = regionWidth - blockWidth - margin
            y = margin
            break
        case "topCenter":
            x = (regionWidth - blockWidth) / 2
            y = margin
            break
        case "bottomLeft":
            x = margin
            y = regionHeight - blockHeight - margin - extraBottomInset
            break
        case "bottomRight":
            x = regionWidth - blockWidth - margin
            y = regionHeight - blockHeight - margin - extraBottomInset
            break
        case "bottomCenter":
            x = (regionWidth - blockWidth) / 2
            y = regionHeight - blockHeight - margin - extraBottomInset
            break
        case "center":
            x = (regionWidth - blockWidth) / 2
            y = (regionHeight - blockHeight) / 2
            break
        default:
            x = margin
            y = regionHeight - blockHeight - margin - extraBottomInset
            break
        }
    }

    onExtraBottomInsetChanged: Qt.callLater(applyPlacement)

    onParentChanged: Qt.callLater(applyPlacement)
    onPositionRootChanged: Qt.callLater(applyPlacement)
    onResolvedPlacementChanged: Qt.callLater(applyPlacement)
    onEdgeMarginChanged: Qt.callLater(applyPlacement)
    onCardWidthChanged: Qt.callLater(applyPlacement)
    onCardHeightChanged: Qt.callLater(applyPlacement)
    onLaidOutContentWidthChanged: Qt.callLater(applyPlacement)
    onHeightChanged: Qt.callLater(applyPlacement)
    Component.onCompleted: Qt.callLater(applyPlacement)
}
