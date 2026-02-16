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
    property bool isBuiltIn: profileName === "Default" || profileName === "High Quality"
    
    // Reference to parent flickable for scroll-into-view
    property Flickable parentFlickable: null
    
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
            "extraArgs": extraArgsList
                .filter(function(arg) { return typeof arg === "string" && arg.trim() !== "" })
                .map(function(arg) { return arg.trim() })
        }
        ConfigManager.setMpvProfile(profileName, data)
        profileChanged(data)
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

    component SubsectionHeader: Rectangle {
        property string text: ""
        Layout.fillWidth: true
        implicitHeight: Math.round(44 * Theme.layoutScale)
        radius: Theme.radiusSmall
        color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.08)
        border.color: Theme.cardBorder
        border.width: 1

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingSmall
            anchors.verticalCenter: parent.verticalCenter
            text: parent.text
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            font.weight: Font.DemiBold
            color: Theme.textPrimary
        }
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
        
        SubsectionHeader {
            text: "Video Output"
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
        
        SubsectionHeader {
            text: "Hardware Decoding"
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
        
        SubsectionHeader {
            text: "Deinterlacing"
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
        
        SubsectionHeader {
            text: "Motion Interpolation"
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

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.borderLight
            Layout.topMargin: Theme.spacingSmall
        }
        
        Button {
            id: advancedToggleBtn
            property bool expanded: false
            Layout.topMargin: Theme.spacingSmall
            Layout.fillWidth: true
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Button
            Accessible.name: (expanded ? "Collapse" : "Expand") + " Advanced Options"
            
            onActiveFocusChanged: if (activeFocus) root.ensureVisible(this)
            
            Keys.onUpPressed: interpolationSwitch.forceActiveFocus()
            Keys.onDownPressed: {
                if (expanded) {
                    if (argRepeater.count > 0 && argRepeater.itemAt(0) && argRepeater.itemAt(0).argumentField) {
                        argRepeater.itemAt(0).argumentField.forceActiveFocus()
                    } else {
                        addArgButton.forceActiveFocus()
                    }
                } else {
                    root.navigateOut("down")
                }
            }
            
            contentItem: RowLayout {
                spacing: Theme.spacingSmall
                
                Text {
                    text: "Advanced Options"
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                    Layout.fillWidth: true
                }

                Text {
                    text: advancedToggleBtn.expanded ? Icons.expandLess : Icons.expandMore
                    font.family: Theme.fontIcon
                    font.pixelSize: Theme.fontSizeBody
                    color: advancedToggleBtn.activeFocus ? Theme.textPrimary : Theme.textSecondary
                }
            }
            
            background: Rectangle {
                implicitHeight: Theme.buttonHeightSmall
                radius: Theme.radiusMedium
                color: advancedToggleBtn.activeFocus ? Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.12)
                     : (advancedToggleBtn.hovered ? Theme.buttonSecondaryBackgroundHover : Theme.buttonSecondaryBackground)
                border.color: advancedToggleBtn.activeFocus ? Theme.focusBorder : Theme.inputBorder
                border.width: advancedToggleBtn.activeFocus ? 2 : Theme.buttonBorderWidth
            }
            
            onClicked: expanded = !expanded
            Keys.onReturnPressed: expanded = !expanded
            Keys.onEnterPressed: expanded = !expanded
        }
        
        // Advanced Content
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: advancedContentColumn.implicitHeight + (Theme.spacingMedium * 2)
            radius: Theme.radiusMedium
            color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.45)
            border.color: Theme.cardBorder
            border.width: Theme.borderWidth
            visible: advancedToggleBtn.expanded
            ColumnLayout {
                id: advancedContentColumn
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Text {
                    text: "Extra MPV Arguments"
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
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

                        RowLayout {
                            id: argRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: Theme.spacingSmall

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -Theme.spacingSmall / 2
                                radius: Theme.radiusSmall
                                color: argField.activeFocus || removeArgButton.activeFocus ? Theme.buttonSecondaryBackground : "transparent"
                                border.color: argField.activeFocus || removeArgButton.activeFocus ? Theme.focusBorder : "transparent"
                                border.width: argField.activeFocus || removeArgButton.activeFocus ? 1 : 0
                                z: -1
                            }

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
                                font.family: "monospace"
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
                                        advancedToggleBtn.forceActiveFocus()
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

                                background: Rectangle {
                                    implicitHeight: Theme.buttonHeightSmall
                                    radius: Theme.radiusSmall
                                    color: Theme.inputBackground
                                    border.color: argField.activeFocus ? Theme.focusBorder : Theme.inputBorder
                                    border.width: argField.activeFocus ? 2 : 1
                                }
                            }

                            Button {
                                id: removeArgButton
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

                                contentItem: Text {
                                    text: parent.text
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.family: Theme.fontPrimary
                                    color: "#ff8c8c"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                background: Rectangle {
                                    implicitWidth: Math.round(110 * Theme.layoutScale)
                                    implicitHeight: Theme.buttonHeightSmall
                                    radius: Theme.radiusSmall
                                    color: removeArgButton.activeFocus || removeArgButton.hovered ? Qt.rgba(1, 0.4, 0.4, 0.2) : "transparent"
                                    border.color: removeArgButton.activeFocus ? Theme.focusBorder : "#ff8c8c"
                                    border.width: removeArgButton.activeFocus ? 2 : 1
                                }
                            }
                        }
                    }
                }

                Button {
                    id: addArgButton
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
                            advancedToggleBtn.forceActiveFocus()
                        }
                        event.accepted = true
                    }
                    Keys.onDownPressed: root.navigateOut("down")

                    onClicked: root.addExtraArg()
                    Keys.onReturnPressed: root.addExtraArg()
                    Keys.onEnterPressed: root.addExtraArg()

                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.accentPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        implicitWidth: Math.round(170 * Theme.layoutScale)
                        implicitHeight: Theme.buttonHeightSmall
                        radius: Theme.radiusSmall
                        color: addArgButton.activeFocus || addArgButton.hovered ? Qt.rgba(0.4, 0.6, 1, 0.2) : "transparent"
                        border.color: addArgButton.activeFocus ? Theme.focusBorder : Theme.accentPrimary
                        border.width: addArgButton.activeFocus ? 2 : 1
                    }
                }
            }

        }
    }
}
