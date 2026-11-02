import QtQuick
import QtQuick.Layouts

import BloomUI

/**
 * TasteSeedPrompt - Home's offer to tune a cold Silo profile
 * (docs/style-guide.md, Taste seeding).
 *
 * A shelf-shaped block in Home's focus order, after Bloom's own shelves and
 * before the server rows: a section header, one line on what it does, and
 * "Pick titles" / "Not now". TasteSeedPromptViewModel decides when it shows;
 * Home owns the keys between shelves through `moveHandler(delta)`, which
 * answers whether focus moved (false lets Up reach the navigation bar).
 */
FocusScope {
    id: root

    /// function(delta) -> bool, from Home's shelf focus order.
    property var moveHandler: null

    signal pickRequested()
    signal dismissRequested()

    implicitHeight: column.implicitHeight

    function moveFocus(event, delta) {
        event.accepted = root.moveHandler ? root.moveHandler(delta) : false
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.sectionTitleSpacing

        SectionHeader {
            text: qsTr("Personalize Home")
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            Layout.fillWidth: true
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        Text {
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            Layout.fillWidth: true
            Layout.maximumWidth: Theme.heroCopyMaxWidth
            text: qsTr("Pick a few titles you like and your recommendations start from your taste.")
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeSmall
            font.weight: Theme.fontWeightMedium
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.leftMargin: Theme.pageMarginX
            Layout.rightMargin: Theme.pageMarginX
            // Room for the focus ring.
            Layout.topMargin: Theme.spacingSmall
            Layout.bottomMargin: Theme.spacingSmall
            spacing: Theme.controlSpacing

            ActionButton {
                id: pickButton
                focus: true
                variant: "primary"
                iconGlyph: Icons.favorite
                text: qsTr("Pick titles")
                Accessible.description: qsTr("Pick a few titles you like to tune your recommendations")
                KeyNavigation.right: notNowButton
                Keys.onUpPressed: function(event) { root.moveFocus(event, -1) }
                Keys.onDownPressed: function(event) { root.moveFocus(event, 1) }
                Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                Keys.onEnterPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                onClicked: root.pickRequested()
            }

            ActionButton {
                id: notNowButton
                variant: "secondary"
                text: qsTr("Not now")
                Accessible.description: qsTr("Stop offering this on Home; Settings still has it")
                KeyNavigation.left: pickButton
                Keys.onUpPressed: function(event) { root.moveFocus(event, -1) }
                Keys.onDownPressed: function(event) { root.moveFocus(event, 1) }
                Keys.onReturnPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                Keys.onEnterPressed: function(event) { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                onClicked: root.dismissRequested()
            }
        }
    }
}
