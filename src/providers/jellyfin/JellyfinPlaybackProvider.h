#pragma once

#include "providers/IPlaybackProvider.h"

class JellyfinPlaybackProvider final : public IPlaybackProvider
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

    PlaybackInfoRequest createPlaybackInfoRequest(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QVariantMap &providerSource) const override;
    PlaybackInfoParseResult parsePlaybackInfoResponse(
        const PlaybackProviderContext &context,
        const Bloom::MediaRef &media,
        const QJsonObject &wireResponse,
        const QVariantMap &providerSource = {}) const override;

    PlaybackReportRequest createReportRequest(const PlaybackReport &report) const override;

    // Remote subtitles (SearchRemoteSubtitles, DownloadRemoteSubtitles).
    // Jellyfin has no status document: the search is always offered and the
    // server answers 403 to users without subtitle management.
    bool supportsSubtitleSearch() const override { return true; }
    SubtitleSearchRequest createSubtitleSearchRequest(
        const PlaybackProviderContext &context,
        const SubtitleSearchTarget &target,
        const QString &language) const override;
    SubtitleSearchParseResult parseSubtitleSearchResponse(const QByteArray &body) const override;
    SubtitleSearchRequest createSubtitleDownloadRequest(
        const PlaybackProviderContext &context,
        const SubtitleSearchTarget &target,
        const Bloom::SubtitleSearchResult &result) const override;
    SubtitleDownloadParseResult parseSubtitleDownloadResponse(
        const PlaybackProviderContext &context,
        const SubtitleSearchTarget &target,
        const Bloom::SubtitleSearchResult &result,
        const QByteArray &body) const override;
};
