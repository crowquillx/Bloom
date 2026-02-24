import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import BloomUI

Dialog {
    id: dialog
    modal: true
    focus: true
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

    function setKeyboardNavigationMode() {
        if (typeof InputModeManager !== "undefined") {
            InputModeManager.setNavigationMode("keyboard")
            InputModeManager.hideCursor(true)
        }
    }

    function restorePointerNavigationMode() {
        if (typeof InputModeManager !== "undefined") {
            InputModeManager.setNavigationMode("pointer")
            InputModeManager.hideCursor(false)
        }
    }

    function focusInitialControl() {
        if (!visible) return
        dialog.forceActiveFocus()
        if (loadingOptions) {
            cancelButton.forceActiveFocus()
            return
        }
        if (serverCombo.visible && serverCombo.enabled) {
            serverCombo.forceActiveFocus()
        } else {
            cancelButton.forceActiveFocus()
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

    function focusPreviousFromServer() {
        cancelButton.forceActiveFocus()
    }

    function focusNextFromRoot() {
        if (isTv && seasonCount > 0) {
            allSeasonsCheck.forceActiveFocus()
        } else {
            cancelButton.forceActiveFocus()
        }
    }

    title: qsTr("Request on Seerr")

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            close()
            event.accepted = true
        }
    }

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
                    focusPolicy: Qt.StrongFocus
                    activeFocusOnTab: true
                    Keys.priority: Keys.BeforeItem
                    model: servers
                    textRole: "name"
                    enabled: servers.length > 0
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary

                    KeyNavigation.up: cancelButton
                    KeyNavigation.down: profileCombo

                    contentItem: Text {
                        text: serverCombo.displayText
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: serverCombo.enabled ? Theme.textPrimary : Theme.textDisabled
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.spacingSmall
                        rightPadding: Theme.spacingXLarge
                        elide: Text.ElideRight
                    }

                    indicator: Text {
                        text: "▼"
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingMedium
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    background: Rectangle {
                        implicitHeight: Theme.buttonHeightSmall
                        radius: Theme.radiusSmall
                        color: Theme.inputBackground
                        border.color: serverCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                        border.width: serverCombo.activeFocus ? 2 : 1
                        opacity: serverCombo.enabled ? 1.0 : 0.5
                    }

                    delegate: ItemDelegate {
                        required property var modelData
                        width: serverCombo.width
                        enabled: serverCombo.enabled
                        readonly property bool isCurrent: ListView.isCurrentItem

                        contentItem: Text {
                            text: modelData.name || ""
                            color: isCurrent ? Theme.textPrimary : Theme.textSecondary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        background: Rectangle {
                            color: isCurrent ? Theme.buttonPrimaryBackground : "transparent"
                            radius: Theme.radiusSmall
                        }
                    }

                    popup: Popup {
                        y: serverCombo.height + 5
                        width: serverCombo.width
                        implicitHeight: Math.min(contentItem.implicitHeight, 280)
                        padding: 1
                        closePolicy: Popup.CloseOnEscape

                        onOpened: {
                            setKeyboardNavigationMode()
                            serverList.currentIndex = Math.max(0, serverCombo.currentIndex)
                            serverList.forceActiveFocus()
                        }

                        onClosed: serverCombo.forceActiveFocus()

                        contentItem: ListView {
                            id: serverList
                            clip: true
                            implicitHeight: contentHeight
                            model: serverCombo.popup.visible ? serverCombo.delegateModel : null
                            currentIndex: Math.max(0, serverCombo.currentIndex)

                            Keys.onReturnPressed: function(event) {
                                serverCombo.currentIndex = currentIndex
                                serverCombo.popup.close()
                                event.accepted = true
                            }

                            Keys.onEnterPressed: function(event) {
                                serverCombo.currentIndex = currentIndex
                                serverCombo.popup.close()
                                event.accepted = true
                            }

                            Keys.onEscapePressed: serverCombo.popup.close()
                        }

                        background: Rectangle {
                            color: Theme.cardBackground
                            border.color: Theme.focusBorder
                            border.width: 1
                            radius: Theme.radiusSmall
                        }
                    }

                    onActivated: function(index) {
                        if (index >= 0 && index < servers.length) {
                            selectedServerId = servers[index].id
                        }
                    }

                    Keys.onPressed: function(event) {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !popup.visible) {
                            popup.open()
                            event.accepted = true
                        }
                    }

                    Keys.onUpPressed: function(event) {
                        if (!popup.visible) {
                            focusPreviousFromServer()
                            event.accepted = true
                        }
                    }

                    Keys.onDownPressed: function(event) {
                        if (!popup.visible) {
                            profileCombo.forceActiveFocus()
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
                    focusPolicy: Qt.StrongFocus
                    activeFocusOnTab: true
                    Keys.priority: Keys.BeforeItem
                    model: profiles
                    textRole: "name"
                    enabled: profiles.length > 0
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary

                    KeyNavigation.up: serverCombo
                    KeyNavigation.down: rootFolderCombo

                    contentItem: Text {
                        text: profileCombo.displayText
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: profileCombo.enabled ? Theme.textPrimary : Theme.textDisabled
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.spacingSmall
                        rightPadding: Theme.spacingXLarge
                        elide: Text.ElideRight
                    }

                    indicator: Text {
                        text: "▼"
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingMedium
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    background: Rectangle {
                        implicitHeight: Theme.buttonHeightSmall
                        radius: Theme.radiusSmall
                        color: Theme.inputBackground
                        border.color: profileCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                        border.width: profileCombo.activeFocus ? 2 : 1
                        opacity: profileCombo.enabled ? 1.0 : 0.5
                    }

                    delegate: ItemDelegate {
                        required property var modelData
                        width: profileCombo.width
                        enabled: profileCombo.enabled
                        readonly property bool isCurrent: ListView.isCurrentItem

                        contentItem: Text {
                            text: modelData.name || ""
                            color: isCurrent ? Theme.textPrimary : Theme.textSecondary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        background: Rectangle {
                            color: isCurrent ? Theme.buttonPrimaryBackground : "transparent"
                            radius: Theme.radiusSmall
                        }
                    }

                    popup: Popup {
                        y: profileCombo.height + 5
                        width: profileCombo.width
                        implicitHeight: Math.min(contentItem.implicitHeight, 280)
                        padding: 1
                        closePolicy: Popup.CloseOnEscape

                        onOpened: {
                            setKeyboardNavigationMode()
                            profileList.currentIndex = Math.max(0, profileCombo.currentIndex)
                            profileList.forceActiveFocus()
                        }

                        onClosed: profileCombo.forceActiveFocus()

                        contentItem: ListView {
                            id: profileList
                            clip: true
                            implicitHeight: contentHeight
                            model: profileCombo.popup.visible ? profileCombo.delegateModel : null
                            currentIndex: Math.max(0, profileCombo.currentIndex)

                            Keys.onReturnPressed: function(event) {
                                profileCombo.currentIndex = currentIndex
                                profileCombo.popup.close()
                                event.accepted = true
                            }

                            Keys.onEnterPressed: function(event) {
                                profileCombo.currentIndex = currentIndex
                                profileCombo.popup.close()
                                event.accepted = true
                            }

                            Keys.onEscapePressed: profileCombo.popup.close()
                        }

                        background: Rectangle {
                            color: Theme.cardBackground
                            border.color: Theme.focusBorder
                            border.width: 1
                            radius: Theme.radiusSmall
                        }
                    }

                    onActivated: function(index) {
                        if (index >= 0 && index < profiles.length) {
                            selectedProfileId = profiles[index].id
                        }
                    }

                    Keys.onPressed: function(event) {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !popup.visible) {
                            popup.open()
                            event.accepted = true
                        }
                    }

                    Keys.onUpPressed: function(event) {
                        if (!popup.visible) {
                            serverCombo.forceActiveFocus()
                            event.accepted = true
                        }
                    }

                    Keys.onDownPressed: function(event) {
                        if (!popup.visible) {
                            rootFolderCombo.forceActiveFocus()
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
                    focusPolicy: Qt.StrongFocus
                    activeFocusOnTab: true
                    Keys.priority: Keys.BeforeItem
                    model: rootFolders
                    textRole: "path"
                    enabled: rootFolders.length > 0
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary

                    KeyNavigation.up: profileCombo
                    KeyNavigation.down: isTv && seasonCount > 0 ? allSeasonsCheck : cancelButton

                    contentItem: Text {
                        text: rootFolderCombo.displayText
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary
                        color: rootFolderCombo.enabled ? Theme.textPrimary : Theme.textDisabled
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.spacingSmall
                        rightPadding: Theme.spacingXLarge
                        elide: Text.ElideRight
                    }

                    indicator: Text {
                        text: "▼"
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: Theme.fontPrimary
                        color: Theme.textSecondary
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingMedium
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    background: Rectangle {
                        implicitHeight: Theme.buttonHeightSmall
                        radius: Theme.radiusSmall
                        color: Theme.inputBackground
                        border.color: rootFolderCombo.activeFocus ? Theme.focusBorder : Theme.inputBorder
                        border.width: rootFolderCombo.activeFocus ? 2 : 1
                        opacity: rootFolderCombo.enabled ? 1.0 : 0.5
                    }

                    delegate: ItemDelegate {
                        required property var modelData
                        width: rootFolderCombo.width
                        enabled: rootFolderCombo.enabled
                        readonly property bool isCurrent: ListView.isCurrentItem

                        contentItem: Text {
                            text: modelData.path || ""
                            color: isCurrent ? Theme.textPrimary : Theme.textSecondary
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        background: Rectangle {
                            color: isCurrent ? Theme.buttonPrimaryBackground : "transparent"
                            radius: Theme.radiusSmall
                        }
                    }

                    popup: Popup {
                        y: rootFolderCombo.height + 5
                        width: rootFolderCombo.width
                        implicitHeight: Math.min(contentItem.implicitHeight, 280)
                        padding: 1
                        closePolicy: Popup.CloseOnEscape

                        onOpened: {
                            setKeyboardNavigationMode()
                            rootFolderList.currentIndex = Math.max(0, rootFolderCombo.currentIndex)
                            rootFolderList.forceActiveFocus()
                        }

                        onClosed: rootFolderCombo.forceActiveFocus()

                        contentItem: ListView {
                            id: rootFolderList
                            clip: true
                            implicitHeight: contentHeight
                            model: rootFolderCombo.popup.visible ? rootFolderCombo.delegateModel : null
                            currentIndex: Math.max(0, rootFolderCombo.currentIndex)

                            Keys.onReturnPressed: function(event) {
                                rootFolderCombo.currentIndex = currentIndex
                                rootFolderCombo.popup.close()
                                event.accepted = true
                            }

                            Keys.onEnterPressed: function(event) {
                                rootFolderCombo.currentIndex = currentIndex
                                rootFolderCombo.popup.close()
                                event.accepted = true
                            }

                            Keys.onEscapePressed: rootFolderCombo.popup.close()
                        }

                        background: Rectangle {
                            color: Theme.cardBackground
                            border.color: Theme.focusBorder
                            border.width: 1
                            radius: Theme.radiusSmall
                        }
                    }

                    onActivated: function(index) {
                        if (index >= 0 && index < rootFolders.length) {
                            selectedRootFolderPath = rootFolders[index].path || ""
                        }
                    }

                    Keys.onPressed: function(event) {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !popup.visible) {
                            popup.open()
                            event.accepted = true
                        }
                    }

                    Keys.onUpPressed: function(event) {
                        if (!popup.visible) {
                            profileCombo.forceActiveFocus()
                            event.accepted = true
                        }
                    }

                    Keys.onDownPressed: function(event) {
                        if (!popup.visible) {
                            focusNextFromRoot()
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
                        focusPolicy: Qt.StrongFocus
                        activeFocusOnTab: true
                        text: qsTr("All Seasons")
                        checked: requestAllSeasons
                        font.pixelSize: Theme.fontSizeBody
                        font.family: Theme.fontPrimary

                        KeyNavigation.up: rootFolderCombo
                        KeyNavigation.down: !requestAllSeasons && seasonRepeater.count > 0 ? seasonRepeater.itemAt(0) : cancelButton

                        indicator: Rectangle {
                            implicitWidth: 20
                            implicitHeight: 20
                            radius: Theme.radiusSmall
                            color: allSeasonsCheck.checked ? Theme.buttonPrimaryBackground : Theme.inputBackground
                            border.color: allSeasonsCheck.activeFocus ? Theme.focusBorder : Theme.inputBorder
                            border.width: allSeasonsCheck.activeFocus ? 2 : 1

                            Text {
                                anchors.centerIn: parent
                                text: allSeasonsCheck.checked ? "✓" : ""
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.fontPrimary
                                color: Theme.textPrimary
                            }
                        }

                        contentItem: Text {
                            text: allSeasonsCheck.text
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            color: Theme.textPrimary
                            leftPadding: allSeasonsCheck.indicator.width + Theme.spacingSmall
                            verticalAlignment: Text.AlignVCenter
                        }

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

                        Keys.onReturnPressed: function(event) {
                            checked = !checked
                            event.accepted = true
                        }

                        Keys.onEnterPressed: function(event) {
                            checked = !checked
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
                                id: seasonCheck
                                required property int index
                                readonly property int seasonNumber: index + 1
                                focusPolicy: Qt.StrongFocus
                                activeFocusOnTab: true
                                text: qsTr("S%1").arg(seasonNumber)
                                checked: selectedSeasons.indexOf(seasonNumber) >= 0
                                onToggled: toggleSeason(seasonNumber, checked)
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.fontPrimary

                                indicator: Rectangle {
                                    implicitWidth: 18
                                    implicitHeight: 18
                                    radius: Theme.radiusSmall
                                    color: seasonCheck.checked ? Theme.buttonPrimaryBackground : Theme.inputBackground
                                    border.color: seasonCheck.activeFocus ? Theme.focusBorder : Theme.inputBorder
                                    border.width: seasonCheck.activeFocus ? 2 : 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: seasonCheck.checked ? "✓" : ""
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.family: Theme.fontPrimary
                                        color: Theme.textPrimary
                                    }
                                }

                                contentItem: Text {
                                    text: seasonCheck.text
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.family: Theme.fontPrimary
                                    color: Theme.textPrimary
                                    leftPadding: seasonCheck.indicator.width + Theme.spacingSmall
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
                                        cancelButton.forceActiveFocus()
                                        event.accepted = true
                                    }
                                }

                                Keys.onReturnPressed: function(event) {
                                    checked = !checked
                                    event.accepted = true
                                }

                                Keys.onEnterPressed: function(event) {
                                    checked = !checked
                                    event.accepted = true
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
                focusPolicy: Qt.StrongFocus
                activeFocusOnTab: true
                text: qsTr("Cancel")
                onClicked: dialog.close()

                KeyNavigation.up: isTv && seasonCount > 0 ? allSeasonsCheck : rootFolderCombo
                KeyNavigation.right: requestButton
                KeyNavigation.down: serverCombo.visible ? serverCombo : null

                contentItem: Text {
                    text: cancelButton.text
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    implicitHeight: Theme.buttonHeightSmall
                    radius: Theme.radiusSmall
                    color: cancelButton.down
                        ? Theme.buttonSecondaryBackgroundPressed
                        : (cancelButton.hovered ? Theme.buttonSecondaryBackgroundHover : Theme.buttonSecondaryBackground)
                    border.color: cancelButton.activeFocus ? Theme.focusBorder : Theme.inputBorder
                    border.width: cancelButton.activeFocus ? 2 : 1
                }

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
                focusPolicy: Qt.StrongFocus
                activeFocusOnTab: true
                text: submitting ? qsTr("Requesting...") : qsTr("Request")
                enabled: !loadingOptions && !submitting
                onClicked: submitRequest()

                KeyNavigation.up: isTv && seasonCount > 0 ? allSeasonsCheck : rootFolderCombo
                KeyNavigation.left: cancelButton
                KeyNavigation.down: serverCombo.visible ? serverCombo : null

                contentItem: Text {
                    text: requestButton.text
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: requestButton.enabled ? Theme.textPrimary : Theme.textDisabled
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    implicitHeight: Theme.buttonHeightSmall
                    radius: Theme.radiusSmall
                    color: requestButton.enabled
                        ? (requestButton.down
                           ? Theme.buttonPrimaryBackgroundPressed
                           : (requestButton.hovered ? Theme.buttonPrimaryBackgroundHover : Theme.buttonPrimaryBackground))
                        : Theme.buttonSecondaryBackground
                    border.color: requestButton.activeFocus ? Theme.focusBorder : Theme.inputBorder
                    border.width: requestButton.activeFocus ? 2 : 1
                }

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
        setKeyboardNavigationMode()
        Qt.callLater(function() { focusInitialControl() })
    }

    onClosed: {
        restorePointerNavigationMode()
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
            if (!dialog.visible || prepMediaType !== mediaType || prepTmdbId !== tmdbId) {
                return
            }

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
            if (!dialog.visible) {
                return
            }

            if (endpoint !== "prepareRequest" && endpoint !== "service/radarr" && endpoint !== "service/sonarr" && endpoint !== "service/details" && endpoint !== "tv/details" && endpoint !== "request") {
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
