import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import BloomUI

/**
 * SearchResultCard - A card displaying a movie or series in search results
 * 
 * Shows poster image, title, year, and watched indicator
 */
Item {
    id: root
    
    // ========================================
    // Properties
    // ========================================
    
    property var itemData: ({})
    property bool isFocused: false
    
    // Computed properties
    property bool isSeerr: itemData.Source === "Seerr"
    property string itemName: itemData.Name || ""
    property string itemType: itemData.Type || ""
    property string itemYear: itemData.ProductionYear ? String(itemData.ProductionYear) : ""
    property string itemId: itemData.Id || ""
    property bool isPlayed: itemData.UserData ? itemData.UserData.Played : false
    property string posterPath: itemData.PosterPath || ""
    property int seerrStatus: itemData.SeerrMediaInfo && itemData.SeerrMediaInfo.status ? itemData.SeerrMediaInfo.status : 0
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
            if (!posterPath || posterPath.length === 0) return ""
            if (posterPath.indexOf("http://") === 0 || posterPath.indexOf("https://") === 0) return posterPath
            return "https://image.tmdb.org/t/p/w342" + posterPath
        }
        return itemId ? LibraryService.getCachedImageUrlWithWidth(itemId, "Primary", 300) : ""
    }
    
    // ========================================
    // Signals
    // ========================================
    
    signal clicked()
    
    // ========================================
    // Visual Properties
    // ========================================
    
    property bool isHovered: InputModeManager.pointerActive && mouseArea.containsMouse
    scale: isFocused ? 1.05 : (isHovered ? 1.02 : 1.0)
    z: isFocused ? 2 : 0
    transformOrigin: Item.Center
    
    Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
    
    // ========================================
    // Card Content
    // ========================================
    
    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.radiusMedium
        color: root.isFocused ? Theme.cardBackgroundFocused 
             : (root.isHovered ? Theme.cardBackgroundHover : Theme.cardBackground)
        border.width: root.isFocused ? 2 : 1
        border.color: root.isFocused ? Theme.cardBorderFocused 
                    : (root.isHovered ? Theme.cardBorderHover : Theme.cardBorder)
        
        Behavior on color { ColorAnimation { duration: 100 } }
        Behavior on border.color { ColorAnimation { duration: 100 } }
        
        clip: true
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingSmall
            spacing: Theme.spacingSmall
            
            // Poster Image
            Rectangle {
                id: posterContainer
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.imageRadius
                antialiasing: true
                color: Theme.backgroundSecondary
                clip: false
                
                Image {
                    id: posterImage
                    anchors.fill: parent
                    source: posterSource
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    smooth: true
                    visible: true

                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: posterMask
                    }
                    
                    // Fallback when no image
                    Rectangle {
                        anchors.fill: parent
                        color: Theme.backgroundSecondary
                        visible: posterImage.status !== Image.Ready
                        
                        Text {
                            anchors.centerIn: parent
                            text: itemType === "Series" ? Icons.tvShows : Icons.movie
                            font.pixelSize: 48
                            font.family: Theme.fontIcon
                            color: Theme.textSecondary
                        }
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

                // Unwatched episode count badge (for Series)
                UnwatchedBadge {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    parentWidth: parent.width
                    count: (itemType === "Series" && itemData.UserData) 
                           ? (itemData.UserData.UnplayedItemCount || 0) : 0
                    isFullyWatched: isPlayed
                    visible: !isSeerr && itemType === "Series"
                }

                // Watched indicator (for Movies only)
                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 8
                    width: 24
                    height: 24
                    radius: 12
                    color: Theme.accentPrimary
                    visible: !isSeerr && itemType === "Movie" && isPlayed
                    
                    Text {
                        anchors.centerIn: parent
                        text: Icons.check
                        font.pixelSize: 16
                        font.family: Theme.fontIcon
                        color: Theme.textPrimary
                    }
                }

                Rectangle {
                    id: seerrBadge
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingSmall
                    width: seerrBadgeContent.implicitWidth + Theme.spacingSmall * 2
                    height: seerrBadgeContent.implicitHeight + Theme.spacingSmall * 2
                    radius: Theme.radiusSmall
                    color: Theme.overlayDark
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
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                        }

                        Text {
                            text: seerrStatusLabel
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.fontPrimary
                            color: Theme.textSecondary
                            visible: seerrStatusLabel.length > 0
                        }
                    }
                }
            }
            
            // Title and year
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                
                Text {
                    text: itemName
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    font.weight: Font.Medium
                    color: Theme.textPrimary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                }
                
                Text {
                    text: itemYear
                    font.pixelSize: Theme.fontSizeSmall
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
