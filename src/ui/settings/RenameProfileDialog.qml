import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

Dialog {
    id: root
    title: qsTr("Rename Profile")
    modal: true
    anchors.centerIn: parent
    width: Math.round(560 * Theme.layoutScale)
    padding: Theme.dialogPadding
    topPadding: Theme.spacingLarge

    property string profileToRename: ""
    property Item restoreFocusTarget: null

    signal profileRenamed(string oldName, string newName)

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
                text: qsTr("Profile Name")
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
            }

            TextField {
                id: nameField
                Layout.fillWidth: true
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                Keys.onDownPressed: { cancelBtn.forceActiveFocus(); event.accepted = true }
                background: FieldBackground { control: nameField; implicitHeight: Theme.buttonHeightSmall }
            }

            Text {
                visible: duplicateName()
                text: qsTr("A profile with this name already exists")
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                color: Theme.errorColor
            }
        }
    }

    function duplicateName() {
        var name = nameField.text.trim()
        return name !== "" && name !== root.profileToRename && ConfigManager.mpvProfileNames.indexOf(name) >= 0
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
                Keys.onUpPressed: { nameField.forceActiveFocus(); event.accepted = true }
                Keys.onRightPressed: { if (renameBtn.enabled) renameBtn.forceActiveFocus(); event.accepted = true }
                Keys.onReturnPressed: root.reject()
                Keys.onEnterPressed: root.reject()
                onClicked: root.reject()
            }

            ActionButton {
                id: renameBtn
                variant: "primary"
                Layout.fillWidth: true
                text: qsTr("Rename")
                focusPolicy: enabled ? Qt.StrongFocus : Qt.NoFocus
                enabled: nameField.text.trim() !== "" && nameField.text.trim() !== root.profileToRename && !root.duplicateName()
                Keys.onUpPressed: { nameField.forceActiveFocus(); event.accepted = true }
                Keys.onLeftPressed: { cancelBtn.forceActiveFocus(); event.accepted = true }
                Keys.onReturnPressed: if (enabled) root.accept()
                Keys.onEnterPressed: if (enabled) root.accept()
                onEnabledChanged: { if (!enabled && activeFocus) cancelBtn.forceActiveFocus() }
                onClicked: root.accept()
            }
        }
    }

    onOpened: {
        nameField.text = root.profileToRename
        Qt.callLater(function() {
            nameField.forceActiveFocus()
            nameField.selectAll()
        })
    }

    onAccepted: {
        profileRenamed(root.profileToRename, nameField.text.trim())
        profileToRename = ""
        nameField.text = ""
    }

    onRejected: {
        profileToRename = ""
        nameField.text = ""
    }

    onClosed: {
        Qt.callLater(function() {
            if (root.restoreFocusTarget) {
                root.restoreFocusTarget.forceActiveFocus()
            }
            root.restoreFocusTarget = null
        })
    }
}
