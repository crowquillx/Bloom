import QtQuick
import QtQuick.Effects

import BloomUI

/**
 * MediaCard - Bloom's one card for media artwork.
 *
 * variant:
 *   "poster"     2:3 artwork with the title and subtitle in a caption below.
 *   "landscape"  16:9 artwork with the title set on a bottom scrim inside the
 *                art and playback progress along its lower edge. A loaded
 *                `logoSource` replaces the title text.
 *
 * Sizes default to the Theme card tokens (fixed design units), so a card
 * covers the same fraction of the screen everywhere. Grids may override
 * `artworkWidth` to fill their columns. Focus lifts the card (Theme.focusScale),
 * draws the shared FocusRing and a soft shadow; selection is never shown here.
 *
 * Set `menuEnabled` when the card has an actions menu: right-click and
 * press-and-hold then emit `menuRequested` (the keyboard and controller
 * reach the same menu through the caller's `nav.menu` handling).
 *
 * Callers own the model and key handling. Typical use in a ListView delegate:
 *   MediaCard {
 *       variant: "poster"
 *       artworkSources: [posterUrl]
 *       title: modelData.name
 *       subtitle: modelData.productionYear
 *       focused: ListView.isCurrentItem && ListView.view.activeFocus
 *       onClicked: openItem(modelData)
 *   }
 */
