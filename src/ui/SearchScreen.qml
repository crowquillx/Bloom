import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import BloomUI

/**
 * SearchScreen - Search for movies and TV shows
 * 
 * Features:
 * - Search input field with keyboard focus
 * - Random suggestions when no search term is entered
 * - Categorized results (TV Shows and Movies)
 * - Full keyboard navigation
 * - Escape returns to home screen
 */
FocusScope {
    id: root
    focus: true
    property string navigationId: "search"
    
    // Navigation signals
    signal navigateToMovie(var movieData)
    signal navigateToSeries(var seriesId)
    
    // ========================================
    // Properties
    // ========================================
    
    property string searchTerm: ""
    property var suggestions: []
    property var movieResults: []
    property var seriesResults: []
    property var seerrResults: []
    property bool isSearching: false
    property bool hasSearched: false
    property bool waitingForLibrarySearch: false
    property bool waitingForSeerrSearch: false

    function ensureItemVisibleInResults(item) {
        if (!item || !contentFlickable || !contentColumn) {
            return
        }

        var pointInContent = item.mapToItem(contentColumn, 0, 0)
        var itemTop = pointInContent.y
        var itemBottom = itemTop + item.height
        var viewTop = contentFlickable.contentY
        var viewBottom = viewTop + contentFlickable.height

        if (itemTop < viewTop) {
            contentFlickable.contentY = Math.max(0, itemTop - Theme.spacingMedium)
        } else if (itemBottom > viewBottom) {
            var maxY = Math.max(0, contentFlickable.contentHeight - contentFlickable.height)
            contentFlickable.contentY = Math.min(maxY, itemBottom - contentFlickable.height + Theme.spacingMedium)
        }
    }
    
    // Debounce timer for search input
    Timer {
        id: searchDebounce
        interval: 300
        repeat: false
        onTriggered: {
            if (searchTerm.trim().length > 0) {
                waitingForLibrarySearch = true
                hasSearched = false
                isSearching = true
                LibraryService.search(searchTerm.trim())

                if (SeerrService.isConfigured()) {
                    waitingForSeerrSearch = true
                    SeerrService.search(searchTerm.trim())
                } else {
                    waitingForSeerrSearch = false
                    seerrResults = []
                }
            } else {
                hasSearched = false
                waitingForLibrarySearch = false
                waitingForSeerrSearch = false
                isSearching = false
                movieResults = []
                seriesResults = []
                seerrResults = []
            }
        }
    }
    
    // ========================================
    // Initialization
    // ========================================
    
    Component.onCompleted: {
        // Load random suggestions
        LibraryService.getRandomItems(20)
        // Focus search input
        searchInput.forceActiveFocus()
    }
    
    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active) {
            searchInput.forceActiveFocus()
        }
    }
    
    // ========================================
    // API Connections
    // ========================================
    
    Connections {
        target: LibraryService
        
        function onRandomItemsLoaded(items) {
            suggestions = []
            for (var i = 0; i < items.length; i++) {
                suggestions.push(items[i])
            }
            suggestionsChanged()
        }
        
        function onSearchResultsLoaded(term, movies, series) {
            if (term === searchTerm.trim()) {
                waitingForLibrarySearch = false
                isSearching = false
                hasSearched = true
                
                movieResults = []
                for (var i = 0; i < movies.length; i++) {
                    movieResults.push(movies[i])
                }
                movieResultsChanged()
                
                seriesResults = []
                for (var j = 0; j < series.length; j++) {
                    seriesResults.push(series[j])
                }
                seriesResultsChanged()
            }
        }
    }

    Connections {
        target: SeerrService

        function onSearchResultsLoaded(term, results) {
            if (term === searchTerm.trim()) {
                waitingForSeerrSearch = false

                seerrResults = []
                for (var i = 0; i < results.length; i++) {
                    seerrResults.push(results[i])
                }
                seerrResultsChanged()
            }
        }

        function onErrorOccurred(endpoint, error) {
            if (endpoint === "search") {
                waitingForSeerrSearch = false
            }
        }
    }
    
    // ========================================
    // Background
    // ========================================
    
    Rectangle {
        anchors.fill: parent
        color: Theme.backgroundPrimary
    }
    
    // ========================================
    // Main Content
    // ========================================
    
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacingLarge
        
        // Search Input Container
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.buttonHeightLarge
            Layout.leftMargin: Theme.spacingXLarge
            Layout.rightMargin: Theme.spacingXLarge
            
            // Center the search bar
            RowLayout {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width, 700)
                height: parent.height
                spacing: Theme.spacingMedium
                
                // Search input field
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.round(50 * Theme.layoutScale)
                    radius: Theme.radiusMedium
                    color: Theme.inputBackground
                    border.color: searchInput.activeFocus ? Theme.focusBorder : Theme.inputBorder
                    border.width: searchInput.activeFocus ? 2 : 1
                    
                    Behavior on border.color { ColorAnimation { duration: Theme.durationShort } }
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.spacingSmall
                        
                        TextField {
                            id: searchInput
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            placeholderText: "Search..."
                            placeholderTextColor: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            background: null
                            focus: true
                            
                            onTextChanged: {
                                searchTerm = text
                                searchDebounce.restart()
                            }
                            
                            Keys.onDownPressed: {
                                // Navigate to results or suggestions
                                if (hasSearched && seriesResults.length > 0) {
                                    seriesGrid.forceActiveFocus()
                                    seriesGrid.currentIndex = 0
                                } else if (hasSearched && movieResults.length > 0) {
                                    moviesGrid.forceActiveFocus()
                                    moviesGrid.currentIndex = 0
                                } else if (hasSearched && seerrResults.length > 0) {
                                    seerrGrid.forceActiveFocus()
                                    seerrGrid.currentIndex = 0
                                } else if (!hasSearched && suggestions.length > 0) {
                                    suggestionsColumn.children[0].forceActiveFocus()
                                }
                            }
                        }
                        
                        // Search icon
                        Text {
                            text: Icons.search
                            font.pixelSize: Theme.fontSizeIcon
                            font.family: Theme.fontIcon
                            color: Theme.textSecondary
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                }
            }
        }
        
        // Content Area (scrollable)
        Flickable {
            id: contentFlickable
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: contentColumn.height
            clip: true
            
            ColumnLayout {
                id: contentColumn
                width: parent.width
                spacing: Theme.spacingLarge
                
                // ========================================
                // Suggestions (shown when no search)
                // ========================================
                
                Item {
                    id: suggestionsSection
                    Layout.fillWidth: true
                    Layout.preferredHeight: suggestionsInner.height
                    visible: !hasSearched && suggestions.length > 0
                    
                    ColumnLayout {
                        id: suggestionsInner
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.spacingMedium
                        
                        // Title
                        Text {
                            text: "Suggestions"
                            font.pixelSize: Theme.fontSizeTitle
                            font.family: Theme.fontPrimary
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                            Layout.alignment: Qt.AlignHCenter
                        }
                        
                        // Suggestions list (centered, vertical list of titles)
                        ColumnLayout {
                            id: suggestionsColumn
                            Layout.alignment: Qt.AlignHCenter
                            spacing: Theme.spacingSmall
                            
                            Repeater {
                                model: suggestions
                                
                                ItemDelegate {
                                    id: suggestionDelegate
                                    required property var modelData
                                    required property int index
                                    
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.preferredWidth: Math.max(suggestionText.implicitWidth + Theme.spacingLarge * 2, 200)
                                    implicitHeight: 40
                                
                                background: Rectangle {
                                    radius: Theme.radiusSmall
                                    color: suggestionDelegate.activeFocus ? Theme.cardBackgroundFocused 
                                         : (suggestionDelegate.hovered ? Theme.cardBackgroundHover : "transparent")
                                    border.color: suggestionDelegate.activeFocus ? Theme.focusBorder : "transparent"
                                    border.width: suggestionDelegate.activeFocus ? 2 : 0
                                    
                                    Behavior on color { ColorAnimation { duration: 100 } }
                                }
                                
                                contentItem: Item {
                                    implicitWidth: suggestionText.implicitWidth
                                    implicitHeight: suggestionText.implicitHeight
                                    
                                    Text {
                                        id: suggestionText
                                        anchors.centerIn: parent
                                        text: suggestionDelegate.modelData.Name || ""
                                        font.pixelSize: Theme.fontSizeBody
                                        font.family: Theme.fontPrimary
                                        color: suggestionDelegate.activeFocus ? Theme.accentPrimary : Theme.textPrimary
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                                
                                onClicked: {
                                    var item = modelData
                                    if (item.Type === "Series") {
                                        root.navigateToSeries(item.Id)
                                    } else if (item.Type === "Movie") {
                                        root.navigateToMovie(item)
                                    }
                                }
                                
                                Keys.onReturnPressed: (event) => {
                                    if (event.isAutoRepeat) {
                                        event.accepted = true
                                        return
                                    }
                                    clicked()
                                    event.accepted = true
                                }
                                Keys.onEnterPressed: (event) => {
                                    if (event.isAutoRepeat) {
                                        event.accepted = true
                                        return
                                    }
                                    clicked()
                                    event.accepted = true
                                }
                                
                                Keys.onUpPressed: {
                                    if (index > 0) {
                                        suggestionsColumn.children[index - 1].forceActiveFocus()
                                    } else {
                                        searchInput.forceActiveFocus()
                                    }
                                }
                                
                                Keys.onDownPressed: {
                                    if (index < suggestions.length - 1) {
                                        suggestionsColumn.children[index + 1].forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }
                    }
                }
                
                // ========================================
                // Search Results
                // ========================================
                
                // Loading indicator
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.round(100 * Theme.layoutScale)
                    visible: isSearching
                    
                    BusyIndicator {
                        anchors.centerIn: parent
                        running: isSearching
                    }
                }
                
                // No results message
                Text {
                    text: "No results found"
                    font.pixelSize: Theme.fontSizeTitle
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    Layout.alignment: Qt.AlignHCenter
                    visible: hasSearched && !isSearching && seriesResults.length === 0 && movieResults.length === 0
                             && seerrResults.length === 0 && !waitingForSeerrSearch
                }

                Text {
                    text: "Searching Seerr..."
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                    Layout.alignment: Qt.AlignHCenter
                    visible: hasSearched && waitingForSeerrSearch
                    opacity: visible ? 1 : 0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationMedium }
                    }
                }
                
                // TV Shows section
                ColumnLayout {
                    id: seriesSection
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spacingXLarge
                    Layout.rightMargin: Theme.spacingXLarge
                    spacing: Theme.spacingMedium
                    visible: hasSearched && seriesResults.length > 0
                    
                    Text {
                        text: "TV Shows"
                        font.pixelSize: Theme.fontSizeTitle
                        font.family: Theme.fontPrimary
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                    }
                    
                    GridView {
                        id: seriesGrid
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.ceil(count / Math.max(1, Math.floor(width / cellWidth))) * cellHeight
                        
                        cellWidth: Math.round(220 * Theme.layoutScale)
                        cellHeight: Math.round(380 * Theme.layoutScale)
                        
                        interactive: false
                        
                        model: seriesResults
                        
                        delegate: FocusScope {
                            id: seriesDelegateScope
                            width: seriesGrid.cellWidth
                            height: seriesGrid.cellHeight
                            
                            required property var modelData
                            required property int index

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    root.ensureItemVisibleInResults(seriesDelegateScope)
                                }
                            }
                            
                            SearchResultCard {
                                id: seriesCard
                                anchors.fill: parent
                                anchors.margins: Theme.spacingSmall
                                
                                itemData: seriesDelegateScope.modelData
                                isFocused: seriesDelegateScope.activeFocus
                                
                                onClicked: {
                                    root.navigateToSeries(seriesDelegateScope.modelData.Id)
                                }
                            }
                            
                            Keys.onReturnPressed: (event) => {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                seriesCard.clicked()
                                event.accepted = true
                            }
                            Keys.onEnterPressed: (event) => {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                seriesCard.clicked()
                                event.accepted = true
                            }
                            
                            Keys.onUpPressed: {
                                var columns = Math.floor(seriesGrid.width / seriesGrid.cellWidth)
                                if (index < columns) {
                                    searchInput.forceActiveFocus()
                                } else {
                                    seriesGrid.currentIndex = index - columns
                                }
                            }
                            
                            Keys.onDownPressed: {
                                var columns = Math.floor(seriesGrid.width / seriesGrid.cellWidth)
                                if (index + columns < seriesResults.length) {
                                    seriesGrid.currentIndex = index + columns
                                } else if (movieResults.length > 0) {
                                    moviesGrid.forceActiveFocus()
                                    moviesGrid.currentIndex = 0
                                } else if (seerrResults.length > 0) {
                                    seerrGrid.forceActiveFocus()
                                    seerrGrid.currentIndex = 0
                                }
                            }
                            
                            Keys.onLeftPressed: {
                                if (index % Math.floor(seriesGrid.width / seriesGrid.cellWidth) > 0) {
                                    seriesGrid.currentIndex = index - 1
                                }
                            }
                            
                            Keys.onRightPressed: {
                                var columns = Math.floor(seriesGrid.width / seriesGrid.cellWidth)
                                if ((index + 1) % columns !== 0 && index + 1 < seriesResults.length) {
                                    seriesGrid.currentIndex = index + 1
                                }
                            }
                        }
                        
                        onCurrentIndexChanged: {
                            if (currentItem) {
                                currentItem.forceActiveFocus()
                                root.ensureItemVisibleInResults(currentItem)
                            }
                        }
                    }
                }
                
                // Movies section
                ColumnLayout {
                    id: moviesSection
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spacingXLarge
                    Layout.rightMargin: Theme.spacingXLarge
                    spacing: Theme.spacingMedium
                    visible: hasSearched && movieResults.length > 0
                    
                    Text {
                        text: "Movies"
                        font.pixelSize: Theme.fontSizeTitle
                        font.family: Theme.fontPrimary
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                    }
                    
                    GridView {
                        id: moviesGrid
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.ceil(count / Math.max(1, Math.floor(width / cellWidth))) * cellHeight
                        
                        cellWidth: Math.round(220 * Theme.layoutScale)
                        cellHeight: Math.round(380 * Theme.layoutScale)
                        
                        interactive: false
                        
                        model: movieResults
                        
                        delegate: FocusScope {
                            id: movieDelegateScope
                            width: moviesGrid.cellWidth
                            height: moviesGrid.cellHeight
                            
                            required property var modelData
                            required property int index

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    root.ensureItemVisibleInResults(movieDelegateScope)
                                }
                            }
                            
                            SearchResultCard {
                                id: movieCard
                                anchors.fill: parent
                                anchors.margins: Theme.spacingSmall
                                
                                itemData: movieDelegateScope.modelData
                                isFocused: movieDelegateScope.activeFocus
                                
                                onClicked: {
                                    root.navigateToMovie(movieDelegateScope.modelData)
                                }
                            }
                            
                            Keys.onReturnPressed: (event) => {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                movieCard.clicked()
                                event.accepted = true
                            }
                            Keys.onEnterPressed: (event) => {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                movieCard.clicked()
                                event.accepted = true
                            }
                            
                            Keys.onUpPressed: {
                                var columns = Math.floor(moviesGrid.width / moviesGrid.cellWidth)
                                if (index < columns) {
                                    if (seriesResults.length > 0) {
                                        seriesGrid.forceActiveFocus()
                                        seriesGrid.currentIndex = Math.min(seriesResults.length - 1, 
                                            seriesResults.length - (seriesResults.length % columns) + index)
                                    } else {
                                        searchInput.forceActiveFocus()
                                    }
                                } else {
                                    moviesGrid.currentIndex = index - columns
                                }
                            }
                            
                            Keys.onDownPressed: {
                                var columns = Math.floor(moviesGrid.width / moviesGrid.cellWidth)
                                if (index + columns < movieResults.length) {
                                    moviesGrid.currentIndex = index + columns
                                } else if (seerrResults.length > 0) {
                                    seerrGrid.forceActiveFocus()
                                    seerrGrid.currentIndex = 0
                                }
                            }
                            
                            Keys.onLeftPressed: {
                                if (index % Math.floor(moviesGrid.width / moviesGrid.cellWidth) > 0) {
                                    moviesGrid.currentIndex = index - 1
                                }
                            }
                            
                            Keys.onRightPressed: {
                                var columns = Math.floor(moviesGrid.width / moviesGrid.cellWidth)
                                if ((index + 1) % columns !== 0 && index + 1 < movieResults.length) {
                                    moviesGrid.currentIndex = index + 1
                                }
                            }
                        }
                        
                        onCurrentIndexChanged: {
                            if (currentItem) {
                                currentItem.forceActiveFocus()
                                root.ensureItemVisibleInResults(currentItem)
                            }
                        }
                    }
                }

                // Seerr section
                ColumnLayout {
                    id: seerrSection
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spacingXLarge
                    Layout.rightMargin: Theme.spacingXLarge
                    spacing: Theme.spacingMedium
                    visible: hasSearched && seerrResults.length > 0
                    opacity: visible ? 1 : 0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.durationMedium }
                    }

                    Text {
                        text: "Seerr"
                        font.pixelSize: Theme.fontSizeTitle
                        font.family: Theme.fontPrimary
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                    }

                    GridView {
                        id: seerrGrid
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.ceil(count / Math.max(1, Math.floor(width / cellWidth))) * cellHeight

                        cellWidth: Math.round(220 * Theme.layoutScale)
                        cellHeight: Math.round(380 * Theme.layoutScale)
                        interactive: false

                        model: seerrResults

                        delegate: FocusScope {
                            id: seerrDelegateScope
                            width: seerrGrid.cellWidth
                            height: seerrGrid.cellHeight

                            required property var modelData
                            required property int index

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    root.ensureItemVisibleInResults(seerrDelegateScope)
                                }
                            }

                            SearchResultCard {
                                id: seerrCard
                                anchors.fill: parent
                                anchors.margins: Theme.spacingSmall

                                itemData: seerrDelegateScope.modelData
                                isFocused: seerrDelegateScope.activeFocus

                                onClicked: {
                                    seerrRequestDialog.openForItem(seerrDelegateScope.modelData, seerrDelegateScope)
                                }
                            }

                            Keys.onReturnPressed: (event) => {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                seerrCard.clicked()
                                event.accepted = true
                            }
                            Keys.onEnterPressed: (event) => {
                                if (event.isAutoRepeat) {
                                    event.accepted = true
                                    return
                                }
                                seerrCard.clicked()
                                event.accepted = true
                            }

                            Keys.onUpPressed: {
                                var columns = Math.floor(seerrGrid.width / seerrGrid.cellWidth)
                                if (index < columns) {
                                    if (movieResults.length > 0) {
                                        moviesGrid.forceActiveFocus()
                                        moviesGrid.currentIndex = Math.min(movieResults.length - 1, index)
                                    } else if (seriesResults.length > 0) {
                                        seriesGrid.forceActiveFocus()
                                        seriesGrid.currentIndex = Math.min(seriesResults.length - 1, index)
                                    } else {
                                        searchInput.forceActiveFocus()
                                    }
                                } else {
                                    seerrGrid.currentIndex = index - columns
                                }
                            }

                            Keys.onDownPressed: {
                                var columns = Math.floor(seerrGrid.width / seerrGrid.cellWidth)
                                if (index + columns < seerrResults.length) {
                                    seerrGrid.currentIndex = index + columns
                                }
                            }

                            Keys.onLeftPressed: {
                                if (index % Math.floor(seerrGrid.width / seerrGrid.cellWidth) > 0) {
                                    seerrGrid.currentIndex = index - 1
                                }
                            }

                            Keys.onRightPressed: {
                                var columns = Math.floor(seerrGrid.width / seerrGrid.cellWidth)
                                if ((index + 1) % columns !== 0 && index + 1 < seerrResults.length) {
                                    seerrGrid.currentIndex = index + 1
                                }
                            }
                        }

                        onCurrentIndexChanged: {
                            if (currentItem) {
                                currentItem.forceActiveFocus()
                                root.ensureItemVisibleInResults(currentItem)
                            }
                        }
                    }
                }
                
                // Bottom padding
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.spacingXLarge
                }
            }
        }
    }

    SeerrRequestDialog {
        id: seerrRequestDialog
        parent: Overlay.overlay
    }
}
