import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window

/**
 * TrackSelector - A dropdown selector for audio/subtitle tracks
 * 
 * Supports keyboard navigation and displays track information in a
 * user-friendly format.
 */
FocusScope {
    id: root
    
    // Model of tracks to display
    // Each track should have: index, displayTitle, language, codec, channels (for audio)
    property var tracks: []
    
    // Currently selected track index (-1 for none/disabled)
    property int selectedIndex: -1
    
    // Label displayed above the selector
    property string label: qsTr("Track")
    
    // Placeholder when no tracks available
    property string emptyText: qsTr("No tracks available")
    
    // Allow "None" option for subtitles
    property bool allowNone: false
    property string noneText: qsTr("None")
    
    // Visual customization
    property int preferredWidth: 300
    
    // Signals
    signal trackSelected(int index)
    signal popupVisibleChanged(bool visible)

    // Public helpers for parent coordination
    function closePopup() { popup.close() }
    function openPopup() {
        if (tracks.length > 0 || allowNone) {
            popup.visible = true
            Qt.callLater(function() { trackListView.forceActiveFocus() })
        }
    }
    
    implicitWidth: preferredWidth
    implicitHeight: contentColumn.implicitHeight
    
    // Computed property for display text
    property string currentDisplayText: {
        if (tracks.length === 0) return emptyText
        if (selectedIndex < 0) {
            if (allowNone) return noneText
            // Find default track
            for (var i = 0; i < tracks.length; i++) {
                if (tracks[i].isDefault) {
                    return tracks[i].displayTitle || formatTrackName(tracks[i])
                }
            }
            return tracks.length > 0 ? (tracks[0].displayTitle || formatTrackName(tracks[0])) : emptyText
        }
        
        // Find track by index
        for (var j = 0; j < tracks.length; j++) {
            if (tracks[j].index === selectedIndex) {
                return tracks[j].displayTitle || formatTrackName(tracks[j])
            }
        }
        return qsTr("Track %1").arg(selectedIndex)
    }
    
    // Helper to format track name from properties
    function formatTrackName(track) {
        var parts = []
        
        // Language
        if (track.language) {
            parts.push(getLanguageName(track.language))
        }
        
        // Codec
        if (track.codec) {
            parts.push(track.codec.toUpperCase())
        }
        
        // Channels for audio
        if (track.channels) {
            parts.push(formatChannels(track.channels, track.channelLayout))
        }
        
        // Title if available
        if (track.title && !track.language) {
            parts.push(track.title)
        }
        
        // Flags
        if (track.isDefault) parts.push(qsTr("Default"))
        if (track.isForced) parts.push(qsTr("Forced"))
        if (track.isHearingImpaired) parts.push(qsTr("SDH"))
        
        return parts.length > 0 ? parts.join(" • ") : qsTr("Track %1").arg(track.index)
    }
    
    function formatChannels(channels, layout) {
        if (layout) return layout
        switch (channels) {
            case 1: return qsTr("Mono")
            case 2: return qsTr("Stereo")
            case 6: return qsTr("5.1")
            case 8: return qsTr("7.1")
            default: return qsTr("%1 ch").arg(channels)
        }
    }
    
    function getLanguageName(code) {
        // Common language codes to full names
        var languages = {
            "eng": qsTr("English"),
            "jpn": qsTr("Japanese"),
            "spa": qsTr("Spanish"),
            "fre": qsTr("French"),
            "fra": qsTr("French"),
            "ger": qsTr("German"),
            "deu": qsTr("German"),
            "ita": qsTr("Italian"),
            "por": qsTr("Portuguese"),
            "rus": qsTr("Russian"),
            "chi": qsTr("Chinese"),
            "zho": qsTr("Chinese"),
            "kor": qsTr("Korean"),
            "ara": qsTr("Arabic"),
            "hin": qsTr("Hindi"),
            "und": qsTr("Unknown")
        }
        return languages[code] || code.toUpperCase()
    }
    
    ColumnLayout {
        id: contentColumn
        width: parent.width
        spacing: 4
        
        // Label
        Text {
            text: root.label
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.fontPrimary
            color: Theme.textSecondary
            visible: root.label !== ""
        }
        
        // Selector button
        Button {
            id: selectorButton
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.buttonHeightSmall
            focus: true
            
            KeyNavigation.up: root.KeyNavigation.up
            KeyNavigation.down: popup.visible ? trackListView : root.KeyNavigation.down
            KeyNavigation.tab: root.KeyNavigation.tab
            KeyNavigation.left: root.KeyNavigation.left
            KeyNavigation.right: root.KeyNavigation.right
            
            Keys.onReturnPressed: togglePopup()
            Keys.onEnterPressed: togglePopup()
            Keys.onSpacePressed: togglePopup()
            
            function togglePopup() {
                if (tracks.length > 0 || allowNone) {
                    popup.visible = !popup.visible
                    if (popup.visible) {
                        Qt.callLater(function() {
                            trackListView.forceActiveFocus()
                        })
                    }
                }
            }
            
            onClicked: togglePopup()
            
            background: Rectangle {
                radius: Theme.radiusSmall
                color: {
                    if (parent.down) return Theme.buttonSecondaryBackgroundPressed
                    if (parent.hovered || popup.visible) return Theme.buttonSecondaryBackgroundHover
                    return Theme.buttonSecondaryBackground
                }
                border.width: parent.activeFocus ? Theme.buttonFocusBorderWidth : Theme.buttonBorderWidth
                border.color: {
                    if (parent.activeFocus) return Theme.buttonSecondaryBorderFocused
                    if (parent.hovered) return Theme.buttonSecondaryBorderHover
                    return Theme.buttonSecondaryBorder
                }
                
                Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
            }
            
            contentItem: RowLayout {
                spacing: 8
                
                Text {
                    Layout.fillWidth: true
                    text: root.currentDisplayText
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignLeft
                }
                
                Text {
                    text: popup.visible ? "▲" : "▼"
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }
            }
        }
        
        Popup {
            id: popup
            focus: true
            x: 0
            y: selectorButton.height + 4
            width: root.preferredWidth
            implicitHeight: Math.min(trackListView.contentHeight + 16, 250)
            padding: 8
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            onVisibleChanged: {
                root.popupVisibleChanged(visible)
                if (visible) {
                    var idx = getModelIndex(selectedIndex)
                    if (idx >= 0) {
                        trackListView.positionViewAtIndex(idx, ListView.Contain)
                        trackListView.currentIndex = idx
                    }
                }
            }
            
            contentItem: Item {
                id: popupContent
                focus: true
                Keys.onEscapePressed: function(event) {
                    popup.close()
                    selectorButton.forceActiveFocus()
                    event.accepted = true
                }
            }
            
            background: Rectangle {
                color: Theme.cardBackground
                radius: Theme.radiusMedium
                border.width: 1
                border.color: Theme.cardBorder
                
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowHorizontalOffset: 0
                    shadowVerticalOffset: 4
                    shadowBlur: 0.4
                    shadowColor: Qt.rgba(0, 0, 0, 0.5)
                }
            }
            
            
            
            function getModelIndex(trackIndex) {
                var offset = allowNone ? 1 : 0
                if (trackIndex < 0 && allowNone) return 0
                for (var i = 0; i < tracks.length; i++) {
                    if (tracks[i].index === trackIndex) return i + offset
                }
                return -1
            }
            
            ListView {
                id: trackListView
                anchors.fill: popupContent
                clip: true
                focus: popup.visible
                
                model: {
                    var items = []
                    if (allowNone) {
                        items.push({ index: -1, display: noneText, displayTitle: noneText, isNone: true })
                    }
                    for (var i = 0; i < tracks.length; i++) {
                        var t = tracks[i]
                        var name = t.display || t.displayTitle || t.title || t.language || qsTr("Track %1").arg(t.index)
                        items.push({
                            index: t.index,
                            display: name,
                            displayTitle: name,
                            isDefault: t.isDefault,
                            isForced: t.isForced,
                            language: t.language
                        })
                    }
                    return items
                }
                
                Keys.onEscapePressed: function(event) {
                    popup.close()
                    selectorButton.forceActiveFocus()
                    event.accepted = true  // Prevent escape from propagating to parent views
                }
                
                Keys.onReturnPressed: selectCurrentItem()
                Keys.onEnterPressed: selectCurrentItem()
                Keys.onSpacePressed: selectCurrentItem()
                Keys.onTabPressed: function(event) {
                    // Allow tabbing out of the open dropdown to the next control
                    popup.close()
                    if (root.KeyNavigation.tab) {
                        root.KeyNavigation.tab.forceActiveFocus()
                    } else if (selectorButton) {
                        selectorButton.forceActiveFocus()
                    }
                    event.accepted = true
                }
                Keys.onBacktabPressed: function(event) {
                    // Move focus to previous control when Shift+Tab
                    popup.close()
                    if (root.KeyNavigation.up) {
                        root.KeyNavigation.up.forceActiveFocus()
                    } else if (selectorButton) {
                        selectorButton.forceActiveFocus()
                    }
                    event.accepted = true
                }
                
                function selectCurrentItem() {
                    if (currentIndex >= 0 && currentIndex < model.length) {
                        var item = model[currentIndex]
                        root.selectedIndex = item.index
                        root.trackSelected(item.index)
                        popup.close()
                        selectorButton.forceActiveFocus()
                    }
                }
                
                delegate: ItemDelegate {
                    id: trackDelegate
                    width: trackListView.width
                    height: 40
                    
                    required property var modelData
                    required property int index
                    
                    property bool isSelected: modelData.index === root.selectedIndex
                    
                    background: Rectangle {
                        color: {
                            if (trackDelegate.isSelected) return Theme.accentPrimary
                            if (trackListView.currentIndex === trackDelegate.index) return Theme.hoverOverlay
                            if (trackDelegate.hovered) return Theme.hoverOverlay
                            return "transparent"
                        }
                        radius: Theme.radiusSmall
                        
                        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                    }
                    
                    contentItem: RowLayout {
                        spacing: 8
                        
                        // Check mark for selected item
                        Text {
                            text: trackDelegate.isSelected ? "✓" : ""
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            Layout.preferredWidth: 20
                        }
                        
                        Text {
                            Layout.fillWidth: true
                            text: modelData.isNone ? noneText : (modelData.displayTitle || formatTrackName(modelData))
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            elide: Text.ElideRight
                        }
                    }
                    
                    onClicked: {
                        root.selectedIndex = modelData.index
                        root.trackSelected(modelData.index)
                        popup.close()
                        selectorButton.forceActiveFocus()
                    }
                }
                
                ScrollBar.vertical: ScrollBar {
                    policy: trackListView.contentHeight > trackListView.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                }
            }
        }
    }
}