Item {
    id: root

    property string variant: "poster"
    /// Artwork URLs in fallback order; the next is tried when one fails.
    property var artworkSources: []
    property string title: ""
    property string subtitle: ""
    /// Landscape only: title logo drawn in place of the title text. The text
    /// stays while the logo is missing or fails to load.
    property string logoSource: ""
    /// Glyph for the placeholder shown while artwork is missing or loading.
    property string placeholderIcon: Icons.movie
    property bool focused: false
    /// Dim artwork for unavailable items (e.g. a missing episode).
    property bool muted: false

    // Playback progress (milliseconds); drawn when position > 0.
    property real positionMs: 0
    property real durationMs: 0

    // Badges. `item` is the canonical media map for server poster badges.
    property var item: null
    property int unplayedCount: 0
    property bool watched: false
    /// Show the unplayed count / watched check (series and movies).
    property bool showWatchState: false
    /// Owning source in merged spaces.
    property string sourceLabel: ""

    property int artworkWidth: variant === "landscape" ? Theme.landscapeCardWidth : Theme.posterCardWidth
    property int artworkHeight: variant === "landscape" ? Math.round(artworkWidth * 9 / 16)
                                                       : Math.round(artworkWidth * 3 / 2)
    readonly property bool hasCaption: variant === "poster"
    readonly property bool hovered: InputModeManager.pointerActive && mouseArea.containsMouse
    readonly property bool lifted: focused || hovered
    readonly property alias artworkStatus: artwork.status

    // Decode size for the artwork; pre-rounded variants are generated at it.
    readonly property int decodeWidth: variant === "landscape" ? 960 : 640
    readonly property int decodeHeight: variant === "landscape" ? 540 : 960
    readonly property string roundedImageMode: typeof ConfigManager !== "undefined"
                                               ? (ConfigManager.roundedImageMode || "auto") : "auto"
    readonly property bool roundedPreprocessEnabled: typeof ConfigManager !== "undefined"
                                                     && ConfigManager.roundedImagePreprocessEnabled
    property string preRoundedSource: ""

    // `keepCurrent` keeps an existing variant while asking again, so a
    // completion for another card's variant never blanks this one.
    function refreshPreRounded(keepCurrent) {
        if (!keepCurrent)
            preRoundedSource = ""
        if (roundedImageMode === "shader" || !roundedPreprocessEnabled)
            return
        if (typeof ImageCacheProvider === "undefined" || !ImageCacheProvider
                || typeof ImageCacheProvider.requestRoundedImage !== "function")
            return
        var base = artwork.currentSource
        if (!base)
            return
        // The radius is in decoded pixels, so scale it to match the on-screen radius.
        var radius = Math.round(Theme.radiusArtwork * decodeWidth / Math.max(1, artworkWidth))
        var fileUrl = ImageCacheProvider.requestRoundedImage(base, radius, decodeWidth, decodeHeight) || ""
        if (fileUrl !== "" || !keepCurrent)
            preRoundedSource = fileUrl
    }

    onRoundedImageModeChanged: refreshPreRounded()
    onRoundedPreprocessEnabledChanged: refreshPreRounded()

    Connections {
        target: typeof ImageCacheProvider !== "undefined" ? ImageCacheProvider : null
        ignoreUnknownSignals: true
        // Variants are keyed by radius and size, and cards of different
        // sizes share artwork URLs. Ask again for this card's own variant
        // rather than taking whichever one finished.
        function onRoundedImageReady(url, fileUrl) {
            if (url && fileUrl && url === artwork.currentSource)
                root.refreshPreRounded(true)
        }
    }

    signal clicked()
    signal menuRequested()

    property bool menuEnabled: false

    implicitWidth: artworkWidth
    implicitHeight: artworkHeight + (hasCaption ? Theme.cardCaptionHeight : 0)
    width: implicitWidth
    height: implicitHeight
    z: focused ? 2 : 0

    Accessible.role: Accessible.Button
    Accessible.name: subtitle !== "" ? title + ", " + subtitle : title
    Accessible.focusable: true

    Item {
        id: frame
        width: root.artworkWidth
        height: root.artworkHeight
        scale: root.focused ? Theme.focusScale : 1.0
        // Lift from the centre so the focus ring never reaches the shelf header.
        transformOrigin: Item.Center
        Behavior on scale {
            enabled: Theme.uiAnimationsEnabled
            NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEmphasized }
        }

        // Lift shadow under the focused card.
        Rectangle {
            id: shadowSource
            anchors.fill: parent
            radius: Theme.radiusArtwork
            color: Theme.elevationShadow
            visible: root.focused && GraphicsInfo.api !== GraphicsInfo.Software
            layer.enabled: visible
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: Theme.elevationShadow
                shadowBlur: 1.0
                shadowVerticalOffset: Math.round(14 * Theme.layoutScale)
                autoPaddingEnabled: true
            }
        }

        ArtworkPlaceholder {
            anchors.fill: parent
            visible: artwork.status !== Image.Ready
            loading: artwork.status === Image.Loading
            iconGlyph: root.placeholderIcon
            title: root.hasCaption ? "" : root.title
        }

        // Hybrid rounded artwork (docs/images.md): cached pre-rounded PNGs
        // when available, otherwise the shader or software path.
        RoundedImage {
            id: artwork
            property int candidateIndex: 0
            readonly property var candidates: (root.artworkSources || []).filter(function(url) { return !!url })
            readonly property string currentSource: candidates.length > 0
                                                    ? candidates[Math.min(candidateIndex, candidates.length - 1)] : ""
            anchors.fill: parent
            source: currentSource
            preRoundedSource: root.preRoundedSource
            mode: root.roundedImageMode
            preferPreRounded: root.roundedPreprocessEnabled
            allowShader: root.roundedImageMode !== "prerender"
            radius: Theme.radiusArtwork
            sourceWidth: root.decodeWidth
            sourceHeight: root.decodeHeight
            fillMode: Image.PreserveAspectCrop
            visible: ready
            opacity: root.muted ? 0.45 : 1.0

            onCandidatesChanged: candidateIndex = 0
            onCurrentSourceChanged: root.refreshPreRounded()
            onStatusChanged: {
                if (status === Image.Error && candidateIndex + 1 < candidates.length)
                    candidateIndex++
            }
        }

        // Hairline edge so dark art separates from a dark stage.
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusArtwork
            color: "transparent"
            border.width: Theme.borderWidth
            border.color: root.lifted ? Theme.surfaceBorderStrong : Theme.surfaceBorder
            antialiasing: true
        }

        // Landscape: title on a bottom scrim inside the art.
        Rectangle {
            visible: !root.hasCaption && (root.title !== "" || progress.visible)
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.height * 0.64
            radius: Theme.radiusArtwork
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.withAlpha(Theme.overlayTextBackground, 0) }
                GradientStop { position: 0.5; color: Theme.withAlpha(Theme.overlayTextBackground, 0.52) }
                GradientStop { position: 1.0; color: Theme.withAlpha(Theme.overlayTextBackground, 0.86) }
            }
        }

        Column {
            visible: !root.hasCaption
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: progress.visible ? progress.top : parent.bottom
            anchors.leftMargin: Theme.spacingMedium + Theme.spacingXSmall
            anchors.rightMargin: Theme.spacingMedium + Theme.spacingXSmall
            anchors.bottomMargin: progress.visible ? -Theme.spacingXSmall : Theme.spacingMedium
            spacing: Math.round(2 * Theme.layoutScale)

            ArtworkLogo {
                id: titleLogo
                // fanart.tv ClearArt and TV thumbs share a 16:9 (1000x562)
                // canvas and are sometimes served as the logo; they carry
                // characters, not just the title. Title logos are almost never
                // 16:9, so such an image keeps the text title.
                readonly property bool notATitleLogo: status === Image.Ready && implicitHeight > 0
                                                     && Math.abs(implicitWidth / implicitHeight - 16 / 9) < 0.02
                // Room for the logo is kept while it loads so the card does not
                // flash the text title first.
                visible: source.toString() !== "" && status !== Image.Error && !notATitleLogo
                opacity: status === Image.Ready ? 1 : 0
                width: Math.round(parent.width * 0.62)
                height: Math.round(root.artworkHeight * 0.3)
                source: root.hasCaption ? "" : root.logoSource
                sourceSize.width: Math.round(width * 2)
                sourceSize.height: Math.round(height * 2)
                horizontalAlignment: Image.AlignLeft
                verticalAlignment: Image.AlignBottom
                smooth: true
                mipmap: true
                Behavior on opacity {
                    enabled: Theme.uiAnimationsEnabled
                    NumberAnimation { duration: Theme.durationNormal }
                }
            }

            Text {
                width: parent.width
                text: root.title
                visible: text !== "" && !titleLogo.visible
                font.pixelSize: Theme.fontSizeBody
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightHeavy
                color: Theme.textOnDarkOverlay
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Text {
                width: parent.width
                visible: text !== ""
                text: root.subtitle
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightMedium
                color: Theme.withAlpha(Theme.textOnDarkOverlay, 0.80)
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }

        MediaProgressBar {
            id: progress
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            barMargin: Theme.spacingMedium
            positionMs: root.positionMs
            durationMs: root.durationMs
        }

        SourceBadge {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: Theme.spacingSmall
            maximumWidth: parent.width - Theme.spacingSmall * 2
            text: root.sourceLabel
        }

        PosterBadgeOverlay {
            anchors.fill: parent
            visible: root.item !== null && badges.length > 0
            item: root.item || ({})
            topRightReserved: watchBadge.visible ? watchBadge.height : 0
        }

        UnwatchedBadge {
            id: watchBadge
            anchors.top: parent.top
            anchors.right: parent.right
            parentWidth: parent.width
            applicable: root.showWatchState
            count: root.unplayedCount
            isFullyWatched: root.watched
        }

        FocusRing {
            active: root.focused
            radius: Theme.radiusArtwork
        }
    }

    // Poster caption below the art.
    Column {
        visible: root.hasCaption
        anchors.top: frame.bottom
        anchors.topMargin: Theme.spacingMedium
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Math.round(4 * Theme.layoutScale)

        Text {
            width: parent.width
            text: root.title
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightStrong
            color: Theme.textPrimary
            opacity: root.lifted ? 1.0 : 0.88
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        Text {
            width: parent.width
            visible: text !== ""
            text: root.subtitle
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightMedium
            color: Theme.textSecondary
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: root.menuEnabled ? (Qt.LeftButton | Qt.RightButton) : Qt.LeftButton
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton)
                root.menuRequested()
            else
                root.clicked()
        }
        // Unaccepted, a long press still ends in clicked().
        onPressAndHold: function(mouse) {
            mouse.accepted = root.menuEnabled
            if (root.menuEnabled)
                root.menuRequested()
        }
    }
}
