import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root
    objectName: "loginScreen"
    focus: true

    signal loginSuccess()

    readonly property string authenticationStep: AuthenticationService.authenticationStep
    readonly property var availableProfiles: AuthenticationService.profiles || []
    property string selectedProfileName: ""
    property string selectedProfileAvatar: ""
    property string selectedProfileId: ""
    property bool serviceLoginReported: false
    property bool completionEmitted: false
    property string statusMessage: ""
    // Signing in to an additional server keeps the previous sign-in; offer a
    // way back. Signed out with saved spaces, offer to reopen one.
    readonly property bool addingConnection: typeof Workspaces !== "undefined" && Workspaces.addingConnection
    readonly property bool hasSavedSpaces: typeof Workspaces !== "undefined" && Workspaces.profiles.length > 0
    readonly property bool showSpaceAction: authenticationStep === "credentials"
                                            && (addingConnection || hasSavedSpaces)
    property bool statusIsError: false

    function setStatus(message, isError) {
        statusMessage = message
        statusIsError = isError
    }

    function clearStatus() {
        setStatus("", false)
    }

    function submitCredentials() {
        if (serverField.text.trim().length === 0) {
            setStatus(qsTr("Enter your server URL."), true)
            serverField.forceActiveFocus()
            return
        }

        setStatus(qsTr("Signing in…"), false)
        AuthenticationService.authenticate(serverField.text.trim(), userField.text, passField.text)
    }

    function profileId(profile) {
        if (!profile)
            return ""
        return profile.id || profile.profileId || profile.profile_id || ""
    }

    function profileName(profile) {
        if (!profile)
            return qsTr("Profile")
        return profile.name || profile.displayName || qsTr("Profile")
    }

    function profileAvatar(profile) {
        if (!profile)
            return ""
        return profile.avatarUrl || profile.avatar_url || profile.imageUrl || ""
    }

    function profileHasPin(profile) {
        return !!(profile && (profile.hasPin || profile.has_pin || profile.pinRequired))
    }

    function syncSelectedProfile() {
        if (authenticationStep === "pin" && selectedProfileId.length === 0) {
            var profile = null
            if (availableProfiles.length === 1) {
                profile = availableProfiles[0]
            } else if (profileList.currentIndex >= 0 && profileList.currentIndex < availableProfiles.length) {
                profile = availableProfiles[profileList.currentIndex]
            }
            if (profile) {
                selectedProfileId = profileId(profile)
                selectedProfileName = profileName(profile)
                selectedProfileAvatar = profileAvatar(profile)
            }
        }
    }

    function chooseProfile(profile) {
        const id = profileId(profile)
        if (id.length === 0)
            return

        selectedProfileId = id
        selectedProfileName = profileName(profile)
        selectedProfileAvatar = profileAvatar(profile)
        clearStatus()
        AuthenticationService.selectProfile(id)
    }

    function submitPin() {
        if (selectedProfileId.length === 0) {
            syncSelectedProfile()
        }
        if (selectedProfileId.length === 0) {
            setStatus(qsTr("Choose a profile before entering a PIN."), true)
            cancelAuthentication()
            return
        }
        if (pinField.text.length === 0) {
            setStatus(qsTr("Enter the profile PIN."), true)
            pinField.forceActiveFocus()
            return
        }

        setStatus(qsTr("Checking PIN…"), false)
        AuthenticationService.verifyProfilePin(selectedProfileId, pinField.text)
    }

    function cancelAuthentication() {
        passField.clear()
        pinField.clear()
        selectedProfileName = ""
        selectedProfileId = ""
        selectedProfileAvatar = ""
        serviceLoginReported = false
        completionEmitted = false
        clearStatus()
        if (AuthenticationService.authenticated
            || authenticationStep === "pin"
            || authenticationStep === "profiles") {
            AuthenticationService.clearAccountState()
        }
        Qt.callLater(restoreStepFocus)
    }

    function restoreStepFocus() {
        Qt.callLater(function() {
            if (!root.visible)
                return

            if (typeof InputModeManager !== "undefined" && !InputModeManager.pointerActive) {
                InputModeManager.setNavigationMode("keyboard")
                InputModeManager.hideCursor(true)
            }

            if (authenticationStep === "profiles") {
                if (availableProfiles.length > 0) {
                    profileList.currentIndex = Math.max(0, Math.min(profileList.currentIndex >= 0 ? profileList.currentIndex : 0,
                                                                      availableProfiles.length - 1))
                    profileList.forceActiveFocus()
                } else {
                    profilesBackButton.forceActiveFocus()
                }
            } else if (authenticationStep === "pin") {
                syncSelectedProfile()
                pinField.forceActiveFocus()
            } else {
                serverField.forceActiveFocus()
            }
        })
    }


    function restoreRetryFocus() {
        Qt.callLater(function() {
            if (!root.visible)
                return

            if (typeof InputModeManager !== "undefined" && !InputModeManager.pointerActive) {
                InputModeManager.setNavigationMode("keyboard")
                InputModeManager.hideCursor(true)
            }

            if (authenticationStep === "pin") {
                pinField.selectAll()
                pinField.forceActiveFocus()
            } else if (authenticationStep === "profiles") {
                if (profileList.count > 0) {
                    profileList.forceActiveFocus()
                } else {
                    profilesBackButton.forceActiveFocus()
                }
            } else {
                passField.selectAll()
                passField.forceActiveFocus()
            }
        })
    }

    function maybeEmitLoginSuccess() {
        if (!serviceLoginReported || completionEmitted || authenticationStep !== "authenticated")
            return
        completionEmitted = true
        loginSuccess()
    }

    onActiveFocusChanged: {
        if (activeFocus)
            restoreStepFocus()
    }

    Keys.onEscapePressed: function(event) {
        if (addingConnection && authenticationStep === "credentials") {
            clearStatus()
            Workspaces.cancelAddConnection()
            event.accepted = true
            return
        }
        if (authenticationStep !== "authenticated") {
            cancelAuthentication()
            event.accepted = true
        }
    }

    Keys.onBackPressed: function(event) {
        if (addingConnection && authenticationStep === "credentials") {
            clearStatus()
            Workspaces.cancelAddConnection()
            event.accepted = true
            return
        }
        if (authenticationStep !== "authenticated") {
            cancelAuthentication()
            event.accepted = true
        }
    }
    Image {
        anchors.fill: parent
        source: "qrc:/images/app/login.jpg"
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.55) }
            GradientStop { position: 1.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.85) }
        }
    }

    Rectangle {
        id: loginCard
        anchors.centerIn: parent
        width: Math.min(Math.round(780 * Theme.layoutScale), parent.width - Theme.spacingLarge * 2)
        height: Math.min(Math.round((root.showSpaceAction ? 876 : 800) * Theme.layoutScale),
                         parent.height - Theme.spacingLarge * 2)
        radius: Theme.radiusPanel
        color: Theme.surfaceGlassStrong
        border.color: Theme.surfaceBorder
        border.width: Theme.borderWidth

        ColumnLayout {
            id: cardContent
            anchors.fill: parent
            anchors.margins: Theme.dialogPadding
            spacing: Theme.controlSpacing

            Image {
                source: "qrc:/images/app/logo_trans.svg"
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredHeight: Math.round(54 * Theme.layoutScale)
                Layout.preferredWidth: Math.round(180 * Theme.layoutScale)
                fillMode: Image.PreserveAspectFit
                sourceSize.height: Math.round(96 * Theme.layoutScale)
                mipmap: true
            }

            Text {
                text: authenticationStep === "profiles" ? qsTr("Who’s watching?")
                      : authenticationStep === "pin" ? qsTr("Enter profile PIN")
                      : qsTr("Connect your media server")
                color: Theme.textPrimary
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeHeader
                font.weight: Theme.fontWeightHeavy
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Text {
                text: authenticationStep === "profiles"
                      ? qsTr("Choose a Silo profile to continue.")
                      : authenticationStep === "pin"
                        ? (selectedProfileName.length > 0
                           ? qsTr("Unlock %1 to continue.").arg(selectedProfileName)
                           : qsTr("Unlock this profile to continue."))
                        : qsTr("Bloom will detect whether this server uses Jellyfin or Silo. Native Silo support is experimental.")
                color: Theme.textSecondary
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeSmall
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: authenticationStep === "profiles" ? 1
                              : authenticationStep === "pin" ? 2 : 0

                ColumnLayout {
                    spacing: Theme.spacingSmall

                    TextField {
                        id: serverField
                        placeholderText: qsTr("media.example.com")
                        placeholderTextColor: Theme.textTertiary
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.buttonHeightMedium
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                        KeyNavigation.up: root.showSpaceAction ? spaceActionButton : connectButton
                        KeyNavigation.down: userField
                        KeyNavigation.tab: userField
                        KeyNavigation.backtab: root.showSpaceAction ? spaceActionButton : credentialCancelButton
                        onAccepted: userField.forceActiveFocus()
                        leftPadding: Theme.spacingMedium
                        rightPadding: Theme.spacingMedium
                        background: FieldBackground { control: serverField }
                    }

                    Text {
                        text: qsTr("Enter your server address. Bloom tries HTTPS, then HTTP; a custom port is optional.")
                        color: Theme.textSecondary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }

                    TextField {
                        id: userField
                        placeholderText: qsTr("Username")
                        placeholderTextColor: Theme.textTertiary
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.buttonHeightMedium
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                        KeyNavigation.up: serverField
                        KeyNavigation.down: passField
                        KeyNavigation.tab: passField
                        KeyNavigation.backtab: serverField
                        onAccepted: passField.forceActiveFocus()
                        leftPadding: Theme.spacingMedium
                        rightPadding: Theme.spacingMedium
                        background: FieldBackground { control: userField }
                    }

                    TextField {
                        id: passField
                        placeholderText: qsTr("Password")
                        placeholderTextColor: Theme.textTertiary
                        echoMode: TextInput.Password
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.buttonHeightMedium
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                        KeyNavigation.up: userField
                        KeyNavigation.down: connectButton
                        KeyNavigation.tab: connectButton
                        KeyNavigation.backtab: userField
                        onAccepted: root.submitCredentials()
                        leftPadding: Theme.spacingMedium
                        rightPadding: Theme.spacingMedium
                        background: FieldBackground { control: passField }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        ActionButton {
                            id: credentialCancelButton
                            variant: "secondary"
                            text: qsTr("Reset")
                            Layout.preferredWidth: Math.round(160 * Theme.layoutScale)
                            Layout.preferredHeight: Theme.buttonHeightLarge
                            focusPolicy: Qt.StrongFocus
                            KeyNavigation.up: passField
                            KeyNavigation.down: root.showSpaceAction ? spaceActionButton : serverField
                            KeyNavigation.left: connectButton
                            KeyNavigation.right: connectButton
                            KeyNavigation.tab: serverField
                            KeyNavigation.backtab: passField
                            onClicked: {
                                serverField.clear()
                                userField.clear()
                                passField.clear()
                                root.cancelAuthentication()
                            }
                        }

                        ActionButton {
                            id: connectButton
                            variant: "primary"
                            text: qsTr("Sign in")
                            enabled: serverField.text.trim().length > 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.buttonHeightLarge
                            focusPolicy: Qt.StrongFocus
                            KeyNavigation.up: passField
                            KeyNavigation.down: root.showSpaceAction ? spaceActionButton : serverField
                            KeyNavigation.left: credentialCancelButton
                            KeyNavigation.right: credentialCancelButton
                            KeyNavigation.tab: credentialCancelButton
                            KeyNavigation.backtab: passField
                            onClicked: root.submitCredentials()
                        }
                    }

                    ActionButton {
                        id: spaceActionButton
                        visible: root.showSpaceAction
                        variant: "ghost"
                        iconGlyph: root.addingConnection ? Icons.arrowBack : Icons.group
                        text: root.addingConnection ? qsTr("Cancel and go back") : qsTr("Open a saved space")
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.buttonHeightMedium
                        focusPolicy: Qt.StrongFocus
                        KeyNavigation.up: connectButton
                        KeyNavigation.down: serverField
                        KeyNavigation.tab: serverField
                        KeyNavigation.backtab: connectButton
                        Keys.onReturnPressed: clicked()
                        Keys.onEnterPressed: clicked()
                        onClicked: {
                            if (root.addingConnection) {
                                root.clearStatus()
                                Workspaces.cancelAddConnection()
                            } else {
                                loginSpacePicker.open()
                            }
                        }
                    }
                }

                ColumnLayout {
                    spacing: Theme.spacingSmall

                    ListView {
                        id: profileList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: Theme.spacingSmall
                        model: root.availableProfiles
                        currentIndex: root.availableProfiles.length > 0 ? 0 : -1
                        focus: true
                        activeFocusOnTab: true
                        KeyNavigation.up: profilesBackButton
                        KeyNavigation.down: profilesBackButton
                        KeyNavigation.tab: profilesBackButton
                        KeyNavigation.backtab: profilesBackButton

                        Keys.onUpPressed: function(event) {
                            if (count > 0) {
                                if (currentIndex > 0) {
                                    currentIndex = currentIndex - 1
                                } else {
                                    profilesBackButton.forceActiveFocus()
                                }
                            } else {
                                profilesBackButton.forceActiveFocus()
                            }
                            event.accepted = true
                        }
                        Keys.onDownPressed: function(event) {
                            if (count > 0) {
                                if (currentIndex < count - 1) {
                                    currentIndex = currentIndex + 1
                                } else {
                                    profilesBackButton.forceActiveFocus()
                                }
                            } else {
                                profilesBackButton.forceActiveFocus()
                            }
                            event.accepted = true
                        }
                        Keys.onReturnPressed: function(event) {
                            if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < count)
                                root.chooseProfile(root.availableProfiles[currentIndex])
                            event.accepted = true
                        }
                        Keys.onEnterPressed: function(event) {
                            if (!event.isAutoRepeat && currentIndex >= 0 && currentIndex < count)
                                root.chooseProfile(root.availableProfiles[currentIndex])
                            event.accepted = true
                        }

                        delegate: Rectangle {
                            required property int index
                            required property var modelData
                            width: profileList.width
                            height: Math.round(88 * Theme.layoutScale)
                            radius: Theme.radiusMedium
                            color: profileList.currentIndex === index ? Theme.surfaceInteractiveHover : Theme.surfaceInteractive

                            FocusRing {
                                inset: true
                                active: profileList.activeFocus && profileList.currentIndex === index
                                radius: parent.radius
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onEntered: {
                                    if (typeof InputModeManager === "undefined" || InputModeManager.pointerActive) {
                                        profileList.currentIndex = index
                                    }
                                }
                                onClicked: {
                                    profileList.currentIndex = index
                                    profileList.forceActiveFocus()
                                    root.chooseProfile(modelData)
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: Theme.spacingSmall
                                spacing: Theme.spacingMedium

                                Rectangle {
                                    Layout.preferredWidth: Math.round(64 * Theme.layoutScale)
                                    Layout.preferredHeight: Math.round(64 * Theme.layoutScale)
                                    radius: width / 2
                                    color: Theme.selectionBackground
                                    clip: true

                                    Text {
                                        anchors.centerIn: parent
                                        text: root.profileName(modelData).charAt(0).toUpperCase()
                                        color: Theme.textPrimary
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: Theme.fontSizeTitle
                                        font.weight: Theme.fontWeightStrong
                                        visible: root.profileAvatar(modelData).length === 0
                                    }

                                    Image {
                                        anchors.fill: parent
                                        source: root.profileAvatar(modelData)
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: true
                                        visible: source.toString().length > 0
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacingXSmall

                                    Text {
                                        text: root.profileName(modelData)
                                        color: Theme.textPrimary
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: Theme.fontSizeBody
                                        font.weight: Theme.fontWeightStrong
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: root.profileHasPin(modelData) ? qsTr("PIN required") : qsTr("No PIN required")
                                        color: Theme.textSecondary
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: Theme.fontSizeCaption
                                        Layout.fillWidth: true
                                    }
                                }

                                Text {
                                    text: root.profileHasPin(modelData) ? "lock" : "arrow_forward"
                                    color: Theme.textPrimary
                                    font.family: Theme.fontIcon
                                    font.pixelSize: Theme.fontSizeIcon
                                }
                            }
                        }

                        Text {
                            parent: profileList
                            anchors.centerIn: parent
                            visible: profileList.count === 0
                            text: qsTr("Loading profiles…")
                            color: Theme.textSecondary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeBody
                        }
                    }

                    ActionButton {
                        id: profilesBackButton
                        variant: "secondary"
                        text: qsTr("Back to sign in")
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.buttonHeightMedium
                        focusPolicy: Qt.StrongFocus
                        KeyNavigation.up: profileList
                        KeyNavigation.down: profileList
                        KeyNavigation.tab: profileList
                        KeyNavigation.backtab: profileList
                        Keys.onUpPressed: function(event) {
                            if (profileList.count > 0) {
                                profileList.currentIndex = profileList.count - 1
                                profileList.forceActiveFocus()
                            }
                            event.accepted = true
                        }
                        Keys.onDownPressed: function(event) {
                            if (profileList.count > 0) {
                                profileList.currentIndex = 0
                                profileList.forceActiveFocus()
                            }
                            event.accepted = true
                        }
                        onClicked: root.cancelAuthentication()
                    }
                }

                ColumnLayout {
                    spacing: Theme.spacingMedium

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: Math.round(104 * Theme.layoutScale)
                        Layout.preferredHeight: Math.round(104 * Theme.layoutScale)
                        radius: width / 2
                        color: Theme.selectionBackground
                        clip: true

                        Text {
                            anchors.centerIn: parent
                            text: root.selectedProfileName.length > 0
                                  ? root.selectedProfileName.charAt(0).toUpperCase() : "lock"
                            color: Theme.textPrimary
                            font.family: root.selectedProfileName.length > 0
                                         ? Theme.fontPrimary : Theme.fontIcon
                            font.pixelSize: Theme.fontSizeHeader
                            font.weight: Theme.fontWeightStrong
                            visible: root.selectedProfileAvatar.length === 0
                        }

                        Image {
                            anchors.fill: parent
                            source: root.selectedProfileAvatar
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                            visible: source.toString().length > 0
                        }
                    }

                    TextField {
                        id: pinField
                        placeholderText: qsTr("Profile PIN")
                        placeholderTextColor: Theme.textTertiary
                        echoMode: TextInput.Password
                        inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                        horizontalAlignment: TextInput.AlignHCenter
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.buttonHeightLarge
                        font.pixelSize: Theme.fontSizeTitle
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                        KeyNavigation.up: pinCancelButton
                        KeyNavigation.down: verifyPinButton
                        KeyNavigation.tab: verifyPinButton
                        KeyNavigation.backtab: pinCancelButton
                        onAccepted: root.submitPin()
                        leftPadding: Theme.spacingMedium
                        rightPadding: Theme.spacingMedium
                        background: FieldBackground { control: pinField }
                    }

                    ActionButton {
                        id: verifyPinButton
                        variant: "primary"
                        text: qsTr("Unlock profile")
                        enabled: pinField.text.length > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.buttonHeightLarge
                        focusPolicy: Qt.StrongFocus
                        KeyNavigation.up: pinField
                        KeyNavigation.down: pinCancelButton
                        KeyNavigation.tab: pinCancelButton
                        KeyNavigation.backtab: pinField
                        onClicked: root.submitPin()
                    }

                    ActionButton {
                        id: pinCancelButton
                        variant: "secondary"
                        text: qsTr("Cancel and return to sign in")
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.buttonHeightMedium
                        focusPolicy: Qt.StrongFocus
                        KeyNavigation.up: verifyPinButton
                        KeyNavigation.down: pinField
                        KeyNavigation.tab: pinField
                        KeyNavigation.backtab: verifyPinButton
                        onClicked: root.cancelAuthentication()
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: statusMessage.length > 0
                                        ? Math.max(Theme.buttonHeightSmall, statusLabel.implicitHeight + Theme.spacingSmall * 2)
                                        : 0
                visible: statusMessage.length > 0
                radius: Theme.radiusControl
                color: statusIsError ? Theme.actionDestructiveBackground : Theme.surfaceInteractive
                border.color: statusIsError ? Theme.errorColor : "transparent"
                border.width: statusIsError ? Theme.borderWidth : 0

                Text {
                    id: statusLabel
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    text: root.statusMessage
                    color: root.statusIsError ? Theme.errorColor : Theme.textPrimary
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    SpacePickerDialog {
        id: loginSpacePicker
        parent: Overlay.overlay
        allowSignOut: false
        allowManage: false
        allowAdd: false
        onClosed: root.restoreStepFocus()
    }

    Component.onCompleted: restoreStepFocus()

    onVisibleChanged: {
        if (visible)
            restoreStepFocus()
    }

    Connections {
        target: AuthenticationService
        ignoreUnknownSignals: true

        function onProfilesChanged() {
            Qt.callLater(function() {
                if (root.authenticationStep !== "profiles" && root.authenticationStep !== "pin")
                    return
                profileList.currentIndex = root.availableProfiles.length > 0 ? 0 : -1
                if (root.authenticationStep === "pin")
                    root.syncSelectedProfile()
                root.restoreStepFocus()
            })
        }

        function onAuthenticationStepChanged() {
            if (root.authenticationStep === "pin") {
                pinField.clear()
                root.syncSelectedProfile()
            }
            if (root.authenticationStep !== "authenticated")
                root.clearStatus()
            root.restoreStepFocus()
            root.maybeEmitLoginSuccess()
        }

        function onLoginSuccess(userId, accessToken, username) {
            root.serviceLoginReported = true
            Qt.callLater(root.maybeEmitLoginSuccess)
        }

        function onLoginError(error) {
            root.setStatus(error, true)
            root.restoreRetryFocus()
        }

        function onLoggedOut() {
            root.serviceLoginReported = false
            root.completionEmitted = false
            root.restoreStepFocus()
        }
    }
}
