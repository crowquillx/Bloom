import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    property int currentSection: 0
    readonly property string aboutAccountSectionKey: "about-account"

    signal enterContentRequested()

    implicitWidth: Math.round(280 * Theme.layoutScale)

    readonly property var sectionModel: [
        { sectionKey: "playback",        name: qsTr("Playback"),        icon: "\ue037" },
        { sectionKey: "display",         name: qsTr("Display"),         icon: "\ue40a" },
        { sectionKey: "video",           name: qsTr("Video"),           icon: "\ue04b" },
        { sectionKey: "mpv",             name: qsTr("MPV"),             icon: "\ue429" },
        { sectionKey: "integrations",    name: qsTr("Integrations"),    icon: "\ue2c3" },
        { sectionKey: aboutAccountSectionKey, name: qsTr("About & Account"), icon: "\ue88e" }
    ]

    onCurrentSectionChanged: {
        if (railList.currentIndex !== currentSection) {
            railList.currentIndex = currentSection
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusLarge
        color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.76)
        border.color: Theme.cardBorder
        border.width: Theme.borderWidth

        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.08) }
            GradientStop { position: 0.3; color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.78) }
            GradientStop { position: 1.0; color: Qt.rgba(Theme.cardBackground.r, Theme.cardBackground.g, Theme.cardBackground.b, 0.66) }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingSmall

        // Settings title
        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.spacingSmall
            spacing: Theme.spacingSmall

            Text {
                text: Icons.settings
                font.family: Theme.fontIcon
                font.pixelSize: Theme.fontSizeTitle
                color: Theme.accentPrimary
            }

            Text {
                text: qsTr("Settings")
                font.pixelSize: Theme.fontSizeTitle
                font.family: Theme.fontPrimary
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.borderLight
        }

        ListView {
            id: railList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: sectionModel
            clip: true
            focus: true
            boundsBehavior: Flickable.StopAtBounds

            onCurrentIndexChanged: {
                if (currentIndex >= 0 && root.currentSection !== currentIndex) {
                    root.currentSection = currentIndex
                }
            }

            Component.onCompleted: {
                currentIndex = root.currentSection
            }

            Keys.onRightPressed: root.enterContentRequested()
            Keys.onReturnPressed: root.enterContentRequested()
            Keys.onEnterPressed: root.enterContentRequested()

            delegate: ItemDelegate {
                id: railDelegate
                width: railList.width
                height: Math.round(56 * Theme.layoutScale)
                focusPolicy: Qt.StrongFocus
                leftPadding: Theme.spacingSmall

                property bool isActive: railList.currentIndex === index
                property bool hasFocus: railDelegate.activeFocus || (railList.activeFocus && isActive)

                background: Rectangle {
                    radius: Theme.radiusMedium
                    color: {
                        if (railDelegate.hasFocus && railDelegate.isActive)
                            return Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.20)
                        if (railDelegate.isActive)
                            return Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.12)
                        if (railDelegate.hovered)
                            return Qt.rgba(1, 1, 1, 0.06)
                        return "transparent"
                    }
                    border.color: railDelegate.hasFocus ? Theme.focusBorder : "transparent"
                    border.width: railDelegate.hasFocus ? 2 : 0

                    Behavior on color { ColorAnimation { duration: Theme.durationShort } }
                }

                // Accent bar on left edge for active item
                Rectangle {
                    width: 4
                    height: parent.height - Theme.spacingSmall * 2
                    anchors.left: parent.left
                    anchors.leftMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 2
                    color: Theme.accentPrimary
                    visible: true
                    opacity: railDelegate.isActive ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: Theme.durationShort } }
                }

                contentItem: RowLayout {
                    spacing: Theme.spacingSmall

                    Text {
                        text: modelData.icon
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.fontSizeBody
                        color: railDelegate.isActive ? Theme.accentPrimary : Theme.textSecondary
                    }

                    Text {
                        text: modelData.name
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        font.weight: railDelegate.isActive ? Font.DemiBold : Font.Normal
                        color: railDelegate.isActive ? Theme.textPrimary : Theme.textSecondary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                onClicked: {
                    root.currentSection = index
                }
            }
        }
    }

    function focusRail() {
        railList.forceActiveFocus()
    }
}
