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
        spacing: 20
        width: 400

        Text {
            text: "Connect to Jellyfin"
            font.pixelSize: 32
            font.family: Theme.fontPrimary
            color: Theme.textPrimary
            Layout.alignment: Qt.AlignHCenter
        }

        TextField {
            id: serverField
            focus: true  // Default focus
            placeholderText: "Server URL (e.g. http://192.168.1.10:8096)"
            text: "http://localhost:8096"
            Layout.fillWidth: true
            font.pixelSize: 18
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
            font.pixelSize: 18
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
            font.pixelSize: 18
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
            Keys.onReturnPressed: (event) => { connectButton.clicked(); event.accepted = true }
            Keys.onEnterPressed: (event) => { connectButton.clicked(); event.accepted = true }
        }

        Button {
            id: connectButton
            text: "Connect"
            font.pixelSize: 24
            font.family: Theme.fontPrimary
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            
            background: Rectangle {
                color: parent.activeFocus ? Theme.accentPrimary : Theme.backgroundSecondary
                radius: Theme.radiusSmall
                border.color: parent.activeFocus ? Theme.textPrimary : "transparent"
                border.width: 2
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
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
        }

        Text {
            id: statusText
            text: ""
            color: Theme.accentSecondary // Using accent for status for now
            font.pixelSize: 16
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
