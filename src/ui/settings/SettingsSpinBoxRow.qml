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

    implicitHeight: content.implicitHeight
    Layout.fillWidth: true

    onActiveFocusChanged: {
        if (activeFocus && ensureVisible) ensureVisible(this)
    }

    Keys.onLeftPressed: spinBox.decrease()
    Keys.onRightPressed: spinBox.increase()

    Rectangle {
        anchors.fill: parent
        anchors.margins: -Theme.spacingSmall
        radius: Theme.radiusSmall
        color: root.activeFocus ? Theme.hoverOverlay : "transparent"
        border.color: root.activeFocus ? Theme.focusBorder : "transparent"
        border.width: root.activeFocus ? 2 : 0
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            ColumnLayout {
                spacing: Math.round(4 * Theme.layoutScale)
                Layout.fillWidth: true

                Text {
                    text: root.label
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                }

                Text {
                    text: root.description
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    visible: text.length > 0
                }
            }

            SpinBox {
                id: spinBox
                from: root.from
                to: root.to
                stepSize: root.stepSize
                value: root.value
                editable: true

                onValueModified: {
                    root.value = value
                    root.spinBoxValueChanged(value)
                }

                valueFromText: function(text, locale) {
                    var cleanText = text
                    if (root.unit) {
                        cleanText = text.replace(root.unit, "").trim()
                    }
                    return Number.fromLocaleString(locale, cleanText)
                }

                contentItem: TextInput {
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
                    implicitWidth: 40
                    implicitHeight: 40
                    color: spinBox.up.pressed ? Theme.buttonPrimaryBackground : "transparent"
                    radius: Theme.radiusSmall

                    Text {
                        text: "+"
                        font.pixelSize: Theme.fontSizeTitle
                        color: Theme.textPrimary
                        anchors.centerIn: parent
                    }
                }

                down.indicator: Rectangle {
                    x: spinBox.mirrored ? parent.width - width : 0
                    height: parent.height
                    implicitWidth: 40
                    implicitHeight: 40
                    color: spinBox.down.pressed ? Theme.buttonPrimaryBackground : "transparent"
                    radius: Theme.radiusSmall

                    Text {
                        text: "-"
                        font.pixelSize: Theme.fontSizeTitle
                        color: Theme.textPrimary
                        anchors.centerIn: parent
                    }
                }

                background: Rectangle {
                    implicitWidth: 140
                    implicitHeight: Theme.buttonHeightSmall
                    color: Theme.inputBackground
                    border.color: Theme.inputBorder
                    border.width: 1
                    radius: Theme.radiusSmall
                }
            }
        }
    }
}
