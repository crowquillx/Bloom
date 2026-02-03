import QtQuick
import QtQuick.Controls
import BloomUI

Column {
    id: root
    property var target               // Expects BaseViewModel-derived object
    property bool autoHide: true      // Hide unless hasError when true
    property string fallbackMessage: qsTr("Something went wrong")
    spacing: 8
    visible: !autoHide || (target && target.hasError)

    Label {
        id: messageLabel
        visible: target && target.hasError
        text: target && target.errorMessage ? target.errorMessage : fallbackMessage
        color: Theme.textMuted
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
    }

    Button {
        id: retryButton
        text: qsTr("Retry")
        enabled: target && !target.isLoading
        onClicked: {
            if (target && target.reload) {
                target.reload()
            }
        }
    }

    BusyIndicator {
        anchors.horizontalCenter: retryButton.horizontalCenter
        running: target && target.isLoading
        visible: running
    }
}







