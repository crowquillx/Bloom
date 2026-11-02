#include "models/SubtitleSearch.h"

#include <algorithm>

namespace Bloom {

QVariantMap SubtitleSearchResult::toVariantMap() const
{
    QVariantMap map{
        {QStringLiteral("id"), id},
        {QStringLiteral("provider"), provider},
        {QStringLiteral("language"), language},
        {QStringLiteral("title"), title},
        {QStringLiteral("format"), format},
        {QStringLiteral("downloads"), downloads},
        {QStringLiteral("score"), score},
        {QStringLiteral("hearingImpaired"), hearingImpaired},
        {QStringLiteral("forced"), forced},
        {QStringLiteral("machineTranslated"), machineTranslated},
        {QStringLiteral("aiTranslated"), aiTranslated},
        {QStringLiteral("hashMatch"), hashMatch},
    };
    if (uploadedAt.isValid()) {
        map.insert(QStringLiteral("uploadedAt"), uploadedAt.toString(Qt::ISODateWithMs));
    }
    return map;
}

SubtitleSearchResult SubtitleSearchResult::fromVariantMap(const QVariantMap &map)
{
    SubtitleSearchResult result;
    result.id = map.value(QStringLiteral("id")).toString();
    result.provider = map.value(QStringLiteral("provider")).toString();
    result.language = map.value(QStringLiteral("language")).toString();
    result.title = map.value(QStringLiteral("title")).toString();
    result.format = map.value(QStringLiteral("format")).toString();
    result.downloads = map.value(QStringLiteral("downloads")).toLongLong();
    result.score = map.value(QStringLiteral("score")).toDouble();
    result.hearingImpaired = map.value(QStringLiteral("hearingImpaired")).toBool();
    result.forced = map.value(QStringLiteral("forced")).toBool();
    result.machineTranslated = map.value(QStringLiteral("machineTranslated")).toBool();
    result.aiTranslated = map.value(QStringLiteral("aiTranslated")).toBool();
    result.hashMatch = map.value(QStringLiteral("hashMatch")).toBool();
    result.uploadedAt = QDateTime::fromString(
        map.value(QStringLiteral("uploadedAt")).toString(), Qt::ISODateWithMs);
    return result;
}

QList<SubtitleSearchResult> sortSubtitleSearchResults(QList<SubtitleSearchResult> results)
{
    std::stable_sort(results.begin(), results.end(),
                     [](const SubtitleSearchResult &a, const SubtitleSearchResult &b) {
                         if (a.hashMatch != b.hashMatch) {
                             return a.hashMatch;
                         }
                         if (a.score != b.score) {
                             return a.score > b.score;
                         }
                         return a.downloads > b.downloads;
                     });
    return results;
}

QVariantMap DownloadedSubtitle::toVariantMap() const
{
    return {
        {QStringLiteral("url"), url.toString(QUrl::FullyEncoded)},
        {QStringLiteral("title"), title},
        {QStringLiteral("language"), language},
        {QStringLiteral("format"), format},
        {QStringLiteral("sourceIndex"), sourceIndex},
    };
}

DownloadedSubtitle DownloadedSubtitle::fromVariantMap(const QVariantMap &map)
{
    DownloadedSubtitle subtitle;
    subtitle.url = QUrl(map.value(QStringLiteral("url")).toString());
    subtitle.title = map.value(QStringLiteral("title")).toString();
    subtitle.language = map.value(QStringLiteral("language")).toString();
    subtitle.format = map.value(QStringLiteral("format")).toString();
    subtitle.sourceIndex = map.value(QStringLiteral("sourceIndex"), -1).toInt();
    return subtitle;
}

} // namespace Bloom
