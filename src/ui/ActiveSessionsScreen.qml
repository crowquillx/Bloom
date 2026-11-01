import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import BloomUI

/**
 * ActiveSessionsScreen - playback or authentication sessions for the signed-in
 * account, opened from Settings > About & Account.
 *
 * Panel archetype (docs/style-guide.md): ambient stage, PageHeader with the
 * page actions on its baseline row, then a column of rows. Each row shows the
 * device, its client and activity, and a Revoke control on the right edge;
 * Return on a focused row revokes it. Up from the list returns to the header
 * actions; Up from the header reaches the navigation bar. When the account may
 * not revoke other devices (a Jellyfin non-administrator), rows carry no
 * Revoke control and a note under the header explains why.
 */
FocusScope {
    id: root
    // Connection whose items this page shows; Main.qml stamps it when shown.
    property string ownerConnectionId: ""
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
            root.rememberListFocus()
            sessionService.fetchActiveSessions();
        }
    }

    // A refresh or revoke can empty the list for a moment (it hides while
    // empty), so remember that it had focus and give it back when rows return.
    property bool restoreListFocus: false

    function rememberListFocus() {
        if (sessionsList.activeFocus)
            root.restoreListFocus = true
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
    readonly property bool loading: !!(sessionService && sessionService.isLoading)
    readonly property bool canRevokeOthers: !!(sessionService && sessionService.canRevokeOtherSessions)
    readonly property string errorText: sessionService ? sessionService.errorString : ""
    readonly property int sessionCount: sessionsList.count

    // Sessions reload after a revoke or refresh; keep focus on the list (or
    // the header when the list empties) instead of losing it.
    Connections {
        target: root.sessionService
        ignoreUnknownSignals: true
        function onSessionsChanged() {
            Qt.callLater(function() {
                if (!root.visible)
                    return
                if (sessionsList.count > 0) {
                    if (sessionsList.currentIndex >= sessionsList.count)
                        sessionsList.currentIndex = sessionsList.count - 1
                    if (root.restoreListFocus) {
                        root.restoreListFocus = false
                        sessionsList.forceActiveFocus()
                    }
                } else if ((root.restoreListFocus || sessionsList.activeFocus) && !root.loading) {
                    // The update finished with no rows: fall back to the header.
                    root.restoreListFocus = false
                    backButton.forceActiveFocus()
                }
            })
        }
    }

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

    // Panel stage: ambient glow, no art.
    StageBackdrop {
        anchors.fill: parent
        mode: "ambient"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMarginX
        anchors.rightMargin: Theme.pageMarginX
        anchors.topMargin: Theme.pageMarginTop
        anchors.bottomMargin: Theme.pageMarginBottom
        spacing: Theme.spacingXLarge

        PageHeader {
            Layout.fillWidth: true
            eyebrow: qsTr("Account")
            title: root.sessionService
                ? root.sessionService.sessionTypeLabel
                : qsTr("Playback Sessions")
            detail: {
                var description = root.sessionService ? root.sessionService.sessionTypeDescription : ""
                if (root.loading || root.sessionCount === 0)
                    return description
                var count = root.sessionCount === 1 ? qsTr("1 session") : qsTr("%1 sessions").arg(root.sessionCount)
                return description.length > 0 ? description + "  ·  " + count : count
            }

            ActionButton {
                id: backButton
                variant: "ghost"
                iconGlyph: Icons.arrowBack
                text: qsTr("Back")
                toolTipText: qsTr("Back")
                Layout.preferredHeight: Theme.buttonHeightMedium
                onClicked: root.backRequested()
                Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                Keys.onEnterPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }

                KeyNavigation.right: refreshButton
                KeyNavigation.down: sessionsList.count > 0 ? sessionsList : null
            }

            ActionButton {
                id: refreshButton
                variant: "secondary"
                iconGlyph: Icons.refresh
                text: qsTr("Refresh")
                toolTipText: qsTr("Refresh")
                busy: root.loading
                enabled: root.sessionService && !root.sessionService.isLoading
                Layout.preferredHeight: Theme.buttonHeightMedium
                onClicked: root.refresh()
                Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                Keys.onEnterPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }

                KeyNavigation.left: backButton
                KeyNavigation.right: revokeAllButton.visible ? revokeAllButton : backButton
                KeyNavigation.down: sessionsList.count > 0 ? sessionsList : null
            }

            ActionButton {
                id: revokeAllButton
                variant: "destructive"
                iconGlyph: Icons.logout
                text: root.sessionService
                    ? qsTr("Revoke All Other %1").arg(root.sessionService.sessionTypeLabel)
                    : qsTr("Revoke All Other Playback Sessions")
                enabled: root.sessionService && !root.sessionService.isLoading
                    && root.sessionService.sessions.length > 1
                visible: !root.authenticationMode && root.canRevokeOthers
                Layout.preferredHeight: Theme.buttonHeightMedium
                onClicked: {
                    if (root.sessionService) root.sessionService.revokeAllOtherSessions()
                }
                Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                Keys.onEnterPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }

                KeyNavigation.left: refreshButton
                KeyNavigation.down: sessionsList.count > 0 ? sessionsList : null
            }
        }

        // Why rows have no Revoke control. Not focusable: it only explains.
        RowLayout {
            id: revokeRestrictionNote
            Layout.fillWidth: true
            visible: !root.authenticationMode && !root.canRevokeOthers && root.sessionService !== null
            spacing: Theme.spacingSmall
            Accessible.role: Accessible.StaticText
            Accessible.name: revokeRestrictionText.text

            Text {
                Layout.alignment: Qt.AlignTop
                text: Icons.info
                font.family: Theme.fontIcon
                font.pixelSize: Theme.fontSizeBody
                color: Theme.textSecondary
                Accessible.ignored: true
            }

            Text {
                id: revokeRestrictionText
                Layout.fillWidth: true
                text: qsTr("Only a server administrator can sign out other devices. To sign out this device, use Sign Out in Settings › About & Account.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                wrapMode: Text.Wrap
                Accessible.ignored: true
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Sessions list
            ListView {
                id: sessionsList
                anchors.fill: parent
                // Room for the inset focus ring is inside each row; clip keeps
                // rows from sliding under the header.
                clip: true
                spacing: Theme.spacingSmall
                model: root.sessionService ? root.sessionService.sessions : []
                visible: count > 0
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0
                keyNavigationEnabled: true
                // The user moved elsewhere while rows were still showing, so a
                // pending refresh or revoke must not pull focus back.
                onActiveFocusChanged: {
                    if (!activeFocus && count > 0)
                        root.restoreListFocus = false
                }
                Accessible.role: Accessible.List
                Accessible.name: root.sessionService
                    ? root.sessionService.sessionTypeLabel
                    : qsTr("Playback Sessions")

                Keys.onUpPressed: function(event) {
                    if (currentIndex > 0)
                        decrementCurrentIndex()
                    else
                        backButton.forceActiveFocus()
                    event.accepted = true
                }

                delegate: FocusScope {
                    id: sessionDelegate
                    required property var modelData
                    required property int index
                    width: sessionsList.width
                    height: Math.round(112 * Theme.layoutScale)
                    property bool isCurrent: root.authenticationMode
                        ? !!modelData.isCurrent
                        : modelData.id === (root.sessionService ? root.sessionService.currentSessionId : "")
                    property bool highlighted: ListView.isCurrentItem && sessionsList.activeFocus
                    readonly property bool canRevoke: root.authenticationMode
                        || (!isCurrent && root.canRevokeOthers)
                    function requestRevoke() {
                        if (canRevoke && revokeButton.enabled && root.sessionService) {
                            root.rememberListFocus()
                            root.sessionService.revokeSession(modelData.id)
                        }
                    }

                    Accessible.role: Accessible.ListItem
                    Accessible.name: (modelData.deviceName || qsTr("Unknown Device"))
                                     + (isCurrent ? ", " + currentSessionLabel.text : "")
                    Accessible.description: secondaryLine.text

                    // Row surface: quiet at rest, fill + ring on focus. The
                    // current device carries the selection tint and indicator.
                    Rectangle {
                        anchors.fill: parent
                        z: -1
                        color: sessionDelegate.highlighted || rowMouse.containsMouse
                               ? Theme.surfaceInteractiveHover
                               : (sessionDelegate.isCurrent ? Theme.selectionBackground : Theme.surfaceInteractive)
                        radius: Theme.radiusMedium
                        border.width: Theme.borderWidth
                        border.color: Theme.surfaceBorder

                        Rectangle {
                            anchors.left: parent.left
                            anchors.leftMargin: Math.round(6 * Theme.layoutScale)
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.selectionIndicatorWidth
                            height: parent.height * 0.5
                            radius: width / 2
                            color: Theme.selectionIndicator
                            visible: sessionDelegate.isCurrent
                        }

                        FocusRing {
                            inset: true
                            active: sessionDelegate.highlighted
                            radius: parent.radius
                        }
                    }

                    MouseArea {
                        id: rowMouse
                        z: 0
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: InputModeManager.pointerActive
                            ? Qt.PointingHandCursor : Qt.BlankCursor
                        onClicked: {
                            InputModeManager.setNavigationMode("pointer")
                            InputModeManager.hideCursor(false)
                            sessionsList.currentIndex = index
                            sessionsList.forceActiveFocus()
                        }
                    }

                    RowLayout {
                        z: 1
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingLarge
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.spacingMedium

                        // Device thumbnail
                        Rectangle {
                            Layout.preferredWidth: Math.round(72 * Theme.layoutScale)
                            Layout.preferredHeight: Layout.preferredWidth
                            Layout.alignment: Qt.AlignVCenter
                            radius: Theme.radiusSmall
                            color: Theme.surfaceInteractive
                            border.width: Theme.borderWidth
                            border.color: Theme.surfaceBorder

                            Text {
                                anchors.centerIn: parent
                                text: root.getDeviceIcon(root.authenticationMode ? sessionDelegate.modelData.deviceName
                                                                            : sessionDelegate.modelData.client)
                                font.family: Icons.materialFamily
                                font.pixelSize: Theme.iconSizeNav
                                color: sessionDelegate.highlighted ? Theme.textPrimary : Theme.textSecondary
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: Math.round(4 * Theme.layoutScale)

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSmall

                                Text {
                                    text: sessionDelegate.modelData.deviceName || qsTr("Unknown Device")
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    font.weight: Theme.fontWeightStrong
                                    color: Theme.textPrimary
                                    elide: Text.ElideRight
                                    // Natural width, shrinking (and eliding) only when the row is full.
                                    Layout.preferredWidth: implicitWidth
                                    Layout.minimumWidth: 0
                                }

                                Rectangle {
                                    visible: sessionDelegate.isCurrent
                                    implicitWidth: currentSessionLabel.implicitWidth + Theme.spacingMedium
                                    implicitHeight: currentSessionLabel.implicitHeight + Theme.spacingXSmall * 2
                                    color: Theme.selectionIndicator
                                    radius: height / 2

                                    Text {
                                        id: currentSessionLabel
                                        anchors.centerIn: parent
                                        text: root.authenticationMode ? qsTr("Current") : qsTr("This Device")
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontPrimary
                                        font.weight: Theme.fontWeightHeavy
                                        color: Theme.textOnAccent
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }

                            Text {
                                id: secondaryLine
                                Layout.fillWidth: true
                                text: {
                                    var data = sessionDelegate.modelData
                                    if (root.authenticationMode)
                                        return qsTr("IP address: %1").arg(data.ipAddress || qsTr("Unknown"))
                                               + "  ·  " + qsTr("Created: %1").arg(root.formatDate(data.createdAt))
                                    return (data.client || "") + (data.clientVersion ? " " + data.clientVersion : "")
                                           + "  ·  " + qsTr("Last active: %1").arg(root.formatDate(data.lastActivityDate))
                                }
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.fontPrimary
                                font.weight: Theme.fontWeightMedium
                                color: Theme.textSecondary
                                elide: Text.ElideRight
                            }
                        }

                        // Revoke (only for other sessions). Return on the row
                        // triggers it; the button itself serves the pointer.
                        ActionButton {
                            id: revokeButton
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredHeight: Theme.buttonHeightSmall
                            visible: sessionDelegate.canRevoke
                            enabled: visible && root.sessionService
                                && !root.sessionService.isLoading
                            variant: sessionDelegate.highlighted ? "destructive" : "ghost"
                            labelColor: Theme.actionDestructiveText
                            iconColor: Theme.actionDestructiveText
                            iconGlyph: Icons.close
                            text: qsTr("Revoke")
                            focusPolicy: Qt.NoFocus
                            accessibleLabel: root.authenticationMode
                                ? qsTr("Revoke authentication session")
                                : qsTr("Revoke playback session")
                            toolTipText: accessibleLabel
                            onClicked: sessionDelegate.requestRevoke()
                        }
                    }
                }

                Keys.onReturnPressed: function(event) {
                    if (!event.isAutoRepeat && currentItem) currentItem.requestRevoke()
                    event.accepted = true
                }
                Keys.onEnterPressed: function(event) {
                    if (!event.isAutoRepeat && currentItem) currentItem.requestRevoke()
                    event.accepted = true
                }
            }

            StateMessage {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: Theme.sectionSpacing * 2
                width: parent.width
                visible: sessionsList.count === 0 && root.loading
                kind: "loading"
                message: qsTr("Loading sessions…")
            }

            StateMessage {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: Theme.sectionSpacing * 2
                width: parent.width
                visible: sessionsList.count === 0 && !root.loading && root.errorText.length === 0
                kind: "empty"
                iconGlyph: Icons.monitor
                title: root.sessionService
                    ? qsTr("No %1 found").arg(root.sessionService.sessionTypeLabel.toLowerCase())
                    : qsTr("No playback sessions found")
            }

            StateMessage {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: sessionsList.count > 0 ? 0 : Theme.sectionSpacing * 2
                width: parent.width
                visible: root.errorText.length > 0 && !root.loading && sessionsList.count === 0
                kind: "error"
                title: qsTr("Couldn't load sessions")
                message: root.errorText
            }
        }

        // An error while rows are still shown (e.g. a failed revoke).
        Text {
            Layout.fillWidth: true
            visible: root.errorText.length > 0 && sessionsList.count > 0
            text: root.errorText
            color: Theme.errorColor
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.fontPrimary
            wrapMode: Text.Wrap
        }
    }

    WheelStepScroller {
        parent: sessionsList
        anchors.fill: parent
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
        if (!client) return "";  // Default device icon
        client = client.toLowerCase();
        if (client.includes("android")) return "";  // Phone
        if (client.includes("ios")) return "";  // Phone
        if (client.includes("web")) return "";  // Web
        if (client.includes("kodi")) return "";  // TV
        if (client.includes("roku")) return "";  // TV
        return "";  // Desktop/HTPC
    }

    function plural(count, unit) {
        return count + " " + unit + (count === 1 ? "" : "s");
    }

    function formatDate(dateValue) {
        if (!dateValue) return "Unknown";
        var date = new Date(dateValue);
        var now = new Date();
        var diff = (now - date) / 1000;  // Seconds

        if (diff < 60) return "Just now";
        if (diff < 3600) return root.plural(Math.floor(diff / 60), "minute") + " ago";
        if (diff < 86400) return root.plural(Math.floor(diff / 3600), "hour") + " ago";
        if (diff < 604800) return root.plural(Math.floor(diff / 86400), "day") + " ago";

        return date.toLocaleDateString();
    }
}
