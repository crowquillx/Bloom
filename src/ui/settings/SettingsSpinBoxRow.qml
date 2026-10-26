import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    property string label: ""
    property string description: ""
    property int value: 0
    property int from: 0
    property int to: 100
    property int stepSize: 1
    property string unit: ""
    property var ensureVisible: null

    Accessible.role: Accessible.SpinBox
    Accessible.name: label
    Accessible.description: description

    signal spinBoxValueChanged(int newValue)

    function formatSpinBoxValue(value) {
        return value + (root.unit ? " " + root.unit : "")
    }

    function stepSpinBox(delta) {
        if (!spinBox.enabled) return
        var nextValue = Math.max(spinBox.from, Math.min(spinBox.to, spinBox.value + (delta * spinBox.stepSize)))
        if (nextValue !== spinBox.value) {
            spinBox.value = nextValue
            root.spinBoxValueChanged(nextValue)
        }
    }

    implicitHeight: frame.implicitHeight
    Layout.fillWidth: true

    onActiveFocusChanged: {
        if (activeFocus && ensureVisible) ensureVisible(root)
    }

    Keys.onLeftPressed: function(event) {
        stepSpinBox(-1)
        event.accepted = true
    }
    Keys.onRightPressed: function(event) {
        stepSpinBox(1)
        event.accepted = true
    }

    SettingsRow {
        id: frame
        anchors.left: parent.left
        anchors.right: parent.right
        label: root.label
        description: root.description
        highlighted: root.activeFocus
        focusTarget: root

        SpinBox {
            id: spinBox
            from: root.from
            to: root.to
            stepSize: root.stepSize
            value: root.value
            editable: true

            onValueModified: {
                root.spinBoxValueChanged(value)
            }

            Component.onDestruction: {
                var currentText = spinBoxTextInput.text
                if (currentText !== "" && currentText !== root.formatSpinBoxValue(spinBox.value)) {
                    var parsed = spinBox.valueFromText(currentText, spinBox.locale)
                    if (!isNaN(parsed) && parsed >= spinBox.from && parsed <= spinBox.to) {
                        root.spinBoxValueChanged(parsed)
                    }
                }
            }

            valueFromText: function(text, locale) {
                var cleanText = text.trim()
                if (root.unit) {
                    if (cleanText.endsWith(root.unit)) {
                        cleanText = cleanText.slice(0, cleanText.length - root.unit.length).trim()
                    }
                }
                return Number.fromLocaleString(locale, cleanText)
            }

            contentItem: TextInput {
                id: spinBoxTextInput
                z: 2
                text: root.formatSpinBoxValue(spinBox.value)
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                color: Theme.textPrimary
                selectionColor: Theme.accentPrimary
                selectedTextColor: Theme.textPrimary
                horizontalAlignment: Qt.AlignHCenter
                verticalAlignment: Qt.AlignVCenter
                readOnly: !spinBox.editable
                validator: spinBox.validator
                inputMethodHints: Qt.ImhDigitsOnly
            }

            up.indicator: Rectangle {
                x: spinBox.mirrored ? 0 : parent.width - width
                height: parent.height
                implicitWidth: Theme.buttonHeightSmall
                implicitHeight: Theme.buttonHeightSmall
                color: spinBox.up.pressed ? Theme.surfaceInteractivePressed : "transparent"
                radius: Theme.radiusSmall

                Text {
                    text: Icons.add
                    font.pixelSize: Theme.fontSizeIcon
                    font.family: Theme.fontIcon
                    color: Theme.textPrimary
                    anchors.centerIn: parent
                }
            }

            down.indicator: Rectangle {
                x: spinBox.mirrored ? parent.width - width : 0
                height: parent.height
                implicitWidth: Theme.buttonHeightSmall
                implicitHeight: Theme.buttonHeightSmall
                color: spinBox.down.pressed ? Theme.surfaceInteractivePressed : "transparent"
                radius: Theme.radiusSmall

                Text {
                    text: Icons.remove
                    font.pixelSize: Theme.fontSizeIcon
                    font.family: Theme.fontIcon
                    color: Theme.textPrimary
                    anchors.centerIn: parent
                }
            }

            opacity: root.enabled ? 1.0 : Theme.disabledOpacity

            background: Rectangle {
                implicitWidth: Math.round(220 * Theme.layoutScale)
                implicitHeight: Theme.buttonHeightSmall
                color: Theme.surfaceInteractive
                radius: Theme.radiusControl
            }
        }
    }
}
