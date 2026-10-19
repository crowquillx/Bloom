import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

Dialog {
    id: root
    title: qsTr("Import MPV Config")
    modal: true
    anchors.centerIn: parent
    width: Math.round(680 * Theme.layoutScale)
    padding: Theme.dialogPadding
    topPadding: Theme.spacingLarge

    property Item restoreFocusTarget: null
    property var candidates: []
    property string selectedPath: ""
    property string summaryText: ""
    property string errorText: ""

    signal profileImported(string name)

    function refreshCandidates() {
        candidates = ConfigManager.detectMpvConfigImportCandidates()
        selectedPath = candidates.length > 0 ? candidates[0] : ""
        pathField.text = selectedPath
    }

    function nameCollides() {
        return ConfigManager.mpvProfileNames.indexOf(nameField.text.trim()) >= 0
    }

    function importConfig() {
        if (!importBtn.enabled)
            return

        var result = ConfigManager.importMpvConfigAsProfile(pathField.text.trim(), nameField.text.trim())
        if (result.success) {
            summaryText = qsTr("Imported %1 options. Filtered %2 options.").arg(result.importedCount).arg(result.filteredOptions.length)
            errorText = ""
            profileImported(result.profileName || nameField.text.trim())
        } else {
            summaryText = ""
            errorText = result.error || qsTr("Import failed.")
        }
    }

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
                text: qsTr("Detected mpv.conf")
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
            }

            ListView {
                id: candidateList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Theme.buttonHeightSmall, Math.min(contentHeight, Math.round(150 * Theme.layoutScale)))
                clip: true
                model: root.candidates
                currentIndex: root.candidates.indexOf(root.selectedPath)
                visible: root.candidates.length > 0
                focus: false
                focusPolicy: Qt.StrongFocus
                activeFocusOnTab: true
                keyNavigationEnabled: true

                Keys.onDownPressed: {
                    if (currentIndex < count - 1) currentIndex++
                    else pathField.forceActiveFocus()
                    event.accepted = true
                }
                Keys.onUpPressed: {
                    if (currentIndex > 0) currentIndex--
                    else nameField.forceActiveFocus()
                    event.accepted = true
                }
                Keys.onReturnPressed: {
                    if (currentIndex >= 0) {
                        root.selectedPath = root.candidates[currentIndex]
                        pathField.text = root.selectedPath
                    }
                    event.accepted = true
                }
                Keys.onEnterPressed: {
                    if (currentIndex >= 0) {
                        root.selectedPath = root.candidates[currentIndex]
                        pathField.text = root.selectedPath
                    }
                    event.accepted = true
                }

                delegate: Button {
                    width: candidateList.width
                    height: Theme.buttonHeightSmall
                    text: modelData
                    focusPolicy: Qt.NoFocus
                    onClicked: {
                        root.selectedPath = modelData
                        pathField.text = modelData
                        candidateList.currentIndex = index
                    }
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontMono
                        color: candidateList.currentIndex === index ? Theme.textPrimary : Theme.textSecondary
                        elide: Text.ElideMiddle
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.spacingSmall
                    }
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: candidateList.currentIndex === index ? Theme.surfaceInteractiveHover : Theme.surfaceInteractive
                        FocusRing {
                            inset: true
                            active: candidateList.activeFocus && candidateList.currentIndex === index
                            radius: parent.radius
                        }
                    }
                }
            }

            Text {
                visible: root.candidates.length === 0
                text: qsTr("No default mpv.conf paths were found.")
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                color: Theme.textSecondary
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Text {
                text: qsTr("Config Path")
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
            }

            TextField {
                id: pathField
                Layout.fillWidth: true
                placeholderText: qsTr("/home/user/.config/mpv/mpv.conf")
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontMono
                color: Theme.textPrimary
                Keys.onUpPressed: {
                    if (candidateList.visible) candidateList.forceActiveFocus()
                    else nameField.forceActiveFocus()
                    event.accepted = true
                }
                Keys.onDownPressed: { nameField.forceActiveFocus(); event.accepted = true }
                background: FieldBackground { control: pathField; implicitHeight: Theme.buttonHeightSmall }
            }

            Text {
                text: qsTr("New Profile Name")
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
            }

            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("Imported MPV Config")
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                Keys.onUpPressed: { pathField.forceActiveFocus(); event.accepted = true }
                Keys.onDownPressed: { cancelBtn.forceActiveFocus(); event.accepted = true }
                background: FieldBackground { control: nameField; implicitHeight: Theme.buttonHeightSmall }
            }

            Text {
                visible: nameField.text.trim() !== "" && root.nameCollides()
                text: qsTr("A profile with this name already exists")
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                color: Theme.errorColor
            }

            Text {
                visible: root.summaryText !== ""
                text: root.summaryText
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                color: Theme.accentPrimary
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Text {
                visible: root.errorText !== ""
                text: root.errorText
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                color: Theme.errorColor
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
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
                text: root.summaryText === "" ? qsTr("Cancel") : qsTr("Close")
                focusPolicy: Qt.StrongFocus
                Keys.onUpPressed: { nameField.forceActiveFocus(); event.accepted = true }
                Keys.onRightPressed: { if (importBtn.enabled) importBtn.forceActiveFocus(); event.accepted = true }
                Keys.onReturnPressed: root.reject()
                Keys.onEnterPressed: root.reject()
                onClicked: root.reject()
            }

            ActionButton {
                id: importBtn
                variant: "primary"
                Layout.fillWidth: true
                text: qsTr("Import")
                enabled: root.summaryText === "" && pathField.text.trim() !== "" && nameField.text.trim() !== "" && !root.nameCollides()
                focusPolicy: enabled ? Qt.StrongFocus : Qt.NoFocus
                Keys.onUpPressed: { nameField.forceActiveFocus(); event.accepted = true }
                Keys.onLeftPressed: { cancelBtn.forceActiveFocus(); event.accepted = true }
                Keys.onReturnPressed: root.importConfig()
                Keys.onEnterPressed: root.importConfig()
                onEnabledChanged: { if (!enabled && activeFocus) cancelBtn.forceActiveFocus() }
                onClicked: root.importConfig()
            }
        }
    }

    onOpened: {
        summaryText = ""
        errorText = ""
        nameField.text = ""
        refreshCandidates()
        Qt.callLater(function() {
            if (pathField.text.trim() === "") {
                pathField.forceActiveFocus()
            } else {
                nameField.forceActiveFocus()
            }
        })
    }

    onRejected: {
        pathField.text = ""
        nameField.text = ""
        summaryText = ""
        errorText = ""
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
