#include "JellyfinPlaybackProvider.h"

#include "providers/jellyfin/JellyfinModelMapper.h"

#include <QUrlQuery>

namespace {

QUrl resolveProviderUrl(const QUrl &serverUrl, const QString &providerUrl)
{
    QUrl url(providerUrl);
    if (!url.isRelative()) {
        return url;
    }

    QUrl base = serverUrl;
    QString basePath = base.path();
    if (!basePath.endsWith(QLatin1Char('/'))) {
        basePath += QLatin1Char('/');
        base.setPath(basePath);
    }

    QString relativePath = providerUrl;
    if (relativePath.startsWith(QLatin1Char('/'))) {
        const QString normalizedBasePath = basePath.chopped(1);
        if (!normalizedBasePath.isEmpty()
            && relativePath.startsWith(normalizedBasePath + QLatin1Char('/'))) {
            QUrl origin = serverUrl;
            origin.setPath(QStringLiteral("/"));
            return origin.resolved(QUrl(relativePath.mid(1)));
        }
        relativePath.remove(0, 1);
    }
    return base.resolved(QUrl(relativePath));
}

Bloom::PlaybackTrack mapTrack(const QVariantMap &stream)
{
    Bloom::PlaybackTrack track;
    track.trackId = QString::number(stream.value(QStringLiteral("index"), -1).toInt());
    track.kind = stream.value(QStringLiteral("type")).toString().toLower();
    track.language = stream.value(QStringLiteral("language")).toString();
    track.codec = stream.value(QStringLiteral("codec")).toString();
    track.displayTitle = stream.value(QStringLiteral("displayTitle")).toString();
    track.isDefault = stream.value(QStringLiteral("isDefault")).toBool();
    track.isForced = stream.value(QStringLiteral("isForced")).toBool();
    track.isExternal = stream.value(QStringLiteral("isExternal")).toBool();
    track.isHearingImpaired = stream.value(QStringLiteral("isHearingImpaired")).toBool();
    return track;
}

QString jellyfinPlayMethod(const QString &method)
{
    const QString normalized = method.trimmed().toLower();
    if (normalized == QStringLiteral("transcode")) {
        return QStringLiteral("Transcode");
    }
    if (normalized == QStringLiteral("directstream")) {
        return QStringLiteral("DirectStream");
    }
    return QStringLiteral("DirectPlay");
}

void addTrackIndex(QJsonObject &body, const QString &field, const QString &trackId)
{
    bool ok = false;
    const int index = trackId.toInt(&ok);
    if (ok && index >= 0) {
        body.insert(field, index);
    }
}

} // namespace

Bloom::PlaybackDescriptor JellyfinPlaybackProvider::createDescriptor(
    const PlaybackProviderContext &context,
    const Bloom::MediaRef &media,
    const QVariantMap &providerSource,
    int selectedAudioTrack,
    int selectedSubtitleTrack,
    qint64 startPositionMs,
    const QString &playbackSessionId) const
{
    Bloom::PlaybackDescriptor descriptor;
    descriptor.media = media;
    descriptor.mediaVersionId = providerSource.value(QStringLiteral("id")).toString();
    descriptor.playbackSessionId = playbackSessionId;
    descriptor.durationMs = JellyfinModelMapper::ticksToMilliseconds(
        providerSource.value(QStringLiteral("runTimeTicks")).toLongLong());
    descriptor.startPositionMs = qMax<qint64>(0, startPositionMs);
    descriptor.selectedAudioTrackId = selectedAudioTrack >= 0
        ? QString::number(selectedAudioTrack) : QString();
    descriptor.selectedSubtitleTrackId = selectedSubtitleTrack >= 0
        ? QString::number(selectedSubtitleTrack) : QString();
    descriptor.reporting = {true, true, true, true};

    for (const QVariant &streamValue : providerSource.value(QStringLiteral("mediaStreams")).toList()) {
        const Bloom::PlaybackTrack track = mapTrack(streamValue.toMap());
        if (track.kind == QStringLiteral("audio")) {
            descriptor.audioTracks.append(track);
        } else if (track.kind == QStringLiteral("subtitle")) {
            descriptor.subtitleTracks.append(track);
        }
    }

    if (media.itemId.isEmpty()) {
        return descriptor;
    }

    QString selectedUrl = providerSource.value(QStringLiteral("directStreamUrl")).toString().trimmed();
    descriptor.stream.method = Bloom::PlaybackMethod::DirectStream;
    if (selectedUrl.isEmpty()) {
        selectedUrl = providerSource.value(QStringLiteral("transcodingUrl")).toString().trimmed();
        descriptor.stream.method = Bloom::PlaybackMethod::Transcode;
    }
    if (selectedUrl.isEmpty()) {
        selectedUrl = QStringLiteral("/Videos/%1/stream?Static=true").arg(media.itemId);
        descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
    }

    QUrl url(selectedUrl);
    if (url.isRelative()) {
        if (!context.serverUrl.isValid()) {
            return descriptor;
        }
        url = resolveProviderUrl(context.serverUrl, selectedUrl);
    }

    QUrlQuery query(url);
    const QString accessToken = context.accessToken;
    if (!accessToken.isEmpty()
        && !query.hasQueryItem(QStringLiteral("api_key"))
        && !query.hasQueryItem(QStringLiteral("X-Emby-Token"))) {
        query.addQueryItem(QStringLiteral("api_key"), accessToken);
    }
    if (!descriptor.mediaVersionId.isEmpty()
        && !query.hasQueryItem(QStringLiteral("MediaSourceId"))) {
        query.addQueryItem(QStringLiteral("MediaSourceId"), descriptor.mediaVersionId);
    }
    if (selectedAudioTrack >= 0
        && !query.hasQueryItem(QStringLiteral("AudioStreamIndex"))) {
        query.addQueryItem(QStringLiteral("AudioStreamIndex"),
                           QString::number(selectedAudioTrack));
    }
    if (selectedSubtitleTrack >= 0
        && !query.hasQueryItem(QStringLiteral("SubtitleStreamIndex"))) {
        query.addQueryItem(QStringLiteral("SubtitleStreamIndex"),
                           QString::number(selectedSubtitleTrack));
    }
    descriptor.stream.pinsAudioTrack = query.hasQueryItem(
        QStringLiteral("AudioStreamIndex"));
    descriptor.stream.pinsSubtitleTrack = query.hasQueryItem(
        QStringLiteral("SubtitleStreamIndex"));
    descriptor.stream.pinnedAudioTrackId = query.queryItemValue(
        QStringLiteral("AudioStreamIndex"));
    descriptor.stream.pinnedSubtitleTrackId = query.queryItemValue(
        QStringLiteral("SubtitleStreamIndex"));
    url.setQuery(query);
    descriptor.stream.url = url;
    return descriptor;
}

