#include "SiloPlaybackProvider.h"

#include "providers/silo/SiloModelMapper.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

QString identity(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (!value.isDouble()) {
        return {};
    }
    const double number = value.toDouble();
    constexpr double maxExact = 9007199254740991.0;
    if (!std::isfinite(number) || std::trunc(number) != number || std::abs(number) > maxExact) {
        return {};
    }
    return QString::number(static_cast<qint64>(number));
}

QString relativeOrAbsoluteUrl(const QUrl &serverUrl, const QString &wireUrl)
{
    const QString trimmed = wireUrl.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    QUrl url(trimmed);
    if (!url.isRelative()) {
        return trimmed;
    }
    if (!serverUrl.isValid()) {
        return {};
    }
    QUrl origin = serverUrl;
    origin.setPath(QStringLiteral("/"));
    origin.setQuery(QString());
    origin.setFragment(QString());
    return origin.resolved(url).toString(QUrl::FullyEncoded);
}

Bloom::PlaybackMethod methodFor(const QString &wireMethod, QString *canonical = nullptr)
{
    const QString method = wireMethod.trimmed().toLower();
    if (method == QStringLiteral("direct") || method == QStringLiteral("directplay")
        || method == QStringLiteral("direct_play")) {
        if (canonical) *canonical = QStringLiteral("DirectPlay");
        return Bloom::PlaybackMethod::DirectPlay;
    }
    if (method == QStringLiteral("remux") || method == QStringLiteral("directstream")
        || method == QStringLiteral("direct_stream")) {
        if (canonical) *canonical = QStringLiteral("DirectStream");
        return Bloom::PlaybackMethod::DirectStream;
    }
    if (method == QStringLiteral("hls") || method == QStringLiteral("transcode")
        || method == QStringLiteral("transcoding")) {
        if (canonical) *canonical = QStringLiteral("Transcode");
        return Bloom::PlaybackMethod::Transcode;
    }
    return Bloom::PlaybackMethod::Unknown;
}

