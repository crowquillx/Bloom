import QtQuick
import QtQuick.Layouts

import BloomUI

/**
 * CollectionsShelf - the detail pages' "Part of" shelf: the collections
 * that hold the title, as poster cards (docs/style-guide.md).
 *
 * Hidden while there are none. The header names the collection when there
 * is one ("Part of Star Wars Collection"). Enter or a click activates a
 * collection; Up/Down ask the page to move to the shelf above or below
 * (`shelfStepRequested`), so the page keeps its own shelf order.
 */
ColumnLayout {
    id: root

    /// Canonical collections (models/MediaCollections.h).
    property var collections: []
    property int cardImageWidth: 640
    readonly property alias list: collectionList

    signal collectionActivated(var collection)
    /// Up (-1) or Down (1) from the shelf.
    signal shelfStepRequested(int delta)

    spacing: Theme.sectionTitleSpacing
    visible: collections.length > 0

    function subtitleFor(collection) {
        var count = collection ? collection.itemCount : -1
        if (count === undefined || count < 0)
            return ""
        return count === 1 ? qsTr("1 title") : qsTr("%1 titles").arg(count)
    }

    function activate(index) {
        if (index >= 0 && index < root.collections.length)
            root.collectionActivated(root.collections[index])
    }

    SectionHeader {
        text: root.collections.length === 1
              ? qsTr("Part of %1").arg(root.collections[0].name || "")
              : qsTr("Collections")
        detail: root.collections.length > 1 ? String(root.collections.length) : ""
        Layout.leftMargin: Theme.pageMarginX
        Layout.rightMargin: Theme.pageMarginX
        Layout.fillWidth: true
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    ShelfList {
        id: collectionList
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        model: root.collections
        Accessible.name: qsTr("Collections with this title")

        delegate: MediaCard {
            required property var modelData
            required property int index
            variant: "poster"
            artworkSources: [modelData.primaryArtwork
                             ? LibraryService.getCachedArtworkUrlFromRef(modelData.primaryArtwork, root.cardImageWidth)
                             : ""]
            title: modelData.name || ""
            subtitle: root.subtitleFor(modelData)
            placeholderIcon: Icons.videoLibrary
            focused: ListView.isCurrentItem && ListView.view.activeFocus
            onClicked: {
                collectionList.currentIndex = index
                collectionList.forceActiveFocus()
                root.activate(index)
            }
            Accessible.description: qsTr("Open the %1 collection").arg(modelData.name || "")
        }

        Keys.onUpPressed: (event) => { root.shelfStepRequested(-1); event.accepted = true }
        Keys.onDownPressed: (event) => { root.shelfStepRequested(1); event.accepted = true }
        Keys.onReturnPressed: (event) => {
            if (!event.isAutoRepeat)
                root.activate(currentIndex)
            event.accepted = true
        }
        Keys.onEnterPressed: (event) => {
            if (!event.isAutoRepeat)
                root.activate(currentIndex)
            event.accepted = true
        }
    }
}
