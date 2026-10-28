import QtQuick

import BloomUI

/**
 * ShelfList - the horizontal row under a shelf header.
 *
 * Aligns its first card to the page gutter, keeps the focused card inside the
 * gutters as focus moves, leaves room for the focus lift above and below, and
 * slides cards aside on inserts, moves and removals. Callers supply `model`,
 * `delegate` (usually a MediaCard) and their own key handling.
 *
 * Height defaults to one card plus focus room; set `cardHeight` to the
 * delegate's height (poster cards include their caption).
 */
ListView {
    id: root

    property int cardHeight: Theme.posterCardHeight + Theme.cardCaptionHeight

    implicitHeight: cardHeight + Theme.shelfFocusMargin * 2
    orientation: ListView.Horizontal
    spacing: Theme.cardGap
    leftMargin: Theme.pageMarginX
    rightMargin: Theme.pageMarginX
    topMargin: Theme.shelfFocusMargin
    bottomMargin: Theme.shelfFocusMargin
    clip: false
    preferredHighlightBegin: Theme.pageMarginX
    preferredHighlightEnd: width - Theme.pageMarginX
    highlightRangeMode: ListView.StrictlyEnforceRange
    highlightMoveDuration: Theme.uiAnimationsEnabled ? Theme.durationNormal : 0
    keyNavigationEnabled: true
    boundsBehavior: Flickable.StopAtBounds

    // No `add` or `populate` fade-in: when the view resizes while one is
    // starting (a model reset during a layout pass), ListView abandons it and
    // leaves the card stuck at whatever opacity it had reached, often 0.
    addDisplaced: Transition {
        NumberAnimation { properties: "x,y"; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
    }
    move: Transition {
        NumberAnimation { properties: "x,y"; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
    }
    remove: Transition {
        NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationShort; easing.type: Easing.OutQuad }
    }
    removeDisplaced: Transition {
        NumberAnimation { properties: "x,y"; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
    }
    displaced: Transition {
        NumberAnimation { properties: "x,y"; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
    }
}
