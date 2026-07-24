#include "MediaSegmentProviderService.h"

#include "AuthenticationService.h"
#include "../utils/ConfigManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include "../utils/BloomLogging.h"

namespace {
constexpr int kProviderTransferTimeoutMs = 30000;

const QSet<MediaSegmentType> kSupportedFillTypes = {
    MediaSegmentType::Intro,
    MediaSegmentType::Outro,
    MediaSegmentType::Recap,
    MediaSegmentType::Preview
};

bool isSupportedFillType(MediaSegmentType type)
{
    return kSupportedFillTypes.contains(type);
}

double parseClockOrNumberSeconds(const QString &raw, bool *ok)
{
    const QString value = raw.trimmed();
    if (value.isEmpty()) {
        *ok = false;
        return 0.0;
    }

    const QStringList parts = value.split(QLatin1Char(':'));
    if (parts.size() == 2 || parts.size() == 3) {
        double totalSeconds = 0.0;
        for (const QString &part : parts) {
            bool partOk = false;
            const double parsed = part.toDouble(&partOk);
            if (!partOk || parsed < 0.0) {
                *ok = false;
                return 0.0;
            }
            totalSeconds = (totalSeconds * 60.0) + parsed;
        }
        *ok = true;
        return totalSeconds;
    }

    bool numberOk = false;
    const double seconds = value.toDouble(&numberOk);
    *ok = numberOk;
    return numberOk ? seconds : 0.0;
}

double secondsFromValue(const QJsonValue &value, bool milliseconds, bool *ok)
{
    if (value.isNull() || value.isUndefined()) {
        *ok = false;
        return 0.0;
    }

    if (value.isDouble()) {
        *ok = true;
        const double parsed = value.toDouble();
        return milliseconds ? parsed / 1000.0 : parsed;
    }

    if (value.isString()) {
        bool stringOk = false;
        const double parsed = parseClockOrNumberSeconds(value.toString(), &stringOk);
        *ok = stringOk;
        if (!stringOk) return 0.0;
        return milliseconds && !value.toString().contains(QLatin1Char(':'))
            ? parsed / 1000.0
            : parsed;
    }

    *ok = false;
    return 0.0;
}

QString normalizeProviderId(const QString &providerId)
{
    return providerId.trimmed().toLower().remove(QLatin1Char('-')).remove(QLatin1Char('_'));
}

double secondsFromMillisecondsValue(const QJsonValue &value, bool *ok)
{
    return secondsFromValue(value, true, ok);
}

double secondsFromProviderObject(const QJsonObject &segment, const QString &secondsKey,
                                 const QString &millisecondsKey, bool *ok)
{
    if (segment.contains(secondsKey) && !segment.value(secondsKey).isNull()) {
        return secondsFromValue(segment.value(secondsKey), false, ok);
    }
    return secondsFromMillisecondsValue(segment.value(millisecondsKey), ok);
}
}

