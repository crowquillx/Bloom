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
    property bool isBuiltIn: profileName === "Default" || profileName === "High Quality"
    
    // Reference to parent flickable for scroll-into-view
    property Flickable parentFlickable: null
    
    signal profileChanged(var newData)
    signal deleteRequested()
    // Signal to navigate out of this component
    signal navigateOut(string direction)
    
    implicitHeight: contentColumn.implicitHeight
    
    // When this FocusScope receives focus, give it to the first control
    onActiveFocusChanged: {
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
        extraArgsField.text = (profileData.extraArgs || []).join("\n")
    }
    
    function saveProfile() {
        var data = {
            "hwdecEnabled": hwdecSwitch.checked,
            "hwdecMethod": hwdecMethodCombo.currentText,
            "deinterlace": deinterlaceSwitch.checked,
            "deinterlaceMethod": deinterlaceMethodField.text,
            "videoOutput": videoOutputCombo.currentText,
            "interpolation": interpolationSwitch.checked,
            "extraArgs": extraArgsField.text.split("\n").filter(function(arg) { return arg.trim() !== "" })
        }
        ConfigManager.setMpvProfile(profileName, data)
        profileChanged(data)
    }
    
    ColumnLayout {
        id: contentColumn
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.spacingMedium
        
        // Profile Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            
            Text {
                text: root.profileName
                font.pixelSize: Theme.fontSizeTitle
                font.family: Theme.fontPrimary
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                Layout.fillWidth: true
            }
            
            // Delete button (only for custom profiles)
            Button {
                id: deleteBtn
                visible: !root.isBuiltIn
                text: "Delete"
                Accessible.role: Accessible.Button
                Accessible.name: "Delete profile " + root.profileName
                focusPolicy: Qt.StrongFocus
                
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: "#ff6b6b"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                background: Rectangle {
                    implicitWidth: 80
                    implicitHeight: Theme.buttonHeightSmall
                    radius: Theme.radiusSmall
                    color: deleteBtn.activeFocus || deleteBtn.hovered ? Qt.rgba(1, 0.4, 0.4, 0.2) : "transparent"
                    border.color: deleteBtn.activeFocus ? Theme.focusBorder : "#ff6b6b"
                    border.width: deleteBtn.activeFocus ? 2 : 1
                }
                
                onClicked: root.deleteRequested()
                Keys.onReturnPressed: root.deleteRequested()
                Keys.onEnterPressed: root.deleteRequested()
            }
        }
        
        // Divider
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.borderLight
        }
        
        // ========================================
        // Common Options
        // ========================================
        
        Text {
            text: "Video Output"
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: Theme.textSecondary
            Layout.topMargin: Theme.spacingSmall
        }
        
        // Video Output
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            
            Text {
                text: "Renderer"
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                Layout.preferredWidth: 150
            }
            
            ComboBox {
                id: videoOutputCombo
                Layout.preferredWidth: 200
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
                
                background: Rectangle {
                    implicitHeight: Theme.buttonHeightSmall
                    radius: Theme.radiusSmall
                    color: Theme.inputBackground
                    border.color: videoOutputCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                    border.width: videoOutputCombo.activeFocus ? 2 : 1
                }
                
                contentItem: Text {
                    text: videoOutputCombo.displayText
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.spacingSmall
                }

                delegate: ItemDelegate {
                    width: videoOutputCombo.width
                    contentItem: Text {
                        text: modelData
                        color: highlighted ? Theme.textPrimary : Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: highlighted ? Theme.buttonPrimaryBackground : "transparent"
                        radius: Theme.radiusSmall
                    }
                    highlighted: ListView.isCurrentItem || videoOutputCombo.highlightedIndex === index
                }

                popup: Popup {
                    y: videoOutputCombo.height + 5
                    width: videoOutputCombo.width
                    implicitHeight: contentItem.implicitHeight
                    padding: 1
                    
                    onOpened: {
                        videoOutputList.currentIndex = videoOutputCombo.highlightedIndex >= 0
                            ? videoOutputCombo.highlightedIndex
                            : videoOutputCombo.currentIndex
                        videoOutputList.forceActiveFocus()
                    }
                    onClosed: videoOutputCombo.forceActiveFocus()

                    Accessible.role: Accessible.Popup
                    Accessible.name: "Video Output Options"

                    contentItem: ListView {
                        id: videoOutputList
                        clip: true
                        implicitHeight: contentHeight
                        model: videoOutputCombo.popup.visible ? videoOutputCombo.delegateModel : null
                        currentIndex: videoOutputCombo.highlightedIndex >= 0
                            ? videoOutputCombo.highlightedIndex
                            : videoOutputCombo.currentIndex

                        ScrollIndicator.vertical: ScrollIndicator { }
                        onCurrentIndexChanged: videoOutputCombo.highlightedIndex = currentIndex
                        
                        Keys.onReturnPressed: { 
                            videoOutputCombo.currentIndex = currentIndex
                            videoOutputCombo.popup.close() 
                        }
                        Keys.onEnterPressed: { 
                            videoOutputCombo.currentIndex = currentIndex
                            videoOutputCombo.popup.close() 
                        }
                        Keys.onEscapePressed: videoOutputCombo.popup.close()
                    }

                    background: Rectangle {
                        color: Theme.cardBackground
                        border.color: Theme.focusBorder
                        border.width: 1
                        radius: Theme.radiusSmall
                    }
                }
            }
        }
        
        // ========================================
        // Hardware Decoding
        // ========================================
        
        Text {
            text: "Hardware Decoding"
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: Theme.textSecondary
            Layout.topMargin: Theme.spacingMedium
        }
        
        // Hardware Decoding Toggle
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            
            Text {
                text: "Enable Hardware Decoding"
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                Layout.fillWidth: true
            }
            
            Switch {
                id: hwdecSwitch
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.CheckBox
                Accessible.name: "Enable Hardware Decoding"
                Accessible.checkable: true
                Accessible.checked: checked
                
                onActiveFocusChanged: if (activeFocus) root.ensureVisible(this)
                
                Keys.onUpPressed: videoOutputCombo.forceActiveFocus()
                Keys.onDownPressed: {
                    if (checked) {
                        hwdecMethodCombo.forceActiveFocus()
                    } else {
                        deinterlaceSwitch.forceActiveFocus()
                    }
                }
                
                onCheckedChanged: {
                    if (root.profileName !== "") {
                        Qt.callLater(root.saveProfile)
                    }
                }
                Keys.onReturnPressed: checked = !checked
                Keys.onEnterPressed: checked = !checked
                Keys.onSpacePressed: checked = !checked

                indicator: Rectangle {
                    implicitWidth: 56
                    implicitHeight: 30
                    radius: height / 2
                    color: hwdecSwitch.checked ? Theme.accentPrimary : Qt.rgba(1, 1, 1, 0.2)
                    border.color: hwdecSwitch.activeFocus ? Theme.focusBorder : Theme.inputBorder
                    border.width: hwdecSwitch.activeFocus ? 2 : 1

                    Rectangle {
                        width: 24
                        height: 24
                        radius: 12
                        x: hwdecSwitch.checked ? parent.width - width - 3 : 3
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.textPrimary
                        border.color: hwdecSwitch.activeFocus ? Theme.focusBorder : "transparent"
                        border.width: hwdecSwitch.activeFocus ? 1 : 0
                        Behavior on x { NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic } }
                    }

                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                    Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                }
            }
        }
        
        // Hardware Decoding Method
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            visible: hwdecSwitch.checked
            
            Text {
                text: "Method"
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                Layout.preferredWidth: 150
            }
            
            ComboBox {
                id: hwdecMethodCombo
                Layout.preferredWidth: 200
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
                
                background: Rectangle {
                    implicitHeight: Theme.buttonHeightSmall
                    radius: Theme.radiusSmall
                    color: Theme.inputBackground
                    border.color: hwdecMethodCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                    border.width: hwdecMethodCombo.activeFocus ? 2 : 1
                }
                
                contentItem: Text {
                    text: hwdecMethodCombo.displayText
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.spacingSmall
                }

                delegate: ItemDelegate {
                    width: hwdecMethodCombo.width
                    contentItem: Text {
                        text: modelData
                        color: highlighted ? Theme.textPrimary : Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: highlighted ? Theme.buttonPrimaryBackground : "transparent"
                        radius: Theme.radiusSmall
                    }
                    highlighted: ListView.isCurrentItem || hwdecMethodCombo.highlightedIndex === index
                }

                popup: Popup {
                    y: hwdecMethodCombo.height + 5
                    width: hwdecMethodCombo.width
                    implicitHeight: contentItem.implicitHeight
                    padding: 1
                    
                    onOpened: {
                        hwdecMethodList.currentIndex = hwdecMethodCombo.highlightedIndex >= 0
                            ? hwdecMethodCombo.highlightedIndex
                            : hwdecMethodCombo.currentIndex
                        hwdecMethodList.forceActiveFocus()
                    }
                    onClosed: hwdecMethodCombo.forceActiveFocus()

                    contentItem: ListView {
                        id: hwdecMethodList
                        clip: true
                        implicitHeight: contentHeight
                        model: hwdecMethodCombo.popup.visible ? hwdecMethodCombo.delegateModel : null
                        currentIndex: hwdecMethodCombo.highlightedIndex >= 0
                            ? hwdecMethodCombo.highlightedIndex
                            : hwdecMethodCombo.currentIndex

                        ScrollIndicator.vertical: ScrollIndicator { }
                        onCurrentIndexChanged: hwdecMethodCombo.highlightedIndex = currentIndex
                        
                        Keys.onReturnPressed: { 
                            hwdecMethodCombo.currentIndex = currentIndex
                            hwdecMethodCombo.popup.close() 
                        }
                        Keys.onEnterPressed: { 
                            hwdecMethodCombo.currentIndex = currentIndex
                            hwdecMethodCombo.popup.close() 
                        }
                        Keys.onEscapePressed: hwdecMethodCombo.popup.close()
                    }

                    background: Rectangle {
                        color: Theme.cardBackground
                        border.color: Theme.focusBorder
                        border.width: 1
                        radius: Theme.radiusSmall
                    }
                }
            }
        }
        
        // ========================================
        // Deinterlacing
        // ========================================
        
        Text {
            text: "Deinterlacing"
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: Theme.textSecondary
            Layout.topMargin: Theme.spacingMedium
        }
        
        // Deinterlace Toggle
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            
            Text {
                text: "Enable Deinterlacing"
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                Layout.fillWidth: true
            }
            
            Switch {
                id: deinterlaceSwitch
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.CheckBox
                Accessible.name: "Enable Deinterlacing"
                Accessible.checkable: true
                Accessible.checked: checked
                
                onActiveFocusChanged: if (activeFocus) root.ensureVisible(this)
                
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
                
                onCheckedChanged: {
                    if (root.profileName !== "") {
                        Qt.callLater(root.saveProfile)
                    }
                }
                Keys.onReturnPressed: checked = !checked
                Keys.onEnterPressed: checked = !checked
                Keys.onSpacePressed: checked = !checked

                indicator: Rectangle {
                    implicitWidth: 56
                    implicitHeight: 30
                    radius: height / 2
                    color: deinterlaceSwitch.checked ? Theme.accentPrimary : Qt.rgba(1, 1, 1, 0.2)
                    border.color: deinterlaceSwitch.activeFocus ? Theme.focusBorder : Theme.inputBorder
                    border.width: deinterlaceSwitch.activeFocus ? 2 : 1

                    Rectangle {
                        width: 24
                        height: 24
                        radius: 12
                        x: deinterlaceSwitch.checked ? parent.width - width - 3 : 3
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.textPrimary
                        border.color: deinterlaceSwitch.activeFocus ? Theme.focusBorder : "transparent"
                        border.width: deinterlaceSwitch.activeFocus ? 1 : 0
                        Behavior on x { NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic } }
                    }

                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                    Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                }
            }
        }
        
        // Deinterlace Method
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            visible: deinterlaceSwitch.checked
            
            Text {
                text: "Method"
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                Layout.preferredWidth: 150
            }
            
            TextField {
                id: deinterlaceMethodField
                Layout.preferredWidth: 200
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
                
                background: Rectangle {
                    implicitHeight: Theme.buttonHeightSmall
                    radius: Theme.radiusSmall
                    color: Theme.inputBackground
                    border.color: deinterlaceMethodField.activeFocus ? Theme.focusBorder : Theme.inputBorder
                    border.width: deinterlaceMethodField.activeFocus ? 2 : 1
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
        
        Text {
            text: "Motion Interpolation"
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: Theme.textSecondary
            Layout.topMargin: Theme.spacingMedium
        }
        
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                
                Text {
                    text: "Enable Interpolation"
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                }
                
                Text {
                    text: "Smooths motion by interpolating frames (higher CPU/GPU usage)"
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
            
            Switch {
                id: interpolationSwitch
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.CheckBox
                Accessible.name: "Enable Motion Interpolation"
                Accessible.description: "Smooths motion by interpolating frames"
                Accessible.checkable: true
                Accessible.checked: checked
                
                onActiveFocusChanged: if (activeFocus) root.ensureVisible(this)
                
                Keys.onUpPressed: {
                    if (deinterlaceSwitch.checked) {
                        deinterlaceMethodField.forceActiveFocus()
                    } else {
                        deinterlaceSwitch.forceActiveFocus()
                    }
                }
                Keys.onDownPressed: advancedToggleBtn.forceActiveFocus()
                
                onCheckedChanged: {
                    if (root.profileName !== "") {
                        Qt.callLater(root.saveProfile)
                    }
                }
                Keys.onReturnPressed: checked = !checked
                Keys.onEnterPressed: checked = !checked
                Keys.onSpacePressed: checked = !checked

                indicator: Rectangle {
                    implicitWidth: 56
                    implicitHeight: 30
                    radius: height / 2
                    color: interpolationSwitch.checked ? Theme.accentPrimary : Qt.rgba(1, 1, 1, 0.2)
                    border.color: interpolationSwitch.activeFocus ? Theme.focusBorder : Theme.inputBorder
                    border.width: interpolationSwitch.activeFocus ? 2 : 1

                    Rectangle {
                        width: 24
                        height: 24
                        radius: 12
                        x: interpolationSwitch.checked ? parent.width - width - 3 : 3
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.textPrimary
                        border.color: interpolationSwitch.activeFocus ? Theme.focusBorder : "transparent"
                        border.width: interpolationSwitch.activeFocus ? 1 : 0
                        Behavior on x { NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic } }
                    }

                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                    Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                }
            }
        }
        
        // ========================================
        // Advanced Section
        // ========================================
        
        Button {
            id: advancedToggleBtn
            property bool expanded: false
            Layout.topMargin: Theme.spacingMedium
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Button
            Accessible.name: (expanded ? "Collapse" : "Expand") + " Advanced Options"
            
            onActiveFocusChanged: if (activeFocus) root.ensureVisible(this)
            
            Keys.onUpPressed: interpolationSwitch.forceActiveFocus()
            Keys.onDownPressed: {
                if (expanded) {
                    extraArgsField.forceActiveFocus()
                } else {
                    root.navigateOut("down")
                }
            }
            
            contentItem: RowLayout {
                spacing: Theme.spacingSmall
                
                Text {
                    text: advancedToggleBtn.expanded ? "▼" : "▶"
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeSmall
                    color: advancedToggleBtn.activeFocus ? Theme.textPrimary : Theme.textSecondary
                }
                
                Text {
                    text: "Advanced Options"
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: advancedToggleBtn.activeFocus ? Theme.textPrimary : Theme.textSecondary
                }
            }
            
            background: Rectangle {
                implicitWidth: 180
                implicitHeight: Theme.buttonHeightSmall
                radius: Theme.radiusSmall
                color: advancedToggleBtn.activeFocus || advancedToggleBtn.hovered ? Theme.buttonSecondaryBackgroundHover : "transparent"
                border.color: advancedToggleBtn.activeFocus ? Theme.focusBorder : Theme.buttonSecondaryBorder
                border.width: advancedToggleBtn.activeFocus ? 2 : Theme.buttonBorderWidth
            }
            
            onClicked: expanded = !expanded
            Keys.onReturnPressed: expanded = !expanded
            Keys.onEnterPressed: expanded = !expanded
        }
        
        // Advanced Content
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            visible: advancedToggleBtn.expanded
            
            Text {
                text: "Extra Arguments (one per line)"
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
            }
            
            Text {
                text: "Add additional mpv command-line arguments. These are appended after the structured options above."
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                
                TextArea {
                    id: extraArgsField
                    font.family: "monospace"
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textPrimary
                    placeholderText: "--fullscreen\n--profile=high-quality"
                    wrapMode: TextArea.NoWrap
                    focusPolicy: Qt.StrongFocus
                    Accessible.role: Accessible.EditableText
                    Accessible.name: "Extra MPV Arguments"
                    
                    onActiveFocusChanged: if (activeFocus) root.ensureVisible(this)
                    
                    // Use Escape to exit the text area
                    Keys.onEscapePressed: advancedToggleBtn.forceActiveFocus()
                    
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.inputBackground
                        border.color: extraArgsField.activeFocus ? Theme.focusBorder : Theme.inputBorder
                        border.width: extraArgsField.activeFocus ? 2 : 1
                    }
                    
                    onTextChanged: {
                        if (root.profileName !== "" && activeFocus) {
                            saveTimer.restart()
                        }
                    }
                }
            }
            
            // Debounce timer for extra args
            Timer {
                id: saveTimer
                interval: 500
                onTriggered: root.saveProfile()
            }
        }
    }
}
