import QtQuick
import QtQuick.Controls
import BloomUI

Column {
    id: root
    property var target               // Expects BaseViewModel-derived object
    property bool autoHide: true      // Hide unless hasError when true
    property string fallbackMessage: qsTr("Something went wrong")
    spacing: Math.round(8 * Theme.layoutScale)
    visible: !root.autoHide || (root.target && root.target.hasError)

    Label {
        id: messageLabel
        visible: root.target && root.target.hasError
        text: root.target && root.target.errorMessage ? root.target.errorMessage : root.fallbackMessage
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontPrimary
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
    }

    ActionButton {
        id: retryButton
        text: qsTr("Retry")
        iconGlyph: Icons.refresh
        enabled: root.target && !root.target.isLoading
        onClicked: {
            if (root.target && root.target.reload) {
                root.target.reload()
            }
        }
    }

    BusyIndicator {
        anchors.horizontalCenter: retryButton.horizontalCenter
        running: root.target && root.target.isLoading
        visible: running
    }
}






