import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    property string label: ""
    property string description: ""
    property real value: 0
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property string unit: ""
    property var ensureVisible: null

    Accessible.role: Accessible.Slider
    Accessible.name: label
    Accessible.description: description

    signal sliderValueChanged(real newValue)

    implicitHeight: content.implicitHeight
    Layout.fillWidth: true

    onActiveFocusChanged: {
        if (activeFocus && ensureVisible) ensureVisible(root)
    }

    Keys.onLeftPressed: decreaseValue()
    Keys.onRightPressed: increaseValue()

    function decreaseValue() {
        var newVal = Math.max(from, value - stepSize)
        if (newVal !== value) {
            value = newVal
            sliderValueChanged(value)
        }
    }

    function increaseValue() {
        var newVal = Math.min(to, value + stepSize)
        if (newVal !== value) {
            value = newVal
            sliderValueChanged(value)
        }
    }

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

            Rectangle {
                width: 60
                height: 32
                radius: Theme.radiusSmall
                color: Theme.inputBackground
                border.color: Theme.inputBorder
                border.width: Theme.borderWidth

                Text {
                    anchors.centerIn: parent
                    text: Math.round(root.value) + root.unit
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                }
            }
        }

        Slider {
            id: slider
            Layout.fillWidth: true
            from: root.from
            to: root.to
            stepSize: root.stepSize
            value: root.value

            onMoved: {
                root.value = value
                root.sliderValueChanged(value)
            }

            background: Rectangle {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: slider.availableWidth
                height: 6
                radius: 3
                color: Qt.rgba(1, 1, 1, 0.2)

                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    radius: 3
                    color: Theme.accentPrimary
                }
            }

            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: 20
                height: 20
                radius: 10
                color: slider.pressed ? Theme.accentPrimary : Theme.textPrimary
                border.color: Theme.accentPrimary
                border.width: 2

                Behavior on color { ColorAnimation { duration: Theme.durationShort } }
            }
        }
    }
}
