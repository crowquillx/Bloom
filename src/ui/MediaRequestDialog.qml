import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

/**
 * MediaRequestDialog - Modal dialog for requesting a movie or series.
 *
 * Opens with openForItem(), which populates the dialog from a discovery item and
 * triggers MediaRequests.prepareRequest(). The backend shapes the form:
 *  - Seerr: the user selects a server, quality profile, and root folder.
 *  - Silo: the server routes the request, so there is nothing to pick; seasons
 *    already in the library or requested are left out of the season grid, and
 *    a title the viewer may not request says why.
 * While options are loading a BusyIndicator is shown. For TV series an optional
 * per-season selection grid is provided.
 *
 * On success the dialog displays a status message and auto-closes after 1.5 s via
 * closeTimer.  On error the message is shown in red and the dialog stays open.
 *
 * Focus management:
 *  - On open: keyboard mode is requested via InputModeManager; focus goes to the
 *    first available combo box (or Cancel if still loading).
 *  - On close: pointer mode is restored and focus returns to restoreFocusTarget.
 *
 * Properties set by caller:
 *  - restoreFocusTarget  Item to refocus after the dialog closes.
 *
 * Key properties (internal state):
 *  - mediaType, tmdbId, mediaTitle  Identify the item being requested.
 *  - backend                    "seerr" or "silo", from the preparation payload.
 *  - servers, profiles, rootFolders  Seerr options.
 *  - selectedServerId, selectedProfileId, selectedRootFolderPath  Current selections.
 *  - seasonEntries, requestAllSeasons, selectedSeasons  TV-only season selection state.
 *  - requestable, notRequestableReason  Silo's verdict for the viewer.
 *  - requestId, canCancelRequest  A request the viewer may withdraw (from the
 *    request list, or Silo's active request when the viewer made it).
 *  - followAvailable/following, watchlistAvailable/inWatchlist  Silo's
 *    follow (notify when available) and watchlist-title actions.
 *  - loadingOptions, submitting, statusText, statusError  UI state flags.
 */
