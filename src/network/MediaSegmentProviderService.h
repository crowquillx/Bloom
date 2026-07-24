#pragma once

#include "Types.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class AuthenticationService;
class ConfigManager;

struct MediaSegmentLookupContext
{
    QString itemId;
    QString type;
    QString seriesId;
    QString imdbId;
    QString tmdbId;
    QString tvdbId;
    int seasonNumber = -1;
    int episodeNumber = -1;
    qint64 durationMs = 0;
};

struct MediaSegmentProviderResult
{
    QString providerId;
    QList<MediaSegmentInfo> segments;
    bool networkOk = false;
    bool used = false;
};

class MediaSegmentProviderService : public QObject
{
    Q_OBJECT

public:
    using SegmentsCallback = std::function<void(const QList<MediaSegmentInfo>&)>;

    explicit MediaSegmentProviderService(AuthenticationService *authService,
                                         ConfigManager *configManager,
                                         QObject *parent = nullptr);

    void fetchExternalSegments(const MediaSegmentLookupContext &context,
                               const QList<MediaSegmentInfo> &existingSegments,
                               SegmentsCallback callback);

    static QList<MediaSegmentInfo> parseTheIntroDbSegments(const QJsonObject &obj,
                                                           const MediaSegmentLookupContext &context);
    static QList<MediaSegmentInfo> parseIntroDbSegments(const QJsonObject &obj,
                                                        const MediaSegmentLookupContext &context);
    static QList<MediaSegmentInfo> mergeSegmentsByType(const QList<MediaSegmentInfo> &baseSegments,
                                                       const QList<MediaSegmentInfo> &candidateSegments);
    static bool hasMissingSupportedSegmentTypes(const QList<MediaSegmentInfo> &segments);

private:
    AuthenticationService *m_authService = nullptr;
    ConfigManager *m_configManager = nullptr;

    void fetchProviderAtIndex(const MediaSegmentLookupContext &context,
                              const QList<MediaSegmentInfo> &currentSegments,
                              const QStringList &providerOrder,
                              int providerIndex,
                              SegmentsCallback callback);
    void fetchTheIntroDbSegments(const MediaSegmentLookupContext &context,
                                 std::function<void(const MediaSegmentProviderResult&)> callback);
    void fetchIntroDbSegments(const MediaSegmentLookupContext &context,
                              std::function<void(const MediaSegmentProviderResult&)> callback);

    static MediaSegmentInfo buildSegment(const MediaSegmentLookupContext &context,
                                         MediaSegmentType type,
                                         const QString &typeString,
                                         const QString &source,
                                         double startSeconds,
                                         double endSeconds,
                                         double confidence = 0.0,
                                         int submissionCount = 0);
};
