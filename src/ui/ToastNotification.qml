import QtQuick
import QtQuick.Controls

/**
 * Toast Notification Component
 * 
 * Displays a brief notification message that auto-hides after a timeout.
 * Used for non-blocking informational messages to the user.
 */
Rectangle {
    id: root
    
    // Configuration
    property string message: ""
    property int displayDuration: 5000  // ms
    property bool autoHide: true
    
    // Internal state
    property bool showing: false
    
    // Positioning
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 80
    
    // Size based on content
    width: Math.min(contentText.implicitWidth + Theme.spacingXLarge * 2, parent.width - Theme.paddingLarge * 2)
    height: contentText.implicitHeight + Theme.paddingLarge * 2
    
    // Appearance
    radius: Theme.radiusMedium
    color: Theme.overlayDark
    opacity: showing ? 1.0 : 0.0
    visible: opacity > 0
    
    Behavior on opacity {
        NumberAnimation {
            duration: Theme.durationShort
            easing.type: Easing.InOutQuad
        }
    }
    
    // Shadow for depth
    layer.enabled: true
    layer.effect: null  // Could add DropShadow if needed
    
    Text {
        id: contentText
        anchors.centerIn: parent
        width: parent.width - Theme.spacingXLarge * 2
        text: root.message
        color: Theme.textPrimary
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontPrimary
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
    }
    
    Timer {
        id: hideTimer
        interval: root.displayDuration
        running: root.showing && root.autoHide
        onTriggered: root.hide()
    }
    
    function show(text) {
        message = text
        showing = true
        hideTimer.restart()
    }
    
    function hide() {
        showing = false
    }
}
