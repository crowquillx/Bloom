pragma ComponentBehavior: Bound

import QtQuick

import BloomUI

/**
 * PosterBadgeOverlay - server-chosen badges in the corners of a poster.
 *
 * `PosterBadges` resolves which badges an item shows and where from its
 * connection's preferences (Silo poster badges); this component only draws
 * them in Bloom's overlay style. Each corner stacks up to three badges.
 * Corners shared with other poster chrome reserve that space.
 *
 * Usage (fill the poster):
 *   PosterBadgeOverlay {
 *       anchors.fill: parent
 *       item: modelData
 *       topRightReserved: unwatchedBadge.visible ? unwatchedBadge.height : 0
 *   }
 */
Item {
    id: root

    // Canonical media item map (needs connectionId)
    property var item: ({})
    // Heights already taken at a corner by other poster chrome
    property real topLeftReserved: 0
    property real topRightReserved: 0
    property real bottomLeftReserved: 0
    property real bottomRightReserved: 0

    readonly property var badges: PosterBadges.revision >= 0 && root.item
                                  ? PosterBadges.badgesFor(root.item) : []
    readonly property int inset: Math.round(12 * Theme.layoutScale)
    // Width a corner may use; the opposite corner on the same edge keeps up
    // to half of the poster, so a lone or narrow neighbour leaves room.
    function cornerWidth(oppositeNaturalWidth) {
        const full = Math.max(0, width - inset * 2)
        if (oppositeNaturalWidth <= 0)
            return full
        return Math.max(0, full - inset - Math.min(oppositeNaturalWidth, (full - inset) / 2))
    }

    visible: badges.length > 0
    Accessible.ignored: true

    function badgesAt(corner) {
        return root.badges.filter(function(badge) { return badge.corner === corner })
    }

    component Badge: Rectangle {
        id: badge

        required property var modelData
        // Widest the badge may grow; set where it is created
        property real maximumWidth: 0
        readonly property string glyph: iconGlyph(modelData.icon || "")
        readonly property string logo: modelData.ratingSource
                                       ? ratingLogo(modelData.ratingSource, modelData.score) : ""
        readonly property real padding: Math.round(10 * Theme.layoutScale)
        // Width of the logo or icon ahead of the label, with its gap
        readonly property real leadWidth: (logoImage.visible ? logoImage.width + content.spacing : 0)
                                          + (glyphText.visible ? glyphText.implicitWidth + content.spacing : 0)
        // Width without eliding; independent of maximumWidth
        readonly property real naturalWidth: padding * 2 + leadWidth + labelText.implicitWidth

        implicitHeight: Math.round(34 * Theme.layoutScale)
        implicitWidth: content.implicitWidth + padding * 2
        width: implicitWidth
        height: implicitHeight
        radius: Theme.radiusSmall
        color: Theme.overlayTextBackground
        border.width: Theme.borderWidth
        border.color: Theme.overlayTextBorder

        function iconGlyph(icon) {
            switch (icon) {
            case "monitor": return Icons.monitor
            case "hdr": return Icons.hdrOn
            case "audio": return Icons.volumeUp
            case "surround": return Icons.surroundSound
            case "film": return Icons.movie
            case "aspectRatio": return Icons.aspectRatio
            case "languages": return Icons.translate
            case "subtitles": return Icons.subtitles
            case "shield": return Icons.shield
            case "group": return Icons.group
            case "clock": return Icons.schedule
            case "globe": return Icons.language
            case "business": return Icons.business
            case "tv": return Icons.tvShows
            }
            return ""
        }

        function ratingLogo(source, score) {
            const value = parseFloat(score) || 0
            if (source === "imdb") return "qrc:/images/ratings/imdb.png"
            if (source === "tmdb") return "qrc:/images/ratings/tmdb.png"
            if (source === "tomatoes") {
                if (value < 60) return "qrc:/images/ratings/tomatoes_rotten.png"
                if (value >= 75) return "qrc:/images/ratings/tomatoes_certified.png"
                return "qrc:/images/ratings/tomatoes.png"
            }
            if (source === "audience") {
                return value < 60 ? "qrc:/images/ratings/audience_rotten.png"
                                  : "qrc:/images/ratings/audience.png"
            }
            return ""
        }

        Row {
            id: content
            anchors.verticalCenter: parent.verticalCenter
            x: badge.padding
            spacing: Math.round(6 * Theme.layoutScale)

            Image {
                id: logoImage
                visible: badge.logo !== ""
                anchors.verticalCenter: parent.verticalCenter
                height: Math.round(18 * Theme.layoutScale)
                width: visible ? Math.round(implicitWidth * height / Math.max(1, implicitHeight)) : 0
                source: badge.logo
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
            }

            Text {
                id: glyphText
                visible: badge.glyph !== "" && badge.logo === ""
                anchors.verticalCenter: parent.verticalCenter
                text: badge.glyph
                font.family: Theme.fontIcon
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textOnDarkOverlay
            }

            Text {
                id: labelText
                anchors.verticalCenter: parent.verticalCenter
                // A rating logo already names its source.
                text: badge.logo !== "" ? (badge.modelData.score || "") : (badge.modelData.text || "")
                // Long names (studios, editions) elide within half the poster.
                width: Math.min(implicitWidth,
                                Math.max(0, badge.maximumWidth - badge.padding * 2 - badge.leadWidth))
                elide: Text.ElideRight
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightStrong
                color: Theme.textOnDarkOverlay
            }
        }
    }

    // Widest badge in a corner before eliding
    function widestBadge(stack) {
        let widest = 0
        for (let i = 0; i < stack.children.length; ++i) {
            const child = stack.children[i]
            if (child.naturalWidth !== undefined)
                widest = Math.max(widest, child.naturalWidth)
        }
        return widest
    }

    Column {
        id: topLeftStack
        readonly property real naturalWidth: root.widestBadge(topLeftStack)
        readonly property real badgeWidthLimit: root.cornerWidth(topRightStack.naturalWidth)
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: Math.max(root.inset, root.topLeftReserved)
        anchors.leftMargin: root.inset
        spacing: Theme.spacingXSmall

        Repeater {
            model: root.badgesAt("topLeft")
            delegate: Badge { maximumWidth: topLeftStack.badgeWidthLimit }
        }
    }

    Column {
        id: topRightStack
        readonly property real naturalWidth: root.widestBadge(topRightStack)
        readonly property real badgeWidthLimit: root.cornerWidth(topLeftStack.naturalWidth)
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Math.max(root.inset, root.topRightReserved)
        anchors.rightMargin: root.inset
        spacing: Theme.spacingXSmall

        Repeater {
            model: root.badgesAt("topRight")
            delegate: Badge {
                maximumWidth: topRightStack.badgeWidthLimit
                anchors.right: parent ? parent.right : undefined
            }
        }
    }

    Column {
        id: bottomLeftStack
        readonly property real naturalWidth: root.widestBadge(bottomLeftStack)
        readonly property real badgeWidthLimit: root.cornerWidth(bottomRightStack.naturalWidth)
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: Math.max(root.inset, root.bottomLeftReserved)
        anchors.leftMargin: root.inset
        spacing: Theme.spacingXSmall

        Repeater {
            model: root.badgesAt("bottomLeft")
            delegate: Badge { maximumWidth: bottomLeftStack.badgeWidthLimit }
        }
    }

    Column {
        id: bottomRightStack
        readonly property real naturalWidth: root.widestBadge(bottomRightStack)
        readonly property real badgeWidthLimit: root.cornerWidth(bottomLeftStack.naturalWidth)
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.bottomMargin: Math.max(root.inset, root.bottomRightReserved)
        anchors.rightMargin: root.inset
        spacing: Theme.spacingXSmall

        Repeater {
            model: root.badgesAt("bottomRight")
            delegate: Badge {
                maximumWidth: bottomRightStack.badgeWidthLimit
                anchors.right: parent ? parent.right : undefined
            }
        }
    }
}