PlaybackReportRequest JellyfinPlaybackProvider::createReportRequest(
    const PlaybackReport &report) const
{
    PlaybackReportRequest request;
    QJsonObject body{
        {QStringLiteral("ItemId"), report.media.itemId},
        {QStringLiteral("CanSeek"), report.canSeek},
        {QStringLiteral("IsPaused"), report.isPaused},
        {QStringLiteral("IsMuted"), report.isMuted},
        {QStringLiteral("PlayMethod"), jellyfinPlayMethod(report.playbackMethod)},
        {QStringLiteral("RepeatMode"), report.repeatMode.isEmpty()
             ? QStringLiteral("RepeatNone") : report.repeatMode},
        {QStringLiteral("PlaybackOrder"), report.playbackOrder.isEmpty()
             ? QStringLiteral("Default") : report.playbackOrder}
    };

    if (report.positionMs >= 0) {
        body.insert(QStringLiteral("PositionTicks"),
                    JellyfinModelMapper::millisecondsToTicks(report.positionMs));
    }
    if (!report.mediaVersionId.isEmpty()) {
        body.insert(QStringLiteral("MediaSourceId"), report.mediaVersionId);
    }
    addTrackIndex(body, QStringLiteral("AudioStreamIndex"), report.audioTrackId);
    addTrackIndex(body, QStringLiteral("SubtitleStreamIndex"), report.subtitleTrackId);
    if (!report.playbackSessionId.isEmpty()) {
        body.insert(QStringLiteral("PlaySessionId"), report.playbackSessionId);
    }

    switch (report.event) {
    case PlaybackReportEvent::Start:
        request.endpoint = QStringLiteral("/Sessions/Playing");
        break;
    case PlaybackReportEvent::Progress:
        request.endpoint = QStringLiteral("/Sessions/Playing/Progress");
        body.insert(QStringLiteral("EventName"), QStringLiteral("TimeUpdate"));
        break;
    case PlaybackReportEvent::Pause:
        request.endpoint = QStringLiteral("/Sessions/Playing/Progress");
        body.insert(QStringLiteral("EventName"), QStringLiteral("Pause"));
        break;
    case PlaybackReportEvent::Resume:
        request.endpoint = QStringLiteral("/Sessions/Playing/Progress");
        body.insert(QStringLiteral("EventName"), QStringLiteral("Unpause"));
        break;
    case PlaybackReportEvent::Stop:
        request.endpoint = QStringLiteral("/Sessions/Playing/Stopped");
        request.deferSessionExpiry = false;
        body.insert(QStringLiteral("EventName"), QStringLiteral("Stop"));
        break;
    }

    request.body = body;
    return request;
}
