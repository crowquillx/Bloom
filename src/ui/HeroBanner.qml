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

    signal detailsRequested(var item)
    signal playRequested(var item)
    signal moveDownRequested()

    implicitHeight: Math.round(Math.max(310, width * 0.30))
    Accessible.role: Accessible.Button
    Accessible.name: {
        var title = primaryTitle(currentItem)
        var subtitle = episodeSubtitle(currentItem)
        return subtitle ? (title + ", " + subtitle) : (title || qsTr("Featured item"))
    }
    Accessible.description: reasonText(currentItem)

    function imageUrl(item, type, width) {
        if (!item) return ""
        var id = item.Id
        if (type === "Backdrop" && item.ParentBackdropImageTags && item.ParentBackdropImageTags.length > 0
                && !(item.BackdropImageTags && item.BackdropImageTags.length > 0)) {
            id = item.ParentBackdropItemId || item.SeriesId || id
        }
        return id ? LibraryService.getCachedImageUrlWithWidth(id, type, width) : ""
    }

    function logoItemId(item) {
        if (!item) return ""
        if (item.Type === "Episode" && item.SeriesId)
            return item.SeriesId
        return item.Id || ""
    }

    function logoImageUrl(item, width) {
        var id = logoItemId(item)
        if (!id) return ""
        return LibraryService.getCachedImageUrlWithWidth(id, "Logo", width || 600)
    }

    function primaryTitle(item) {
        if (!item) return ""
        if (item.Type === "Episode")
            return item.SeriesName || item.Name || ""
        return item.Name || item.SeriesName || ""
    }

    function episodeSubtitle(item) {
        if (!item || item.Type !== "Episode") return ""
        var episodeName = item.Name || ""
        if (!episodeName) return ""
        var seriesName = item.SeriesName || ""
        if (episodeName === seriesName) return ""
        return episodeName
    }

    function reasonText(item) {
        return ""
    }

    function badgeText(item) { return item ? qsTr(item.__heroReason || "") : "" }
    function formatRuntime(ticks) {
        if (!ticks) return ""
        var minutes = Math.round(ticks / 600000000)
        return minutes >= 60 ? Math.floor(minutes / 60) + "h " + (minutes % 60) + "m" : minutes + "m"
    }
    function episodeText(item) {
        if (!item || item.Type !== "Episode") return ""
        return "S" + String(item.ParentIndexNumber || 0).padStart(2, "0")
             + "E" + String(item.IndexNumber || 0).padStart(2, "0")
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
            Layout.preferredWidth: alignRight || alignCenter ? Theme.seriesLogoMaxWidth : undefined
            Layout.preferredHeight: heroLogoImage.visible && heroLogoImage.status !== Image.Error
                                    ? heroLogoImage.height
                                    : titleFallback.visible
                                      ? titleFallback.implicitHeight
                                      : 0

            Image {
                id: heroLogoImage
                width: Math.min(Theme.seriesLogoMaxWidth, parent.width > 0 ? parent.width : Theme.seriesLogoMaxWidth)
                height: root.logoPlacement === "centerLarge"
                        ? Math.round(Theme.detailViewLogoHeight * 1.35)
                        : Theme.detailViewLogoHeight
                anchors.left: logoSection.alignRight || logoSection.alignCenter ? undefined : parent.left
                anchors.right: logoSection.alignRight ? parent.right : undefined
                anchors.horizontalCenter: logoSection.alignCenter ? parent.horizontalCenter : undefined
                source: root.currentLogoUrl
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                visible: source !== "" && status !== Image.Error
                opacity: status === Image.Ready ? 1.0 : 0.0
                Behavior on opacity { NumberAnimation { duration: Theme.durationFade } }
            }

            Text {
                id: titleFallback
                width: parent.width
                anchors.left: logoSection.alignRight || logoSection.alignCenter ? undefined : parent.left
                anchors.right: logoSection.alignRight ? parent.right : undefined
                anchors.horizontalCenter: logoSection.alignCenter ? parent.horizontalCenter : undefined
                text: root.primaryTitle(root.currentItem)
                visible: root.currentLogoUrl === "" || heroLogoImage.status === Image.Error
                font.pixelSize: Theme.fontSizeDisplay
                font.family: Theme.fontPrimary
                font.bold: true
                color: Theme.textPrimary
                style: Text.Outline
                styleColor: "#000000"
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                horizontalAlignment: logoSection.alignRight ? Text.AlignRight
                                     : (logoSection.alignCenter ? Text.AlignHCenter : Text.AlignLeft)
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: logoSection.layoutAlignment
            text: root.episodeSubtitle(root.currentItem)
            visible: text.length > 0
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: Theme.textSecondary
            style: Text.Outline
            styleColor: "#000000"
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            horizontalAlignment: logoSection.alignRight ? Text.AlignRight
                                 : (logoSection.alignCenter ? Text.AlignHCenter : Text.AlignLeft)
        }
    }

    component HeroBadge: Rectangle {
        property int layoutBlockAlignment: Qt.AlignLeft
        Layout.alignment: layoutBlockAlignment
        implicitWidth: badgeLabel.implicitWidth + Theme.spacingMedium
        implicitHeight: badgeLabel.implicitHeight + Theme.spacingSmall
        radius: height / 2
        color: Theme.accentSecondary
        Text {
            id: badgeLabel
            anchors.centerIn: parent
            text: root.badgeText(root.currentItem)
            color: Theme.backgroundPrimary
            font.pixelSize: Theme.fontSizeSmall
            font.bold: true
        }
    }

    component HeroMetadataRow: RowLayout {
        property int layoutBlockAlignment: Qt.AlignLeft
        Layout.alignment: layoutBlockAlignment
        Layout.fillWidth: layoutBlockAlignment === Qt.AlignLeft
        spacing: Theme.spacingSmall
        MetadataChip { text: root.currentItem ? String(root.currentItem.ProductionYear || "") : "" }
        MetadataChip { text: root.episodeText(root.currentItem) }
        MetadataChip { text: root.currentItem ? root.formatRuntime(root.currentItem.RunTimeTicks) : "" }
        MetadataChip { text: root.currentItem ? (root.currentItem.OfficialRating || "") : "" }
    }

    component HeroActionsRow: RowLayout {
        id: actionsRow
        property int layoutBlockAlignment: Qt.AlignLeft
        Layout.alignment: layoutBlockAlignment
        Layout.fillWidth: layoutBlockAlignment === Qt.AlignLeft
        spacing: Theme.spacingSmall
        property Item playButtonRef: playBtn
        property Item detailsButtonRef: detailsBtn

        SecondaryActionButton {
            id: playBtn
            visible: root.currentItem && root.currentItem.Type !== "Series"
            focusPolicy: root.actionsFocused ? Qt.StrongFocus : Qt.NoFocus
            text: root.currentItem && root.currentItem.__heroReason === "Continue Watching" ? qsTr("Resume") : qsTr("Play")
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
        SecondaryActionButton {
            id: detailsBtn
            focusPolicy: root.actionsFocused ? Qt.StrongFocus : Qt.NoFocus
            text: qsTr("Details")
            iconGlyph: Icons.info
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

    Component {
        id: combinedLayoutComponent
        Item {
            anchors.fill: parent
            property alias playButton: heroActions.playButtonRef
            property alias detailsButton: heroActions.detailsButtonRef

            HeroBannerLayoutHost {
                id: combinedHost
                positionRoot: heroCard
                placement: root.logoPlacement
                centerLarge: root.logoPlacement === "centerLarge"
                extraBottomInset: root.carouselBottomInset
                HeroBadge { layoutBlockAlignment: combinedHost.contentLayoutAlignment }
                HeroLogoSection { blockPlacement: root.logoPlacement }
                HeroMetadataRow { layoutBlockAlignment: combinedHost.contentLayoutAlignment }
                HeroActionsRow {
                    id: heroActions
                    layoutBlockAlignment: combinedHost.contentLayoutAlignment
                }
            }
        }
    }

    Component {
        id: splitLayoutComponent
        Item {
            anchors.fill: parent
            property alias playButton: heroActions.playButtonRef
            property alias detailsButton: heroActions.detailsButtonRef

            HeroBannerLayoutHost {
                id: logoHost
                positionRoot: heroCard
                placement: root.logoPlacement
                centerLarge: root.logoPlacement === "centerLarge"
                extraBottomInset: root.carouselBottomInset
                HeroLogoSection { blockPlacement: root.logoPlacement }
            }

            HeroBannerLayoutHost {
                id: infoHost
                positionRoot: heroCard
                placement: root.infoPlacement
                extraBottomInset: root.carouselBottomInset
                HeroBadge { layoutBlockAlignment: infoHost.contentLayoutAlignment }
                HeroMetadataRow { layoutBlockAlignment: infoHost.contentLayoutAlignment }
                HeroActionsRow {
                    id: heroActions
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

    onCurrentBackdropUrlChanged: {
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
        anchors.leftMargin: Theme.paddingLarge
        anchors.rightMargin: Theme.paddingLarge
        radius: Theme.radiusLarge
        color: Theme.cardBackground
        border.color: root.activeFocus && !root.actionsFocused ? Theme.focusBorder : Theme.cardBorder
        border.width: root.activeFocus && !root.actionsFocused ? Theme.buttonFocusBorderWidth : Theme.borderWidth
        scale: root.activeFocus && !root.actionsFocused ? 1.01 : 1
        Behavior on scale { NumberAnimation { duration: Theme.uiAnimationsEnabled ? Theme.durationShort : 0 } }

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
            radius: Theme.imageRadius
            visible: false
            layer.enabled: true
            layer.smooth: true
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.imageRadius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#E610141C" }
                GradientStop { position: 0.58; color: "#8010141C" }
                GradientStop { position: 1; color: "#2010141C" }
            }
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

        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingLarge
            spacing: Theme.spacingSmall
            visible: root.heroModel.length > 1
            Repeater {
                model: root.heroModel.length
                Rectangle {
                    width: index === root.currentIndex ? Theme.spacingLarge : Theme.spacingSmall
                    height: Theme.spacingSmall
                    radius: height / 2
                    color: index === root.currentIndex ? Theme.accentPrimary : Theme.textSecondary
                    Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } enabled: Theme.uiAnimationsEnabled }
                    Behavior on color { ColorAnimation { duration: Theme.durationNormal } enabled: Theme.uiAnimationsEnabled }
                }
            }
        }
    }
}
