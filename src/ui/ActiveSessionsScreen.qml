import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import BloomUI

FocusScope {
    id: root

    property var sessionService: null
    property var sessionManager: null
    property var authService: null

    signal backRequested

    function refresh() {
        if (sessionService) {
            sessionService.fetchActiveSessions();
        }
    }

    // Auto-refresh when screen becomes visible
    onVisibleChanged: {
        if (visible) {
            refresh();
        }
    }

    // Handle self-session revocation
    Connections {
        target: sessionService
        function onSelfSessionRevoked() {
            // Current session was revoked - show message and navigate to login
            toast.show("Your session was revoked on another device. Logging out...");
            Qt.callLater(function() {
                if (authService) {
                    authService.logout();
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
                width: Theme.iconSizeLarge
                height: Theme.iconSizeLarge

                onClicked: root.backRequested()
                onActivated: root.backRequested()

                KeyNavigation.right: refreshButton
                KeyNavigation.down: sessionsList
            }

            Label {
                text: "Active Sessions"
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
                width: Theme.iconSizeMedium
                height: Theme.iconSizeMedium
                toolTipText: "Refresh"

                onClicked: refresh()
                onActivated: refresh()

                KeyNavigation.left: backButton
                KeyNavigation.down: sessionsList
            }

            A11yButton {
                id: revokeAllButton
                text: "Revoke All Others"
                font.pixelSize: Theme.fontSizeBody
                height: Theme.iconSizeMedium
                enabled: sessionService && sessionService.sessions.length > 1
                opacity: enabled ? 1.0 : 0.5

                onClicked: {
                    if (sessionService) {
                        sessionService.revokeAllOtherSessions();
                    }
                }
                onActivated: {
                    if (sessionService) {
                        sessionService.revokeAllOtherSessions();
                    }
                }

                KeyNavigation.left: refreshButton
                KeyNavigation.down: sessionsList
            }
        }

        // Loading indicator
        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: sessionService && sessionService.isLoading
            visible: running
            width: Theme.iconSizeLarge
            height: Theme.iconSizeLarge
        }

        // Error display
        Label {
            Layout.fillWidth: true
            text: sessionService ? sessionService.errorString : ""
            visible: sessionService && sessionService.errorString.length > 0
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
            model: sessionService ? sessionService.sessions : []

            KeyNavigation.up: backButton

            delegate: FocusScope {
                id: sessionDelegate
                width: sessionsList.width
                height: 80
                property bool isCurrent: model.id === (sessionService ? sessionService.currentSessionId : "")

                // Background
                Rectangle {
                    anchors.fill: parent
                    color: sessionDelegate.activeFocus ? Theme.accentColor : (isCurrent ? Theme.accentColor + "20" : Theme.backgroundSecondary)
                    radius: Theme.radiusMedium
                    border.width: isCurrent ? 2 : 0
                    border.color: Theme.accentColor
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    // Device icon
                    Label {
                        text: getDeviceIcon(model.client)
                        font.family: Icons.materialFamily
                        font.pixelSize: Theme.iconSizeMedium
                        color: sessionDelegate.activeFocus ? Theme.textPrimary : Theme.textSecondary
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // Device info
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXSmall

                        RowLayout {
                            spacing: Theme.spacingSmall

                            Label {
                                text: model.deviceName || "Unknown Device"
                                font.pixelSize: Theme.fontSizeBody
                                font.weight: Font.Medium
                                color: sessionDelegate.activeFocus ? Theme.textPrimary : Theme.textPrimary
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            // Current session badge
                            Rectangle {
                                visible: sessionDelegate.isCurrent
                                width: currentSessionLabel.width + Theme.spacingSmall * 2
                                height: currentSessionLabel.height + Theme.spacingXSmall * 2
                                color: Theme.accentColor
                                radius: Theme.radiusSmall

                                Label {
                                    id: currentSessionLabel
                                    anchors.centerIn: parent
                                    text: "This Device"
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.weight: Font.Bold
                                    color: Theme.textPrimary
                                }
                            }
                        }

                        Label {
                            text: model.client + (model.clientVersion ? " " + model.clientVersion : "")
                            font.pixelSize: Theme.fontSizeCaption
                            color: sessionDelegate.activeFocus ? Theme.textSecondary : Theme.textMuted
                        }

                        Label {
                            text: "Last active: " + formatDate(model.lastActivityDate)
                            font.pixelSize: Theme.fontSizeCaption
                            color: sessionDelegate.activeFocus ? Theme.textSecondary : Theme.textMuted
                        }
                    }

                    // Revoke button (only for other sessions)
                    A11yButton {
                        id: revokeButton
                        visible: !sessionDelegate.isCurrent
                        enabled: visible
                        text: "\ue14c"  // Close/cancel icon
                        font.family: Icons.materialFamily
                        font.pixelSize: Theme.iconSizeSmall
                        width: Theme.iconSizeMedium
                        height: Theme.iconSizeMedium
                        toolTipText: "Revoke session"

                        onClicked: {
                            if (sessionService) {
                                sessionService.revokeSession(model.id);
                            }
                        }
                        onActivated: {
                            if (sessionService) {
                                sessionService.revokeSession(model.id);
                            }
                        }

                        KeyNavigation.left: sessionDelegate
                    }
                }

                Keys.onReturnPressed: {
                    if (!sessionDelegate.isCurrent && revokeButton.visible) {
                        revokeButton.clicked();
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: sessionDelegate.forceActiveFocus()
                }
            }

            // Empty state
            Label {
                visible: parent.count === 0 && (!sessionService || !sessionService.isLoading)
                anchors.centerIn: parent
                text: "No active sessions found"
                font.pixelSize: Theme.fontSizeBody
                color: Theme.textMuted
            }
        }
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
