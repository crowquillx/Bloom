import QtQuick
import QtQuick.Layouts

import BloomUI

Rectangle {
    id: ratingChip

    property var ratingData: ({})

    property string originalSource: ratingData && ratingData.source ? String(ratingData.source) : ""
    property var score: ratingData && ratingData.score !== undefined ? ratingData.score : ratingData.value

    readonly property string normalizedSource: normalizedRatingSource(originalSource)
    readonly property string logoSource: ratingLogoSource(normalizedSource, score)
    readonly property string displayValue: ratingDisplayValue(normalizedSource, score)
    readonly property string fallbackText: ratingFallbackLabel(normalizedSource, originalSource)

    implicitHeight: Math.round(38 * Theme.layoutScale)
    implicitWidth: ratingRow.implicitWidth + Math.round(20 * Theme.layoutScale)
    radius: implicitHeight / 2
    color: Theme.chipBackground
    border.width: 1
    border.color: Theme.chipBorder
    visible: displayValue !== ""

    function normalizedRatingSource(source) {
        const normalized = String(source || "").toLowerCase().replace(/\s+/g, "_")
        if (normalized.indexOf("tomatoes") !== -1) return normalized.indexOf("audience") !== -1 ? "audience" : "tomatoes"
        if (normalized.indexOf("imdb") !== -1) return "imdb"
        if (normalized.indexOf("metacritic") !== -1) return "metacritic"
        if (normalized.indexOf("tmdb") !== -1) return "tmdb"
        if (normalized.indexOf("trakt") !== -1) return "trakt"
        if (normalized.indexOf("letterboxd") !== -1) return "letterboxd"
        if (normalized.indexOf("myanimelist") !== -1 || normalized === "mal") return "mal"
        if (normalized.indexOf("anilist") !== -1) return "anilist"
        if (normalized.indexOf("rogerebert") !== -1) return "rogerebert"
        if (normalized.indexOf("kinopoisk") !== -1) return "kinopoisk"
        if (normalized.indexOf("douban") !== -1) return "douban"
        return normalized
    }

    function ratingLogoSource(source, score) {
        const value = parseFloat(score) || 0
        if (source === "imdb") return "qrc:/images/ratings/imdb.png"
        if (source === "tmdb") return "qrc:/images/ratings/tmdb.png"
        if (source === "mal") return "qrc:/images/ratings/mal.png"
        if (source === "anilist") return "qrc:/images/ratings/anilist.png"
        if (source === "trakt") return "qrc:/images/ratings/trakt.png"
        if (source === "letterboxd") return "qrc:/images/ratings/letterboxd.png"
        if (source === "metacritic") return "qrc:/images/ratings/metacritic.png"
        if (source === "rogerebert") return "qrc:/images/ratings/rogerebert.png"
        if (source === "kinopoisk") return "qrc:/images/ratings/kinopoisk.png"
        if (source === "douban") return "qrc:/images/ratings/douban.png"
        if (source === "tomatoes") {
            if (value < 60) return "qrc:/images/ratings/tomatoes_rotten.png"
            if (value >= 75) return "qrc:/images/ratings/tomatoes_certified.png"
            return "qrc:/images/ratings/tomatoes.png"
        }
        if (source === "audience") {
            if (value < 60) return "qrc:/images/ratings/audience_rotten.png"
            return "qrc:/images/ratings/audience.png"
        }
        return ""
    }

    function ratingFallbackLabel(source, originalSource) {
        if (source === "imdb") return "IMDb"
        if (source === "tomatoes") return "RT"
        if (source === "audience") return "Popcorn"
        if (source === "metacritic") return "Meta"
        if (source === "mal") return "MAL"
        if (source === "anilist") return "AniList"
        return String(originalSource || "")
    }

    function ratingDisplayValue(source, score) {
        const value = parseFloat(score)
        if (!isFinite(value) || value <= 0) {
            return ""
        }
        if (source === "tomatoes" || source === "audience" || source === "rogerebert") {
            return Math.round(value) + "%"
        }
        return Number.isInteger(value) ? String(value) : value.toFixed(1)
    }

    RowLayout {
        id: ratingRow
        anchors.centerIn: parent
        spacing: Math.round(6 * Theme.layoutScale)

        Item {
            Layout.preferredWidth: Math.round(42 * Theme.layoutScale)
            Layout.preferredHeight: Math.round(16 * Theme.layoutScale)
            Layout.alignment: Qt.AlignVCenter

            Image {
                anchors.fill: parent
                source: ratingChip.logoSource
                fillMode: Image.PreserveAspectFit
                visible: source !== ""
                asynchronous: true
                cache: true
            }

            Text {
                anchors.centerIn: parent
                visible: ratingChip.logoSource === ""
                text: ratingChip.fallbackText
                font.pixelSize: Math.round(13 * Theme.layoutScale)
                font.family: Theme.fontPrimary
                font.weight: Font.Black
                color: Theme.textSecondary
            }
        }

        Text {
            text: ratingChip.displayValue
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.fontPrimary
            font.weight: Font.Black
            color: Theme.textPrimary
            verticalAlignment: Text.AlignVCenter
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
