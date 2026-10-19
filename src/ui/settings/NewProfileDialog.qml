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
    padding: Theme.dialogPadding
    topPadding: Theme.spacingLarge
    property Item restoreFocusTarget: null

    signal profileCreated(string name)

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
                placeholderText: "My Custom Profile"
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                Keys.onDownPressed: { cancelBtn.forceActiveFocus(); event.accepted = true }
                background: FieldBackground { control: nameField; implicitHeight: Theme.buttonHeightSmall }
            }

            Text {
                visible: nameField.text.trim() !== "" && ConfigManager.mpvProfileNames.indexOf(nameField.text.trim()) >= 0
                text: qsTr("A profile with this name already exists")
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                color: Theme.errorColor
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
                text: "Cancel"
                focusPolicy: Qt.StrongFocus
                Keys.onUpPressed: { nameField.forceActiveFocus(); event.accepted = true }
                Keys.onRightPressed: { if (createBtn.enabled) createBtn.forceActiveFocus(); event.accepted = true }
                Keys.onReturnPressed: root.reject()
                Keys.onEnterPressed: root.reject()
                onClicked: root.reject()
            }

            ActionButton {
                id: createBtn
                variant: "primary"
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
