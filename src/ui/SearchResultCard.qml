import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import BloomUI

/**
 * SearchResultCard - A card displaying a movie or series in search results.
 *
 * Supports canonical library items and Seerr discovery items.
 * Shows a poster, title, year, and source-appropriate watched, unwatched, or availability badges.
 * Responds to pointer hover and keyboard/gamepad focus.
 */
Item {
    id: root
    
    // ========================================
    // Properties
    // ========================================
    
    property var itemData: ({})
    property bool isFocused: false
    
    // Computed properties
    property bool isSeerr: itemData.source === "seerr"
    property string itemName: itemData.name || ""
    property string itemType: itemData.mediaType || ""
    property string itemYear: itemData.productionYear ? String(itemData.productionYear) : ""
    property string itemId: itemData.itemId || ""
    property bool isPlayed: itemData.watched || false
    property int seerrStatus: itemData.seerrMediaInfo && itemData.seerrMediaInfo.status
                              ? itemData.seerrMediaInfo.status : 0
    property string seerrStatusLabel: {
        switch (seerrStatus) {
        case 2: return qsTr("Pending")
        case 3: return qsTr("Processing")
        case 4: return qsTr("Partial")
        case 5: return qsTr("Available")
        case 6: return qsTr("Deleted")
        default: return ""
        }
    }
    property string posterSource: {
        if (isSeerr) {
            return itemData.imageUrl || ""
        }
        const artwork = itemData.primaryArtwork
        if (!artwork || !artwork.itemId) {
            return ""
        }
        return LibraryService.getCachedArtworkUrlFromRef(artwork, 300)
    }
    
    // ========================================
    // Signals
    // ========================================
    
    signal clicked()
    
    // ========================================
    // Visual Properties
    // ========================================
    
    property bool isHovered: InputModeManager.pointerActive && mouseArea.containsMouse
    scale: isFocused ? Theme.focusScale : 1.0
    z: isFocused ? 2 : 0
    transformOrigin: Item.Center
    
    Behavior on scale { NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic } enabled: Theme.uiAnimationsEnabled }
    
    // ========================================
    // Card Content
    // ========================================
    
    Rectangle {
        id: card
        anchors.fill: parent
        // Poster-first card: no chrome of its own, focus rings the artwork.
        color: "transparent"
        
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingSmall
            
            // Poster Image
            Rectangle {
                id: posterContainer
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.imageRadius
                antialiasing: true
                color: "transparent"
                clip: false

                ArtworkPlaceholder {
                    anchors.fill: parent
                    visible: posterImage.status !== Image.Ready
                    loading: posterImage.status === Image.Loading
                    iconGlyph: itemType === "Series" ? Icons.tvShows : Icons.movie
                    title: itemName
                }
                
                Image {
                    id: posterImage
                    anchors.fill: parent
                    source: posterSource
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    smooth: true
                    visible: status === Image.Ready

                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: posterMask
                    }
                }

                Rectangle {
                    id: posterMask
                    anchors.fill: parent
                    radius: Theme.imageRadius
                    visible: false
                    layer.enabled: true
                    layer.smooth: true
                }

                // Owning server/account in merged spaces.
                SourceBadge {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.spacingSmall
                    maximumWidth: parent.width - Theme.spacingSmall * 2
                    text: isSeerr ? "" : (itemData.sourceLabel || "")
                }

                // Unwatched episode count badge (for Series)
                UnwatchedBadge {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    parentWidth: parent.width
                    count: itemType === "Series" ? (itemData.unplayedItemCount || 0) : 0
                    isFullyWatched: isPlayed
                    visible: !isSeerr && itemType === "Series"
                }

                // Watched indicator (for Movies only)
                UnwatchedBadge {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    count: 0
                    isFullyWatched: true
                    visible: !isSeerr && itemType === "Movie" && isPlayed
                }

                FocusRing {
                    active: root.isFocused
                    radius: Theme.imageRadius
                }

                Rectangle {
                    id: seerrBadge
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingSmall
                    width: seerrBadgeContent.implicitWidth + Theme.spacingSmall * 2
                    height: seerrBadgeContent.implicitHeight + Theme.spacingSmall * 2
                    radius: height / 2
                    color: Theme.overlayTextBackground
                    border.width: Theme.borderWidth
                    border.color: Theme.overlayTextBorder
                    visible: isSeerr

                    RowLayout {
                        id: seerrBadgeContent
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.topMargin: Theme.spacingSmall
                        spacing: Theme.spacingSmall

                        Text {
                            text: "Seerr"
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontPrimary
                            font.weight: Theme.fontWeightHeavy
                            color: Theme.textOnDarkOverlay
                        }

                        Text {
                            text: seerrStatusLabel
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.fontPrimary
                            color: Theme.withAlpha(Theme.textOnDarkOverlay, 0.78)
                            visible: seerrStatusLabel.length > 0
                        }
                    }
                }
            }
            
            // Title and year
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Math.round(2 * Theme.layoutScale)
                
                Text {
                    text: itemName
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    font.weight: Theme.fontWeightStrong
                    color: Theme.textPrimary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                }
                
                Text {
                    text: itemYear
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    visible: itemYear.length > 0
                }
            }
        }
        
        // Mouse area for click handling
        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            
            onClicked: root.clicked()
        }
    }
}
