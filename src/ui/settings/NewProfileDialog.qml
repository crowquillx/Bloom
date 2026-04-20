import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

Dialog {
    id: root
    title: qsTr("Create New Profile")
    modal: true
    anchors.centerIn: parent
    width: Math.round(560 * Theme.layoutScale)
    padding: Theme.spacingLarge
    property Item restoreFocusTarget: null

    signal profileCreated(string name)

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
                text: qsTr("Profile Name")
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
            }

            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: "My Custom Profile"
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                Keys.onDownPressed: { cancelBtn.forceActiveFocus(); event.accepted = true }
                background: Rectangle {
                    implicitHeight: Theme.buttonHeightSmall
                    radius: Theme.radiusSmall
                    color: Theme.inputBackground
                    border.color: nameField.activeFocus ? Theme.focusBorder : Theme.inputBorder
                    border.width: nameField.activeFocus ? 2 : 1
                }
            }

            Text {
                visible: nameField.text.trim() !== "" && ConfigManager.mpvProfileNames.indexOf(nameField.text.trim()) >= 0
                text: qsTr("A profile with this name already exists")
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                color: "#ff6b6b"
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
                text: "Cancel"
                focusPolicy: Qt.StrongFocus
                Keys.onUpPressed: { nameField.forceActiveFocus(); event.accepted = true }
                Keys.onRightPressed: { if (createBtn.enabled) createBtn.forceActiveFocus(); event.accepted = true }
                Keys.onReturnPressed: root.reject()
                Keys.onEnterPressed: root.reject()
                onClicked: root.reject()
                contentItem: Text { text: cancelBtn.text; font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontPrimary; color: Theme.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { implicitHeight: Theme.buttonHeightSmall; radius: Theme.radiusSmall; color: cancelBtn.hovered ? Theme.buttonSecondaryBackgroundHover : Theme.buttonSecondaryBackground; border.color: cancelBtn.activeFocus ? Theme.focusBorder : Theme.inputBorder; border.width: cancelBtn.activeFocus ? 2 : 1 }
            }

            Button {
                id: createBtn
                Layout.fillWidth: true
                text: qsTr("Create")
                focusPolicy: enabled ? Qt.StrongFocus : Qt.NoFocus
                enabled: nameField.text.trim() !== "" && ConfigManager.mpvProfileNames.indexOf(nameField.text.trim()) < 0
                Keys.onUpPressed: { nameField.forceActiveFocus(); event.accepted = true }
                Keys.onLeftPressed: { cancelBtn.forceActiveFocus(); event.accepted = true }
                Keys.onReturnPressed: if (enabled) root.accept()
                Keys.onEnterPressed: if (enabled) root.accept()
                onEnabledChanged: { if (!enabled && activeFocus) cancelBtn.forceActiveFocus() }
                onClicked: root.accept()
                contentItem: Text { text: createBtn.text; font.pixelSize: Theme.fontSizeBody; font.family: Theme.fontPrimary; color: parent.enabled ? Theme.textPrimary : Theme.textDisabled; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { implicitHeight: Theme.buttonHeightSmall; radius: Theme.radiusSmall; color: parent.enabled ? (createBtn.hovered ? Theme.buttonPrimaryBackgroundHover : Theme.buttonPrimaryBackground) : Theme.buttonSecondaryBackground; border.color: createBtn.activeFocus ? Theme.focusBorder : Theme.inputBorder; border.width: createBtn.activeFocus ? 2 : 1 }
            }
        }
    }

    onOpened: {
        Qt.callLater(function() {
            nameField.forceActiveFocus()
            nameField.selectAll()
        })
    }

    onAccepted: {
        var name = nameField.text.trim()
        if (name !== "") {
            profileCreated(name)
            nameField.text = ""
        }
    }

    onRejected: { nameField.text = "" }

    onClosed: {
        Qt.callLater(function() {
            if (root.restoreFocusTarget) {
                root.restoreFocusTarget.forceActiveFocus()
            }
            root.restoreFocusTarget = null
        })
    }
}
