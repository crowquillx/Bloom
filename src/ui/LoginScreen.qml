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

    // ========================================
    // Stage
    // ========================================

    // The login art under the standard stage scrims; no navigation bar here,
    // so the brand sits where the bar puts it once signed in.
    StageBackdrop {
        id: stage
        anchors.fill: parent
        mode: "ambient"
        source: "qrc:/images/app/login.jpg"
    }

    Row {
        id: brand
        x: Theme.pageMarginX
        y: Theme.navBarInsetTop + Math.round((Theme.navBarHeight - height) / 2)
        spacing: Math.round(14 * Theme.layoutScale)
        Accessible.role: Accessible.StaticText
        Accessible.name: "Bloom"

        Image {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.round(44 * Theme.layoutScale)
            height: width
            source: "qrc:/images/app/logo_trans.svg"
            sourceSize.width: width * 2
            sourceSize.height: height * 2
            fillMode: Image.PreserveAspectFit
            smooth: true
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "bloom"
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontSizeTitle
            font.weight: Theme.fontWeightHeavy
            font.letterSpacing: Theme.displayLetterSpacing
            color: Theme.textPrimary
        }
    }

    // ========================================
    // Form
    // ========================================

    // Left-aligned at the gutter like every page's copy, centred vertically
    // in the space below the brand.
    ColumnLayout {
        id: formColumn
        x: Theme.pageMarginX
        // Centre on the form without the status line, so a status appearing
        // below the buttons doesn't shift the fields.
        y: Math.max(Theme.pageMarginTop,
                    Math.round((root.height - implicitHeight
                                + (statusBox.visible ? statusBox.height + statusBox.Layout.topMargin : 0)) / 2))
        width: Math.min(root.width - Theme.pageMarginX * 2, Math.round(880 * Theme.layoutScale))
        spacing: 0

        Text {
            Layout.fillWidth: true
            text: authenticationStep === "profiles" ? qsTr("Silo profiles")
                  : authenticationStep === "pin" ? qsTr("Profile locked")
                  : root.addingConnection ? qsTr("Add a server")
                  : qsTr("Welcome to Bloom")
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeCaption
            font.weight: Theme.fontWeightHeavy
            font.capitalization: Font.AllUppercase
            font.letterSpacing: Theme.eyebrowLetterSpacing
            color: Theme.accentPrimary
            Accessible.ignored: true
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingSmall
            text: authenticationStep === "profiles" ? qsTr("Who’s watching?")
                  : authenticationStep === "pin" ? qsTr("Enter profile PIN")
                  : qsTr("Connect your media server")
            color: Theme.textPrimary
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontSizeDisplay
            font.weight: Theme.fontWeightHeavy
            font.letterSpacing: Theme.displayLetterSpacing
            wrapMode: Text.Wrap
            maximumLineCount: 2
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingMedium
            text: authenticationStep === "profiles"
                  ? qsTr("Choose a Silo profile to continue.")
                  : authenticationStep === "pin"
                    ? (selectedProfileName.length > 0
                       ? qsTr("Unlock %1 to continue.").arg(selectedProfileName)
                       : qsTr("Unlock this profile to continue."))
                    : qsTr("Bloom will detect whether this server uses Jellyfin or Silo. Native Silo support is experimental.")
            color: Theme.textSecondary
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            lineHeight: 1.15
            wrapMode: Text.Wrap
        }

        // ---------- Credentials ----------
        ColumnLayout {
            id: credentialsStep
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingXLarge
            spacing: Theme.spacingSmall
            visible: authenticationStep !== "profiles" && authenticationStep !== "pin"

            Text {
                text: qsTr("Server")
                color: Theme.textSecondary
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Theme.fontWeightStrong
                Accessible.ignored: true
            }

            TextField {
                id: serverField
                placeholderText: qsTr("media.example.com")
                placeholderTextColor: Theme.textTertiary
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.buttonHeightLarge
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                verticalAlignment: TextInput.AlignVCenter
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                Accessible.name: qsTr("Server address")
                KeyNavigation.up: root.showSpaceAction ? spaceActionButton : connectButton
                KeyNavigation.down: userField
                KeyNavigation.tab: userField
                KeyNavigation.backtab: root.showSpaceAction ? spaceActionButton : credentialCancelButton
                onAccepted: userField.forceActiveFocus()
                leftPadding: Theme.spacingLarge
                rightPadding: Theme.spacingLarge
                background: FieldBackground { control: serverField }
            }

            Text {
                text: qsTr("Enter your server address. Bloom tries HTTPS, then HTTP; a custom port is optional.")
                color: Theme.textTertiary
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeSmall
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            Text {
                Layout.topMargin: Theme.spacingMedium
                text: qsTr("Username")
                color: Theme.textSecondary
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Theme.fontWeightStrong
                Accessible.ignored: true
            }

            TextField {
                id: userField
                placeholderText: qsTr("Username")
                placeholderTextColor: Theme.textTertiary
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.buttonHeightLarge
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                verticalAlignment: TextInput.AlignVCenter
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                Accessible.name: qsTr("Username")
                KeyNavigation.up: serverField
                KeyNavigation.down: passField
                KeyNavigation.tab: passField
                KeyNavigation.backtab: serverField
                onAccepted: passField.forceActiveFocus()
                leftPadding: Theme.spacingLarge
                rightPadding: Theme.spacingLarge
                background: FieldBackground { control: userField }
            }

            Text {
                Layout.topMargin: Theme.spacingMedium
                text: qsTr("Password")
                color: Theme.textSecondary
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Theme.fontWeightStrong
                Accessible.ignored: true
            }

            TextField {
                id: passField
                placeholderText: qsTr("Password")
                placeholderTextColor: Theme.textTertiary
                echoMode: TextInput.Password
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.buttonHeightLarge
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                verticalAlignment: TextInput.AlignVCenter
                Accessible.name: qsTr("Password")
                KeyNavigation.up: userField
                KeyNavigation.down: connectButton
                KeyNavigation.tab: connectButton
                KeyNavigation.backtab: userField
                onAccepted: root.submitCredentials()
                leftPadding: Theme.spacingLarge
                rightPadding: Theme.spacingLarge
                background: FieldBackground { control: passField }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingLarge
                spacing: Theme.controlSpacing

                ActionButton {
                    id: connectButton
                    variant: "primary"
                    text: qsTr("Sign in")
                    iconGlyph: Icons.login
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
                    Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                    Keys.onEnterPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                    onClicked: root.submitCredentials()
                }

                ActionButton {
                    id: credentialCancelButton
                    variant: "secondary"
                    text: qsTr("Reset")
                    Layout.preferredWidth: Math.round(220 * Theme.layoutScale)
                    Layout.preferredHeight: Theme.buttonHeightLarge
                    focusPolicy: Qt.StrongFocus
                    KeyNavigation.up: passField
                    KeyNavigation.down: root.showSpaceAction ? spaceActionButton : serverField
                    KeyNavigation.left: connectButton
                    KeyNavigation.right: connectButton
                    KeyNavigation.tab: root.showSpaceAction ? spaceActionButton : serverField
                    KeyNavigation.backtab: connectButton
                    Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                    Keys.onEnterPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                    onClicked: {
                        serverField.clear()
                        userField.clear()
                        passField.clear()
                        root.cancelAuthentication()
                    }
                }
            }

            ActionButton {
                id: spaceActionButton
                visible: root.showSpaceAction
                variant: "ghost"
                iconGlyph: root.addingConnection ? Icons.arrowBack : Icons.group
                text: root.addingConnection ? qsTr("Cancel and go back") : qsTr("Open a saved space")
                Layout.topMargin: Theme.spacingSmall
                Layout.preferredHeight: Theme.buttonHeightMedium
                focusPolicy: Qt.StrongFocus
                KeyNavigation.up: connectButton
                KeyNavigation.down: serverField
                KeyNavigation.tab: serverField
                KeyNavigation.backtab: credentialCancelButton
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

        // ---------- Profile selection (Silo) ----------
        ColumnLayout {
            id: profilesStep
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingXLarge
            spacing: Theme.spacingLarge
            visible: authenticationStep === "profiles"

            ListView {
                id: profileList
                readonly property int rowHeight: Math.round(104 * Theme.layoutScale)
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(1, Math.min(count, 5)) * (rowHeight + spacing) - spacing
                clip: true
                spacing: Theme.spacingSmall
                model: root.availableProfiles
                currentIndex: root.availableProfiles.length > 0 ? 0 : -1
                activeFocusOnTab: true
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Profiles")
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
                    id: profileRow
                    required property int index
                    required property var modelData
                    readonly property bool highlighted: profileList.activeFocus && profileList.currentIndex === index
                    width: profileList.width
                    height: profileList.rowHeight
                    radius: Theme.radiusMedium
                    color: highlighted || profileRowMouse.containsMouse ? Theme.surfaceInteractiveHover
                                                                       : Theme.surfaceInteractive
                    border.width: Theme.borderWidth
                    border.color: Theme.surfaceBorder
                    Accessible.role: Accessible.ListItem
                    Accessible.name: root.profileName(modelData)
                    Accessible.description: root.profileHasPin(modelData) ? qsTr("PIN required") : qsTr("No PIN required")

                    FocusRing {
                        inset: true
                        active: profileRow.highlighted
                        radius: parent.radius
                    }

                    MouseArea {
                        id: profileRowMouse
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
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingLarge
                        spacing: Theme.spacingMedium

                        Rectangle {
                            Layout.preferredWidth: Math.round(72 * Theme.layoutScale)
                            Layout.preferredHeight: Layout.preferredWidth
                            radius: width / 2
                            color: Theme.selectionBackground
                            clip: true

                            Text {
                                anchors.centerIn: parent
                                text: root.profileName(modelData).charAt(0).toUpperCase()
                                color: Theme.textPrimary
                                font.family: Theme.fontDisplay
                                font.pixelSize: Theme.fontSizeTitle
                                font.weight: Theme.fontWeightHeavy
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
                            spacing: Math.round(4 * Theme.layoutScale)

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
                                font.pixelSize: Theme.fontSizeSmall
                                Layout.fillWidth: true
                            }
                        }

                        Text {
                            text: root.profileHasPin(modelData) ? "lock" : Icons.arrowForward
                            color: profileRow.highlighted ? Theme.textPrimary : Theme.textSecondary
                            font.family: Theme.fontIcon
                            font.pixelSize: Theme.iconSizeNav
                        }
                    }
                }

                StateMessage {
                    parent: profileList
                    anchors.fill: parent
                    visible: profileList.count === 0
                    kind: "loading"
                    compact: true
                    message: qsTr("Loading profiles…")
                }
            }

            ActionButton {
                id: profilesBackButton
                variant: "secondary"
                iconGlyph: Icons.arrowBack
                text: qsTr("Back to sign in")
                Layout.preferredHeight: Theme.buttonHeightLarge
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
                Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                Keys.onEnterPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                onClicked: root.cancelAuthentication()
            }
        }

        // ---------- Profile PIN ----------
        ColumnLayout {
            id: pinStep
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingXLarge
            spacing: Theme.controlSpacing
            visible: authenticationStep === "pin"

            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: Theme.spacingSmall
                spacing: Theme.spacingMedium

                Rectangle {
                    Layout.preferredWidth: Math.round(104 * Theme.layoutScale)
                    Layout.preferredHeight: Layout.preferredWidth
                    radius: width / 2
                    color: Theme.selectionBackground
                    clip: true

                    Text {
                        anchors.centerIn: parent
                        text: root.selectedProfileName.length > 0
                              ? root.selectedProfileName.charAt(0).toUpperCase() : "lock"
                        color: Theme.textPrimary
                        font.family: root.selectedProfileName.length > 0
                                     ? Theme.fontDisplay : Theme.fontIcon
                        font.pixelSize: Theme.fontSizeHeader
                        font.weight: Theme.fontWeightHeavy
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

                Text {
                    Layout.fillWidth: true
                    text: root.selectedProfileName
                    visible: text.length > 0
                    color: Theme.textPrimary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeTitle
                    font.weight: Theme.fontWeightStrong
                    elide: Text.ElideRight
                }
            }

            TextField {
                id: pinField
                placeholderText: qsTr("Profile PIN")
                placeholderTextColor: Theme.textTertiary
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.buttonHeightHero
                font.pixelSize: Theme.fontSizeTitle
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                verticalAlignment: TextInput.AlignVCenter
                Accessible.name: qsTr("Profile PIN")
                KeyNavigation.up: pinCancelButton
                KeyNavigation.down: verifyPinButton
                KeyNavigation.tab: verifyPinButton
                KeyNavigation.backtab: pinCancelButton
                onAccepted: root.submitPin()
                leftPadding: Theme.spacingLarge
                rightPadding: Theme.spacingLarge
                background: FieldBackground { control: pinField }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingMedium
                spacing: Theme.controlSpacing

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
                    KeyNavigation.right: pinCancelButton
                    KeyNavigation.tab: pinCancelButton
                    KeyNavigation.backtab: pinField
                    Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                    Keys.onEnterPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                    onClicked: root.submitPin()
                }

                ActionButton {
                    id: pinCancelButton
                    variant: "secondary"
                    text: qsTr("Cancel and return to sign in")
                    Layout.preferredHeight: Theme.buttonHeightLarge
                    focusPolicy: Qt.StrongFocus
                    KeyNavigation.up: pinField
                    KeyNavigation.down: pinField
                    KeyNavigation.left: verifyPinButton
                    KeyNavigation.tab: pinField
                    KeyNavigation.backtab: verifyPinButton
                    Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                    Keys.onEnterPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                    onClicked: root.cancelAuthentication()
                }
            }
        }

        // ---------- Status ----------
        Rectangle {
            id: statusBox
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingLarge
            Layout.preferredHeight: Math.max(Theme.buttonHeightMedium,
                                             statusRow.implicitHeight + Theme.spacingMedium * 2)
            visible: statusMessage.length > 0
            radius: Theme.radiusControl
            color: statusIsError ? Theme.actionDestructiveBackground : Theme.surfaceInteractive
            border.color: statusIsError ? Theme.errorColor : Theme.surfaceBorder
            border.width: Theme.borderWidth
            Accessible.role: Accessible.AlertMessage
            Accessible.name: root.statusMessage

            RowLayout {
                id: statusRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingMedium
                spacing: Theme.spacingMedium

                Text {
                    visible: root.statusIsError
                    text: Icons.error
                    font.family: Theme.fontIcon
                    font.pixelSize: Theme.fontSizeIcon
                    color: Theme.errorColor
                    Layout.alignment: Qt.AlignVCenter
                }

                BusyIndicator {
                    visible: !root.statusIsError
                    running: visible && root.statusMessage.length > 0
                    Layout.preferredWidth: Math.round(36 * Theme.layoutScale)
                    Layout.preferredHeight: Layout.preferredWidth
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    id: statusLabel
                    Layout.fillWidth: true
                    text: root.statusMessage
                    color: root.statusIsError ? Theme.errorColor : Theme.textPrimary
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightMedium
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
