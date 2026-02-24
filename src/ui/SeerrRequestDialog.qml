import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

Dialog {
    id: dialog
    modal: true
    anchors.centerIn: parent
    width: Math.round(720 * Theme.layoutScale)
    padding: Theme.spacingLarge

    property Item restoreFocusTarget: null
    property string mediaType: ""
    property int tmdbId: -1
    property string mediaTitle: ""
    property int seasonCount: 0

    property var servers: []
    property var profiles: []
    property var rootFolders: []

    property int selectedServerId: -1
    property int selectedProfileId: -1
    property string selectedRootFolderPath: ""

    property bool requestAllSeasons: true
    property var selectedSeasons: []

    property bool loadingOptions: false
    property bool submitting: false
    property string statusText: ""
    property bool statusError: false

    readonly property bool isTv: mediaType === "tv"

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            close()
            event.accepted = true
        }
    }

    function openForItem(itemData, focusTarget) {
        restoreFocusTarget = focusTarget || null

        mediaType = String(itemData.SeerrMediaType || "").toLowerCase()
        tmdbId = Number(itemData.SeerrTmdbId || -1)
        mediaTitle = itemData.Name || ""

        seasonCount = 0
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
            SeerrService.prepareRequest(mediaType, tmdbId, mediaTitle)
        } else {
            loadingOptions = false
            statusText = qsTr("Invalid Seerr item")
            statusError = true
        }
    }

    function rebuildSeasonSelection() {
        selectedSeasons = []
        for (var i = 1; i <= seasonCount; ++i) {
            selectedSeasons.push(i)
        }
    }

    function updateAllSeasonsFlag() {
        requestAllSeasons = (seasonCount > 0 && selectedSeasons.length === seasonCount)
    }

    function toggleSeason(seasonNumber, checked) {
        var updated = []
        var exists = false
        for (var i = 0; i < selectedSeasons.length; ++i) {
            var season = selectedSeasons[i]
            if (season === seasonNumber) {
                exists = true
                if (checked) updated.push(season)
            } else {
                updated.push(season)
            }
        }
        if (checked && !exists) {
            updated.push(seasonNumber)
        }
        updated.sort(function(a, b) { return a - b })
        selectedSeasons = updated
        updateAllSeasonsFlag()
    }

    function submitRequest() {
        statusText = ""
        statusError = false

        if (tmdbId <= 0 || (mediaType !== "movie" && mediaType !== "tv")) {
            statusText = qsTr("Invalid request target")
            statusError = true
            return
        }

        if (mediaType === "tv" && !requestAllSeasons && selectedSeasons.length === 0) {
            statusText = qsTr("Select at least one season")
            statusError = true
            return
        }

        submitting = true
        SeerrService.createRequest(
            mediaType,
            tmdbId,
            requestAllSeasons,
            selectedSeasons,
            selectedServerId,
            selectedProfileId,
            selectedRootFolderPath
        )
    }

    title: qsTr("Request on Seerr")

    background: Rectangle {
        color: Theme.cardBackground
        radius: Theme.radiusMedium
        border.color: Theme.cardBorder
        border.width: 1
    }

    header: Rectangle {
        color: "transparent"
        height: Math.round(64 * Theme.layoutScale)

        Text {
            text: dialog.title
            font.pixelSize: Theme.fontSizeTitle
            font.family: Theme.fontPrimary
            font.weight: Font.DemiBold
            color: Theme.textPrimary
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingLarge
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    contentItem: Item {
        implicitWidth: Math.round(640 * Theme.layoutScale)
        implicitHeight: requestLayout.implicitHeight

        ColumnLayout {
            id: requestLayout
            anchors.fill: parent
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
                spacing: Theme.spacingSmall
                visible: !loadingOptions

                Text {
                    text: qsTr("Server")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }

                ComboBox {
                    id: serverCombo
                    Layout.fillWidth: true
                    model: servers
                    textRole: "name"

                    onActivated: function(index) {
                        if (index >= 0 && index < servers.length) {
                            selectedServerId = servers[index].id
                        }
                    }

                    Keys.onDownPressed: function(event) {
                        if (!popup.visible) {
                            profileCombo.forceActiveFocus()
                            event.accepted = true
                        }
                    }

                    Keys.onUpPressed: function(event) {
                        if (!popup.visible) {
                            cancelButton.forceActiveFocus()
                            event.accepted = true
                        }
                    }
                }

                Text {
                    text: qsTr("Quality Profile")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }

                ComboBox {
                    id: profileCombo
                    Layout.fillWidth: true
                    model: profiles
                    textRole: "name"

                    onActivated: function(index) {
                        if (index >= 0 && index < profiles.length) {
                            selectedProfileId = profiles[index].id
                        }
                    }

                    Keys.onDownPressed: function(event) {
                        if (!popup.visible) {
                            rootFolderCombo.forceActiveFocus()
                            event.accepted = true
                        }
                    }

                    Keys.onUpPressed: function(event) {
                        if (!popup.visible) {
                            serverCombo.forceActiveFocus()
                            event.accepted = true
                        }
                    }
                }

                Text {
                    text: qsTr("Root Folder")
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.fontPrimary
                    color: Theme.textSecondary
                }

                ComboBox {
                    id: rootFolderCombo
                    Layout.fillWidth: true
                    model: rootFolders
                    textRole: "path"

                    onActivated: function(index) {
                        if (index >= 0 && index < rootFolders.length) {
                            selectedRootFolderPath = rootFolders[index].path || ""
                        }
                    }

                    Keys.onDownPressed: function(event) {
                        if (!popup.visible) {
                            if (isTv && seasonCount > 0) {
                                allSeasonsCheck.forceActiveFocus()
                            } else {
                                cancelButton.forceActiveFocus()
                            }
                            event.accepted = true
                        }
                    }

                    Keys.onUpPressed: function(event) {
                        if (!popup.visible) {
                            profileCombo.forceActiveFocus()
                            event.accepted = true
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    visible: isTv && seasonCount > 0

                    CheckBox {
                        id: allSeasonsCheck
                        text: qsTr("All Seasons")
                        checked: requestAllSeasons
                        onToggled: {
                            requestAllSeasons = checked
                            if (checked) {
                                rebuildSeasonSelection()
                            }
                        }

                        Keys.onDownPressed: function(event) {
                            if (!requestAllSeasons && seasonCount > 0 && seasonRepeater.count > 0) {
                                var first = seasonRepeater.itemAt(0)
                                if (first) {
                                    first.forceActiveFocus()
                                    event.accepted = true
                                    return
                                }
                            }
                            cancelButton.forceActiveFocus()
                            event.accepted = true
                        }

                        Keys.onUpPressed: function(event) {
                            rootFolderCombo.forceActiveFocus()
                            event.accepted = true
                        }
                    }

                    GridLayout {
                        columns: 4
                        rowSpacing: Theme.spacingSmall
                        columnSpacing: Theme.spacingMedium
                        visible: !requestAllSeasons

                        Repeater {
                            id: seasonRepeater
                            model: seasonCount

                            CheckBox {
                                required property int index
                                readonly property int seasonNumber: index + 1
                                text: qsTr("S%1").arg(seasonNumber)
                                checked: selectedSeasons.indexOf(seasonNumber) >= 0
                                onToggled: toggleSeason(seasonNumber, checked)

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
                                        cancelButton.forceActiveFocus()
                                        event.accepted = true
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Text {
                text: statusText
                visible: statusText.length > 0
                color: statusError ? "#ff6b6b" : Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    footer: Item {
        implicitHeight: Theme.buttonHeightSmall + Theme.spacingLarge * 2
        implicitWidth: parent ? parent.width : 400

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Button {
                id: cancelButton
                Layout.fillWidth: true
                text: qsTr("Cancel")
                onClicked: dialog.close()

                Keys.onRightPressed: function(event) {
                    requestButton.forceActiveFocus()
                    event.accepted = true
                }

                Keys.onUpPressed: function(event) {
                    if (isTv && seasonCount > 0) {
                        if (!requestAllSeasons && seasonRepeater.count > 0) {
                            var last = seasonRepeater.itemAt(seasonRepeater.count - 1)
                            if (last) {
                                last.forceActiveFocus()
                                event.accepted = true
                                return
                            }
                        }
                        allSeasonsCheck.forceActiveFocus()
                    } else {
                        rootFolderCombo.forceActiveFocus()
                    }
                    event.accepted = true
                }
            }

            Button {
                id: requestButton
                Layout.fillWidth: true
                text: submitting ? qsTr("Requesting...") : qsTr("Request")
                enabled: !loadingOptions && !submitting
                onClicked: submitRequest()

                Keys.onLeftPressed: function(event) {
                    cancelButton.forceActiveFocus()
                    event.accepted = true
                }

                Keys.onUpPressed: function(event) {
                    if (isTv && seasonCount > 0) {
                        if (!requestAllSeasons && seasonRepeater.count > 0) {
                            var last = seasonRepeater.itemAt(seasonRepeater.count - 1)
                            if (last) {
                                last.forceActiveFocus()
                                event.accepted = true
                                return
                            }
                        }
                        allSeasonsCheck.forceActiveFocus()
                    } else {
                        rootFolderCombo.forceActiveFocus()
                    }
                    event.accepted = true
                }
            }
        }
    }

    onOpened: {
        Qt.callLater(function() {
            if (loadingOptions) {
                cancelButton.forceActiveFocus()
            } else {
                serverCombo.forceActiveFocus()
            }
        })
    }

    onClosed: {
        Qt.callLater(function() {
            if (restoreFocusTarget) {
                restoreFocusTarget.forceActiveFocus()
            }
            restoreFocusTarget = null
        })
    }

    Connections {
        target: SeerrService

        function onRequestPreparationLoaded(prepMediaType, prepTmdbId, data) {
            if (!dialog.visible || prepMediaType !== mediaType || prepTmdbId !== tmdbId) return

            loadingOptions = false

            servers = data.servers || []
            profiles = data.profiles || []
            rootFolders = data.rootFolders || []

            selectedServerId = (data.selectedServerId !== undefined && data.selectedServerId !== null) ? data.selectedServerId : -1
            selectedProfileId = (data.selectedProfileId !== undefined && data.selectedProfileId !== null) ? data.selectedProfileId : -1
            selectedRootFolderPath = data.selectedRootFolderPath || ""

            if (isTv) {
                seasonCount = data.seasonCount || 0
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

            Qt.callLater(function() { serverCombo.forceActiveFocus() })
        }

        function onRequestCreated(requestMediaType, requestTmdbId, requestData) {
            if (!dialog.visible || requestMediaType !== mediaType || requestTmdbId !== tmdbId) return

            submitting = false
            statusError = false
            var statusCode = requestData.status || 0
            if (statusCode === 1) {
                statusText = qsTr("Request submitted (pending approval)")
            } else if (statusCode === 2) {
                statusText = qsTr("Request submitted and approved")
            } else {
                statusText = qsTr("Request submitted")
            }

            Qt.callLater(function() { dialog.close() })
        }

        function onErrorOccurred(endpoint, error) {
            if (!dialog.visible) return
            if (endpoint !== "prepareRequest" && endpoint !== "service/radarr" && endpoint !== "service/sonarr" && endpoint !== "service/details" && endpoint !== "tv/details" && endpoint !== "request") {
                return
            }
            loadingOptions = false
            submitting = false
            statusText = error
            statusError = true
        }
    }
}
