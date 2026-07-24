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

    PlaybackReportRequest createReportRequest(const PlaybackReport &report) const override;
};