MediaSegmentProviderService::MediaSegmentProviderService(AuthenticationService *authService,
                                                         ConfigManager *configManager,
                                                         QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_configManager(configManager)
{
}

void MediaSegmentProviderService::fetchExternalSegments(const MediaSegmentLookupContext &context,
                                                        const QList<MediaSegmentInfo> &existingSegments,
                                                        SegmentsCallback callback)
{
    if (!m_configManager || !m_configManager->getExternalSegmentProvidersEnabled()
        || !hasMissingSupportedSegmentTypes(existingSegments)) {
        callback(existingSegments);
        return;
    }

    fetchProviderAtIndex(context, existingSegments, m_configManager->getMediaSegmentProviderOrder(), 0, std::move(callback));
}

void MediaSegmentProviderService::fetchProviderAtIndex(const MediaSegmentLookupContext &context,
                                                       const QList<MediaSegmentInfo> &currentSegments,
                                                       const QStringList &providerOrder,
                                                       int providerIndex,
                                                       SegmentsCallback callback)
{
    if (providerIndex >= providerOrder.size() || !hasMissingSupportedSegmentTypes(currentSegments)) {
        callback(currentSegments);
        return;
    }

    const QString providerId = normalizeProviderId(providerOrder.at(providerIndex));
    auto continueWith = [this, context, currentSegments, providerOrder, providerIndex, callback](const MediaSegmentProviderResult &result) mutable {
        const QList<MediaSegmentInfo> merged = result.used
            ? mergeSegmentsByType(currentSegments, result.segments)
            : currentSegments;
        fetchProviderAtIndex(context, merged, providerOrder, providerIndex + 1, std::move(callback));
    };

    if (providerId == QStringLiteral("theintrodb")) {
        fetchTheIntroDbSegments(context, continueWith);
    } else if (providerId == QStringLiteral("introdb")) {
        fetchIntroDbSegments(context, continueWith);
    } else {
        qCDebug(lcMediaSegments) << "Skipping unknown media segment provider" << providerOrder.at(providerIndex);
        continueWith(MediaSegmentProviderResult{providerId, {}, false, false});
    }
}

void MediaSegmentProviderService::fetchTheIntroDbSegments(const MediaSegmentLookupContext &context,
                                                          std::function<void(const MediaSegmentProviderResult&)> callback)
{
    MediaSegmentProviderResult result;
    result.providerId = QStringLiteral("theintrodb");

    if (!m_authService || !m_authService->networkManager() || context.tmdbId.trimmed().isEmpty()) {
        callback(result);
        return;
    }

    QUrl url(QStringLiteral("https://api.theintrodb.org/v2/media"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("tmdb_id"), context.tmdbId.trimmed());
    if (context.type.compare(QStringLiteral("Episode"), Qt::CaseInsensitive) == 0
        && context.seasonNumber > 0 && context.episodeNumber > 0) {
        query.addQueryItem(QStringLiteral("season"), QString::number(context.seasonNumber));
        query.addQueryItem(QStringLiteral("episode"), QString::number(context.episodeNumber));
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(kProviderTransferTimeoutMs);

    QNetworkReply *reply = m_authService->networkManager()->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, context, callback, result]() mutable {
        reply->deleteLater();
        result.networkOk = reply->error() == QNetworkReply::NoError;
        if (result.networkOk) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                result.segments = parseTheIntroDbSegments(doc.object(), context);
                result.used = !result.segments.isEmpty();
            }
        }
        callback(result);
    });
}

void MediaSegmentProviderService::fetchIntroDbSegments(const MediaSegmentLookupContext &context,
                                                       std::function<void(const MediaSegmentProviderResult&)> callback)
{
    MediaSegmentProviderResult result;
    result.providerId = QStringLiteral("introdb");

    if (!m_authService || !m_authService->networkManager()
        || context.type.compare(QStringLiteral("Episode"), Qt::CaseInsensitive) != 0
        || context.imdbId.trimmed().isEmpty()
        || context.seasonNumber <= 0 || context.episodeNumber <= 0) {
        callback(result);
        return;
    }

    QUrl url(QStringLiteral("https://api.introdb.app/segments"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("imdb_id"), context.imdbId.trimmed());
    query.addQueryItem(QStringLiteral("season"), QString::number(context.seasonNumber));
    query.addQueryItem(QStringLiteral("episode"), QString::number(context.episodeNumber));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(kProviderTransferTimeoutMs);

    QNetworkReply *reply = m_authService->networkManager()->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, context, callback, result]() mutable {
        reply->deleteLater();
        result.networkOk = reply->error() == QNetworkReply::NoError;
        if (result.networkOk) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                result.segments = parseIntroDbSegments(doc.object(), context);
                result.used = !result.segments.isEmpty();
            }
        }
        callback(result);
    });
}

QList<MediaSegmentInfo> MediaSegmentProviderService::parseTheIntroDbSegments(const QJsonObject &obj,
                                                                             const MediaSegmentLookupContext &context)
{
    const struct Mapping { QString key; MediaSegmentType type; QString label; } mappings[] = {
        {QStringLiteral("intro"), MediaSegmentType::Intro, QStringLiteral("Intro")},
        {QStringLiteral("recap"), MediaSegmentType::Recap, QStringLiteral("Recap")},
        {QStringLiteral("credits"), MediaSegmentType::Outro, QStringLiteral("Outro")},
        {QStringLiteral("preview"), MediaSegmentType::Preview, QStringLiteral("Preview")}
    };

    QList<MediaSegmentInfo> segments;
    const double durationSeconds = context.durationMs > 0
        ? static_cast<double>(context.durationMs) / 1000.0
        : 0.0;

    for (const Mapping &mapping : mappings) {
        const QJsonValue value = obj.value(mapping.key);
        QJsonArray values;
        if (value.isArray()) {
            values = value.toArray();
        } else if (value.isObject()) {
            values.append(value);
        }

        for (const QJsonValue &entry : values) {
            const QJsonObject segment = entry.toObject();
            bool hasStart = false;
            bool hasEnd = false;
            double startSeconds = secondsFromMillisecondsValue(segment.value(QStringLiteral("start_ms")), &hasStart);
            double endSeconds = secondsFromMillisecondsValue(segment.value(QStringLiteral("end_ms")), &hasEnd);
            if (!hasStart && !hasEnd) continue;
            if (!hasStart) startSeconds = 0.0;
            if (!hasEnd && durationSeconds > 0.0) endSeconds = durationSeconds;
            if (!hasEnd && durationSeconds <= 0.0) continue;

            const MediaSegmentInfo info = buildSegment(context, mapping.type, mapping.label, QStringLiteral("theintrodb"),
                                                       startSeconds, endSeconds);
            if (info.type != MediaSegmentType::Unknown) segments.append(info);
        }
    }

    return segments;
}

