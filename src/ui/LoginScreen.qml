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
    readonly property string providerSelection: AuthenticationService.providerSelection
    readonly property var availableProfiles: AuthenticationService.profiles || []
    property string selectedProfileName: ""
    property string selectedProfileAvatar: ""
    property string selectedProfileId: ""
    property bool serviceLoginReported: false
    property bool completionEmitted: false
    property string statusMessage: ""
    property bool statusIsError: false

    function providerTitle() {
        if (providerSelection === "jellyfin")
            return qsTr("Sign in to Jellyfin")
        if (providerSelection === "silo")
            return qsTr("Sign in to Silo")
        return qsTr("Connect your media server")
    }

    function providerDescription() {
        if (providerSelection === "jellyfin")
            return qsTr("Connect directly to your Jellyfin server.")
        if (providerSelection === "silo")
            return qsTr("Sign in to your Silo account, then choose a household profile.")
        return qsTr("Bloom will detect whether this server uses Jellyfin or Silo.")
    }

    function selectedProviderButton() {
        if (providerSelection === "jellyfin")
            return jellyfinButton
        if (providerSelection === "silo")
            return siloButton
        return autoButton
    }

    function setStatus(message, isError) {
        statusMessage = message
        statusIsError = isError
    }

    function clearStatus() {
        setStatus("", false)
    }

    function selectProvider(provider) {
        clearStatus()
        AuthenticationService.setProviderSelection(provider)
        Qt.callLater(function() {
            selectedProviderButton().forceActiveFocus()
        })
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
        AuthenticationService.logout()
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
                pinField.forceActiveFocus()
            } else {
                selectedProviderButton().forceActiveFocus()
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
        if (authenticationStep !== "authenticated") {
            cancelAuthentication()
            event.accepted = true
        }
    }

    Keys.onBackPressed: function(event) {
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
        color: Theme.overlayDark
    }

    Rectangle {
        id: loginCard
        anchors.centerIn: parent
        width: Math.min(Math.round(720 * Theme.layoutScale), parent.width - Theme.spacingLarge * 2)
        height: Math.min(Math.round(660 * Theme.layoutScale), parent.height - Theme.spacingLarge * 2)
        radius: Theme.radiusXLarge
        color: Theme.backgroundGlass
        border.color: Theme.borderLight
        border.width: Theme.borderWidth

        ColumnLayout {
            id: cardContent
            anchors.fill: parent
            anchors.margins: Theme.spacingLarge
            spacing: Theme.spacingSmall

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
                      : providerTitle()
                color: Theme.textPrimary
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeHeader
                font.bold: true
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
                        : providerDescription()
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

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Button {
                            id: autoButton
                            text: qsTr("Auto")
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.buttonHeightMedium
                            focusPolicy: Qt.StrongFocus
                            KeyNavigation.left: siloButton
                            KeyNavigation.right: jellyfinButton
                            KeyNavigation.up: credentialCancelButton
                            KeyNavigation.down: serverField
                            KeyNavigation.tab: jellyfinButton
                            KeyNavigation.backtab: credentialCancelButton
                            onClicked: root.selectProvider("auto")
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: root.providerSelection === "auto" ? Theme.accentPrimary : Theme.backgroundSecondary
                                border.color: autoButton.activeFocus ? Theme.focusBorder : Theme.inputBorder
                                border.width: autoButton.activeFocus ? Theme.buttonFocusBorderWidth : Theme.borderWidth
                            }
                            contentItem: Text {
                                text: autoButton.text
                                color: root.providerSelection === "auto" ? Theme.textOnAccent : Theme.textPrimary
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.fontSizeSmall
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Button {
                            id: jellyfinButton
                            text: qsTr("Jellyfin")
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.buttonHeightMedium
                            focusPolicy: Qt.StrongFocus
                            KeyNavigation.left: autoButton
                            KeyNavigation.right: siloButton
                            KeyNavigation.up: credentialCancelButton
                            KeyNavigation.down: serverField
                            KeyNavigation.tab: siloButton
                            KeyNavigation.backtab: autoButton
                            onClicked: root.selectProvider("jellyfin")
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: root.providerSelection === "jellyfin" ? Theme.accentPrimary : Theme.backgroundSecondary
                                border.color: jellyfinButton.activeFocus ? Theme.focusBorder : Theme.inputBorder
                                border.width: jellyfinButton.activeFocus ? Theme.buttonFocusBorderWidth : Theme.borderWidth
                            }
                            contentItem: Text {
                                text: jellyfinButton.text
                                color: root.providerSelection === "jellyfin" ? Theme.textOnAccent : Theme.textPrimary
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.fontSizeSmall
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Button {
                            id: siloButton
                            text: qsTr("Silo")
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.buttonHeightMedium
                            focusPolicy: Qt.StrongFocus
                            KeyNavigation.left: jellyfinButton
                            KeyNavigation.right: autoButton
                            KeyNavigation.up: credentialCancelButton
                            KeyNavigation.down: serverField
                            KeyNavigation.tab: serverField
                            KeyNavigation.backtab: jellyfinButton
                            onClicked: root.selectProvider("silo")
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: root.providerSelection === "silo" ? Theme.accentPrimary : Theme.backgroundSecondary
                                border.color: siloButton.activeFocus ? Theme.focusBorder : Theme.inputBorder
                                border.width: siloButton.activeFocus ? Theme.buttonFocusBorderWidth : Theme.borderWidth
                            }
                            contentItem: Text {
                                text: siloButton.text
                                color: root.providerSelection === "silo" ? Theme.textOnAccent : Theme.textPrimary
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.fontSizeSmall
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Text {
                        text: qsTr("Server type")
                        color: Theme.textSecondary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    TextField {
                        id: serverField
                        placeholderText: qsTr("https://media.example.com")
                        placeholderTextColor: Theme.textSecondary
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.buttonHeightMedium
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: Theme.textPrimary
                        KeyNavigation.up: root.selectedProviderButton()
                        KeyNavigation.down: userField
                        KeyNavigation.tab: userField
                        KeyNavigation.backtab: root.selectedProviderButton()
                        onAccepted: userField.forceActiveFocus()
                        background: Rectangle {
                            color: Theme.inputBackground
                            radius: Theme.radiusSmall
                            border.color: serverField.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: serverField.activeFocus ? Theme.buttonFocusBorderWidth : Theme.borderWidth
                        }
                    }

                    Text {
                        text: qsTr("Enter the full server URL. A custom port is optional.")
                        color: Theme.textSecondary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }

                    TextField {
                        id: userField
                        placeholderText: root.providerSelection === "silo" ? qsTr("Silo account username") : qsTr("Username")
                        placeholderTextColor: Theme.textSecondary
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
                        background: Rectangle {
                            color: Theme.inputBackground
                            radius: Theme.radiusSmall
                            border.color: userField.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: userField.activeFocus ? Theme.buttonFocusBorderWidth : Theme.borderWidth
                        }
                    }

                    TextField {
                        id: passField
                        placeholderText: qsTr("Password")
                        placeholderTextColor: Theme.textSecondary
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
                        background: Rectangle {
                            color: Theme.inputBackground
                            radius: Theme.radiusSmall
                            border.color: passField.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: passField.activeFocus ? Theme.buttonFocusBorderWidth : Theme.borderWidth
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Button {
                            id: credentialCancelButton
                            text: qsTr("Reset")
                            Layout.preferredWidth: Math.round(160 * Theme.layoutScale)
                            Layout.preferredHeight: Theme.buttonHeightLarge
                            focusPolicy: Qt.StrongFocus
                            KeyNavigation.up: passField
                            KeyNavigation.down: root.selectedProviderButton()
                            KeyNavigation.left: connectButton
                            KeyNavigation.right: connectButton
                            KeyNavigation.tab: root.selectedProviderButton()
                            KeyNavigation.backtab: passField
                            onClicked: {
                                serverField.clear()
                                userField.clear()
                                passField.clear()
                                root.cancelAuthentication()
                            }
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: Theme.backgroundSecondary
                                border.color: credentialCancelButton.activeFocus ? Theme.focusBorder : Theme.inputBorder
                                border.width: credentialCancelButton.activeFocus ? Theme.buttonFocusBorderWidth : Theme.borderWidth
                            }
                            contentItem: Text {
                                text: credentialCancelButton.text
                                color: Theme.textPrimary
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.fontSizeSmall
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Button {
                            id: connectButton
                            text: qsTr("Sign in")
                            enabled: serverField.text.trim().length > 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.buttonHeightLarge
                            focusPolicy: Qt.StrongFocus
                            KeyNavigation.up: passField
                            KeyNavigation.down: root.selectedProviderButton()
                            KeyNavigation.left: credentialCancelButton
                            KeyNavigation.right: credentialCancelButton
                            KeyNavigation.tab: credentialCancelButton
                            KeyNavigation.backtab: passField
                            onClicked: root.submitCredentials()
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: connectButton.enabled ? Theme.accentPrimary : Theme.backgroundSecondary
                                border.color: connectButton.activeFocus ? Theme.focusBorder : Theme.inputBorder
                                border.width: connectButton.activeFocus ? Theme.buttonFocusBorderWidth : Theme.borderWidth
                            }
                            contentItem: Text {
                                text: connectButton.text
                                color: connectButton.enabled ? Theme.textOnAccent : Theme.textDisabled
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.fontSizeTitle
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
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
                            color: profileList.currentIndex === index ? Theme.accentPrimary : Theme.backgroundSecondary
                            border.color: profileList.activeFocus && profileList.currentIndex === index
                                          ? Theme.focusBorder : Theme.inputBorder
                            border.width: profileList.activeFocus && profileList.currentIndex === index
                                          ? Theme.buttonFocusBorderWidth : Theme.borderWidth

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onEntered: profileList.currentIndex = index
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
                                    color: Theme.inputBackground
                                    clip: true

                                    Text {
                                        anchors.centerIn: parent
                                        text: root.profileName(modelData).charAt(0).toUpperCase()
                                        color: profileList.currentIndex === index ? Theme.textOnAccent : Theme.textPrimary
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: Theme.fontSizeTitle
                                        font.bold: true
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
                                        color: profileList.currentIndex === index ? Theme.textOnAccent : Theme.textPrimary
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: Theme.fontSizeBody
                                        font.bold: true
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: root.profileHasPin(modelData) ? qsTr("PIN required") : qsTr("No PIN required")
                                        color: profileList.currentIndex === index ? Theme.textOnAccent : Theme.textSecondary
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: Theme.fontSizeCaption
                                        Layout.fillWidth: true
                                    }
                                }

                                Text {
                                    text: root.profileHasPin(modelData) ? "lock" : "arrow_forward"
                                    color: profileList.currentIndex === index ? Theme.textOnAccent : Theme.textPrimary
                                    font.family: "Material Symbols Outlined"
                                    font.pixelSize: Theme.fontSizeIcon
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: profileList.count === 0
                            text: qsTr("Loading profiles…")
                            color: Theme.textSecondary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeBody
                        }
                    }

                    Button {
                        id: profilesBackButton
                        text: qsTr("Back to sign in")
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.buttonHeightMedium
                        focusPolicy: Qt.StrongFocus
                        KeyNavigation.up: profileList
                        KeyNavigation.down: profileList
                        KeyNavigation.tab: profileList
                        KeyNavigation.backtab: profileList
                        onClicked: root.cancelAuthentication()
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: Theme.backgroundSecondary
                            border.color: profilesBackButton.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: profilesBackButton.activeFocus ? Theme.buttonFocusBorderWidth : Theme.borderWidth
                        }
                        contentItem: Text {
                            text: profilesBackButton.text
                            color: Theme.textPrimary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeSmall
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
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
                        color: Theme.inputBackground
                        clip: true

                        Text {
                            anchors.centerIn: parent
                            text: root.selectedProfileName.length > 0
                                  ? root.selectedProfileName.charAt(0).toUpperCase() : "lock"
                            color: Theme.textPrimary
                            font.family: root.selectedProfileName.length > 0
                                         ? Theme.fontPrimary : "Material Symbols Outlined"
                            font.pixelSize: Theme.fontSizeHeader
                            font.bold: true
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
                        placeholderTextColor: Theme.textSecondary
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
                        background: Rectangle {
                            color: Theme.inputBackground
                            radius: Theme.radiusSmall
                            border.color: pinField.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: pinField.activeFocus ? Theme.buttonFocusBorderWidth : Theme.borderWidth
                        }
                    }

                    Button {
                        id: verifyPinButton
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
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: verifyPinButton.enabled ? Theme.accentPrimary : Theme.backgroundSecondary
                            border.color: verifyPinButton.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: verifyPinButton.activeFocus ? Theme.buttonFocusBorderWidth : Theme.borderWidth
                        }
                        contentItem: Text {
                            text: verifyPinButton.text
                            color: verifyPinButton.enabled ? Theme.textOnAccent : Theme.textDisabled
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeTitle
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Button {
                        id: pinCancelButton
                        text: qsTr("Cancel and return to sign in")
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.buttonHeightMedium
                        focusPolicy: Qt.StrongFocus
                        KeyNavigation.up: verifyPinButton
                        KeyNavigation.down: pinField
                        KeyNavigation.tab: pinField
                        KeyNavigation.backtab: verifyPinButton
                        onClicked: root.cancelAuthentication()
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: Theme.backgroundSecondary
                            border.color: pinCancelButton.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: pinCancelButton.activeFocus ? Theme.buttonFocusBorderWidth : Theme.borderWidth
                        }
                        contentItem: Text {
                            text: pinCancelButton.text
                            color: Theme.textPrimary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeSmall
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
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
                radius: Theme.radiusSmall
                color: Theme.inputBackground
                border.color: statusIsError ? Theme.errorColor : Theme.borderLight
                border.width: Theme.borderWidth

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

    Component.onCompleted: restoreStepFocus()

    onVisibleChanged: {
        if (visible)
            restoreStepFocus()
    }

    Connections {
        target: AuthenticationService
        ignoreUnknownSignals: true

        function onProviderSelectionChanged() {
            Qt.callLater(root.restoreStepFocus)
        }

        function onProfilesChanged() {
            Qt.callLater(function() {
                if (root.authenticationStep !== "profiles")
                    return
                profileList.currentIndex = root.availableProfiles.length > 0 ? 0 : -1
                root.restoreStepFocus()
            })
        }

        function onAuthenticationStepChanged() {
            if (root.authenticationStep === "pin")
                pinField.clear()
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
