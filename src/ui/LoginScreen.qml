import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root
    focus: true

    signal loginSuccess()

    Image {
        anchors.fill: parent
        source: "qrc:/images/app/login.jpg"
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0.01, 0.04, 0.10, 0.55)
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: Qt.rgba(0.02, 0.06, 0.12, 0.30) }
            GradientStop { position: 0.45; color: Qt.rgba(0.02, 0.05, 0.12, 0.62) }
            GradientStop { position: 1.0; color: Qt.rgba(0.01, 0.02, 0.07, 0.88) }
        }
    }

    Item {
        anchors.fill: parent

        Rectangle {
            id: loginCard
            anchors.centerIn: parent
            width: Math.min(Math.round(560 * Theme.layoutScale), parent.width * 0.92)
            radius: Theme.radiusXLarge
            color: Qt.rgba(0.07, 0.12, 0.20, 0.48)
            border.color: Qt.rgba(1, 1, 1, 0.28)
            border.width: 1
            implicitHeight: cardContent.implicitHeight + Math.round(54 * Theme.layoutScale) * 2

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.color: Qt.rgba(1, 1, 1, 0.12)
                border.width: 1
                opacity: 0.8
            }

            Rectangle {
                anchors {
                    left: parent.left
                    right: parent.right
                    top: parent.top
                }
                height: Math.round(parent.height * 0.52)
                radius: parent.radius
                gradient: Gradient {
                    orientation: Gradient.Vertical
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.16) }
                    GradientStop { position: 0.55; color: Qt.rgba(1, 1, 1, 0.07) }
                    GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
                }
            }

            ColumnLayout {
                id: cardContent
                anchors {
                    fill: parent
                    margins: Math.round(54 * Theme.layoutScale)
                }
                spacing: Theme.spacingMedium

                Image {
                    source: "qrc:/images/app/logo_trans.svg"
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredHeight: Math.round(86 * Theme.layoutScale)
                    fillMode: Image.PreserveAspectFit
                    sourceSize.height: Math.round(120 * Theme.layoutScale)
                    mipmap: true
                }

                Text {
                    text: qsTr("Bloom")
                    font.pixelSize: Theme.fontSizeHeader
                    font.family: Theme.fontPrimary
                    font.bold: true
                    color: Theme.textPrimary
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: qsTr("Sign in to Jellyfin to watch your media.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    Layout.alignment: Qt.AlignHCenter
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                Item {
                    Layout.preferredHeight: Theme.spacingSmall
                }

                TextField {
                    id: serverField
                    focus: true
                    placeholderText: qsTr("http://localhost:8096")
                    placeholderTextColor: Theme.textSecondary
                    Layout.fillWidth: true
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary

                    background: Rectangle {
                        color: Theme.inputBackground
                        radius: Theme.radiusSmall
                        border.color: serverField.activeFocus ? Theme.focusBorder : Theme.inputBorder
                        border.width: Theme.borderWidth
                    }

                    KeyNavigation.up: connectButton
                    KeyNavigation.down: userField
                    onAccepted: userField.forceActiveFocus()
                    Keys.onReturnPressed: (event) => { userField.forceActiveFocus(); event.accepted = true }
                    Keys.onEnterPressed: (event) => { userField.forceActiveFocus(); event.accepted = true }
                }

                TextField {
                    id: userField
                    placeholderText: qsTr("Username")
                    placeholderTextColor: Theme.textSecondary
                    Layout.fillWidth: true
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary

                    background: Rectangle {
                        color: Theme.inputBackground
                        radius: Theme.radiusSmall
                        border.color: userField.activeFocus ? Theme.focusBorder : Theme.inputBorder
                        border.width: Theme.borderWidth
                    }

                    KeyNavigation.up: serverField
                    KeyNavigation.down: passField
                    onAccepted: passField.forceActiveFocus()
                    Keys.onReturnPressed: (event) => { passField.forceActiveFocus(); event.accepted = true }
                    Keys.onEnterPressed: (event) => { passField.forceActiveFocus(); event.accepted = true }
                }

                TextField {
                    id: passField
                    placeholderText: qsTr("Password")
                    placeholderTextColor: Theme.textSecondary
                    echoMode: TextInput.Password
                    Layout.fillWidth: true
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary

                    background: Rectangle {
                        color: Theme.inputBackground
                        radius: Theme.radiusSmall
                        border.color: passField.activeFocus ? Theme.focusBorder : Theme.inputBorder
                        border.width: Theme.borderWidth
                    }

                    KeyNavigation.up: userField
                    KeyNavigation.down: connectButton
                    onAccepted: connectButton.clicked()
                    Keys.onReturnPressed: (event) => {
                        if (event.isAutoRepeat) {
                            event.accepted = true
                            return
                        }
                        connectButton.clicked()
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (event.isAutoRepeat) {
                            event.accepted = true
                            return
                        }
                        connectButton.clicked()
                        event.accepted = true
                    }
                }

                Button {
                    id: connectButton
                    text: qsTr("Connect")
                    enabled: serverField.text.length > 0
                    font.pixelSize: Theme.fontSizeTitle
                    font.family: Theme.fontPrimary
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.buttonHeightLarge

                    background: Rectangle {
                        color: parent.activeFocus ? Theme.accentPrimary : Theme.backgroundSecondary
                        radius: Theme.radiusSmall
                        border.color: parent.activeFocus ? Theme.textPrimary : "transparent"
                        border.width: Theme.buttonFocusBorderWidth
                    }

                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: Theme.textPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        console.log("=== LoginScreen: Connect button clicked ===")
                        console.log("LoginScreen: server=", serverField.text, "user=", userField.text)
                        statusText.text = qsTr("Connecting...")
                        statusText.color = Theme.textPrimary
                        AuthenticationService.authenticate(serverField.text, userField.text, passField.text)
                        console.log("LoginScreen: authenticate() called")
                    }

                    KeyNavigation.up: passField
                    KeyNavigation.down: serverField
                    Keys.onReturnPressed: (event) => {
                        if (event.isAutoRepeat) {
                            event.accepted = true
                            return
                        }
                        clicked()
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        if (event.isAutoRepeat) {
                            event.accepted = true
                            return
                        }
                        clicked()
                        event.accepted = true
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(Math.round(40 * Theme.layoutScale), statusText.implicitHeight + Math.round(12 * Theme.layoutScale))
                    radius: Theme.radiusSmall
                    color: statusText.text.length > 0 ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                    border.color: statusText.text.length > 0 ? Qt.rgba(1, 1, 1, 0.16) : "transparent"
                    border.width: 1
                    visible: statusText.text.length > 0

                    Text {
                        id: statusText
                        anchors.centerIn: parent
                        text: ""
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                    }
                }
            }
        }
    }

    Component.onCompleted: Qt.callLater(function() {
        serverField.forceActiveFocus()
    })

    Connections {
        target: AuthenticationService
        function onLoginSuccess(userId, accessToken, username) {
            console.log("=== LoginScreen: onLoginSuccess received ===")
            console.log("LoginScreen: userId=", userId, "username=", username)
            statusText.text = qsTr("Connected!")
            statusText.color = "#8AEFC1"
            console.log("LoginScreen: About to emit root.loginSuccess()")
            root.loginSuccess()
            console.log("LoginScreen: root.loginSuccess() emitted")
        }
        function onLoginError(error) {
            console.log("=== LoginScreen: onLoginError received ===", error)
            statusText.text = qsTr("Error: ") + error
            statusText.color = "#FF8A95"
        }
    }
}
