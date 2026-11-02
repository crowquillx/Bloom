import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BloomUI

/**
 * UserRatingDialog - rate a title from a detail page's More options menu.
 *
 * Holds the shared UserRatingControl for `ratingState`. A rating applies as
 * soon as it is chosen (the view model rolls it back if the server refuses);
 * Done, Escape, or Back closes the dialog and returns focus to
 * `restoreFocusTarget`.
 */
Dialog {
    id: root
    modal: true
    focus: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.round(560 * Theme.layoutScale)
    padding: Theme.dialogPadding
    topPadding: Theme.spacingLarge

    property var ratingState: null
    property string itemTitle: ""
    property Item restoreFocusTarget: null

    /// The route to the title's connection failed; nothing was sent.
    signal ratingFailed(string message)

    title: itemTitle ? qsTr("Rate %1").arg(itemTitle) : qsTr("Your Rating")

    background: DialogSurface {}
    Overlay.modal: ModalScrim {}

    // Detail pages restore their own focus after async loads.
    ModalFocusGuard {
        popup: root
        fallbackItem: doneButton
    }

    function openFor(focusTarget) {
        restoreFocusTarget = focusTarget || null
        open()
    }

    header: Item {
        implicitHeight: Math.round(68 * Theme.layoutScale) + Theme.spacingLarge
        Text {
            text: root.title
            font.pixelSize: Theme.fontSizeTitle
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightHeavy
            color: Theme.textPrimary
            elide: Text.ElideRight
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.dialogPadding
            anchors.rightMargin: Theme.dialogPadding
            anchors.bottom: parent.bottom
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMedium

        Text {
            Layout.fillWidth: true
            text: ratingControl.valueText
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
        }

        UserRatingControl {
            id: ratingControl
            Layout.alignment: Qt.AlignLeft
            ratingState: root.ratingState
            onFailed: (message) => root.ratingFailed(message)
            Keys.onDownPressed: function(event) {
                doneButton.forceActiveFocus()
                event.accepted = true
            }
        }
    }

    footer: Item {
        implicitHeight: Theme.buttonHeightMedium + Theme.dialogPadding + Theme.spacingMedium
        implicitWidth: parent ? parent.width : 400
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogPadding
            anchors.topMargin: Theme.spacingMedium

            ActionButton {
                id: doneButton
                variant: "secondary"
                Layout.fillWidth: true
                text: qsTr("Done")
                focusPolicy: Qt.StrongFocus
                Keys.onUpPressed: function(event) {
                    event.accepted = ratingControl.focusCurrent()
                }
                Keys.onReturnPressed: root.close()
                Keys.onEnterPressed: root.close()
                onClicked: root.close()
            }
        }
    }

    onOpened: {
        Qt.callLater(function() {
            if (!ratingControl.focusCurrent())
                doneButton.forceActiveFocus()
        })
    }

    onClosed: {
        Qt.callLater(function() {
            if (root.restoreFocusTarget)
                root.restoreFocusTarget.forceActiveFocus()
            root.restoreFocusTarget = null
        })
    }
}
