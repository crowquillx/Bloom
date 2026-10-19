import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import BloomUI

FocusScope {
    id: root

    property var heroModel: []
    property int currentIndex: 0
    property bool scrolling: false
    property bool actionsFocused: false

    // Hero item transition state. Commits happen mid-transition via a ScriptAction
    // so logo/title/badge/metadata/buttons update while content is invisible.
    property int pendingIndex: 0
    property int pendingDirection: 0
    property real contentOpacity: 1.0
    property real contentSlideX: 0
    property bool committingPendingIndex: false
    readonly property real contentSlideOffset: Theme.spacingLarge

    readonly property bool hasContent: heroModel && heroModel.length > 0
    readonly property var currentItem: hasContent ? heroModel[Math.min(currentIndex, heroModel.length - 1)] : null
    readonly property int backdropIndex: heroTransition.running ? pendingIndex : currentIndex
    readonly property var backdropItem: hasContent ? heroModel[Math.min(backdropIndex, heroModel.length - 1)] : null
    readonly property string currentBackdropUrl: imageUrl(backdropItem, "Backdrop", Math.round(width * 2))
    readonly property string currentLogoUrl: logoImageUrl(currentItem)
    readonly property bool buttonsFocused: contentLoader.item
                                         && contentLoader.item.playButton
                                         && (contentLoader.item.playButton.activeFocus
                                             || contentLoader.item.detailsButton.activeFocus)
    readonly property string logoPlacement: ConfigManager.heroBannerLogoPlacement
    readonly property string infoPlacement: ConfigManager.heroBannerInfoPlacement
    readonly property bool combinedLayout: logoPlacement === infoPlacement
    /// Cardless "floating cinema" presentation: the hero artwork is the page
    /// backdrop (backdrop sync) and the hero content floats over it. When
    /// false the hero draws its own artwork inside a rounded panel.
    property bool cinematic: false
    readonly property bool carouselFocused: activeFocus && !actionsFocused && !buttonsFocused
    readonly property real contentEdgeMargin: cinematic ? 0 : Theme.spacingXLarge + Theme.spacingSmall

    signal detailsRequested(var item)
    signal playRequested(var item)
    signal moveDownRequested()

    implicitHeight: Math.round(width * 0.30)
    Accessible.role: Accessible.Button
    Accessible.name: {
        var title = primaryTitle(currentItem)
        var subtitle = episodeSubtitle(currentItem)
        return subtitle ? (title + ", " + subtitle) : (title || qsTr("Featured item"))
    }
    Accessible.description: reasonText(currentItem)

    function artworkUrlFromRef(artwork, width) {
        if (!artwork || !artwork.itemId)
            return ""
        return LibraryService.getCachedArtworkUrlFromRef(artwork, width || 0)
    }

    function imageUrl(item, type, width) {
        if (!item) return ""
        if (type === "Backdrop")
            return artworkUrlFromRef(item.backdropArtwork, width)
        if (type === "Logo")
            return logoImageUrl(item, width)
        if (type === "Primary")
            return artworkUrlFromRef(item.primaryArtwork, width)
        if (type === "Thumb")
            return artworkUrlFromRef(item.thumbArtwork, width)
        return ""
    }

    function logoImageUrl(item, width) {
        if (!item) return ""
        if (item.logoArtwork && item.logoArtwork.itemId)
            return artworkUrlFromRef(item.logoArtwork, width || 600)
        // Episodes historically resolved Logo via the parent series id; keep that when the
        // episode itself has no logo ArtworkRef (series logo is not mapped on episodes).
        if (item.mediaType === "Episode" && item.seriesId) {
            return LibraryService.getCachedArtworkUrlForConnection(
                        item.connectionId || "",
                        item.seriesId,
                        "logo",
                        0,
                        "",
                        width || 600)
        }
        return ""
    }

    function primaryTitle(item) {
        if (!item) return ""
        if (item.mediaType === "Episode")
            return item.seriesName || item.name || ""
        return item.name || item.seriesName || ""
    }

    function episodeSubtitle(item) {
        if (!item || item.mediaType !== "Episode") return ""
        var episodeName = item.name || ""
        if (!episodeName) return ""
        var seriesName = item.seriesName || ""
        if (episodeName === seriesName) return ""
        return episodeName
    }

    function reasonText(item) {
        return synopsisText(item)
    }

    function badgeText(item) { return item ? qsTr(item.__heroReason || "") : "" }
    function synopsisText(item) {
        if (!item) return ""
        if (item.mediaType === "Episode") {
            var episodeOverview = item.overview || ""
            if (ConfigManager.heroBannerEpisodeSynopsisEnabled && episodeOverview !== "")
                return episodeOverview
            return item.__seriesOverview || ""
        }
        return item.overview || ""
    }
    function playLabel(item) {
        if (!item) return qsTr("Play")
        var resume = (item.positionMs || 0) > 0 || item.__heroReason === "Continue Watching"
        var code = ""
        if (item.mediaType === "Episode" && item.parentIndexNumber >= 0 && item.indexNumber >= 0)
            code = " S" + item.parentIndexNumber + " E" + item.indexNumber
        return (resume ? qsTr("Resume") : qsTr("Play")) + code
    }
    function progressFraction(item) {
        if (!item || !(item.durationMs > 0) || !(item.positionMs > 0)) return 0
        return Math.min(1, item.positionMs / item.durationMs)
    }
    function remainingText(item) {
        if (!item || !(item.durationMs > 0)) return ""
        var minutes = Math.max(1, Math.round((item.durationMs - (item.positionMs || 0)) / 60000))
        return minutes >= 60
               ? qsTr("%1h %2m left").arg(Math.floor(minutes / 60)).arg(minutes % 60)
               : qsTr("%1 min left").arg(minutes)
    }
    function formatRuntime(durationMs) {
        if (!durationMs) return ""
        var minutes = Math.round(durationMs / 60000)
        return minutes >= 60 ? Math.floor(minutes / 60) + "h " + (minutes % 60) + "m" : minutes + "m"
    }
    function episodeText(item) {
        if (!item || item.mediaType !== "Episode") return ""
        var season = (item.parentIndexNumber >= 0) ? item.parentIndexNumber : 0
        var episode = (item.indexNumber >= 0) ? item.indexNumber : 0
        return "S" + String(season).padStart(2, "0")
             + "E" + String(episode).padStart(2, "0")
    }
    function transitionToIndex(newIndex, direction) {
        if (heroTransition.running) {
            if (newIndex === pendingIndex) return
            heroTransition.stop()
            contentOpacity = 1.0
            contentSlideX = 0
        }
        if (newIndex === currentIndex) return
        pendingDirection = direction
        pendingIndex = newIndex
        heroTransition.start()
    }

    function cycle(delta) {
        if (!hasContent || heroModel.length < 2) return
        transitionToIndex((currentIndex + delta + heroModel.length) % heroModel.length, delta)
        cycleTimer.stop()
        manualPause.restart()
    }
    function focusButtons() {
        actionsFocused = true
        Qt.callLater(function() {
            if (!root.activeFocus) {
                root.actionsFocused = false
                return
            }
            var host = contentLoader.item
            if (!host || !host.playButton) {
                root.actionsFocused = false
                return
            }
            if (host.playButton.visible)
                host.playButton.forceActiveFocus()
            else if (host.detailsButton)
                host.detailsButton.forceActiveFocus()
            else
                root.actionsFocused = false
        })
    }
    function exitActions() {
        actionsFocused = false
        root.forceActiveFocus()
    }
    function resetToCarousel() {
        actionsFocused = false
    }

    readonly property real carouselBottomInset: heroModel.length > 1
                                               ? Theme.spacingLarge + Theme.spacingSmall
                                               : 0

    function reapplyHeroPlacements() {
        if (!contentLoader.item)
            return
        var layer = contentLoader.item
        for (var i = 0; i < layer.children.length; ++i) {
            var child = layer.children[i]
            if (child && child.applyPlacement)
                child.applyPlacement()
        }
    }

    onLogoPlacementChanged: Qt.callLater(reapplyHeroPlacements)
    onInfoPlacementChanged: Qt.callLater(reapplyHeroPlacements)

    component HeroLogoSection: ColumnLayout {
        id: logoSection
        property string blockPlacement: root.logoPlacement
        readonly property bool alignRight: blockPlacement === "bottomRight" || blockPlacement === "topRight"
        readonly property bool alignCenter: blockPlacement === "bottomCenter" || blockPlacement === "topCenter"
                || blockPlacement === "center" || blockPlacement === "centerLarge"
        readonly property int layoutAlignment: alignRight ? Qt.AlignRight
                                             : (alignCenter ? Qt.AlignHCenter : Qt.AlignLeft)
        spacing: Theme.spacingSmall
        Layout.fillWidth: !alignRight && !alignCenter
        Layout.alignment: layoutAlignment

        Item {
            Layout.fillWidth: !alignRight && !alignCenter
            Layout.alignment: logoSection.layoutAlignment
            Layout.preferredWidth: alignRight || alignCenter ? Theme.seriesLogoMaxWidth : -1
            Layout.preferredHeight: heroLogoImage.visible && heroLogoImage.status !== Image.Error
                                    ? heroLogoImage.height
                                    : titleFallback.visible
                                      ? titleFallback.implicitHeight
                                      : 0

            ArtworkLogo {
                id: heroLogoImage
                width: Math.min(Theme.seriesLogoMaxWidth, parent.width > 0 ? parent.width : Theme.seriesLogoMaxWidth)
                height: root.logoPlacement === "centerLarge"
                        ? Math.round(Theme.detailViewLogoHeight * 1.35)
                        : Math.round(Theme.detailViewLogoHeight * 1.15)
                anchors.left: logoSection.alignRight || logoSection.alignCenter ? undefined : parent.left
                anchors.right: logoSection.alignRight ? parent.right : undefined
                anchors.horizontalCenter: logoSection.alignCenter ? parent.horizontalCenter : undefined
                source: root.currentLogoUrl
                fillMode: Image.PreserveAspectFit
                horizontalAlignment: logoSection.alignRight ? Image.AlignRight
                                     : (logoSection.alignCenter ? Image.AlignHCenter : Image.AlignLeft)
                asynchronous: true
                cache: true
                visible: source !== "" && status !== Image.Error
                opacity: status === Image.Ready ? 1.0 : 0.0
                Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }
            }

            // Missing logo: the title is set large in the logo slot.
            Text {
                id: titleFallback
                width: parent.width
                anchors.left: logoSection.alignRight || logoSection.alignCenter ? undefined : parent.left
                anchors.right: logoSection.alignRight ? parent.right : undefined
                anchors.horizontalCenter: logoSection.alignCenter ? parent.horizontalCenter : undefined
                text: root.primaryTitle(root.currentItem)
                visible: root.currentLogoUrl === "" || heroLogoImage.status === Image.Error
                font.pixelSize: Theme.fontSizeHero
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightHeavy
                font.letterSpacing: -Math.round(0.5 * Theme.layoutScale)
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeight: 0.95
                horizontalAlignment: logoSection.alignRight ? Text.AlignRight
                                     : (logoSection.alignCenter ? Text.AlignHCenter : Text.AlignLeft)
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: logoSection.layoutAlignment
            text: root.episodeSubtitle(root.currentItem)
            visible: text.length > 0
            font.pixelSize: Theme.fontSizeMedium
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightStrong
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            horizontalAlignment: logoSection.alignRight ? Text.AlignRight
                                 : (logoSection.alignCenter ? Text.AlignHCenter : Text.AlignLeft)
        }
    }

    // Why this item is featured, set as a quiet eyebrow rather than a pill.
    component HeroBadge: Text {
        property int layoutBlockAlignment: Qt.AlignLeft
        Layout.alignment: layoutBlockAlignment
        text: root.badgeText(root.currentItem)
        visible: text !== ""
        font.pixelSize: Theme.fontSizeCaption
        font.family: Theme.fontPrimary
        font.weight: Theme.fontWeightHeavy
        font.capitalization: Font.AllUppercase
        font.letterSpacing: Math.round(2 * Theme.layoutScale)
        color: Theme.accentPrimary
    }

    component HeroMetadataRow: MetadataLine {
        property int layoutBlockAlignment: Qt.AlignLeft
        Layout.alignment: layoutBlockAlignment
        Layout.maximumWidth: parent ? parent.width : implicitWidth
        font.pixelSize: Theme.fontSizeBody
        color: Theme.textPrimary
        opacity: 0.86
        items: root.currentItem ? [
            String(root.currentItem.productionYear || ""),
            root.episodeText(root.currentItem),
            root.formatRuntime(root.currentItem.durationMs),
            root.currentItem.officialRating || ""
        ] : []
    }

    component HeroSynopsisText: Text {
        property int layoutBlockAlignment: Qt.AlignLeft
        Layout.alignment: layoutBlockAlignment
        Layout.fillWidth: layoutBlockAlignment === Qt.AlignLeft
        Layout.preferredWidth: parent ? parent.width : implicitWidth
        text: root.synopsisText(root.currentItem)
        visible: text.length > 0
        font.pixelSize: Theme.fontSizeBody
        font.family: Theme.fontPrimary
        color: Theme.textPrimary
        opacity: 0.86
        wrapMode: Text.WordWrap
        maximumLineCount: 3
        elide: Text.ElideRight
        lineHeight: 1.12
        horizontalAlignment: layoutBlockAlignment === Qt.AlignRight ? Text.AlignRight
                             : (layoutBlockAlignment === Qt.AlignHCenter ? Text.AlignHCenter : Text.AlignLeft)
    }

    component HeroActionsRow: ColumnLayout {
        id: actionsColumn
        property int layoutBlockAlignment: Qt.AlignLeft
        Layout.alignment: layoutBlockAlignment
        Layout.topMargin: Theme.spacingSmall
        spacing: Theme.spacingMedium
        property Item playButtonRef: playBtn
        property Item detailsButtonRef: detailsBtn

        RowLayout {
            Layout.alignment: actionsColumn.layoutBlockAlignment
            spacing: Theme.controlSpacing

            ActionButton {
                id: playBtn
                variant: "primary"
                visible: root.currentItem && root.currentItem.mediaType !== "Series"
                focusPolicy: root.actionsFocused ? Qt.StrongFocus : Qt.NoFocus
                implicitHeight: Theme.buttonHeightLarge
                text: root.playLabel(root.currentItem)
                iconGlyph: Icons.playArrow
                onClicked: root.playRequested(root.currentItem)
                Keys.onRightPressed: function(event) { detailsBtn.forceActiveFocus(); event.accepted = true }
                Keys.onLeftPressed: function(event) { event.accepted = true }
                Keys.onUpPressed: function(event) { root.exitActions(); event.accepted = true }
                Keys.onEscapePressed: function(event) { root.exitActions(); event.accepted = true }
                Keys.onDownPressed: function(event) {
                    root.resetToCarousel()
                    root.moveDownRequested()
                    event.accepted = true
                }
            }
            ActionButton {
                id: detailsBtn
                variant: playBtn.visible ? "secondary" : "primary"
                focusPolicy: root.actionsFocused ? Qt.StrongFocus : Qt.NoFocus
                implicitHeight: Theme.buttonHeightLarge
                text: qsTr("Details")
                iconGlyph: playBtn.visible ? "" : Icons.info
                onClicked: root.detailsRequested(root.currentItem)
                Keys.onLeftPressed: function(event) {
                    if (playBtn.visible) playBtn.forceActiveFocus()
                    event.accepted = true
                }
                Keys.onRightPressed: function(event) { event.accepted = true }
                Keys.onUpPressed: function(event) { root.exitActions(); event.accepted = true }
                Keys.onEscapePressed: function(event) { root.exitActions(); event.accepted = true }
                Keys.onDownPressed: function(event) {
                    root.resetToCarousel()
                    root.moveDownRequested()
                    event.accepted = true
                }
            }
        }

        // Resume progress under the primary action.
        RowLayout {
            Layout.alignment: actionsColumn.layoutBlockAlignment
            visible: root.progressFraction(root.currentItem) > 0
            spacing: Theme.spacingMedium

            Rectangle {
                Layout.preferredWidth: playBtn.width
                Layout.preferredHeight: Theme.progressHeight
                radius: height / 2
                color: Theme.progressTrack

                Rectangle {
                    width: Math.max(height, parent.width * root.progressFraction(root.currentItem))
                    height: parent.height
                    radius: height / 2
                    color: Theme.progressFill
                }
            }

            Text {
                text: root.remainingText(root.currentItem)
                font.pixelSize: Theme.fontSizeCaption
                font.family: Theme.fontPrimary
                font.weight: Theme.fontWeightMedium
                color: Theme.textSecondary
            }
        }
    }

    Component {
        id: combinedLayoutComponent
        Item {
            anchors.fill: parent
            property alias playButton: combinedHeroActions.playButtonRef
            property alias detailsButton: combinedHeroActions.detailsButtonRef

            HeroBannerLayoutHost {
                id: combinedHost
                positionRoot: heroCard
                placement: root.logoPlacement
                centerLarge: root.logoPlacement === "centerLarge"
                edgeMargin: root.contentEdgeMargin
                extraBottomInset: root.carouselBottomInset
                focusFramed: root.cinematic && root.carouselFocused
                HeroBadge { layoutBlockAlignment: combinedHost.contentLayoutAlignment }
                HeroLogoSection { blockPlacement: root.logoPlacement }
                HeroMetadataRow { layoutBlockAlignment: combinedHost.contentLayoutAlignment }
                HeroSynopsisText { layoutBlockAlignment: combinedHost.contentLayoutAlignment }
                HeroActionsRow {
                    id: combinedHeroActions
                    layoutBlockAlignment: combinedHost.contentLayoutAlignment
                }
            }
        }
    }

    Component {
        id: splitLayoutComponent
        Item {
            anchors.fill: parent
            property alias playButton: splitHeroActions.playButtonRef
            property alias detailsButton: splitHeroActions.detailsButtonRef

            HeroBannerLayoutHost {
                id: logoHost
                positionRoot: heroCard
                placement: root.logoPlacement
                centerLarge: root.logoPlacement === "centerLarge"
                edgeMargin: root.contentEdgeMargin
                extraBottomInset: root.carouselBottomInset
                HeroLogoSection { blockPlacement: root.logoPlacement }
            }

            HeroBannerLayoutHost {
                id: infoHost
                positionRoot: heroCard
                placement: root.infoPlacement
                edgeMargin: root.contentEdgeMargin
                extraBottomInset: root.carouselBottomInset
                focusFramed: root.cinematic && root.carouselFocused
                HeroBadge { layoutBlockAlignment: infoHost.contentLayoutAlignment }
                HeroMetadataRow { layoutBlockAlignment: infoHost.contentLayoutAlignment }
                HeroSynopsisText { layoutBlockAlignment: infoHost.contentLayoutAlignment }
                HeroActionsRow {
                    id: splitHeroActions
                    layoutBlockAlignment: infoHost.contentLayoutAlignment
                }
            }
        }
    }

    onHeroModelChanged: {
        if (heroTransition.running) {
            heroTransition.stop()
            contentOpacity = 1.0
            contentSlideX = 0
        }
        currentIndex = Math.min(currentIndex, Math.max(0, heroModel.length - 1))
    }
    onCurrentIndexChanged: {
        if (heroTransition.running && !committingPendingIndex) {
            pendingIndex = currentIndex
            heroTransition.stop()
            contentOpacity = 1.0
            contentSlideX = 0
        }
    }
    onActiveFocusChanged: {
        if (!activeFocus)
            resetToCarousel()
        else
            ensureVisibleTimer.restart()
    }
    Keys.onLeftPressed: function(event) {
        if (actionsFocused) return
        cycle(-1)
        event.accepted = true
    }
    Keys.onRightPressed: function(event) {
        if (actionsFocused) return
        cycle(1)
        event.accepted = true
    }
    Keys.onDownPressed: function(event) {
        resetToCarousel()
        moveDownRequested()
        event.accepted = true
    }
    Keys.onReturnPressed: function(event) {
        if (!actionsFocused) {
            focusButtons()
            event.accepted = true
        } else if (!buttonsFocused) {
            focusButtons()
            event.accepted = true
        }
    }
    Keys.onEnterPressed: function(event) {
        if (!actionsFocused) {
            focusButtons()
            event.accepted = true
        } else if (!buttonsFocused) {
            focusButtons()
            event.accepted = true
        }
    }
    Keys.onEscapePressed: function(event) {
        if (actionsFocused) { exitActions(); event.accepted = true }
    }

    Timer { id: ensureVisibleTimer; interval: 0 }
    Timer {
        id: cycleTimer
        interval: ConfigManager.heroBannerAutoCycleInterval
        repeat: true
        running: ConfigManager.heroBannerEnabled && ConfigManager.heroBannerAutoCycleEnabled
                 && root.heroModel.length > 1 && !root.activeFocus && !root.buttonsFocused
                 && !root.actionsFocused && !root.scrolling && !PlayerController.isPlaybackActive && !manualPause.running
        onTriggered: root.transitionToIndex((root.currentIndex + 1) % root.heroModel.length, 1)
    }
    Timer { id: manualPause; interval: 3000 }

    // Directional cross-fade content transition. The two halves are each
    // Theme.durationFade / 2, so the swap lands at the midpoint of the in-card
    // backdrop cross-fade (which runs full Theme.durationFade). With animations
    // disabled, all durations are 0 and the swap is instantaneous.
    SequentialAnimation {
        id: heroTransition
        running: false

        ParallelAnimation {
            NumberAnimation { target: root; property: "contentOpacity"; to: 0.0; duration: Theme.durationFade / 2; easing.type: Easing.OutQuad }
            NumberAnimation { target: root; property: "contentSlideX"; to: -root.contentSlideOffset * (root.pendingDirection || 1); duration: Theme.durationFade / 2; easing.type: Easing.OutQuad }
        }
        ScriptAction {
            script: {
                root.committingPendingIndex = true
                root.currentIndex = root.pendingIndex
                root.committingPendingIndex = false
                root.contentSlideX = root.contentSlideOffset * (root.pendingDirection || 1)
            }
        }
        ParallelAnimation {
            NumberAnimation { target: root; property: "contentOpacity"; to: 1.0; duration: Theme.durationFade / 2; easing.type: Easing.InQuad }
            NumberAnimation { target: root; property: "contentSlideX"; to: 0; duration: Theme.durationFade / 2; easing.type: Easing.InQuad }
        }
    }

    onCurrentBackdropUrlChanged: loadCardBackdrop()
    onCinematicChanged: loadCardBackdrop()

    // The card's own artwork is only needed when the hero is not driving the
    // page backdrop.
    function loadCardBackdrop() {
        if (cinematic)
            return
        if (currentBackdropUrl === "") {
            heroCard.clearBackdrop()
            return
        }
        var target = heroCard.showBackdropA ? heroBackdropB : heroBackdropA
        if (target.source.toString() === currentBackdropUrl && target.status === Image.Ready) {
            heroCard.showBackdropA = (target === heroBackdropA)
            heroCard.showBackdropNeutral = false
            return
        }
        target.source = currentBackdropUrl
    }

    Rectangle {
        id: heroCard
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMarginX
        anchors.rightMargin: Theme.pageMarginX
        radius: root.cinematic ? 0 : Theme.radiusPanel
        color: root.cinematic ? "transparent" : Theme.artworkPlaceholder
        antialiasing: true

        property bool showBackdropA: true
        property bool showBackdropNeutral: true

        function clearBackdrop() {
            showBackdropNeutral = true
            if (showBackdropA) {
                heroBackdropB.source = ""
            } else {
                heroBackdropA.source = ""
            }
        }

        function checkBackdropStatus(img) {
            if (img.source.toString() === "") {
                if (root.currentBackdropUrl === "") {
                    clearBackdrop()
                }
                return
            }
            if (img.status === Image.Error) {
                if (img.source.toString() === root.currentBackdropUrl) {
                    clearBackdrop()
                }
                return
            }
            if (img.status !== Image.Ready) return
            // Only switch if this image is the one we last pointed at the current URL.
            if (img.source.toString() !== root.currentBackdropUrl) return
            showBackdropNeutral = false
            showBackdropA = (img === heroBackdropA)
        }

        Item {
            id: cardArtwork
            anchors.fill: parent
            visible: !root.cinematic

            Image {
                id: heroBackdropA
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                opacity: !heroCard.showBackdropNeutral && heroCard.showBackdropA ? 1.0 : 0.0
                Behavior on opacity { NumberAnimation { duration: Theme.durationFade } enabled: Theme.uiAnimationsEnabled }
                onStatusChanged: heroCard.checkBackdropStatus(this)

                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: heroMask
                }
            }

            Image {
                id: heroBackdropB
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                opacity: !heroCard.showBackdropNeutral && !heroCard.showBackdropA ? 1.0 : 0.0
                Behavior on opacity { NumberAnimation { duration: Theme.durationFade } enabled: Theme.uiAnimationsEnabled }
                onStatusChanged: heroCard.checkBackdropStatus(this)

                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: heroMask
                }
            }

            Rectangle {
                id: heroMask
                anchors.fill: parent
                radius: heroCard.radius
                visible: false
                layer.enabled: true
                layer.smooth: true
            }

            // Hold the scrim across the text column so the synopsis stays
            // legible over bright backdrops, then let the artwork through.
            Rectangle {
                anchors.fill: parent
                radius: heroCard.radius
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.92) }
                    GradientStop { position: 0.42; color: Theme.withAlpha(Theme.backgroundPrimary, 0.74) }
                    GradientStop { position: 0.72; color: Theme.withAlpha(Theme.backgroundPrimary, 0.22) }
                    GradientStop { position: 1; color: Theme.withAlpha(Theme.backgroundPrimary, 0.0) }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: parent.height * 0.45
                radius: heroCard.radius
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.0) }
                    GradientStop { position: 1; color: Theme.withAlpha(Theme.backgroundPrimary, 0.55) }
                }
            }
        }

        // Card mode: focus is a ring around the whole panel.
        FocusRing {
            active: !root.cinematic && root.carouselFocused
            radius: heroCard.radius
        }

        Loader {
            id: contentLoader
            anchors.fill: parent
            z: 1
            opacity: root.contentOpacity
            transform: Translate { x: root.contentSlideX }
            sourceComponent: root.combinedLayout ? combinedLayoutComponent : splitLayoutComponent

            onSourceComponentChanged: Qt.callLater(root.reapplyHeroPlacements)
        }

        // Page indicator. Left/Right hints appear while the carousel has focus.
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: root.cinematic ? 0 : Theme.spacingLarge
            anchors.bottomMargin: root.cinematic ? Theme.spacingSmall : Theme.spacingLarge
            spacing: Theme.spacingSmall
            visible: root.heroModel.length > 1
            z: 2

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Icons.chevronLeft
                font.family: Theme.fontIcon
                font.pixelSize: Theme.fontSizeIcon
                color: Theme.textPrimary
                opacity: root.carouselFocused ? 0.9 : 0.0
                Behavior on opacity { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }
            }

            Repeater {
                model: root.heroModel.length
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: index === root.currentIndex ? Theme.spacingLarge : Theme.spacingSmall + Theme.spacingXSmall
                    height: Theme.spacingSmall + Theme.spacingXSmall
                    radius: height / 2
                    color: index === root.currentIndex ? Theme.textPrimary : Theme.withAlpha(Theme.textPrimary, 0.38)
                    Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } enabled: Theme.uiAnimationsEnabled }
                    Behavior on color { ColorAnimation { duration: Theme.durationNormal } enabled: Theme.uiAnimationsEnabled }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Icons.chevronRight
                font.family: Theme.fontIcon
                font.pixelSize: Theme.fontSizeIcon
                color: Theme.textPrimary
                opacity: root.carouselFocused ? 0.9 : 0.0
                Behavior on opacity { NumberAnimation { duration: Theme.durationShort } enabled: Theme.uiAnimationsEnabled }
            }
        }
    }
}
