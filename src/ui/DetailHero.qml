import QtQuick

import BloomUI

/**
 * DetailHero - the open hero block of a detail page (movie, series, season,
 * episode). It sits on the page's StageBackdrop at the gutter, at most about
 * half the screen wide, and reads top to bottom (docs/style-guide.md, Hero):
 *
 *   eyebrow, logo or display title, subtitle, metadata line, quality and
 *   rating chips, an optional detail line, synopsis with Read More, the action
 *   row, and an optional progress or footnote line.
 *
 * Objects declared inside become the action row: one primary ActionButton
 * (height buttonHeightHero, label fontSizeMedium), then secondary buttons,
 * then icon buttons. Point `primaryAction` at the primary button so Read More
 * can hand focus down to it and the progress bar matches its width.
 *
 * Read More is the top-most control when shown; it leaves Up unhandled so the
 * key reaches the navigation bar. Callers own the action buttons' keys.
 */
Column {
    id: root

    property string eyebrow: ""
    property string logoSource: ""
    property string title: ""
    property string subtitle: ""
    /// Facts for the MetadataLine (empty entries are skipped).
    property var metadataItems: []
    /// Quality labels shown as chips (e.g. "4K", "Dolby Vision").
    property var qualityBadges: []
    /// Rating maps for RatingMetadataChip.
    property var ratings: []
    /// Quiet secondary facts, e.g. genres.
    property var detailItems: []
    /// One more line of supporting text, e.g. key creatives.
    property string detailText: ""
    property string synopsis: ""
    /// Shown, dimmed, when there is no synopsis. Empty hides the synopsis.
    property string synopsisPlaceholder: ""
    property bool synopsisExpanded: false
    property int synopsisCollapsedLines: 3
    property int synopsisExpandedLines: 14
    /// Keep the synopsis and Read More slots at their collapsed height even
    /// when the text is short, so the hero does not jump while browsing.
    property bool stableSynopsisHeight: false
    /// Keep one chip row reserved even while it is empty. Quality badges and
    /// ratings load after the page's details, so pages that expect them set
    /// this to stop the hero jumping when they arrive. Chips that would wrap
    /// past the reserved row are clipped.
    property bool reserveChipRow: false
    /// Keep the progress/footnote line reserved even while it is empty, for
    /// footnotes that load after the page's details.
    property bool reserveFootnote: false
    property Item primaryAction: null
    /// Playback progress (0..1) drawn under the primary action.
    property real progress: 0
    /// Caption next to the progress bar, or alone when there is no progress.
    property string footnote: ""

    readonly property alias readMoreButton: readMore
    readonly property bool hasLogo: logoSource !== "" && logo.status !== Image.Error
    readonly property int synopsisLineHeight: Math.ceil(synopsisText.font.pixelSize * synopsisText.lineHeight * 1.2)

    default property alias actions: actionRow.data

    spacing: Theme.spacingMedium

    Text {
        width: parent.width
        text: root.eyebrow
        visible: text !== ""
        font.pixelSize: Theme.fontSizeCaption
        font.family: Theme.fontPrimary
        font.weight: Theme.fontWeightHeavy
        font.capitalization: Font.AllUppercase
        font.letterSpacing: Theme.eyebrowLetterSpacing
        color: Theme.accentPrimary
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    // Provider logo, or the title in display type.
    Item {
        width: parent.width
        height: root.hasLogo ? Theme.detailViewLogoHeight : titleText.implicitHeight
        visible: root.hasLogo || root.title !== ""

        ArtworkLogo {
            id: logo
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(Theme.seriesLogoMaxWidth, parent.width)
            height: parent.height
            source: root.logoSource
            horizontalAlignment: Image.AlignLeft
            visible: root.hasLogo
            opacity: status === Image.Ready ? 1.0 : 0.0
            Behavior on opacity {
                enabled: Theme.uiAnimationsEnabled
                NumberAnimation { duration: Theme.durationFade }
            }
        }

        Text {
            id: titleText
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: !root.hasLogo
            text: root.title
            font.pixelSize: Theme.fontSizeHero
            font.family: Theme.fontDisplay
            font.weight: Theme.fontWeightHeavy
            font.letterSpacing: Theme.displayLetterSpacing
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            lineHeight: 0.95
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
    }

    Text {
        width: parent.width
        text: root.subtitle
        visible: text !== ""
        font.pixelSize: Theme.fontSizeMedium
        font.family: Theme.fontPrimary
        font.weight: Theme.fontWeightStrong
        color: Theme.textPrimary
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    MetadataLine {
        width: parent.width
        items: root.metadataItems
        font.pixelSize: Theme.fontSizeBody
        color: Theme.textPrimary
        opacity: 0.86
    }

    Item {
        readonly property int reservedHeight: Math.round(40 * Theme.layoutScale)
        readonly property bool hasChips: qualityRepeater.visibleCount > 0 || ratingRepeater.count > 0

        width: parent.width
        height: root.reserveChipRow ? reservedHeight : chipFlow.height
        visible: root.reserveChipRow || hasChips
        clip: root.reserveChipRow

        Flow {
            id: chipFlow
            width: parent.width
            spacing: Theme.spacingSmall
            opacity: parent.hasChips ? 1.0 : 0.0
            Behavior on opacity {
                enabled: Theme.uiAnimationsEnabled
                NumberAnimation { duration: Theme.durationFade }
            }

            Repeater {
                id: qualityRepeater
                readonly property int visibleCount: {
                    var total = 0
                    var badges = root.qualityBadges || []
                    for (var i = 0; i < badges.length; ++i) {
                        if (badges[i])
                            total++
                    }
                    return total
                }
                model: root.qualityBadges

                MetadataChip {
                    required property var modelData
                    text: modelData || ""
                }
            }

            Repeater {
                id: ratingRepeater
                model: root.ratings

                RatingMetadataChip {
                    required property var modelData
                    ratingData: modelData
                }
            }
        }
    }

    MetadataLine {
        width: parent.width
        items: root.detailItems
        separator: "  ·  "
        color: Theme.textTertiary
    }

    Text {
        width: parent.width
        text: root.detailText
        visible: text !== ""
        font.pixelSize: Theme.fontSizeSmall
        font.family: Theme.fontPrimary
        color: Theme.textSecondary
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    // Synopsis with Read More. The button sits under the text, so it is the
    // top-most focusable control of the hero.
    Column {
        id: synopsisBlock
        width: parent.width
        spacing: Theme.spacingXSmall
        visible: root.synopsis !== "" || root.synopsisPlaceholder !== ""

        Text {
            id: synopsisText
            width: parent.width
            height: root.stableSynopsisHeight && !root.synopsisExpanded
                    ? root.synopsisLineHeight * root.synopsisCollapsedLines
                    : implicitHeight
            verticalAlignment: Text.AlignTop
            text: root.synopsis !== "" ? root.synopsis : root.synopsisPlaceholder
            font.pixelSize: Theme.fontSizeBody
            font.family: Theme.fontPrimary
            color: root.synopsis !== "" ? Theme.textPrimary : Theme.textSecondary
            opacity: root.synopsis !== "" ? 0.86 : 1.0
            lineHeight: 1.12
            wrapMode: Text.WordWrap
            maximumLineCount: root.synopsisExpanded ? root.synopsisExpandedLines : root.synopsisCollapsedLines
            elide: Text.ElideRight
        }

        Item {
            width: parent.width
            height: readMore.implicitHeight
            visible: readMore.visible || root.stableSynopsisHeight

            ActionButton {
                id: readMore
                x: -Theme.spacingMedium
                variant: "ghost"
                visible: synopsisText.truncated || root.synopsisExpanded
                implicitHeight: Theme.buttonHeightSmall
                sidePadding: Theme.spacingMedium
                labelPixelSize: Theme.fontSizeSmall
                text: root.synopsisExpanded ? qsTr("Show Less") : qsTr("Read More")
                iconGlyph: root.synopsisExpanded ? Icons.expandLess : Icons.expandMore
                onClicked: root.synopsisExpanded = !root.synopsisExpanded
                Keys.onReturnPressed: (event) => { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                Keys.onEnterPressed: (event) => { if (!event.isAutoRepeat) clicked(); event.accepted = true }
                Keys.onDownPressed: (event) => {
                    if (root.primaryAction && root.primaryAction.visible && root.primaryAction.enabled) {
                        root.primaryAction.forceActiveFocus()
                        event.accepted = true
                    } else {
                        event.accepted = false
                    }
                }
                // Top-most control: Up goes on to the navigation bar.
                Keys.onUpPressed: (event) => { event.accepted = false }
                Keys.onLeftPressed: (event) => { event.accepted = true }
                Keys.onRightPressed: (event) => { event.accepted = true }
            }
        }
    }

    Row {
        id: actionRow
        topPadding: Theme.spacingSmall
        spacing: Theme.controlSpacing
    }

    // Resume progress under the primary action, or a short footnote.
    Row {
        spacing: Theme.spacingMedium
        height: root.reserveFootnote ? Math.max(implicitHeight, footnoteText.implicitHeight) : implicitHeight
        visible: root.reserveFootnote || root.progress > 0 || root.footnote !== ""

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.progress > 0
            width: root.primaryAction ? root.primaryAction.width : Math.round(300 * Theme.layoutScale)
            height: Theme.progressHeight
            radius: height / 2
            color: Theme.progressTrack

            Rectangle {
                width: Math.max(height, parent.width * Math.min(1, root.progress))
                height: parent.height
                radius: height / 2
                color: Theme.progressFill
            }
        }

        Text {
            id: footnoteText
            anchors.verticalCenter: parent.verticalCenter
            text: root.footnote
            visible: text !== ""
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightMedium
            color: Theme.textSecondary
        }
    }
}
