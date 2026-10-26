import QtQuick
import QtQuick.Layouts
import BloomUI

/**
 * SettingsRow - the shared frame for one settings row (Panel archetype).
 *
 * Label (fontSizeBody, strong) and an optional description (fontSizeSmall,
 * textSecondary) on the left; the control goes in the default slot and sits on
 * the right edge. The row is at least navBarItemHeight tall and shows focus
 * with the shared SettingsRowBackground (hover fill + inset FocusRing).
 *
 * Wrapping a control (the control keeps focus and its own key handling):
 *   SettingsRow {
 *       label: qsTr("Audio Language")
 *       highlighted: languageCombo.activeFocus || languageCombo.popup.visible
 *       SettingsComboBox { id: languageCombo }
 *   }
 *
 * A row can also take focus itself (disclosure or navigation rows): focus it
 * and handle `activated()`, which fires on Return, Enter, Space and click.
 */
Item {
    id: row

    property string label: ""
    property string description: ""
    /// Draw the focus treatment. Defaults to the row's own focus; rows that
    /// wrap a control bind it to the control's focus.
    property bool highlighted: activeFocus
    /// Dim label and description (the control dims itself).
    property bool dimmed: !enabled
    /// Focused when the row is clicked (then `activated()` fires); defaults to
    /// the row itself.
    property Item focusTarget: null
    property int verticalPadding: Math.round(14 * Theme.layoutScale)
    default property alias trailing: trailingRow.data
    readonly property bool hovered: rowMouse.containsMouse

    signal activated()

    Layout.fillWidth: true
    implicitHeight: Math.max(Theme.navBarItemHeight,
                             Math.max(textColumn.implicitHeight, trailingRow.implicitHeight)
                             + verticalPadding * 2)

    Accessible.name: label
    Accessible.description: description

    Keys.onReturnPressed: function(event) { row.activateFromKey(event) }
    Keys.onEnterPressed: function(event) { row.activateFromKey(event) }
    Keys.onSpacePressed: function(event) { row.activateFromKey(event) }

    function activateFromKey(event) {
        // Only when the row itself is focused; wrapped controls handle their keys.
        if (!row.activeFocus || !row.enabled) {
            event.accepted = false
            return
        }
        row.activated()
        event.accepted = true
    }

    SettingsRowBackground {
        focused: row.highlighted
        hovered: row.hovered && row.enabled
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: row.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            (row.focusTarget || row).forceActiveFocus()
            row.activated()
        }
    }

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingLarge

        ColumnLayout {
            id: textColumn
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Math.round(4 * Theme.layoutScale)

            Text {
                Layout.fillWidth: true
                text: row.label
                visible: text.length > 0
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightStrong
                color: row.dimmed ? Theme.textDisabled : Theme.textPrimary
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: row.description
                visible: text.length > 0
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                color: row.dimmed ? Theme.textDisabled : Theme.textSecondary
                wrapMode: Text.WordWrap
            }
        }

        RowLayout {
            id: trailingRow
            // SettingsComboBox reads this to leave the focus ring to the row.
            objectName: "settingsRowTrailing"
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            spacing: Theme.controlSpacing
        }
    }
}
