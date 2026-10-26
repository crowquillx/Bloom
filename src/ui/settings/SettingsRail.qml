pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

/**
 * SettingsRail - the category list on the left of Settings.
 *
 * Selection (the category shown on the right) uses selectionBackground and the
 * accent indicator; keyboard focus adds the shared FocusRing. Right or Enter
 * moves into the category's rows. Up from the first entry is not accepted, so
 * it reaches Main and focuses the navigation bar.
 */
FocusScope {
    id: root

    property int currentSection: 0
    readonly property string aboutAccountSectionKey: "about-account"
    readonly property string spacesSectionKey: "spaces"
    readonly property alias listHasFocus: railList.activeFocus

    signal enterContentRequested()

    implicitWidth: Math.round(520 * Theme.layoutScale)
    implicitHeight: railList.contentHeight

    readonly property var sectionModel: [
        { sectionKey: "playback",        name: qsTr("Playback"),        icon: "" },
        { sectionKey: "home",            name: qsTr("Home"),            icon: "" },
        { sectionKey: "display",         name: qsTr("Display"),         icon: "" },
        { sectionKey: "video",           name: qsTr("Video"),           icon: "" },
        { sectionKey: "mpv",             name: qsTr("MPV"),             icon: "" },
        { sectionKey: "input",           name: qsTr("Input"),           icon: "" },
        { sectionKey: "integrations",    name: qsTr("Integrations"),    icon: "" },
        { sectionKey: spacesSectionKey, name: qsTr("Spaces"),          icon: Icons.group },
        { sectionKey: aboutAccountSectionKey, name: qsTr("About & Account"), icon: "" }
    ]

    onCurrentSectionChanged: {
        if (railList.currentIndex !== currentSection) {
            railList.currentIndex = currentSection
        }
    }

    ListView {
        id: railList
        anchors.fill: parent
        model: root.sectionModel
        // Clipped, so focus rings are drawn inset.
        clip: true
        focus: true
        spacing: Theme.spacingXSmall
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: Theme.durationShort
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Settings categories")

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

            required property int index
            required property var modelData

            width: railList.width
            height: Theme.navBarItemHeight
            focusPolicy: Qt.NoFocus
            leftPadding: Theme.spacingMedium + Theme.spacingSmall
            rightPadding: Theme.spacingMedium

            readonly property bool isActive: railList.currentIndex === index
            readonly property bool hasFocus: railList.activeFocus && isActive

            Accessible.role: Accessible.ListItem
            Accessible.name: modelData.name
            Accessible.selected: isActive

            background: Rectangle {
                id: railItemBackground
                radius: Theme.radiusMedium
                color: {
                    if (railDelegate.isActive) return Theme.selectionBackground
                    if (railDelegate.hovered) return Theme.surfaceInteractive
                    return "transparent"
                }

                Behavior on color { enabled: Theme.uiAnimationsEnabled; ColorAnimation { duration: Theme.durationShort } }

                // Selection indicator on the leading edge.
                Rectangle {
                    width: Theme.selectionIndicatorWidth
                    height: Math.round(parent.height * 0.44)
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.verticalCenter: parent.verticalCenter
                    radius: width / 2
                    color: Theme.selectionIndicator
                    opacity: railDelegate.isActive ? 1.0 : 0.0
                    Behavior on opacity { enabled: Theme.uiAnimationsEnabled; NumberAnimation { duration: Theme.durationShort } }
                }

                FocusRing {
                    inset: true
                    active: railDelegate.hasFocus
                    radius: railItemBackground.radius
                }
            }

            contentItem: RowLayout {
                spacing: Theme.spacingMedium

                Text {
                    text: railDelegate.modelData.icon
                    font.family: Theme.fontIcon
                    font.pixelSize: Theme.iconSizeLarge
                    color: railDelegate.isActive ? Theme.selectionIndicator : Theme.textSecondary
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: Theme.iconSizeLarge
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.ignored: true
                }

                Text {
                    text: railDelegate.modelData.name
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    font.weight: railDelegate.isActive ? Theme.fontWeightStrong : Theme.fontWeightMedium
                    color: railDelegate.isActive || railDelegate.hovered ? Theme.textPrimary : Theme.textSecondary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            onClicked: {
                root.currentSection = index
                railList.forceActiveFocus()
            }
        }
    }

    function focusRail() {
        railList.forceActiveFocus()
    }
}
