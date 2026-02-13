import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root
    focus: true
    
    signal loginSuccess()

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.spacingMedium
        width: Math.min(Math.round(400 * Theme.layoutScale), parent.width * 0.9)

        Text {
            text: "Connect to Jellyfin"
            font.pixelSize: Theme.fontSizeDisplay
            font.family: Theme.fontPrimary
            color: Theme.textPrimary
            Layout.alignment: Qt.AlignHCenter
        }

        TextField {
            id: serverField
            focus: true  // Default focus
            placeholderText: "http://localhost:8096"
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
            
            KeyNavigation.up: connectButton // Wrap around
            KeyNavigation.down: userField
            onAccepted: userField.forceActiveFocus()
            Keys.onReturnPressed: (event) => { userField.forceActiveFocus(); event.accepted = true }
            Keys.onEnterPressed: (event) => { userField.forceActiveFocus(); event.accepted = true }
        }

        TextField {
            id: userField
            placeholderText: "Username"
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
            placeholderText: "Password"
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
            text: "Connect"
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
                statusText.text = "Connecting..."
                AuthenticationService.authenticate(serverField.text, userField.text, passField.text)
                console.log("LoginScreen: authenticate() called")
            }
            
            KeyNavigation.up: passField
            KeyNavigation.down: serverField // Wrap around
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

        Text {
            id: statusText
            text: ""
            color: Theme.accentSecondary // Using accent for status for now
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.fontPrimary
            Layout.alignment: Qt.AlignHCenter
        }
    }

    Connections {
        target: AuthenticationService
        function onLoginSuccess(userId, accessToken, username) {
            console.log("=== LoginScreen: onLoginSuccess received ===")
            console.log("LoginScreen: userId=", userId, "username=", username)
            statusText.text = "Connected!"
            statusText.color = "green"
            console.log("LoginScreen: About to emit root.loginSuccess()")
            root.loginSuccess()
            console.log("LoginScreen: root.loginSuccess() emitted")
        }
        function onLoginError(error) {
            console.log("=== LoginScreen: onLoginError received ===", error)
            statusText.text = "Error: " + error
            statusText.color = "red"
        }
    }
}
