#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace Bloom {

/**
 * One subtitle an online provider offers for a media file, found through the
 * server (Silo searchSubtitles, Jellyfin SearchRemoteSubtitles). Provider
 * adapters build it from their wire results; the player UI and view models
 * see only this shape (camelCase keys in toVariantMap).
 */
struct SubtitleSearchResult
{
    QString id;           // provider result id, opaque; the download names it
    QString provider;     // provider name ("opensubtitles")
    QString language;     // as the server sent it (BCP 47 on Silo, ISO 639-2 on Jellyfin)
    QString title;        // release name
    QString format;       // "srt", "ass", ...
    qint64 downloads = 0;
    double score = 0.0;   // provider score or community rating; higher is better
    bool hearingImpaired = false;
    bool forced = false;
    bool machineTranslated = false;
    bool aiTranslated = false;
    bool hashMatch = false; // the provider matched this exact file
    QDateTime uploadedAt;

    [[nodiscard]] bool isValid() const { return !id.isEmpty(); }
    [[nodiscard]] QVariantMap toVariantMap() const;
    [[nodiscard]] static SubtitleSearchResult fromVariantMap(const QVariantMap &map);

    bool operator==(const SubtitleSearchResult &) const = default;
};

/// Best first: results that match the file, then by score, then by
/// downloads. Ties keep the server's order.
[[nodiscard]] QList<SubtitleSearchResult> sortSubtitleSearchResults(QList<SubtitleSearchResult> results);

/**
 * A subtitle the server has just downloaded, ready for mpv. sourceIndex is
 * the provider's stream index when the subtitle became a stream of the
 * media source (Jellyfin), so a pick of it can be saved; otherwise -1.
 */
struct DownloadedSubtitle
{
    QUrl url;
    QString title;
    QString language;
    QString format;
    int sourceIndex = -1;

    [[nodiscard]] bool isValid() const { return url.isValid() && !url.isEmpty(); }
    [[nodiscard]] QVariantMap toVariantMap() const;
    [[nodiscard]] static DownloadedSubtitle fromVariantMap(const QVariantMap &map);
};

} // namespace Bloom
