import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

/**
 * MpvProfileEditor - Component for editing MPV profile settings
 *
 * Provides a UI for editing structured MPV options (hardware decoding,
 * video output, etc.) plus an advanced section for raw arguments.
 */
FocusScope {
    id: root
    activeFocusOnTab: true

    property string profileName: ""
    property var profileData: ({})
    property var extraArgsList: []
    property bool skipNextAutoFocus: false
    property bool isBuiltIn: isBuiltInProfileName(profileName)

    // Reference to parent flickable for scroll-into-view
    property SettingsScrollView parentFlickable: null

    signal profileChanged(var newData)
    signal deleteRequested()
    // Signal to navigate out of this component
    signal navigateOut(string direction)

    function focusBottomAnchor() {
        skipNextAutoFocus = true
        if (advancedToggleBtn.expanded) {
            addArgButton.forceActiveFocus()
        } else {
            advancedToggleBtn.forceActiveFocus()
        }
    }

    implicitHeight: contentColumn.implicitHeight

    // When this FocusScope receives focus, give it to the first control
    onActiveFocusChanged: {
        if (skipNextAutoFocus) {
            skipNextAutoFocus = false
            return
        }
        if (activeFocus && !videoOutputCombo.activeFocus && !hwdecSwitch.activeFocus) {
            videoOutputCombo.forceActiveFocus()
        }
    }

    // Helper to scroll item into view
    function ensureVisible(item) {
        if (parentFlickable && item) {
            parentFlickable.ensureFocusVisible(item)
        }
    }

    // Load profile data when profileName changes
    onProfileNameChanged: {
        if (profileName !== "") {
            loadProfile()
        }
    }

    function loadProfile() {
        profileData = ConfigManager.getMpvProfile(profileName)
        hwdecSwitch.checked = profileData.hwdecEnabled || false
        hwdecMethodCombo.currentIndex = hwdecMethodCombo.model.indexOf(profileData.hwdecMethod || "auto")
        deinterlaceSwitch.checked = profileData.deinterlace || false
        deinterlaceMethodField.text = profileData.deinterlaceMethod || ""
        videoOutputCombo.currentIndex = videoOutputCombo.model.indexOf(profileData.videoOutput || "gpu-next")
        interpolationSwitch.checked = profileData.interpolation || false
        windowsRenderApiCombo.currentIndex = windowsRenderApiIndex(profileData.windowsRenderApi || "auto")
        hdrMetadataModeCombo.currentIndex = hdrMetadataModeIndex(profileData.hdrMetadataMode || "target")
        windows10BitOutputSwitch.checked = profileData.windows10BitOutput || false

        var incoming = profileData.extraArgs
        if (incoming === undefined || incoming === null) {
            incoming = []
        } else if (typeof incoming === "string") {
            incoming = incoming.split("\n")
        }
        extraArgsList = incoming
            .filter(function(arg) { return typeof arg === "string" && arg.trim() !== "" })
            .map(function(arg) { return arg.trim() })
    }

    function saveProfile() {
        var data = {
            "hwdecEnabled": hwdecSwitch.checked,
            "hwdecMethod": hwdecMethodCombo.currentText,
            "deinterlace": deinterlaceSwitch.checked,
            "deinterlaceMethod": deinterlaceMethodField.text,
            "videoOutput": videoOutputCombo.currentText,
            "interpolation": interpolationSwitch.checked,
            "windowsRenderApi": windowsRenderApiValue(windowsRenderApiCombo.currentIndex),
            "hdrMetadataMode": hdrMetadataModeValue(hdrMetadataModeCombo.currentIndex),
            "windows10BitOutput": windows10BitOutputSwitch.checked,
            "extraArgs": extraArgsList
                .filter(function(arg) { return typeof arg === "string" && arg.trim() !== "" })
                .map(function(arg) { return arg.trim() })
        }
        ConfigManager.setMpvProfile(profileName, data)
        profileChanged(data)
    }

    function windowsRenderApiIndex(value) {
        if (value === "d3d11") return 1
        if (value === "vulkan") return 2
        return 0
    }

    function windowsRenderApiValue(index) {
        if (index === 1) return "d3d11"
        if (index === 2) return "vulkan"
        return "auto"
    }

    function hdrMetadataModeIndex(value) {
        if (value === "source-dynamic") return 1
        return 0
    }

    function hdrMetadataModeValue(index) {
        if (index === 1) return "source-dynamic"
        return "target"
    }

    function isBuiltInProfileName(name) {
        return name === "Low Quality"
            || name === "Medium Quality"
            || name === "High Quality"
            || name === "ArtCNN"
            || name === "ArtCNN-Deband"
            || name === "nnedi3"
            || name === "nnedi3-deband"
    }

    function updateExtraArg(index, text) {
        if (index < 0 || index >= extraArgsList.length) return
        var updated = extraArgsList.slice()
        updated[index] = text
        extraArgsList = updated
    }

    function addExtraArg() {
        var updated = extraArgsList.slice()
        updated.push("--")
        extraArgsList = updated
        Qt.callLater(function() {
            if (argRepeater.count > 0) {
                var item = argRepeater.itemAt(argRepeater.count - 1)
                if (item && item.argumentField) {
                    item.argumentField.forceActiveFocus()
                }
            }
        })
        Qt.callLater(root.saveProfile)
    }

    function removeExtraArg(index) {
        if (index < 0 || index >= extraArgsList.length) return
        var updated = extraArgsList.slice()
        updated.splice(index, 1)
        extraArgsList = updated

        Qt.callLater(function() {
            if (argRepeater.count > 0) {
                var nextIndex = Math.min(index, argRepeater.count - 1)
                var item = argRepeater.itemAt(nextIndex)
                if (item && item.argumentField) {
                    item.argumentField.forceActiveFocus()
                    return
                }
            }
            addArgButton.forceActiveFocus()
        })

        Qt.callLater(root.saveProfile)
    }

    // Label for a group of rows nested inside the profile (eyebrow style, so
    // it sits below the page's group headers in the hierarchy).
    component SubsectionHeader: Item {
        property string text: ""
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingSmall
        implicitHeight: Math.round(44 * Theme.layoutScale)

        Text {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingXSmall
            text: parent.text
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightHeavy
            font.capitalization: Font.AllUppercase
            font.letterSpacing: Theme.eyebrowLetterSpacing
            color: Theme.accentPrimary
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
    }

    ColumnLayout {
        id: contentColumn
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.spacingSmall

        // Profile Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Text {
                text: root.profileName
                font.pixelSize: Theme.fontSizeTitle
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightStrong
                color: Theme.textPrimary
                elide: Text.ElideRight
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            // Delete button (only for custom profiles)
            ActionButton {
                id: deleteBtn
                variant: "destructive"
                visible: !root.isBuiltIn
                text: "Delete"
                Accessible.role: Accessible.Button
                Accessible.name: "Delete profile " + root.profileName
                focusPolicy: Qt.StrongFocus

                onClicked: root.deleteRequested()
                Keys.onReturnPressed: root.deleteRequested()
                Keys.onEnterPressed: root.deleteRequested()
            }
        }

        // Divider
        SettingsGroupDivider {
            Layout.fillWidth: true
        }

        // ========================================
        // Common Options
        // ========================================

        SubsectionHeader {
            text: "Video Output"
            Layout.topMargin: Theme.spacingSmall
        }

        // Video Output
        SettingsRow {
            label: "Renderer"
            dimmed: !videoOutputCombo.enabled
            highlighted: videoOutputCombo.activeFocus || videoOutputCombo.popup.visible
            focusTarget: videoOutputCombo
            onActivated: if (videoOutputCombo.enabled) videoOutputCombo.popup.open()

            SettingsComboBox {
                id: videoOutputCombo
                model: ["gpu-next", "gpu", "dmabuf-wayland", "x11", "drm"]
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.ComboBox
                Accessible.name: "Video Output Renderer"

                onActiveFocusChanged: if (activeFocus) root.ensureVisible(this)

                Keys.onUpPressed: function(event) {
                    if (!popup.visible) {
                        root.navigateOut("up")
                        event.accepted = true
                    }
                }
                Keys.onDownPressed: function(event) {
                    if (!popup.visible) {
                        hwdecSwitch.forceActiveFocus()
                        event.accepted = true
                    }
                }

                // Keyboard handling for opening popup
                Keys.onReturnPressed: popup.open()
                Keys.onEnterPressed: popup.open()

                onCurrentTextChanged: {
                    if (root.profileName !== "") {
                        Qt.callLater(root.saveProfile)
                    }
                }
            }
        }

        // ========================================
        // Hardware Decoding
        // ========================================

        SubsectionHeader {
            text: "Hardware Decoding"
            Layout.topMargin: Theme.spacingMedium
        }

        SettingsToggleRow {
            id: hwdecSwitch
            label: "Enable Hardware Decoding"
            checked: false
            ensureVisible: function(item) { root.ensureVisible(item) }

            Keys.onUpPressed: videoOutputCombo.forceActiveFocus()
            Keys.onDownPressed: {
                if (checked) {
                    hwdecMethodCombo.forceActiveFocus()
                } else {
                    deinterlaceSwitch.forceActiveFocus()
                }
            }

            onToggled: function(value) {
                checked = value
                if (root.profileName !== "") {
                    Qt.callLater(root.saveProfile)
                }
            }
        }

        // Hardware Decoding Method
        SettingsRow {
            visible: hwdecSwitch.checked
            label: "Method"
            dimmed: !hwdecMethodCombo.enabled
            highlighted: hwdecMethodCombo.activeFocus || hwdecMethodCombo.popup.visible
            focusTarget: hwdecMethodCombo
            onActivated: if (hwdecMethodCombo.enabled) hwdecMethodCombo.popup.open()

            SettingsComboBox {
                id: hwdecMethodCombo
                model: ["auto", "auto-safe", "auto-copy", "vaapi", "nvdec", "nvdec-copy", "cuda", "cuda-copy", "videotoolbox", "videotoolbox-copy", "d3d11va", "d3d11va-copy", "dxva2", "dxva2-copy"]
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.ComboBox
                Accessible.name: "Hardware Decoding Method"

                onActiveFocusChanged: if (activeFocus) root.ensureVisible(this)

                Keys.onUpPressed: function(event) {
                    if (!popup.visible) {
                        hwdecSwitch.forceActiveFocus()
                        event.accepted = true
                    }
                }
                Keys.onDownPressed: function(event) {
                    if (!popup.visible) {
                        deinterlaceSwitch.forceActiveFocus()
                        event.accepted = true
                    }
                }

                // Keyboard handling for opening popup
                Keys.onReturnPressed: popup.open()
                Keys.onEnterPressed: popup.open()

                onCurrentTextChanged: {
                    if (root.profileName !== "" && hwdecSwitch.checked) {
                        Qt.callLater(root.saveProfile)
                    }
                }
            }
        }

        // ========================================
        // Deinterlacing
        // ========================================

        SubsectionHeader {
            text: "Deinterlacing"
            Layout.topMargin: Theme.spacingMedium
        }

        SettingsToggleRow {
            id: deinterlaceSwitch
            label: "Enable Deinterlacing"
            checked: false
            ensureVisible: function(item) { root.ensureVisible(item) }

            Keys.onUpPressed: {
                if (hwdecSwitch.checked) {
                    hwdecMethodCombo.forceActiveFocus()
                } else {
                    hwdecSwitch.forceActiveFocus()
                }
            }
            Keys.onDownPressed: {
                if (checked) {
                    deinterlaceMethodField.forceActiveFocus()
                } else {
                    interpolationSwitch.forceActiveFocus()
                }
            }

            onToggled: function(value) {
                checked = value
                if (root.profileName !== "") {
                    Qt.callLater(root.saveProfile)
                }
            }
        }

        // Deinterlace Method
        SettingsRow {
            visible: deinterlaceSwitch.checked
            label: "Method"
            highlighted: deinterlaceMethodField.activeFocus
            focusTarget: deinterlaceMethodField

            TextField {
                id: deinterlaceMethodField
                Layout.preferredWidth: Math.round(380 * Theme.layoutScale)
                placeholderText: "yadif, bwdif, etc."
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.EditableText
                Accessible.name: "Deinterlacing Method"

                onActiveFocusChanged: if (activeFocus) root.ensureVisible(this)

                Keys.onUpPressed: deinterlaceSwitch.forceActiveFocus()
                Keys.onDownPressed: interpolationSwitch.forceActiveFocus()

                leftPadding: Theme.spacingMedium
                rightPadding: Theme.spacingMedium
                placeholderTextColor: Theme.textTertiary
                // The row draws focus; the field only marks where typing goes.
                background: Rectangle {
                    implicitHeight: Theme.buttonHeightSmall
                    radius: Theme.radiusControl
                    color: deinterlaceMethodField.activeFocus ? Theme.surfaceInteractivePressed : Theme.surfaceInteractive
                    border.width: Theme.borderWidth
                    border.color: deinterlaceMethodField.activeFocus ? Theme.surfaceBorderStrong : Theme.surfaceBorder
                }

                onTextEdited: {
                    if (root.profileName !== "") {
                        Qt.callLater(root.saveProfile)
                    }
                }
            }
        }

        // ========================================
        // Interpolation
        // ========================================

        SubsectionHeader {
            text: "Motion Interpolation"
            Layout.topMargin: Theme.spacingMedium
        }

        SettingsToggleRow {
            id: interpolationSwitch
            label: "Enable Interpolation"
            description: "Smooths motion by interpolating frames (higher CPU/GPU usage)"
            checked: false
            ensureVisible: function(item) { root.ensureVisible(item) }

            Keys.onUpPressed: {
                if (deinterlaceSwitch.checked) {
                    deinterlaceMethodField.forceActiveFocus()
                } else {
                    deinterlaceSwitch.forceActiveFocus()
                }
            }
            Keys.onDownPressed: advancedToggleBtn.forceActiveFocus()

            onToggled: function(value) {
                checked = value
                if (root.profileName !== "") {
                    Qt.callLater(root.saveProfile)
                }
            }
        }

        // ========================================
        // Advanced Section
        // ========================================

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.borderLight
            Layout.topMargin: Theme.spacingSmall
        }

        SettingsRow {
            id: advancedToggleBtn
            property bool expanded: false
            label: qsTr("Advanced Options")
            description: qsTr("Windows render API, HDR metadata and extra mpv arguments")
            Layout.topMargin: Theme.spacingSmall
            Accessible.role: Accessible.Button
            Accessible.name: (expanded ? "Collapse" : "Expand") + " Advanced Options"

            onActiveFocusChanged: if (activeFocus) root.ensureVisible(this)

            Keys.onUpPressed: interpolationSwitch.forceActiveFocus()
            Keys.onDownPressed: {
                if (expanded) {
                    windowsRenderApiCombo.forceActiveFocus()
                } else {
                    root.navigateOut("down")
                }
            }

            onActivated: expanded = !expanded

            Text {
                text: advancedToggleBtn.expanded ? Icons.expandLess : Icons.expandMore
                font.family: Theme.fontIcon
                font.pixelSize: Theme.iconSizeLarge
                color: advancedToggleBtn.highlighted ? Theme.textPrimary : Theme.textSecondary
                Accessible.ignored: true
            }
        }

        // Advanced Content
        Item {
            Layout.fillWidth: true
            implicitHeight: advancedContentColumn.implicitHeight
            visible: advancedToggleBtn.expanded
            ColumnLayout {
                id: advancedContentColumn
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.spacingSmall

                SubsectionHeader {
                    text: "Windows Render API"
                }

                SettingsRow {
                    label: "Windows embedded playback"
                    description: "Auto keeps the current mpv default. Vulkan/winvk is opt-in for shader-heavy profiles."
                    dimmed: !windowsRenderApiCombo.enabled
                    highlighted: windowsRenderApiCombo.activeFocus || windowsRenderApiCombo.popup.visible
                    focusTarget: windowsRenderApiCombo
                    onActivated: if (windowsRenderApiCombo.enabled) windowsRenderApiCombo.popup.open()

                    SettingsComboBox {
                        id: windowsRenderApiCombo
                        model: ["Auto", "D3D11", "Vulkan/winvk"]
                        focusPolicy: Qt.StrongFocus
                        Accessible.role: Accessible.ComboBox
                        Accessible.name: "Windows Render API"

                        onActiveFocusChanged: if (activeFocus) root.ensureVisible(this)

                        Keys.onUpPressed: advancedToggleBtn.forceActiveFocus()
                        Keys.onDownPressed: hdrMetadataModeCombo.forceActiveFocus()
                        Keys.onReturnPressed: popup.open()
                        Keys.onEnterPressed: popup.open()

                        onCurrentIndexChanged: {
                            if (root.profileName !== "") {
                                Qt.callLater(root.saveProfile)
                            }
                        }
                    }
                }

                SettingsRow {
                    label: "HDR Metadata Mode"
                    description: "Target is the stable default. Source Dynamic is experimental and only affects HDR output."
                    dimmed: !hdrMetadataModeCombo.enabled
                    highlighted: hdrMetadataModeCombo.activeFocus || hdrMetadataModeCombo.popup.visible
                    focusTarget: hdrMetadataModeCombo
                    onActivated: if (hdrMetadataModeCombo.enabled) hdrMetadataModeCombo.popup.open()

                    SettingsComboBox {
                        id: hdrMetadataModeCombo
                        model: ["Target", "Source Dynamic (Experimental)"]
                        focusPolicy: Qt.StrongFocus
                        Accessible.role: Accessible.ComboBox
                        Accessible.name: "HDR Metadata Mode"

                        onActiveFocusChanged: if (activeFocus) root.ensureVisible(this)

                        Keys.onUpPressed: windowsRenderApiCombo.forceActiveFocus()
                        Keys.onDownPressed: {
                            if (windows10BitOutputSwitch.enabled) {
                                windows10BitOutputSwitch.forceActiveFocus()
                            } else if (argRepeater.count > 0 && argRepeater.itemAt(0) && argRepeater.itemAt(0).argumentField) {
                                argRepeater.itemAt(0).argumentField.forceActiveFocus()
                            } else {
                                addArgButton.forceActiveFocus()
                            }
                        }
                        Keys.onReturnPressed: popup.open()
                        Keys.onEnterPressed: popup.open()

                        onCurrentIndexChanged: {
                            if (root.profileName !== "") {
                                Qt.callLater(root.saveProfile)
                            }
                        }
                    }
                }

                SettingsToggleRow {
                    id: windows10BitOutputSwitch
                    label: "10-bit D3D11 Output"
                    description: windowsRenderApiValue(windowsRenderApiCombo.currentIndex) === "d3d11"
                        ? "Uses rgb10_a2 output format for Windows D3D11 playback."
                        : "Available only when Windows Render API is D3D11."
                    checked: false
                    enabled: windowsRenderApiValue(windowsRenderApiCombo.currentIndex) === "d3d11"
                    ensureVisible: function(item) { root.ensureVisible(item) }

                    onEnabledChanged: {
                        if (!enabled && activeFocus) {
                            hdrMetadataModeCombo.forceActiveFocus()
                        }
                    }

                    Keys.onUpPressed: hdrMetadataModeCombo.forceActiveFocus()
                    Keys.onDownPressed: {
                        if (argRepeater.count > 0 && argRepeater.itemAt(0) && argRepeater.itemAt(0).argumentField) {
                            argRepeater.itemAt(0).argumentField.forceActiveFocus()
                        } else {
                            addArgButton.forceActiveFocus()
                        }
                    }

                    onToggled: function(value) {
                        checked = value
                        if (root.profileName !== "") {
                            Qt.callLater(root.saveProfile)
                        }
                    }
                }

                SettingsGroupDivider {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingSmall
                    Layout.bottomMargin: Theme.spacingSmall
                }

                SubsectionHeader {
                    text: "Extra MPV Arguments"
                }

                Text {
                    text: "Add one argument at a time. Order is preserved and appended after the structured options above."
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Text {
                    visible: extraArgsList.length === 0
                    text: "No custom arguments yet."
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    font.italic: true
                }

                Repeater {
                    id: argRepeater
                    model: extraArgsList

                    delegate: FocusScope {
                        id: argItem
                        required property int index
                        required property string modelData

                        property alias argumentField: argField

                        Layout.fillWidth: true
                        implicitHeight: argRow.implicitHeight

                        // Row surface while the argument or its Remove button has focus
                        // (the focused control draws the ring).
                        Rectangle {
                            anchors.fill: argRow
                            anchors.leftMargin: -Theme.spacingMedium
                            anchors.rightMargin: -Theme.spacingMedium
                            anchors.topMargin: -Theme.spacingXSmall
                            anchors.bottomMargin: -Theme.spacingXSmall
                            radius: Theme.radiusMedium
                            color: argField.activeFocus || removeArgButton.activeFocus ? Theme.surfaceInteractiveHover : "transparent"
                        }

                        RowLayout {
                            id: argRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: Theme.spacingSmall

                            Text {
                                text: (index + 1) + "."
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.fontPrimary
                                color: Theme.textSecondary
                                Layout.preferredWidth: Math.round(34 * Theme.layoutScale)
                            }

                            TextField {
                                id: argField
                                Layout.fillWidth: true
                                text: modelData
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.textPrimary
                                placeholderText: "--profile=high-quality"
                                focusPolicy: Qt.StrongFocus
                                Accessible.role: Accessible.EditableText
                                Accessible.name: "MPV argument " + (index + 1)

                                onActiveFocusChanged: if (activeFocus) root.ensureVisible(this)

                                Keys.onUpPressed: {
                                    if (index > 0) {
                                        var prev = argRepeater.itemAt(index - 1)
                                        if (prev && prev.argumentField) {
                                            prev.argumentField.forceActiveFocus()
                                        }
                                    } else {
                                        windowsRenderApiCombo.forceActiveFocus()
                                    }
                                    event.accepted = true
                                }
                                Keys.onDownPressed: {
                                    if (index < argRepeater.count - 1) {
                                        var next = argRepeater.itemAt(index + 1)
                                        if (next && next.argumentField) {
                                            next.argumentField.forceActiveFocus()
                                        }
                                    } else {
                                        addArgButton.forceActiveFocus()
                                    }
                                    event.accepted = true
                                }
                                Keys.onRightPressed: {
                                    removeArgButton.forceActiveFocus()
                                    event.accepted = true
                                }

                                onEditingFinished: {
                                    root.updateExtraArg(index, text.trim())
                                    Qt.callLater(root.saveProfile)
                                }

                                background: FieldBackground { control: argField; implicitHeight: Theme.buttonHeightSmall }
                            }

                            ActionButton {
                                id: removeArgButton
                                variant: "destructive"
                                text: "Remove"
                                focusPolicy: Qt.StrongFocus
                                Accessible.role: Accessible.Button
                                Accessible.name: "Remove MPV argument " + (index + 1)

                                Keys.onLeftPressed: {
                                    argField.forceActiveFocus()
                                    event.accepted = true
                                }
                                Keys.onUpPressed: {
                                    argField.forceActiveFocus()
                                    event.accepted = true
                                }
                                Keys.onDownPressed: {
                                    if (index < argRepeater.count - 1) {
                                        var next = argRepeater.itemAt(index + 1)
                                        if (next && next.argumentField) {
                                            next.argumentField.forceActiveFocus()
                                        }
                                    } else {
                                        addArgButton.forceActiveFocus()
                                    }
                                    event.accepted = true
                                }

                                onClicked: root.removeExtraArg(index)
                                Keys.onReturnPressed: root.removeExtraArg(index)
                                Keys.onEnterPressed: root.removeExtraArg(index)

                            }
                        }
                    }
                }

                ActionButton {
                    id: addArgButton
                    variant: "secondary"
                    text: "Add Argument"
                    focusPolicy: Qt.StrongFocus
                    Accessible.role: Accessible.Button
                    Accessible.name: "Add MPV argument"

                    onActiveFocusChanged: if (activeFocus) root.ensureVisible(this)

                    Keys.onUpPressed: {
                        if (argRepeater.count > 0) {
                            var last = argRepeater.itemAt(argRepeater.count - 1)
                            if (last && last.argumentField) {
                                last.argumentField.forceActiveFocus()
                            }
                        } else {
                            windowsRenderApiCombo.forceActiveFocus()
                        }
                        event.accepted = true
                    }
                    Keys.onDownPressed: root.navigateOut("down")

                    onClicked: root.addExtraArg()
                    Keys.onReturnPressed: root.addExtraArg()
                    Keys.onEnterPressed: root.addExtraArg()

                }
            }

        }
    }
}
