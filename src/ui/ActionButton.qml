import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

/**
 * ActionButton - Bloom's shared button.
 *
 * variant:
 *   "primary"     inverse fill (the one main action on a screen or card)
 *   "secondary"   translucent glass fill (supporting actions)
 *   "ghost"       no fill until hovered/focused (low-emphasis actions)
 *   "destructive" tinted error fill (sign out, delete)
 *
 * Focus draws the shared FocusRing outside the button. Key activation stays
 * with callers (Button handles Space; screens wire Return/Enter), matching the
 * existing SecondaryActionButton contract.
 */
Button {
    id: actionButton

    property string variant: "secondary"
    /// Toggled-on state for secondary/ghost buttons (open drawer, active
    /// filter): tinted with the selection colours, distinct from focus.
    property bool selected: false
    property string iconGlyph: ""
    /// Work in progress: the icon becomes a spinner while the label stays
    /// readable, so the button keeps its identity (e.g. "Play" while loading).
    property bool busy: false
    property color iconColor: variant === "primary" ? Theme.actionPrimaryText
                             : (variant === "destructive" ? Theme.actionDestructiveText : Theme.textPrimary)
    property color labelColor: variant === "primary" ? Theme.actionPrimaryText
                              : (variant === "destructive" ? Theme.actionDestructiveText : Theme.textPrimary)
    property bool showLabel: text !== ""
    property string accessibleLabel: ""
    property string toolTipText: ""
    property int labelPixelSize: Theme.fontSizeBody
    property real cornerRadius: Theme.radiusControl
    property int sidePadding: Math.round(30 * Theme.layoutScale)

    readonly property bool iconOnly: !showLabel || text === ""

    padding: 0
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true

    implicitHeight: Theme.buttonHeightMedium
    implicitWidth: iconOnly
                   ? implicitHeight
                   : buttonContent.implicitWidth + sidePadding * 2

    opacity: enabled ? 1.0 : Theme.disabledOpacity

    Accessible.role: Accessible.Button
    Accessible.name: accessibleLabel || text
    Accessible.description: busy ? qsTr("Loading") : ""
    Accessible.focusable: true

    ToolTip.visible: hovered && enabled
                     && (toolTipText.length > 0
                         || (iconOnly && (accessibleLabel || text).length > 0))
    ToolTip.text: toolTipText || accessibleLabel || text
    ToolTip.delay: 500

    background: Rectangle {
        id: buttonBackground
        radius: actionButton.cornerRadius
        color: {
            const v = actionButton.variant
            if (v === "primary") {
                if (actionButton.down) return Theme.actionPrimaryBackgroundPressed
                if (actionButton.hovered) return Theme.actionPrimaryBackgroundHover
                return Theme.actionPrimaryBackground
            }
            if (v === "destructive") {
                if (actionButton.down || actionButton.hovered || actionButton.activeFocus)
                    return Theme.actionDestructiveBackgroundHover
                return Theme.actionDestructiveBackground
            }
            if (actionButton.selected) {
                return actionButton.hovered || actionButton.activeFocus
                        ? Theme.mix(Theme.selectionBackground, Theme.actionSecondaryBackgroundHover, 0.5)
                        : Theme.selectionBackground
            }
            if (v === "ghost") {
                if (actionButton.down) return Theme.actionSecondaryBackgroundPressed
                if (actionButton.hovered || actionButton.activeFocus) return Theme.actionSecondaryBackgroundHover
                return "transparent"
            }
            if (!actionButton.enabled) return Theme.surfaceInteractiveDisabled
            if (actionButton.down) return Theme.actionSecondaryBackgroundPressed
            if (actionButton.hovered || actionButton.activeFocus) return Theme.actionSecondaryBackgroundHover
            return Theme.actionSecondaryBackground
        }
        border.width: actionButton.variant === "secondary" || actionButton.selected ? Theme.borderWidth : 0
        border.color: actionButton.selected ? Theme.selectionIndicator
                    : (actionButton.hovered ? Theme.surfaceBorderStrong : Theme.actionSecondaryBorder)
        antialiasing: true

        Behavior on color {
            enabled: Theme.uiAnimationsEnabled
            ColorAnimation { duration: Theme.durationShort }
        }

        FocusRing {
            active: actionButton.activeFocus
            radius: buttonBackground.radius
        }
    }

    contentItem: Item {
        id: buttonContent
        implicitWidth: buttonInnerRow.implicitWidth
        implicitHeight: buttonInnerRow.implicitHeight

        RowLayout {
            id: buttonInnerRow
            anchors.centerIn: parent
            spacing: Math.round(12 * Theme.layoutScale)

            Text {
                text: actionButton.iconGlyph
                visible: text !== "" && !actionButton.busy
                font.family: Theme.fontIcon
                font.pixelSize: Theme.fontSizeIcon
                color: actionButton.iconColor
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                text: Icons.refresh
                visible: actionButton.busy
                RotationAnimator on rotation {
                    running: actionButton.busy && Theme.uiAnimationsEnabled
                    from: 0
                    to: 360
                    duration: 1000
                    loops: Animation.Infinite
                }
                font.family: Theme.fontIcon
                font.pixelSize: Theme.fontSizeIcon
                color: actionButton.iconColor
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                text: actionButton.text
                visible: actionButton.showLabel && text !== ""
                font.pixelSize: actionButton.labelPixelSize
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightStrong
                color: actionButton.labelColor
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }
}
