import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

FocusScope {
    id: root

    property int currentSection: 0
    readonly property string aboutAccountSectionKey: "about-account"
    readonly property string workspacesSectionKey: "workspaces"

    signal enterContentRequested()

    implicitWidth: Math.round(380 * Theme.layoutScale)

    readonly property var sectionModel: [
        { sectionKey: "playback",        name: qsTr("Playback"),        icon: "\ue037" },
        { sectionKey: "home",            name: qsTr("Home"),            icon: "\ue88a" },
        { sectionKey: "display",         name: qsTr("Display"),         icon: "\ue40a" },
        { sectionKey: "video",           name: qsTr("Video"),           icon: "\ue04b" },
        { sectionKey: "mpv",             name: qsTr("MPV"),             icon: "\ue429" },
        { sectionKey: "input",           name: qsTr("Input"),           icon: "\ue312" },
        { sectionKey: "integrations",    name: qsTr("Integrations"),    icon: "\ue2c3" },
        { sectionKey: workspacesSectionKey, name: qsTr("Workspaces"),      icon: "" },
        { sectionKey: aboutAccountSectionKey, name: qsTr("About & Account"), icon: "\ue88e" }
    ]

    onCurrentSectionChanged: {
        if (railList.currentIndex !== currentSection) {
            railList.currentIndex = currentSection
        }
    }

    GlassPanel {
        anchors.fill: parent
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMedium + Theme.spacingXSmall
        spacing: Theme.spacingXSmall

        // Settings title
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingSmall
            Layout.topMargin: Theme.spacingSmall
            Layout.bottomMargin: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Text {
                text: qsTr("Settings")
                font.pixelSize: Theme.fontSizeHeader
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightHeavy
                color: Theme.textPrimary
            }
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
                height: Theme.navRailItemHeight - Theme.spacingSmall
                focusPolicy: Qt.StrongFocus
                leftPadding: Theme.spacingMedium + Theme.spacingSmall
                rightPadding: Theme.spacingMedium

                property bool isActive: railList.currentIndex === index
                property bool hasFocus: railDelegate.activeFocus || (railList.activeFocus && isActive)

                background: Rectangle {
                    id: railItemBackground
                    radius: Theme.radiusMedium
                    color: {
                        if (railDelegate.hasFocus) return Theme.surfaceInteractiveHover
                        if (railDelegate.isActive) return Theme.selectionBackground
                        if (railDelegate.hovered) return Theme.surfaceInteractive
                        return "transparent"
                    }

                    Behavior on color { enabled: Theme.uiAnimationsEnabled; ColorAnimation { duration: Theme.durationShort } }

                    FocusRing {
                        inset: true
                        active: railDelegate.hasFocus
                        radius: railItemBackground.radius
                    }
                }

                // Accent bar on left edge for active item
                Rectangle {
                    width: Theme.selectionIndicatorWidth
                    height: Math.round(parent.height * 0.44)
                    anchors.left: parent.left
                    anchors.leftMargin: Math.round(6 * Theme.layoutScale)
                    anchors.verticalCenter: parent.verticalCenter
                    radius: width / 2
                    color: Theme.selectionIndicator
                    visible: true
                    opacity: railDelegate.isActive ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: Theme.durationShort } }
                }

                contentItem: RowLayout {
                    spacing: Theme.spacingMedium

                    Text {
                        text: modelData.icon
                        font.family: Theme.fontIcon
                        font.pixelSize: Theme.fontSizeIcon
                        color: railDelegate.isActive ? Theme.selectionIndicator : Theme.textSecondary
                    }

                    Text {
                        text: modelData.name
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        font.weight: railDelegate.isActive || railDelegate.hasFocus ? Theme.fontWeightStrong : Theme.fontWeightRegular
                        color: railDelegate.isActive || railDelegate.hasFocus ? Theme.textPrimary : Theme.textSecondary
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