QString nativeTrackUrl(const QJsonObject &wire)
{
    for (const QString &key : {QStringLiteral("url"), QStringLiteral("external_url"),
                               QStringLiteral("subtitle_url"), QStringLiteral("stream_url"),
                               QStringLiteral("download_url"), QStringLiteral("downloaded_url")}) {
        const QString value = wire.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}
QString nativeTrackUrl(const QVariantMap &stream)
{
    for (const QString &key : {QStringLiteral("externalUrl"), QStringLiteral("url"),
                               QStringLiteral("external_url"), QStringLiteral("subtitleUrl"),
                               QStringLiteral("subtitle_url"), QStringLiteral("streamUrl"),
                               QStringLiteral("stream_url"), QStringLiteral("downloadUrl"),
                               QStringLiteral("download_url")}) {
        const QString value = stream.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}


Bloom::PlaybackTrack trackFromJson(const QJsonObject &wire, const QString &kind,
                                   int fallbackIndex, const QUrl &serverUrl)
{
    Bloom::PlaybackTrack result;
    const QString index = identity(wire.value(QStringLiteral("index")));
    result.trackId = index.isEmpty() ? QString::number(fallbackIndex) : index;
    result.kind = kind.toLower();
    result.language = wire.value(QStringLiteral("language")).toString();
    result.codec = wire.value(QStringLiteral("codec")).toString();
    result.displayTitle = wire.value(QStringLiteral("title")).toString(
        wire.value(QStringLiteral("embedded_title")).toString());
    result.isDefault = wire.value(QStringLiteral("default")).toBool();
    result.isForced = wire.value(QStringLiteral("forced")).toBool();
    const QString wireUrl = nativeTrackUrl(wire);
    result.isExternal = wire.value(QStringLiteral("external")).toBool()
        || wire.value(QStringLiteral("downloaded")).toBool()
        || !wireUrl.isEmpty()
        || !wire.value(QStringLiteral("path")).toString().isEmpty();
    result.isHearingImpaired = wire.value(QStringLiteral("hearing_impaired")).toBool();
    if (result.kind == QStringLiteral("subtitle") && !wireUrl.isEmpty()) {
        result.externalUrl = QUrl(relativeOrAbsoluteUrl(serverUrl, wireUrl));
        if (!result.externalUrl.isValid() || result.externalUrl.isEmpty()) {
            result.externalUrl = QUrl();
        }
    }
    return result;
}

void appendTracks(const QJsonValue &value, const QString &kind, QList<Bloom::PlaybackTrack> &out,
                  const QUrl &serverUrl)
{
    if (!value.isArray()) {
        return;
    }
    const QJsonArray tracks = value.toArray();
    for (qsizetype i = 0; i < tracks.size(); ++i) {
        if (tracks.at(i).isObject()) {
            out.append(trackFromJson(tracks.at(i).toObject(), kind, static_cast<int>(i), serverUrl));
        }
    }
}

void appendSubtitleUrls(const QJsonValue &value, Bloom::PlaybackDescriptor &descriptor,
                        const QUrl &serverUrl)
{
    if (!value.isArray()) {
        return;
    }
    const QJsonArray urls = value.toArray();
    for (qsizetype i = 0; i < urls.size(); ++i) {
        QString url;
        int index = static_cast<int>(i);
        QString language;
        if (urls.at(i).isString()) {
            url = urls.at(i).toString();
        } else if (urls.at(i).isObject()) {
            const QJsonObject object = urls.at(i).toObject();
            url = object.value(QStringLiteral("url")).toString(
                object.value(QStringLiteral("stream_url")).toString(
                    object.value(QStringLiteral("subtitle_url")).toString()));
            const QString wireIndex = identity(object.value(QStringLiteral("index")));
            if (!wireIndex.isEmpty()) {
                bool ok = false;
                const int parsedIndex = wireIndex.toInt(&ok);
                if (ok && parsedIndex >= 0) {
                    index = parsedIndex;
                }
            }
            language = object.value(QStringLiteral("language")).toString();
        }
        const QUrl resolvedUrl(relativeOrAbsoluteUrl(serverUrl, url));
        if (!resolvedUrl.isValid() || resolvedUrl.isEmpty()) {
            continue;
        }

        const QString trackId = QString::number(index);
        auto existing = std::find_if(
            descriptor.subtitleTracks.begin(), descriptor.subtitleTracks.end(),
            [&trackId](const Bloom::PlaybackTrack &track) {
                return track.trackId == trackId;
            });
        if (existing != descriptor.subtitleTracks.end()) {
            existing->externalUrl = resolvedUrl;
            existing->isExternal = true;
            if (existing->language.isEmpty()) {
                existing->language = language;
            }
            if (existing->displayTitle.isEmpty()) {
                existing->displayTitle = language;
            }
            continue;
        }

        Bloom::PlaybackTrack track;
        track.trackId = trackId;
        track.kind = QStringLiteral("subtitle");
        track.language = language;
        track.isExternal = true;
        track.externalUrl = resolvedUrl;
        track.displayTitle = language;
        descriptor.subtitleTracks.append(track);
    }
}
QJsonObject capabilities()
{
    const QJsonArray video{QStringLiteral("h264"), QStringLiteral("hevc"), QStringLiteral("vp8"),
                           QStringLiteral("vp9"), QStringLiteral("av1"), QStringLiteral("mpeg2video"),
                           QStringLiteral("mpeg4"), QStringLiteral("vc1"), QStringLiteral("theora")};
    const QJsonArray audio{QStringLiteral("aac"), QStringLiteral("ac3"), QStringLiteral("eac3"),
                           QStringLiteral("dts"), QStringLiteral("truehd"), QStringLiteral("flac"),
                           QStringLiteral("opus"), QStringLiteral("vorbis"), QStringLiteral("mp3"),
                           QStringLiteral("alac")};
    const QJsonArray containers{QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm"),
                                QStringLiteral("mov"), QStringLiteral("ts"), QStringLiteral("avi"),
                                QStringLiteral("mpegts"), QStringLiteral("ogg"), QStringLiteral("flac")};
    return {
        {QStringLiteral("codecs_video"), video},
        {QStringLiteral("codecs_audio"), audio},
        {QStringLiteral("containers"), containers},
        {QStringLiteral("max_resolution"), QStringLiteral("8k")},
        {QStringLiteral("hdr"), true},
        {QStringLiteral("audio_passthrough"),
         QJsonObject{{QStringLiteral("passthrough_codecs"), audio},
                     {QStringLiteral("spatializer_enabled"), false},
                     {QStringLiteral("max_channels"), 8}}},
        {QStringLiteral("supports_bitmap_subtitle_burn_in"), true}
    };
}

void addNumeric(QJsonObject &object, const QString &key, const QVariant &value)
{
    bool ok = false;
    const qlonglong number = value.toLongLong(&ok);
    if (ok && number >= 0) {
        object.insert(key, static_cast<double>(number));
    }
}

Bloom::PlaybackDescriptor descriptorFromResponse(const PlaybackProviderContext &context,
                                                 const Bloom::MediaRef &media,
                                                 const QJsonObject &wire,
                                                 const QVariantMap &source)
{
    const QString streamUrl = wire.value(QStringLiteral("stream_url")).toString(
        wire.value(QStringLiteral("hls_url")).toString(
        wire.value(QStringLiteral("transcode_url")).toString(
        wire.value(QStringLiteral("url")).toString())));
    const QString sessionId = identity(wire.value(QStringLiteral("session_id")));
    const QString fileId = identity(wire.value(QStringLiteral("media_file_id")));
    QString methodName;
    const QString wireMethod = wire.value(QStringLiteral("play_method")).toString(
        wire.value(QStringLiteral("method")).toString());
    const Bloom::PlaybackMethod method = methodFor(wireMethod, &methodName);
    if (sessionId.isEmpty() || fileId.isEmpty() || streamUrl.trimmed().isEmpty()
        || method == Bloom::PlaybackMethod::Unknown) {
        return {};
    }
    Bloom::PlaybackDescriptor descriptor;
    descriptor.media = media;

    descriptor.playbackSessionId = sessionId;
    descriptor.mediaVersionId = fileId;
    descriptor.stream.method = method;
    descriptor.stream.url = QUrl(relativeOrAbsoluteUrl(context.serverUrl, streamUrl));
    if (!descriptor.stream.url.isValid()) {
        return {};
    }
    descriptor.stream.pinsAudioTrack = wire.value(QStringLiteral("audio_track_index")).isDouble();
    descriptor.stream.pinsSubtitleTrack = wire.value(QStringLiteral("subtitle_track_index")).isDouble();
    descriptor.stream.pinnedAudioTrackId = identity(wire.value(QStringLiteral("audio_track_index")));
    descriptor.stream.pinnedSubtitleTrackId = identity(wire.value(QStringLiteral("subtitle_track_index")));

    const QJsonObject info = wire.value(QStringLiteral("playback_info")).toObject();
    QJsonValue duration = info.value(QStringLiteral("duration"));
    if (!duration.isDouble()) {
        duration = info.value(QStringLiteral("duration_seconds"));
    }
    if (!duration.isDouble()) {
        duration = wire.value(QStringLiteral("duration_seconds"));
    }
    descriptor.durationMs = duration.isDouble()
        ? SiloModelMapper::secondsToMilliseconds(duration.toDouble())
        : source.value(QStringLiteral("durationMs")).toLongLong();
    QJsonValue start = wire.value(QStringLiteral("position"));
    if (!start.isDouble()) {
        start = wire.value(QStringLiteral("start_position"));
    }
    if (!start.isDouble()) {
        start = info.value(QStringLiteral("start_position"));
    }
    if (!start.isDouble()) {
        start = info.value(QStringLiteral("start_position_seconds"));
    }
    if (!start.isDouble()) {
        start = wire.value(QStringLiteral("start_position_seconds"));
    }
    descriptor.startPositionMs = start.isDouble()
        ? SiloModelMapper::secondsToMilliseconds(start.toDouble()) : 0;
    appendTracks(info.value(QStringLiteral("audio_tracks")), QStringLiteral("Audio"),
                 descriptor.audioTracks, context.serverUrl);
    appendTracks(info.value(QStringLiteral("subtitle_tracks")), QStringLiteral("Subtitle"),
                 descriptor.subtitleTracks, context.serverUrl);
    if (descriptor.audioTracks.isEmpty()) {
        for (const QVariant &entry : source.value(QStringLiteral("audioTracks")).toList()) {
            const QVariantMap track = entry.toMap();
            Bloom::PlaybackTrack mapped;
            mapped.trackId = QString::number(track.value(QStringLiteral("index"), -1).toInt());
            mapped.kind = QStringLiteral("audio");
            mapped.language = track.value(QStringLiteral("language")).toString();
            mapped.codec = track.value(QStringLiteral("codec")).toString();
            mapped.displayTitle = track.value(QStringLiteral("displayTitle")).toString();
            mapped.isDefault = track.value(QStringLiteral("isDefault")).toBool();
            descriptor.audioTracks.append(mapped);
        }
    }
    appendSubtitleUrls(wire.value(QStringLiteral("subtitle_urls")).isArray()
                           ? wire.value(QStringLiteral("subtitle_urls"))
                           : wire.value(QStringLiteral("subtitles")),
                       descriptor, context.serverUrl);
    descriptor.reporting = {false, true, true, true};
    if (!descriptor.audioTracks.isEmpty() && descriptor.stream.pinnedAudioTrackId.isEmpty()) {
        descriptor.selectedAudioTrackId = descriptor.audioTracks.first().trackId;
    } else {
        descriptor.selectedAudioTrackId = descriptor.stream.pinnedAudioTrackId;
    }
    descriptor.selectedSubtitleTrackId = descriptor.stream.pinnedSubtitleTrackId;
    return descriptor;
}

} // namespace

Bloom::PlaybackDescriptor SiloPlaybackProvider::createDescriptor(
    const PlaybackProviderContext &context, const Bloom::MediaRef &media,
    const QVariantMap &providerSource, int selectedAudioTrack, int selectedSubtitleTrack,
    qint64 startPositionMs, const QString &playbackSessionId) const
{
    Bloom::PlaybackDescriptor descriptor;
    descriptor.media = media;
    descriptor.mediaVersionId = providerSource.value(QStringLiteral("fileId"),
                                                     providerSource.value(QStringLiteral("id"))).toString();
    descriptor.playbackSessionId = playbackSessionId;
    descriptor.durationMs = qMax<qint64>(0, providerSource.value(QStringLiteral("durationMs")).toLongLong());
    descriptor.startPositionMs = qMax<qint64>(0, startPositionMs);
    QString selectedMethod;
    descriptor.stream.method = methodFor(
        providerSource.value(QStringLiteral("playMethod")).toString(), &selectedMethod);
    if (descriptor.stream.method == Bloom::PlaybackMethod::Unknown) {
        descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
    }
    const QString url = providerSource.value(QStringLiteral("streamUrl"),
                                              providerSource.value(QStringLiteral("directStreamUrl"),
                                              providerSource.value(QStringLiteral("transcodingUrl")))).toString();
    descriptor.selectedAudioTrackId = selectedAudioTrack >= 0 ? QString::number(selectedAudioTrack) : QString();
    descriptor.selectedSubtitleTrackId = selectedSubtitleTrack >= 0 ? QString::number(selectedSubtitleTrack) : QString();
    for (const QVariant &entry : providerSource.value(QStringLiteral("mediaStreams")).toList()) {
        const QVariantMap stream = entry.toMap();
        Bloom::PlaybackTrack track;
        track.trackId = QString::number(stream.value(QStringLiteral("index"), -1).toInt());
        track.kind = stream.value(QStringLiteral("type")).toString().toLower();
        track.language = stream.value(QStringLiteral("language")).toString();
        track.codec = stream.value(QStringLiteral("codec")).toString();
        track.displayTitle = stream.value(QStringLiteral("displayTitle")).toString();
        track.isDefault = stream.value(QStringLiteral("isDefault")).toBool();
        track.isForced = stream.value(QStringLiteral("isForced")).toBool();
        const QString wireTrackUrl = nativeTrackUrl(stream);
        track.isExternal = stream.value(QStringLiteral("isExternal")).toBool()
            || !wireTrackUrl.isEmpty();
        if (track.kind == QStringLiteral("subtitle") && !wireTrackUrl.isEmpty()) {
            track.externalUrl = QUrl(relativeOrAbsoluteUrl(context.serverUrl, wireTrackUrl));
            if (!track.externalUrl.isValid() || track.externalUrl.isEmpty()) {
                track.externalUrl = QUrl();
            }
        }
        if (track.kind == QStringLiteral("audio")) {
            descriptor.audioTracks.append(track);
        } else if (track.kind == QStringLiteral("subtitle")) {
            descriptor.subtitleTracks.append(track);
        }
    }
    descriptor.stream.pinsAudioTrack = selectedAudioTrack >= 0;
    descriptor.stream.pinsSubtitleTrack = selectedSubtitleTrack >= 0;
    descriptor.stream.pinnedAudioTrackId = descriptor.selectedAudioTrackId;
    descriptor.stream.pinnedSubtitleTrackId = descriptor.selectedSubtitleTrackId;
    descriptor.reporting = {false, true, true, true};
    return descriptor;
}

QUrl SiloPlaybackProvider::createTrickplayTileUrl(const PlaybackProviderContext &, const QString &, int, int) const
{
    return {};
}

PlaybackInfoRequest SiloPlaybackProvider::createPlaybackInfoRequest(
    const PlaybackProviderContext &, const Bloom::MediaRef &media, const QVariantMap &) const
{
    PlaybackInfoRequest request;
    if (!media.itemId.isEmpty()) {
        request.endpoint = QStringLiteral("/api/v1/catalog/items/%1")
                               .arg(QString::fromLatin1(QUrl::toPercentEncoding(media.itemId)));
    }
    return request;
}

PlaybackInfoParseResult SiloPlaybackProvider::parsePlaybackInfoResponse(
    const PlaybackProviderContext &, const Bloom::MediaRef &, const QJsonObject &wireResponse,
    const QVariantMap &) const
{
    PlaybackInfoParseResult result;
    result.response = SiloModelMapper::playbackInfo(wireResponse);
    result.valid = !result.response.mediaSources.isEmpty();
    if (!result.valid) {
        result.error = QStringLiteral("Silo playback info did not contain media files");
    }
    return result;
}

PlaybackStartRequest SiloPlaybackProvider::createPlaybackStartRequest(
    const PlaybackProviderContext &context, const Bloom::MediaRef &, const QVariantMap &source,
    int selectedAudioTrack, int selectedSubtitleTrack, qint64 startPositionMs) const
{
    PlaybackStartRequest request;
    request.endpoint = QStringLiteral("/api/v1/playback/start");
    addNumeric(request.body, QStringLiteral("file_id"), source.value(
        QStringLiteral("fileId"), source.value(QStringLiteral("id"),
        source.value(QStringLiteral("mediaVersionId"), source.value(QStringLiteral("file_id"))))));
    if (!request.body.contains(QStringLiteral("file_id"))) {
        request.endpoint.clear();
        return request;
    }
    request.body.insert(QStringLiteral("profile_id"), context.profileId);
    request.body.insert(QStringLiteral("start_position"),
                        qMax<qint64>(0, startPositionMs) / 1000.0);
    if (selectedAudioTrack >= 0) {
        request.body.insert(QStringLiteral("audio_track_index"), selectedAudioTrack);
    }
    Q_UNUSED(selectedSubtitleTrack)
    const QJsonObject caps = capabilities();
    for (auto it = caps.constBegin(); it != caps.constEnd(); ++it) {
        request.body.insert(it.key(), it.value());
    }
    return request;
}

PlaybackStartParseResult SiloPlaybackProvider::parsePlaybackStartResponse(
    const PlaybackProviderContext &context, const Bloom::MediaRef &media,
    const QJsonObject &wireResponse, const QVariantMap &source) const
{
    PlaybackStartParseResult result;
    result.descriptor = descriptorFromResponse(context, media, wireResponse, source);
    result.valid = result.descriptor.isValid();
    if (!result.valid) {
        result.error = QStringLiteral("Malformed Silo playback start response");
    }
    return result;
}

PlaybackAudioSwitchRequest SiloPlaybackProvider::createAudioSwitchRequest(
    const PlaybackProviderContext &, const QString &sessionId, int audioTrackIndex,
    qint64 positionMs) const
{
    PlaybackAudioSwitchRequest request;
    if (sessionId.isEmpty() || audioTrackIndex < 0) {
        return request;
    }
    request.endpoint = QStringLiteral("/api/v1/playback/%1/audio")
                           .arg(QString::fromLatin1(QUrl::toPercentEncoding(sessionId)));
    request.body.insert(QStringLiteral("audio_track_index"), audioTrackIndex);
    request.body.insert(QStringLiteral("position"), qMax<qint64>(0, positionMs) / 1000.0);
    return request;
}

PlaybackAudioSwitchParseResult SiloPlaybackProvider::parseAudioSwitchResponse(
    const PlaybackProviderContext &context, const QJsonObject &wireResponse) const
{
    PlaybackAudioSwitchParseResult result;
    const QString url = wireResponse.value(QStringLiteral("reload_url")).toString(
        wireResponse.value(QStringLiteral("stream_url")).toString());
    const QString resolved = relativeOrAbsoluteUrl(context.serverUrl, url);
    if (resolved.isEmpty()) {
        result.error = QStringLiteral("Malformed Silo audio switch response");
        return result;
    }
    result.reloadUrl = QUrl(resolved);
    result.valid = result.reloadUrl.isValid();
    return result;
}

PlaybackRecoveryRequest SiloPlaybackProvider::createPlaybackRecoveryRequest(
    const PlaybackProviderContext &context, const Bloom::MediaRef &media,
    const QVariantMap &source, qint64 startPositionMs) const
{
    const PlaybackStartRequest start = createPlaybackStartRequest(context, media, source, -1, -1, startPositionMs);
    PlaybackRecoveryRequest request;
    request.endpoint = start.endpoint;
    request.body = start.body;
    return request;
}

PlaybackStartParseResult SiloPlaybackProvider::parsePlaybackRecoveryResponse(
    const PlaybackProviderContext &context, const Bloom::MediaRef &media,
    const QJsonObject &wireResponse, const QVariantMap &source) const
{
    return parsePlaybackStartResponse(context, media, wireResponse, source);
}

PlaybackReportRequest SiloPlaybackProvider::createReportRequest(const PlaybackReport &report) const
{
    PlaybackReportRequest request;
    if (report.event == PlaybackReportEvent::Start || report.playbackSessionId.isEmpty()) {
        return request;
    }
    const QString session = QString::fromLatin1(QUrl::toPercentEncoding(report.playbackSessionId));
    request.endpoint = QStringLiteral("/api/v1/playback/%1").arg(session);
    if (report.event == PlaybackReportEvent::Stop) {
        request.method = QStringLiteral("DELETE");
        request.deferSessionExpiry = false;
        return request;
    }
    request.endpoint += QStringLiteral("/progress");
    request.method = QStringLiteral("POST");
    request.body.insert(QStringLiteral("seconds"), qMax<qint64>(0, report.positionMs) / 1000.0);
    request.body.insert(QStringLiteral("is_paused"),
                        report.event == PlaybackReportEvent::Pause || report.isPaused);
    return request;
}
