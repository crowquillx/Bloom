import QtQuick

import BloomUI

/**
 * MetadataLine - a single line of facts separated by a thin divider,
 * e.g. "2023  |  TV-14  |  1 season". Empty entries are skipped, so callers can
 * pass optional values directly.
 */
Text {
    id: root

    property var items: []
    property string separator: "   │   "

    readonly property var visibleItems: {
        const result = []
        const source = items || []
        for (let i = 0; i < source.length; ++i) {
            const value = source[i]
            if (value !== undefined && value !== null && String(value).trim() !== "")
                result.push(String(value))
        }
        return result
    }

    visible: visibleItems.length > 0
    text: visibleItems.join(separator)
    font.pixelSize: Theme.fontSizeSmall
    font.family: Theme.fontPrimary
    font.weight: Theme.fontWeightMedium
    color: Theme.textSecondary
    elide: Text.ElideRight
    maximumLineCount: 1
}
