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
    padding: Theme.spacingLarge

    property string profileToDelete: ""
    property Item restoreFocusTarget: null

    signal profileDeleted(string name)

    background: Rectangle {
        color: Theme.cardBackground
        radius: Theme.radiusMedium
        border.color: Theme.cardBorder
        border.width: 1
    }

    header: Rectangle {
        color: "transparent"
        height: Math.round(68 * Theme.layoutScale)
        Text {
            text: root.title
            font.pixelSize: Theme.fontSizeTitle
            font.family: Theme.fontPrimary
            font.weight: Font.DemiBold
            color: Theme.textPrimary
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingLarge
            anchors.verticalCenter: parent.verticalCenter
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
        implicitHeight: Theme.buttonHeightSmall + Theme.spacingLarge * 2
        implicitWidth: parent ? parent.width : 400
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Button {
                id: cancelBtn
                Layout.fillWidth: true
                text: qsTr("Cancel")
                focusPolicy: Qt.StrongFocus
                Keys.onRightPressed: { confirmBtn.forceActiveFocus(); event.accepted = true }
                Keys.onReturnPressed: root.reject()
                Keys.onEnterPressed: root.reject()
                onClicked: root.reject()
                contentItem: Text { text: cancelBtn.text; font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontPrimary; color: Theme.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { implicitHeight: Theme.buttonHeightSmall; radius: Theme.radiusSmall; color: cancelBtn.hovered ? Theme.buttonSecondaryBackgroundHover : Theme.buttonSecondaryBackground; border.color: cancelBtn.activeFocus ? Theme.focusBorder : Theme.inputBorder; border.width: cancelBtn.activeFocus ? 2 : 1 }
            }

            Button {
                id: confirmBtn
                Layout.fillWidth: true
                text: qsTr("Delete")
                focusPolicy: Qt.StrongFocus
                Keys.onLeftPressed: { cancelBtn.forceActiveFocus(); event.accepted = true }
                Keys.onReturnPressed: root.accept()
                Keys.onEnterPressed: root.accept()
                onClicked: root.accept()
                contentItem: Text { text: confirmBtn.text; font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontPrimary; color: Theme.colorDestructive; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { implicitHeight: Theme.buttonHeightSmall; radius: Theme.radiusSmall; color: confirmBtn.hovered ? Theme.destructiveHoverBg : "transparent"; border.color: confirmBtn.activeFocus ? Theme.focusBorder : Theme.destructiveBorder; border.width: confirmBtn.activeFocus ? 2 : 1 }
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
