import QtQuick
import QtQuick.Controls
import BloomUI

/**
 * UserRatingControl - the viewer's own rating of a title.
 *
 * Bound to a C++ UserRatingState (`ratingState`). The provider picks the
 * scale: five stars (Silo) or like / dislike (Jellyfin). Selecting the
 * current value clears it. Left/Right move between the buttons and leave the
 * control at either end; Enter, Return, or Space rates. Hidden callers check
 * `available` and keep it out of the focus chain.
 *
 * The buttons stay enabled (and focusable) while a change is in flight, so
 * focus never drops; they dim instead and presses are ignored. A rating
 * goes through SourceRouter to the item's own connection first.
 */
FocusScope {
    id: root

    property var ratingState: null
    property int buttonHeight: Theme.buttonHeightMedium

    /// The route to the item's connection failed; nothing was sent.
    signal failed(string message)

    readonly property string scale: ratingState ? ratingState.scale : ""
    readonly property bool available: !!ratingState && ratingState.supported
    readonly property int value: ratingState ? ratingState.value : 0
    readonly property bool pending: !!ratingState && ratingState.pending
    readonly property var choices: scale === "stars" ? [1, 2, 3, 4, 5]
                                 : (scale === "likes" ? [1, -1] : [])
    /// What the rating reads as, for labels and screen readers.
    readonly property string valueText: {
        if (scale === "stars")
            return value > 0 ? (value === 1 ? qsTr("1 star") : qsTr("%1 stars").arg(value))
                             : qsTr("Not rated")
        if (scale === "likes")
            return value > 0 ? qsTr("Liked") : (value < 0 ? qsTr("Disliked") : qsTr("Not rated"))
        return ""
    }

    implicitWidth: buttons.implicitWidth
    implicitHeight: buttons.implicitHeight

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Your rating: %1").arg(valueText)

    function choiceLabel(choice) {
        if (scale === "likes")
            return choice > 0 ? qsTr("Like") : qsTr("Dislike")
        return choice === 1 ? qsTr("Rate 1 star") : qsTr("Rate %1 stars").arg(choice)
    }

    function choiceSelected(choice) {
        if (scale === "stars")
            return value > 0 && choice <= value
        return value !== 0 && choice === value
    }

    /// Focuses the current rating, or the first choice when unrated.
    function focusCurrent() {
        if (repeater.count === 0)
            return false
        let index = 0
        for (let i = 0; i < choices.length; ++i) {
            if (choices[i] === value)
                index = i
        }
        const button = repeater.itemAt(index)
        if (!button)
            return false
        button.forceActiveFocus()
        return true
    }

    function rate(choice) {
        if (!ratingState || ratingState.pending || ratingRouter.busy)
            return
        const state = ratingState
        const itemId = state.itemId
        ratingRouter.run(state.connectionId, function() {
            if (state.itemId === itemId)
                state.rate(choice)
        })
    }

    SourceRouter {
        id: ratingRouter
        onFailed: (message) => root.failed(message || qsTr("Couldn't reach the server for this title."))
    }

    onActiveFocusChanged: {
        if (activeFocus && !buttons.activeFocus)
            focusCurrent()
    }

    Row {
        id: buttons
        opacity: root.pending || ratingRouter.busy ? 0.6 : 1.0
        Behavior on opacity {
            enabled: Theme.uiAnimationsEnabled
            NumberAnimation { duration: Theme.durationShort }
        }
        spacing: root.scale === "stars" ? Theme.spacingXSmall : Theme.controlSpacing

        Repeater {
            id: repeater
            model: root.choices

            ActionButton {
                id: choiceButton
                required property var modelData
                required property int index
                readonly property bool isSelected: root.choiceSelected(choiceButton.modelData)

                variant: root.scale === "likes" ? "secondary" : "ghost"
                selected: root.scale === "likes" && choiceButton.isSelected
                implicitHeight: root.buttonHeight
                iconPixelSize: Theme.iconSizeLarge
                iconGlyph: root.scale === "likes" ? (choiceButton.modelData > 0 ? Icons.thumbUp : Icons.thumbDown)
                                                  : (choiceButton.isSelected ? Icons.star : Icons.starBorder)
                iconColor: choiceButton.isSelected ? Theme.accentPrimary : Theme.textPrimary
                accessibleLabel: root.choiceLabel(choiceButton.modelData)
                Accessible.checkable: true
                Accessible.checked: choiceButton.isSelected

                Keys.onLeftPressed: function(event) {
                    const previous = repeater.itemAt(choiceButton.index - 1)
                    if (previous) {
                        previous.forceActiveFocus()
                        event.accepted = true
                    } else {
                        event.accepted = false
                    }
                }
                Keys.onRightPressed: function(event) {
                    const next = repeater.itemAt(choiceButton.index + 1)
                    if (next) {
                        next.forceActiveFocus()
                        event.accepted = true
                    } else {
                        event.accepted = false
                    }
                }
                Keys.onReturnPressed: function(event) { choiceButton.activate(event) }
                Keys.onEnterPressed: function(event) { choiceButton.activate(event) }
                onClicked: root.rate(choiceButton.modelData)

                function activate(event) {
                    if (!event.isAutoRepeat)
                        root.rate(choiceButton.modelData)
                    event.accepted = true
                }
            }
        }
    }
}
