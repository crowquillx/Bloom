#pragma once

#include "providers/IPlaybackProvider.h"
#include <QHash>
#include <QSet>
#include <QDateTime>

class SiloPlaybackProvider final : public IPlaybackProvider
{
public:
    QString capabilitiesEndpoint() const override;
    bool acceptCapabilities(const PlaybackProviderContext &context,
                            const QJsonObject &wire) const override;
    Bloom::PlaybackDescriptor createDescriptor(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QVariantMap &providerSource,
        int selectedAudioTrack,
        int selectedSubtitleTrack,
        qint64 startPositionMs,
        const QString &playbackSessionId = QString()) const override;

    PlaybackInfoRequest createTrickplayInfoRequest(
        const PlaybackProviderContext &context, const Bloom::MediaRef &media,
        const QString &fileId) const override;
    TrickplayTileInfoMap parseTrickplayInfoResponse(
        const PlaybackProviderContext &context, const Bloom::MediaRef &media,
        const QJsonObject &wire, const QString &fileId) const override;

    QUrl createTrickplayTileUrl(
        const PlaybackProviderContext &context,
        const QString &itemId,
        int width,
        int tileIndex) const override;

    PlaybackReportRequest createReportRequest(const PlaybackReport &report) const override;
    TrackPreferenceWriteRequest createTrackPreferenceWriteRequest(
        const TrackPreferenceWrite &write) const override;
    PlaybackInfoRequest createTrackPreferenceReadRequest(
        const PlaybackInfoResponse &response) const override;
    void applyTrackPreferenceRead(PlaybackInfoResponse &response, bool stored) const override;
    // Silo expires a session after about 45 s without stream reads or progress.
    bool queuedSessionsExpire() const override { return true; }
    PlaybackSessionLoss classifyReportFailure(
        const PlaybackReport &report, int httpStatus) const override;

    PlaybackInfoRequest createPlaybackInfoRequest(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QVariantMap &providerSource) const override;

    PlaybackInfoParseResult parsePlaybackInfoResponse(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QJsonObject &wireResponse,
        const QVariantMap &providerSource = {}) const override;

    PlaybackStartRequest createPlaybackStartRequest(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QVariantMap &providerSource,
        int selectedAudioTrack,
        int selectedSubtitleTrack,
        qint64 startPositionMs) const override;

    PlaybackStartParseResult parsePlaybackStartResponse(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QJsonObject &wireResponse,
        const QVariantMap &providerSource = {}) const override;

    PlaybackReplanRequest createReplanRequest(
        const PlaybackProviderContext &context,
        const QString &playbackSessionId,
        const PlaybackReplan &replan) const override;

    PlaybackReplanParseResult parseReplanResponse(
        const PlaybackProviderContext &context,
        const QJsonObject &wireResponse) const override;

private:
    struct Session {
        Bloom::MediaRef media;
        PlaybackProviderContext context;
        QString installationId;
        QJsonObject plan;
        QVariantMap source;
        qint64 sequence = 0;
        quint64 order = 0;
    };
    struct Previews {
        int width = 0;
        QList<QUrl> sheets;
        QDateTime expiresAt;
    };
    mutable QSet<QString> m_previewScopes;
    mutable QHash<QString, QSet<QString>> m_previewFiles;
    mutable QHash<QString, Previews> m_previews;
    mutable QHash<QString, QString> m_installations;
    mutable QHash<QString, QSet<QString>> m_serverFeatures;
    mutable QHash<QString, Session> m_sessions;
    mutable quint64 m_sessionOrder = 0;
};
