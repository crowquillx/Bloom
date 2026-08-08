#pragma once

#include "providers/IPlaybackProvider.h"

class SiloPlaybackProvider final : public IPlaybackProvider
{
public:
    Bloom::PlaybackDescriptor createDescriptor(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QVariantMap &providerSource,
        int selectedAudioTrack,
        int selectedSubtitleTrack,
        qint64 startPositionMs,
        const QString &playbackSessionId = QString()) const override;

    QUrl createTrickplayTileUrl(
        const PlaybackProviderContext &context,
        const QString &itemId,
        int width,
        int tileIndex) const override;

    PlaybackReportRequest createReportRequest(const PlaybackReport &report) const override;

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

    PlaybackAudioSwitchRequest createAudioSwitchRequest(
        const PlaybackProviderContext &context,
        const QString &playbackSessionId,
        int audioTrackIndex,
        qint64 positionMs) const override;

    PlaybackAudioSwitchParseResult parseAudioSwitchResponse(
        const PlaybackProviderContext &context,
        const QJsonObject &wireResponse) const override;

    PlaybackRecoveryRequest createPlaybackRecoveryRequest(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QVariantMap &providerSource,
        int selectedAudioTrack,
        qint64 startPositionMs) const override;

    PlaybackStartParseResult parsePlaybackRecoveryResponse(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QJsonObject &wireResponse,
        const QVariantMap &providerSource = {}) const override;
};