QList<MediaSegmentInfo> MediaSegmentProviderService::parseIntroDbSegments(const QJsonObject &obj,
                                                                          const MediaSegmentLookupContext &context)
{
    const struct Mapping { QString key; MediaSegmentType type; QString label; } mappings[] = {
        {QStringLiteral("intro"), MediaSegmentType::Intro, QStringLiteral("Intro")},
        {QStringLiteral("recap"), MediaSegmentType::Recap, QStringLiteral("Recap")},
        {QStringLiteral("outro"), MediaSegmentType::Outro, QStringLiteral("Outro")}
    };

    QList<MediaSegmentInfo> segments;
    for (const Mapping &mapping : mappings) {
        const QJsonValue value = obj.value(mapping.key);
        if (!value.isObject()) continue;

        const QJsonObject segment = value.toObject();
        bool hasStart = false;
        bool hasEnd = false;
        const double startSeconds = secondsFromProviderObject(segment, QStringLiteral("start_sec"),
                                                              QStringLiteral("start_ms"), &hasStart);
        const double endSeconds = secondsFromProviderObject(segment, QStringLiteral("end_sec"),
                                                            QStringLiteral("end_ms"), &hasEnd);
        if (!hasStart || !hasEnd) continue;

        const MediaSegmentInfo info = buildSegment(context, mapping.type, mapping.label, QStringLiteral("introdb"),
                                                   startSeconds, endSeconds,
                                                   segment.value(QStringLiteral("confidence")).toDouble(),
                                                   segment.value(QStringLiteral("submission_count")).toInt());
        if (info.type != MediaSegmentType::Unknown) segments.append(info);
    }
    return segments;
}

QList<MediaSegmentInfo> MediaSegmentProviderService::mergeSegmentsByType(const QList<MediaSegmentInfo> &baseSegments,
                                                                         const QList<MediaSegmentInfo> &candidateSegments)
{
    QList<MediaSegmentInfo> merged = baseSegments;
    QSet<MediaSegmentType> presentTypes;
    for (const MediaSegmentInfo &segment : baseSegments) {
        if (isSupportedFillType(segment.type)) presentTypes.insert(segment.type);
    }
    for (const MediaSegmentInfo &segment : candidateSegments) {
        if (!isSupportedFillType(segment.type) || presentTypes.contains(segment.type)) continue;
        merged.append(segment);
        presentTypes.insert(segment.type);
    }
    return merged;
}

bool MediaSegmentProviderService::hasMissingSupportedSegmentTypes(const QList<MediaSegmentInfo> &segments)
{
    QSet<MediaSegmentType> presentTypes;
    for (const MediaSegmentInfo &segment : segments) {
        if (isSupportedFillType(segment.type)) presentTypes.insert(segment.type);
    }
    return presentTypes.size() < kSupportedFillTypes.size();
}

MediaSegmentInfo MediaSegmentProviderService::buildSegment(const MediaSegmentLookupContext &context,
                                                           MediaSegmentType type,
                                                           const QString &typeString,
                                                           const QString &source,
                                                           double startSeconds,
                                                           double endSeconds,
                                                           double confidence,
                                                           int submissionCount)
{
    MediaSegmentInfo info;
    info.itemId = context.itemId;
    info.type = type;
    info.typeString = typeString;
    info.source = source;
    info.confidence = confidence;
    info.submissionCount = submissionCount;

    if (startSeconds < 0.0 || endSeconds <= startSeconds) {
        return {};
    }

    info.startMs = qRound64(startSeconds * 1000.0);
    info.endMs = qRound64(endSeconds * 1000.0);
    return info;
}
