import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

Dialog {
    id: root
    title: qsTr("Delete Profile")
    modal: true
    anchors.centerIn: parent
    width: Math.round(560 * Theme.layoutScale)
    padding: Theme.dialogPadding
    topPadding: Theme.spacingLarge

    property string profileToDelete: ""
    property Item restoreFocusTarget: null

    signal profileDeleted(string name)

    background: DialogSurface {}
    Overlay.modal: ModalScrim {}

    header: Item {
        implicitHeight: Math.round(68 * Theme.layoutScale) + Theme.spacingLarge
        Text {
            text: root.title
            font.pixelSize: Theme.fontSizeTitle
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightHeavy
            color: Theme.textPrimary
            anchors.left: parent.left
            anchors.leftMargin: Theme.dialogPadding
            anchors.bottom: parent.bottom
        }
    }

    contentItem: Item {
        implicitHeight: contentCol.implicitHeight + Theme.spacingSmall * 2
        implicitWidth: contentCol.implicitWidth + Theme.spacingSmall * 2
        ColumnLayout {
            id: contentCol
            anchors.fill: parent
            anchors.margins: Theme.spacingSmall
            spacing: Theme.spacingMedium

            Text {
                text: qsTr("Are you sure you want to delete the profile \"%1\"?").arg(root.profileToDelete)
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Text {
                text: qsTr("This will also remove any library or series assignments using this profile.")
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    footer: Item {
        implicitHeight: Theme.buttonHeightMedium + Theme.dialogPadding + Theme.spacingMedium
        implicitWidth: parent ? parent.width : 400
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogPadding
            anchors.topMargin: Theme.spacingMedium
            spacing: Theme.controlSpacing

            ActionButton {
                id: cancelBtn
                variant: "secondary"
                Layout.fillWidth: true
                text: qsTr("Cancel")
                focusPolicy: Qt.StrongFocus
                Keys.onRightPressed: { confirmBtn.forceActiveFocus(); event.accepted = true }
                Keys.onReturnPressed: root.reject()
                Keys.onEnterPressed: root.reject()
                onClicked: root.reject()
            }

            ActionButton {
                id: confirmBtn
                variant: "destructive"
                Layout.fillWidth: true
                text: qsTr("Delete")
                focusPolicy: Qt.StrongFocus
                Keys.onLeftPressed: { cancelBtn.forceActiveFocus(); event.accepted = true }
                Keys.onReturnPressed: root.accept()
                Keys.onEnterPressed: root.accept()
                onClicked: root.accept()
            }
        }
    }

    onOpened: {
        Qt.callLater(function() { cancelBtn.forceActiveFocus() })
    }

    onAccepted: {
        profileDeleted(profileToDelete)
        profileToDelete = ""
    }

    onRejected: { profileToDelete = "" }

    onClosed: {
        Qt.callLater(function() {
            if (root.restoreFocusTarget) {
                root.restoreFocusTarget.forceActiveFocus()
            }
            root.restoreFocusTarget = null
        })
    }
}