Dialog {
    id: dialog
    modal: true
    focus: true
    anchors.centerIn: parent
    width: Math.round(720 * Theme.layoutScale)
    height: Math.round(860 * Theme.layoutScale)
    padding: Theme.dialogPadding
    topPadding: Theme.spacingLarge

    property Item restoreFocusTarget: null
    property string mediaType: ""
    property int tmdbId: -1
    property string mediaTitle: ""
    property string backend: ""
    /// Season numbers the user can choose.
    property var seasonEntries: []
    /// Seasons left out of the grid because the library has them or they are requested.
    property var coveredSeasons: []
    property bool requestable: true
    property string notRequestableReason: ""
    property string requestId: ""
    property bool canCancelRequest: false
    property bool followAvailable: false
    property bool following: false
    property bool watchlistAvailable: false
    property bool inWatchlist: false
    property bool watchlistRequests: false
    /// A follow, watchlist, or withdraw call is in flight.
    property bool actionBusy: false

    property var servers: []
    property var profiles: []
    property var rootFolders: []

    property int selectedServerId: -1
    property int selectedProfileId: -1
    property string selectedRootFolderPath: ""

    property bool requestAllSeasons: true
    property var selectedSeasons: []
    property bool syncingSeasonChecks: false

    property bool loadingOptions: false
    property bool submitting: false
    property string statusText: ""
    property bool statusError: false

    readonly property bool isTv: mediaType === "tv"
    /// Seerr hands the choice of server, profile, and root folder to the user.
    readonly property bool needsServiceSelection: backend === "seerr"
    readonly property bool hasSeasonChoices: isTv && seasonEntries.length > 0
    readonly property bool canWithdraw: canCancelRequest && requestId !== ""
    readonly property bool hasExtraActions: !loadingOptions && (followAvailable || watchlistAvailable || canWithdraw)
    readonly property bool hasValidRequestSelections: needsServiceSelection
                                                      ? selectedServerId >= 0
                                                        && selectedProfileId >= 0
                                                        && selectedRootFolderPath.trim().length > 0
                                                      : backend === "silo" && requestable

    Timer {
        id: closeTimer
        interval: 1500
        repeat: false
        onTriggered: dialog.close()
    }

    /** @brief Switches InputModeManager to keyboard/gamepad navigation and hides the cursor. */
    function setKeyboardNavigationMode() {
        if (typeof InputModeManager !== "undefined") {
            InputModeManager.setNavigationMode("keyboard")
            InputModeManager.hideCursor(true)
        }
    }

    /** @brief Restores InputModeManager to pointer navigation and shows the cursor. */
    function restorePointerNavigationMode() {
        if (typeof InputModeManager !== "undefined") {
            InputModeManager.setNavigationMode("pointer")
            InputModeManager.hideCursor(false)
        }
    }

    /**
     * Moves focus to the most appropriate control after the dialog becomes visible.
     *
     * Routes to the first option (serverCombo for Seerr, the season choice for a
     * Silo series), then the Request button, falling back to cancelButton so the
     * user can always dismiss the dialog via keyboard.
     */
    function focusInitialControl() {
        if (!visible) return
        dialog.forceActiveFocus()
        if (loadingOptions) {
            cancelButton.forceActiveFocus()
            return
        }
        if (needsServiceSelection && serverCombo.visible && serverCombo.enabled) {
            serverCombo.forceActiveFocus()
        } else if (hasSeasonChoices) {
            allSeasonsCheck.forceActiveFocus()
        } else if (requestButton.enabled) {
            requestButton.forceActiveFocus()
        } else if (hasExtraActions) {
            focusExtraAction(0)
        } else {
            cancelButton.forceActiveFocus()
        }
    }

    /// The follow, watchlist, and withdraw buttons on screen, left to right.
    function visibleExtraActions() {
        return [followButton, watchlistButton, withdrawButton].filter(function(button) {
            return button.visible
        })
    }

    function focusExtraAction(index) {
        var buttons = visibleExtraActions()
        if (index >= 0 && index < buttons.length)
            buttons[index].forceActiveFocus()
    }

    function moveExtraAction(from, delta) {
        var buttons = visibleExtraActions()
        focusExtraAction(buttons.indexOf(from) + delta)
    }

    /// Moves focus from the last option to the extra actions, else the buttons.
    function focusBelowOptions() {
        if (hasExtraActions)
            focusExtraAction(0)
        else
            cancelButton.forceActiveFocus()
    }

    /// Moves focus to the control above the season choice, if any.
    function focusAboveSeasons() {
        if (needsServiceSelection)
            rootFolderCombo.forceActiveFocus()
    }

    /// Moves focus from the buttons to the extra actions, else the options.
    function focusAboveButtons() {
        if (hasExtraActions)
            focusExtraAction(0)
        else
            focusAboveExtras()
    }

    /// Moves focus from the extra actions to the last option above them.
    function focusAboveExtras() {
        if (hasSeasonChoices) {
            if (!requestAllSeasons && seasonRepeater.count > 0) {
                var last = seasonRepeater.itemAt(seasonRepeater.count - 1)
                if (last) {
                    last.forceActiveFocus()
                    return
                }
            }
            allSeasonsCheck.forceActiveFocus()
        } else if (needsServiceSelection) {
            rootFolderCombo.forceActiveFocus()
        }
    }

    /// Why Silo will not take a request for this title.
    function notRequestableText(reason) {
        switch (reason) {
        case "already_requested": return qsTr("This title has already been requested.")
        case "already_available": return qsTr("This title is already in the library.")
        default: return qsTr("You can't request this title right now.")
        }
    }

    function seasonListText(seasons) {
        var labels = []
        for (var i = 0; i < seasons.length; ++i)
            labels.push(qsTr("S%1").arg(seasons[i]))
        return labels.join(", ")
    }

    /**
     * Resets all state and opens the dialog for the given search-result item.
     *
     * Extracts mediaType, tmdbId, and mediaTitle from itemData then calls
     * MediaRequests.prepareRequest() to load the backend's options
     * asynchronously.  If itemData is missing required fields the dialog shows an
     * error immediately without making a network call.
     *
     * @param itemData     A discovery map (must contain requestMediaType and requestTmdbId).
     * @param focusTarget  Optional Item to restore focus to when the dialog closes.
     */
    function openForItem(itemData, focusTarget) {
        closeTimer.stop()
        restoreFocusTarget = focusTarget || null

        mediaType = String(itemData.requestMediaType || "").toLowerCase()
        tmdbId = Number(itemData.requestTmdbId || -1)
        mediaTitle = itemData.name || ""

        backend = ""
        seasonEntries = []
        coveredSeasons = []
        requestable = true
        notRequestableReason = ""
        requestId = String(itemData.requestId || "")
        canCancelRequest = !!itemData.canCancel
        followAvailable = false
        following = false
        watchlistAvailable = false
        inWatchlist = false
        watchlistRequests = false
        actionBusy = false
        servers = []
        profiles = []
        rootFolders = []
        selectedServerId = -1
        selectedProfileId = -1
        selectedRootFolderPath = ""
        requestAllSeasons = true
        selectedSeasons = []
        loadingOptions = true
        submitting = false
        statusText = ""
        statusError = false

        open()

        if (tmdbId > 0 && (mediaType === "movie" || mediaType === "tv")) {
            MediaRequests.prepareRequest(mediaType, tmdbId, mediaTitle)
        } else {
            loadingOptions = false
            statusText = qsTr("Invalid request item")
            statusError = true
        }
    }

    /**
     * Selects every season in seasonEntries.
     * Called when the dialog opens for a TV item or when "All Seasons" is re-checked.
     */
    function rebuildSeasonSelection() {
        selectedSeasons = seasonEntries.slice()
    }

    /**
     * Adds or removes a season number from selectedSeasons.
     *
     * @param seasonNumber  The 1-based season number to toggle.
     * @param checked       true to include the season, false to exclude it.
     */
    function toggleSeason(seasonNumber, checked) {
        var updated = []
        var exists = false
        for (var i = 0; i < selectedSeasons.length; ++i) {
            var season = selectedSeasons[i]
            if (season === seasonNumber) {
                exists = true
                if (checked) {
                    updated.push(season)
                }
            } else {
                updated.push(season)
            }
        }
        if (checked && !exists) {
            updated.push(seasonNumber)
        }
        updated.sort(function(a, b) { return a - b })
        selectedSeasons = updated
    }

    /**
     * Synchronises the checked state of every season CheckBox with selectedSeasons.
     *
     * Uses syncingSeasonChecks to suppress re-entrant toggleSeason() calls triggered
     * by the programmatic check-state changes.
     */
    function syncSeasonChecks() {
        if (!seasonRepeater) return
        syncingSeasonChecks = true
        for (var i = 0; i < seasonRepeater.count; ++i) {
            var item = seasonRepeater.itemAt(i)
            if (item) {
                item.checked = selectedSeasons.indexOf(item["seasonNumber"]) >= 0
            }
        }
        syncingSeasonChecks = false
    }

    /**
     * Validates selections and calls MediaRequests.createRequest() to submit the request.
     *
     * Validates the backend's selections (Seerr: server, profile, root folder;
     * Silo: the viewer may request the title); for TV items with
     * per-season selection, also validates that at least one season is chosen.  Sets
     * statusText and statusError on validation failure.  On success, sets submitting=true
     * and waits for the onRequestCreated or onErrorOccurred signal.
     */
    function submitRequest() {
        statusText = ""
        statusError = false

        if (!hasValidRequestSelections) {
            statusText = needsServiceSelection ? qsTr("Please select server/profile/root")
                                               : notRequestableText(notRequestableReason)
            statusError = true
            submitting = false
            return
        }

        if (tmdbId <= 0 || (mediaType !== "movie" && mediaType !== "tv")) {
            statusText = qsTr("Invalid request target")
            statusError = true
            return
        }

        if (hasSeasonChoices && !requestAllSeasons && selectedSeasons.length === 0) {
            statusText = qsTr("Select at least one season")
            statusError = true
            return
        }

        submitting = true
        MediaRequests.createRequest(
            mediaType,
            tmdbId,
            requestAllSeasons,
            selectedSeasons,
            selectedServerId,
            selectedProfileId,
            selectedRootFolderPath
        )
    }

    /** @brief Moves focus back to cancelButton when navigating up from serverCombo. */
    function focusPreviousFromServer() {
        cancelButton.forceActiveFocus()
    }

    /** @brief Moves focus forward from rootFolderCombo to the season section (TV) or cancelButton (movies). */
    function focusNextFromRoot() {
        if (hasSeasonChoices) {
            allSeasonsCheck.forceActiveFocus()
        } else {
            focusBelowOptions()
        }
    }

    /**
     * Scrolls the dialog's ScrollView so that @p item is fully visible.
     *
     * Maps the item's position into the content column coordinate space and
     * adjusts contentFlickable.contentY if the item is above or below the
     * current viewport.
     *
     * @param item  The QML Item that should be scrolled into view.
     */
    function ensureDialogItemVisible(item) {
        if (!item || !requestLayout || !contentScroll || !contentScroll.contentItem) {
            return
        }

        var flick = contentScroll.contentItem
        var pointInContent = item.mapToItem(requestLayout, 0, 0)
        var itemTop = pointInContent.y
        var itemBottom = itemTop + item.height
        var viewTop = flick["contentY"]
        var viewBottom = viewTop + flick.height

        if (itemTop < viewTop) {
            flick["contentY"] = Math.max(0, itemTop - Theme.spacingSmall)
        } else if (itemBottom > viewBottom) {
            var maxY = Math.max(0, flick["contentHeight"] - flick.height)
            flick["contentY"] = Math.min(maxY, itemBottom - flick.height + Theme.spacingSmall)
        }
    }

    /**
     * Fills the form from a Silo title detail: whether the viewer may request
     * it, and which seasons are still missing from the library and unrequested.
     */
    function applySiloPreparation(data) {
        requestable = !!data.requestable
        notRequestableReason = data.notRequestableReason || ""
        // Someone else's active request can be followed.
        var requestState = data.requestState || ""
        followAvailable = !!data.followSupported && requestState !== ""
                          && !data.requestedByViewer && data.requestStatus !== "available"
        following = !!data.following
        // Library titles use the library watchlist on their details page.
        watchlistAvailable = !!data.watchlistTitlesSupported && !(data.libraryItemId || "")
        inWatchlist = !!data.inWatchlist
        watchlistRequests = !!data.watchlistRequests
        if (requestId === "" && data.requestedByViewer && (data.activeRequestId || "") !== "") {
            requestId = data.activeRequestId
            canCancelRequest = requestState === "pending" || requestState === "approved"
        }
        if (isTv && data.seasonRequestsSupported) {
            var open = []
            var covered = []
            var seasons = data.seasons || []
            for (var i = 0; i < seasons.length; ++i) {
                var season = seasons[i]
                if (season.availability === "available" || season.requested)
                    covered.push(season.seasonNumber)
                else
                    open.push(season.seasonNumber)
            }
            seasonEntries = open
            coveredSeasons = covered
            rebuildSeasonSelection()
            requestAllSeasons = true
        }
        Qt.callLater(function() { focusInitialControl() })
    }

    title: MediaRequests.backendName !== "" ? qsTr("Request on %1").arg(MediaRequests.backendName)
                                            : qsTr("Request")

    // Keys cannot attach to the Dialog (a Popup), so content and footer
    // forward to this shared handler instead.
    function handleDialogKey(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
            close()
            return true
        }
        return false
    }

    background: DialogSurface {}
    Overlay.modal: ModalScrim {}

    header: Item {
        implicitHeight: Math.round(64 * Theme.layoutScale) + Theme.dialogPadding

        Text {
            text: dialog.title
            font.pixelSize: Theme.fontSizeTitle
            font.family: Theme.fontPrimary
            font.weight: Theme.fontWeightHeavy
            color: Theme.textPrimary
            anchors.left: parent.left
            anchors.leftMargin: Theme.dialogPadding
            anchors.bottom: parent.bottom
        }
    }

    contentItem: Item {
        implicitWidth: Math.round(640 * Theme.layoutScale)
        implicitHeight: Math.round(720 * Theme.layoutScale)

        Keys.onPressed: function(event) {
            if (dialog.handleDialogKey(event)) {
                event.accepted = true
            }
        }

        ScrollView {
            id: contentScroll
            anchors.fill: parent
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                id: requestLayout
                width: contentScroll.availableWidth
                spacing: Theme.spacingMedium

                Text {
                    text: mediaTitle
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    text: isTv ? qsTr("TV Series") : qsTr("Movie")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }

                BusyIndicator {
                    running: loadingOptions
                    visible: loadingOptions
                    Layout.alignment: Qt.AlignHCenter
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    visible: !loadingOptions

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    visible: needsServiceSelection

                Text {
                    text: qsTr("Server")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }

                SeerrComboBox {
                    id: serverCombo
                    Layout.fillWidth: true
                    model: servers
                    textRole: "name"
                    enabled: servers.length > 0

                    KeyNavigation.up: cancelButton
                    KeyNavigation.down: profileCombo

                    onPopupOpenedForKeyboardNav: setKeyboardNavigationMode()

                    onActivated: function(index) {
                        if (index >= 0 && index < servers.length) {
                            selectedServerId = servers[index].id
                        }
                    }

                    onNavigateUpRequested: focusPreviousFromServer()
                    onNavigateDownRequested: profileCombo.forceActiveFocus()
                }

                Text {
                    text: qsTr("Quality Profile")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }

                SeerrComboBox {
                    id: profileCombo
                    Layout.fillWidth: true
                    model: profiles
                    textRole: "name"
                    enabled: profiles.length > 0

                    KeyNavigation.up: serverCombo
                    KeyNavigation.down: rootFolderCombo

                    onPopupOpenedForKeyboardNav: setKeyboardNavigationMode()

                    onActivated: function(index) {
                        if (index >= 0 && index < profiles.length) {
                            selectedProfileId = profiles[index].id
                        }
                    }

                    onNavigateUpRequested: serverCombo.forceActiveFocus()
                    onNavigateDownRequested: rootFolderCombo.forceActiveFocus()
                }

                Text {
                    text: qsTr("Root Folder")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }

                SeerrComboBox {
                    id: rootFolderCombo
                    Layout.fillWidth: true
                    model: rootFolders
                    textRole: "path"
                    enabled: rootFolders.length > 0

                    KeyNavigation.up: profileCombo
                    KeyNavigation.down: hasSeasonChoices ? allSeasonsCheck : null

                    onPopupOpenedForKeyboardNav: setKeyboardNavigationMode()

                    onActivated: function(index) {
                        if (index >= 0 && index < rootFolders.length) {
                            selectedRootFolderPath = rootFolders[index].path || ""
                        }
                    }

                    onNavigateUpRequested: profileCombo.forceActiveFocus()
                    onNavigateDownRequested: focusNextFromRoot()
                }
                }

                Text {
                    text: notRequestableText(notRequestableReason)
                    visible: backend === "silo" && !requestable
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    visible: hasSeasonChoices

                    Text {
                        text: qsTr("Already in the library or requested: %1").arg(seasonListText(coveredSeasons))
                        visible: coveredSeasons.length > 0
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    CheckBox {
                        id: allSeasonsCheck
                        focusPolicy: Qt.StrongFocus
                        activeFocusOnTab: true
                        // Silo fills in only the aired seasons the library lacks.
                        text: backend === "silo" ? qsTr("All Missing Seasons") : qsTr("All Seasons")
                        checked: requestAllSeasons
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary

                        KeyNavigation.up: needsServiceSelection ? rootFolderCombo : null
                        KeyNavigation.down: !allSeasonsCheck.checked && seasonRepeater.count > 0 ? seasonRepeater.itemAt(0) : null

                        indicator: BloomCheckIndicator { control: allSeasonsCheck }
                        background: Item {
                            implicitHeight: Theme.buttonHeightSmall
                            FocusRing { active: allSeasonsCheck.activeFocus; radius: Theme.radiusSmall }
                        }

                        contentItem: Text {
                            text: allSeasonsCheck.text
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            leftPadding: allSeasonsCheck.indicator.width + Theme.spacingMedium
                            verticalAlignment: Text.AlignVCenter
                        }

                        onToggled: {
                            requestAllSeasons = checked
                            if (checked) {
                                rebuildSeasonSelection()
                            } else {
                                Qt.callLater(function() {
                                    var first = seasonRepeater.itemAt(0)
                                    if (first) {
                                        ensureDialogItemVisible(first)
                                        first.forceActiveFocus()
                                    }
                                })
                            }
                        }

                        Keys.onDownPressed: function(event) {
                            if (!allSeasonsCheck.checked && seasonRepeater.count > 0) {
                                var first = seasonRepeater.itemAt(0)
                                if (first) {
                                    first.forceActiveFocus()
                                    event.accepted = true
                                    return
                                }
                            }
                            focusBelowOptions()
                            event.accepted = true
                        }

                        Keys.onUpPressed: function(event) {
                            focusAboveSeasons()
                            event.accepted = true
                        }

                    }

                    GridLayout {
                        columns: 4
                        rowSpacing: Theme.spacingMedium
                        columnSpacing: Theme.spacingMedium
                        visible: !allSeasonsCheck.checked

                        Repeater {
                            id: seasonRepeater
                            model: seasonEntries
                            onItemAdded: function(index, item) {
                                syncSeasonChecks()
                            }

                            CheckBox {
                                id: seasonCheck
                                required property int index
                                required property var modelData
                                readonly property int seasonNumber: Number(modelData)
                                focusPolicy: Qt.StrongFocus
                                activeFocusOnTab: true
                                text: qsTr("S%1").arg(seasonNumber)
                                checked: false
                                onToggled: {
                                    if (!syncingSeasonChecks) {
                                        toggleSeason(seasonNumber, checked)
                                    }
                                }
                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        ensureDialogItemVisible(seasonCheck)
                                    }
                                }
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.fontPrimary

                                indicator: BloomCheckIndicator { control: seasonCheck }
                                background: Item {
                                    implicitHeight: Theme.buttonHeightSmall
                                    FocusRing { active: seasonCheck.activeFocus; radius: Theme.radiusSmall }
                                }

                                contentItem: Text {
                                    text: seasonCheck.text
                                    font.pixelSize: Theme.fontSizeBody
                                    font.family: Theme.fontPrimary
                                    color: Theme.textPrimary
                                    leftPadding: seasonCheck.indicator.width + Theme.spacingMedium
                                    verticalAlignment: Text.AlignVCenter
                                }

                                Keys.onLeftPressed: function(event) {
                                    if ((index % 4) > 0) {
                                        var prev = seasonRepeater.itemAt(index - 1)
                                        if (prev) {
                                            prev.forceActiveFocus()
                                            event.accepted = true
                                        }
                                    }
                                }

                                Keys.onRightPressed: function(event) {
                                    if (((index + 1) % 4) !== 0 && (index + 1) < seasonRepeater.count) {
                                        var next = seasonRepeater.itemAt(index + 1)
                                        if (next) {
                                            next.forceActiveFocus()
                                            event.accepted = true
                                        }
                                    }
                                }

                                Keys.onUpPressed: function(event) {
                                    if (index >= 4) {
                                        var up = seasonRepeater.itemAt(index - 4)
                                        if (up) {
                                            up.forceActiveFocus()
                                            event.accepted = true
                                        }
                                    } else {
                                        allSeasonsCheck.forceActiveFocus()
                                        event.accepted = true
                                    }
                                }

                                Keys.onDownPressed: function(event) {
                                    if (index + 4 < seasonRepeater.count) {
                                        var down = seasonRepeater.itemAt(index + 4)
                                        if (down) {
                                            down.forceActiveFocus()
                                            event.accepted = true
                                        }
                                    } else {
                                        focusBelowOptions()
                                        event.accepted = true
                                    }
                                }

                                Keys.onReturnPressed: function(event) {
                                    toggleSeason(seasonNumber, !checked)
                                    event.accepted = true
                                }

                                Keys.onEnterPressed: function(event) {
                                    toggleSeason(seasonNumber, !checked)
                                    event.accepted = true
                                }
                            }
                        }
                    }
                }
            }

                Text {
                    text: qsTr("Adding it to your watchlist also requests it.")
                    visible: !loadingOptions && watchlistAvailable && watchlistRequests && !inWatchlist && requestable
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                // Follow, watchlist, and withdraw: actions besides requesting.
                Flow {
                    id: extraActions
                    Layout.fillWidth: true
                    spacing: Theme.controlSpacing
                    visible: hasExtraActions

                    ActionButton {
                        id: followButton
                        variant: "secondary"
                        visible: followAvailable
                        enabled: !actionBusy
                        focusPolicy: Qt.StrongFocus
                        iconGlyph: following ? Icons.check : Icons.schedule
                        text: following ? qsTr("Notifying You") : qsTr("Notify Me When Available")
                        onClicked: {
                            actionBusy = true
                            statusText = ""
                            MediaRequests.setFollowing(mediaType, tmdbId, !following)
                        }
                        Keys.onLeftPressed: function(event) { moveExtraAction(followButton, -1); event.accepted = true }
                        Keys.onRightPressed: function(event) { moveExtraAction(followButton, 1); event.accepted = true }
                        Keys.onUpPressed: function(event) { focusAboveExtras(); event.accepted = true }
                        Keys.onDownPressed: function(event) { cancelButton.forceActiveFocus(); event.accepted = true }
                        Keys.onReturnPressed: function(event) { if (enabled) clicked(); event.accepted = true }
                        Keys.onEnterPressed: function(event) { if (enabled) clicked(); event.accepted = true }
                        onActiveFocusChanged: if (activeFocus) ensureDialogItemVisible(followButton)
                    }

                    ActionButton {
                        id: watchlistButton
                        variant: "secondary"
                        visible: watchlistAvailable
                        enabled: !actionBusy
                        focusPolicy: Qt.StrongFocus
                        iconGlyph: inWatchlist ? Icons.bookmark : Icons.bookmarkBorder
                        text: inWatchlist ? qsTr("On Watchlist") : qsTr("Add to Watchlist")
                        onClicked: {
                            actionBusy = true
                            statusText = ""
                            MediaRequests.setWatchlisted(mediaType, tmdbId, !inWatchlist)
                        }
                        Keys.onLeftPressed: function(event) { moveExtraAction(watchlistButton, -1); event.accepted = true }
                        Keys.onRightPressed: function(event) { moveExtraAction(watchlistButton, 1); event.accepted = true }
                        Keys.onUpPressed: function(event) { focusAboveExtras(); event.accepted = true }
                        Keys.onDownPressed: function(event) { cancelButton.forceActiveFocus(); event.accepted = true }
                        Keys.onReturnPressed: function(event) { if (enabled) clicked(); event.accepted = true }
                        Keys.onEnterPressed: function(event) { if (enabled) clicked(); event.accepted = true }
                        onActiveFocusChanged: if (activeFocus) ensureDialogItemVisible(watchlistButton)
                    }

                    ActionButton {
                        id: withdrawButton
                        variant: "destructive"
                        visible: canWithdraw
                        enabled: !actionBusy
                        focusPolicy: Qt.StrongFocus
                        iconGlyph: Icons.delete_
                        text: qsTr("Withdraw Request")
                        onClicked: {
                            actionBusy = true
                            statusText = ""
                            MediaRequests.cancelRequest(requestId, mediaType, tmdbId)
                        }
                        Keys.onLeftPressed: function(event) { moveExtraAction(withdrawButton, -1); event.accepted = true }
                        Keys.onRightPressed: function(event) { moveExtraAction(withdrawButton, 1); event.accepted = true }
                        Keys.onUpPressed: function(event) { focusAboveExtras(); event.accepted = true }
                        Keys.onDownPressed: function(event) { cancelButton.forceActiveFocus(); event.accepted = true }
                        Keys.onReturnPressed: function(event) { if (enabled) clicked(); event.accepted = true }
                        Keys.onEnterPressed: function(event) { if (enabled) clicked(); event.accepted = true }
                        onActiveFocusChanged: if (activeFocus) ensureDialogItemVisible(withdrawButton)
                    }
                }

                Text {
                    text: statusText
                    visible: statusText.length > 0
                    color: statusError ? Theme.errorColor : Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
    }

    footer: Item {
        implicitHeight: Theme.buttonHeightMedium + Theme.dialogPadding + Theme.spacingMedium
        implicitWidth: parent ? parent.width : 400

        Keys.onPressed: function(event) {
            if (dialog.handleDialogKey(event)) {
                event.accepted = true
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogPadding
            anchors.topMargin: Theme.spacingMedium
            spacing: Theme.controlSpacing

            ActionButton {
                id: cancelButton
                variant: "secondary"
                Layout.fillWidth: true
                focusPolicy: Qt.StrongFocus
                activeFocusOnTab: true
                text: qsTr("Cancel")
                onClicked: dialog.close()

                KeyNavigation.right: requestButton
                KeyNavigation.down: serverCombo.visible ? serverCombo : null



                Keys.onRightPressed: function(event) {
                    requestButton.forceActiveFocus()
                    event.accepted = true
                }

                Keys.onReturnPressed: function(event) {
                    clicked()
                    event.accepted = true
                }

                Keys.onEnterPressed: function(event) {
                    clicked()
                    event.accepted = true
                }

                Keys.onUpPressed: function(event) {
                    focusAboveButtons()
                    event.accepted = true
                }
            }

            ActionButton {
                id: requestButton
                variant: "primary"
                Layout.fillWidth: true
                focusPolicy: Qt.StrongFocus
                activeFocusOnTab: true
                text: submitting ? qsTr("Requesting...") : qsTr("Request")
                enabled: !loadingOptions && !submitting && hasValidRequestSelections
                onClicked: submitRequest()

                KeyNavigation.left: cancelButton
                KeyNavigation.down: serverCombo.visible ? serverCombo : null



                Keys.onLeftPressed: function(event) {
                    cancelButton.forceActiveFocus()
                    event.accepted = true
                }

                Keys.onReturnPressed: function(event) {
                    if (enabled) {
                        clicked()
                    }
                    event.accepted = true
                }

                Keys.onEnterPressed: function(event) {
                    if (enabled) {
                        clicked()
                    }
                    event.accepted = true
                }

                Keys.onUpPressed: function(event) {
                    focusAboveButtons()
                    event.accepted = true
                }
            }
        }
    }

    onOpened: {
        setKeyboardNavigationMode()
        Qt.callLater(function() { focusInitialControl() })
    }

    onClosed: {
        closeTimer.stop()
        restorePointerNavigationMode()
        Qt.callLater(function() {
            if (restoreFocusTarget) {
                restoreFocusTarget.forceActiveFocus()
            }
            restoreFocusTarget = null
        })
    }

    onSelectedSeasonsChanged: {
        Qt.callLater(function() { syncSeasonChecks() })
    }

    Connections {
        target: MediaRequests

        function onRequestPreparationLoaded(prepMediaType, prepTmdbId, data) {
            if (!dialog.visible || prepMediaType !== mediaType || prepTmdbId !== tmdbId) {
                return
            }

            loadingOptions = false
            backend = data.backend || ""

            if (backend === "silo") {
                applySiloPreparation(data)
                return
            }

            servers = data.servers || []
            profiles = data.profiles || []
            rootFolders = data.rootFolders || []

            selectedServerId = (data.selectedServerId !== undefined && data.selectedServerId !== null) ? data.selectedServerId : -1
            selectedProfileId = (data.selectedProfileId !== undefined && data.selectedProfileId !== null) ? data.selectedProfileId : -1
            selectedRootFolderPath = data.selectedRootFolderPath || ""

            if (isTv) {
                var entries = []
                for (var s = 1; s <= (data.seasonCount || 0); ++s)
                    entries.push(s)
                seasonEntries = entries
                rebuildSeasonSelection()
                requestAllSeasons = true
            }

            if (servers.length > 0) {
                for (var i = 0; i < servers.length; ++i) {
                    if (servers[i].id === selectedServerId) {
                        serverCombo.currentIndex = i
                        break
                    }
                }
            }

            if (profiles.length > 0) {
                for (var j = 0; j < profiles.length; ++j) {
                    if (profiles[j].id === selectedProfileId) {
                        profileCombo.currentIndex = j
                        break
                    }
                }
            }

            if (rootFolders.length > 0) {
                for (var k = 0; k < rootFolders.length; ++k) {
                    if ((rootFolders[k].path || "") === selectedRootFolderPath) {
                        rootFolderCombo.currentIndex = k
                        break
                    }
                }
            }

            if (servers.length > 0 && selectedServerId < 0) {
                serverCombo.currentIndex = 0
                selectedServerId = servers[0].id
            }
            if (profiles.length > 0 && selectedProfileId < 0) {
                profileCombo.currentIndex = 0
                selectedProfileId = profiles[0].id
            }
            if (rootFolders.length > 0 && selectedRootFolderPath === "") {
                rootFolderCombo.currentIndex = 0
                selectedRootFolderPath = rootFolders[0].path || ""
            }

            Qt.callLater(function() { focusInitialControl() })
        }

        function onRequestCreated(requestMediaType, requestTmdbId, requestData) {
            if (!dialog.visible || requestMediaType !== mediaType || requestTmdbId !== tmdbId) {
                return
            }

            submitting = false
            statusError = false
            switch (requestData.requestState || "") {
            case "pending":
                statusText = qsTr("Request submitted (pending approval)")
                break
            case "approved":
            case "processing":
                statusText = qsTr("Request submitted and approved")
                break
            case "available":
            case "partially_available":
                statusText = qsTr("Request submitted; it's already partly in the library")
                break
            default:
                statusText = qsTr("Request submitted")
            }

            closeTimer.start()
        }

        function onFollowingChanged(changedMediaType, changedTmdbId, nowFollowing) {
            if (!dialog.visible || changedMediaType !== mediaType || changedTmdbId !== tmdbId)
                return
            actionBusy = false
            following = nowFollowing
            statusError = false
            statusText = nowFollowing ? qsTr("You'll be notified when it's available.")
                                      : qsTr("You won't be notified about this title.")
        }

        function onWatchlistChanged(changedMediaType, changedTmdbId, nowInWatchlist) {
            if (!dialog.visible || changedMediaType !== mediaType || changedTmdbId !== tmdbId)
                return
            actionBusy = false
            inWatchlist = nowInWatchlist
            statusError = false
            statusText = nowInWatchlist ? qsTr("Added to your watchlist.")
                                        : qsTr("Removed from your watchlist.")
        }

        function onRequestCancelled(cancelledRequestId, cancelledMediaType, cancelledTmdbId) {
            if (!dialog.visible || cancelledRequestId !== requestId)
                return
            actionBusy = false
            canCancelRequest = false
            statusError = false
            statusText = qsTr("Request withdrawn")
            closeTimer.start()
        }

        function onErrorOccurred(endpoint, error) {
            if (!dialog.visible) {
                return
            }

            // An extra action failed: say so and keep the dialog as it was.
            if (endpoint === "cancel" || endpoint === "follow" || endpoint === "watchlist") {
                actionBusy = false
                statusText = error
                statusError = true
                return
            }

            if (endpoint !== "prepareRequest" && endpoint !== "request") {
                return
            }

            loadingOptions = false
            submitting = false
            statusText = error
            statusError = true
            Qt.callLater(function() { cancelButton.forceActiveFocus() })
        }
    }
}
