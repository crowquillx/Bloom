import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import BloomUI

FocusScope {
    id: root
    focus: true

    property var sessionService: null
    property var sessionManager: null
    property var authService: null

    signal backRequested

    Keys.onEscapePressed: function(event) {
        root.backRequested()
        event.accepted = true
    }
    Keys.onBackPressed: function(event) {
        root.backRequested()
        event.accepted = true
    }

    function refresh() {
        if (sessionService) {
            sessionService.fetchActiveSessions();
        }
    }

    Component.onCompleted: Qt.callLater(function() {
        if (root.visible) backButton.forceActiveFocus()
    })

    // Auto-refresh when screen becomes visible.
    onVisibleChanged: {
        if (visible) {
            refresh()
            Qt.callLater(function() { backButton.forceActiveFocus() })
        }
    }

    readonly property bool authenticationMode: sessionService
        && sessionService.authenticationSessionMode

    // Handle self-session revocation
    Connections {
        target: root.sessionService
        function onSelfSessionRevoked() {
            // Current session was revoked - show message and navigate to login
            toast.show("Your session was revoked on another device. Logging out...");
            Qt.callLater(function() {
                if (root.authService) {
                    root.authService.logout();
                }
            });
        }
    }

    // Background
    Rectangle {
        anchors.fill: parent
        color: Theme.backgroundPrimary
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacingMedium

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            A11yButton {
                id: backButton
                text: "\ue92f"  // Back arrow icon
                font.family: Icons.materialFamily
                font.pixelSize: Theme.iconSizeMedium
                Layout.preferredWidth: Theme.iconSizeLarge
                Layout.preferredHeight: Theme.iconSizeLarge

                onActivated: root.backRequested()

                KeyNavigation.right: refreshButton
                KeyNavigation.down: sessionsList
            }

            Label {
                text: root.sessionService
                    ? root.sessionService.sessionTypeLabel
                    : qsTr("Playback Sessions")
                font.pixelSize: Theme.fontSizeHeader
                font.weight: Font.Bold
                color: Theme.textPrimary
                Layout.fillWidth: true
            }

            A11yButton {
                id: refreshButton
                text: "\ue5d5"  // Refresh icon
                font.family: Icons.materialFamily
                font.pixelSize: Theme.iconSizeSmall
                Layout.preferredWidth: Theme.iconSizeMedium
                Layout.preferredHeight: Theme.iconSizeMedium
                toolTipText: qsTr("Refresh")
                enabled: root.sessionService && !root.sessionService.isLoading

                onActivated: refresh()

                KeyNavigation.left: backButton
                KeyNavigation.right: revokeAllButton.visible ? revokeAllButton : backButton
                KeyNavigation.down: sessionsList
            }

            A11yButton {
                id: revokeAllButton
                text: root.sessionService
                    ? qsTr("Revoke All Other %1").arg(root.sessionService.sessionTypeLabel)
                    : qsTr("Revoke All Other Playback Sessions")
                font.pixelSize: Theme.fontSizeBody
                height: Theme.iconSizeMedium
                enabled: root.sessionService && !root.sessionService.isLoading
                    && root.sessionService.sessions.length > 1
                visible: !root.authenticationMode
                opacity: enabled ? 1.0 : 0.5

                onActivated: {
                    if (root.sessionService) root.sessionService.revokeAllOtherSessions()
                }

                KeyNavigation.left: refreshButton
                KeyNavigation.down: sessionsList
            }
        }

        Label {
            text: root.sessionService ? root.sessionService.sessionTypeDescription : ""
            visible: text.length > 0
            Layout.fillWidth: true
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
        }

        // Loading indicator
        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: root.sessionService && root.sessionService.isLoading
            visible: running
            Layout.preferredWidth: Theme.iconSizeLarge
            Layout.preferredHeight: Theme.iconSizeLarge
        }

        // Error display
        Label {
            Layout.fillWidth: true
            text: root.sessionService ? root.sessionService.errorString : ""
            visible: root.sessionService && root.sessionService.errorString.length > 0
            color: Theme.errorColor
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }

        // Sessions list
        ListView {
            id: sessionsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingSmall
            model: root.sessionService ? root.sessionService.sessions : []

            KeyNavigation.up: backButton

            delegate: FocusScope {
                id: sessionDelegate
                width: sessionsList.width
                height: 80
                property bool isCurrent: root.authenticationMode
                    ? !!model.isCurrent
                    : model.id === (root.sessionService ? root.sessionService.currentSessionId : "")
                property bool highlighted: ListView.isCurrentItem && sessionsList.activeFocus
                function requestRevoke() {
                    if ((!isCurrent || root.authenticationMode)
                        && revokeButton.enabled && root.sessionService) {
                        root.sessionService.revokeSession(model.id)
                    }
                }
                // Background
                Rectangle {
                    anchors.fill: parent
                    z: -1
                    color: sessionDelegate.highlighted
                        ? Theme.accentColor
                        : (isCurrent
                            ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g,
                                      Theme.accentColor.b, 0.12)
                            : Theme.backgroundSecondary)
                    radius: Theme.radiusMedium
                    border.width: isCurrent ? 2 : 0
                    border.color: Theme.accentColor
                }

                RowLayout {
                    z: 1
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingMedium
                    // Device icon
                    Label {
                        text: getDeviceIcon(root.authenticationMode ? model.deviceName : model.client)
                        font.family: Icons.materialFamily
                        font.pixelSize: Theme.iconSizeMedium
                        color: sessionDelegate.highlighted ? Theme.textPrimary : Theme.textSecondary
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // Device info
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXSmall

                        RowLayout {
                            spacing: Theme.spacingSmall

                            Label {
                                text: model.deviceName || qsTr("Unknown Device")
                                font.pixelSize: Theme.fontSizeBody
                                font.weight: Font.Medium
                                color: Theme.textPrimary
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                visible: sessionDelegate.isCurrent
                                implicitWidth: currentSessionLabel.implicitWidth + Theme.spacingSmall * 2
                                implicitHeight: currentSessionLabel.implicitHeight + Theme.spacingXSmall * 2
                                color: Theme.accentColor
                                radius: Theme.radiusSmall

                                Label {
                                    id: currentSessionLabel
                                    anchors.centerIn: parent
                                    text: root.authenticationMode ? qsTr("Current") : qsTr("This Device")
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.weight: Font.Bold
                                    color: Theme.textPrimary
                                }
                            }
                        }

                        Label {
                            text: root.authenticationMode
                                ? qsTr("IP address: %1").arg(model.ipAddress || qsTr("Unknown"))
                                : model.client + (model.clientVersion ? " " + model.clientVersion : "")
                            font.pixelSize: Theme.fontSizeCaption
                            color: sessionDelegate.highlighted ? Theme.textSecondary : Theme.textMuted
                        }

                        Label {
                            text: root.authenticationMode
                                ? qsTr("Created: %1").arg(formatDate(model.createdAt))
                                : qsTr("Last active: %1").arg(formatDate(model.lastActivityDate))
                            font.pixelSize: Theme.fontSizeCaption
                            color: sessionDelegate.highlighted ? Theme.textSecondary : Theme.textMuted
                        }
                    }

                    // Revoke button (only for other sessions)
                    A11yButton {
                        id: revokeButton
                        visible: !sessionDelegate.isCurrent || root.authenticationMode
                        enabled: visible && root.sessionService
                            && !root.sessionService.isLoading
                        text: "\ue14c"  // Close/cancel icon
                        font.family: Icons.materialFamily
                        font.pixelSize: Theme.iconSizeSmall
                        Layout.preferredWidth: Theme.iconSizeMedium
                        Layout.preferredHeight: Theme.iconSizeMedium
                        toolTipText: root.authenticationMode
                            ? qsTr("Revoke authentication session")
                            : qsTr("Revoke playback session")

                        onActivated: sessionDelegate.requestRevoke()

                        KeyNavigation.left: sessionDelegate
                    }
                }


                MouseArea {
                    z: 0
                    anchors.fill: parent
                    cursorShape: InputModeManager.pointerActive
                        ? Qt.PointingHandCursor : Qt.BlankCursor
                    onClicked: {
                        InputModeManager.setNavigationMode("pointer")
                        InputModeManager.hideCursor(false)
                        sessionsList.currentIndex = index
                        sessionsList.forceActiveFocus()
                    }
                }
            }

            // Empty state
            Label {
                visible: parent.count === 0 && (!root.sessionService || !root.sessionService.isLoading)
                anchors.centerIn: parent
                text: root.sessionService
                    ? qsTr("No %1 found").arg(root.sessionService.sessionTypeLabel.toLowerCase())
                    : qsTr("No playback sessions found")
                font.pixelSize: Theme.fontSizeBody
                color: Theme.textMuted
            }
            Keys.onReturnPressed: {
                if (currentItem) currentItem.requestRevoke()
            }
            Keys.onEnterPressed: {
                if (currentItem) currentItem.requestRevoke()
            }
        }

    }

    WheelStepScroller {
        anchors.fill: sessionsList
        target: sessionsList
        stepPx: Math.round(80 * Theme.layoutScale)
    }

    // Toast notification
    ToastNotification {
        id: toast
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLarge
        anchors.horizontalCenter: parent.horizontalCenter
    }

    // Helper functions
    function getDeviceIcon(client) {
        if (!client) return "\ue30a";  // Default device icon
        client = client.toLowerCase();
        if (client.includes("android")) return "\ue859";  // Phone
        if (client.includes("ios")) return "\ue859";  // Phone
        if (client.includes("web")) return "\ue894";  // Web
        if (client.includes("kodi")) return "\ue333";  // TV
        if (client.includes("roku")) return "\ue333";  // TV
        return "\ue30a";  // Desktop/HTPC
    }

    function formatDate(dateValue) {
        if (!dateValue) return "Unknown";
        var date = new Date(dateValue);
        var now = new Date();
        var diff = (now - date) / 1000;  // Seconds

        if (diff < 60) return "Just now";
        if (diff < 3600) return Math.floor(diff / 60) + " minutes ago";
        if (diff < 86400) return Math.floor(diff / 3600) + " hours ago";
        if (diff < 604800) return Math.floor(diff / 86400) + " days ago";

        return date.toLocaleDateString();
    }
}
