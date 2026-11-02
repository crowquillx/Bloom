#include "JellyfinPlaybackProvider.h"

#include "providers/jellyfin/JellyfinModelMapper.h"
#include "player/AudioPolicy.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>

#include <algorithm>

namespace {

// Jellyfin's DeviceProfile defaults MaxStreamingBitrate to 8 Mbps, which forces
// LAN sources above that into a transcode. Bloom is a local-network client;
// remote users stay capped by the server's per-user remote bitrate limit.
constexpr int kMaxStreamingBitrate = 1'000'000'000;

// Jellyfin copies this list into the transcode URL's AudioCodec parameter,
// which it validates as at most 40 characters (EncodingHelper
// ContainerValidationRegexStr). FLAC leads as the lossless encode target;
// the rest let common source audio be copied instead of re-encoded.
constexpr char kTranscodeAudioCodecs[] = "flac,aac,ac3,eac3,dts,truehd,opus,mp3";
static_assert(sizeof(kTranscodeAudioCodecs) - 1 <= 40);

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

bool sameOrigin(const QUrl &left, const QUrl &right)
{
    const auto port = [](const QUrl &url) {
        return url.port(url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
                            ? 443 : 80);
    };
    return left.scheme().compare(right.scheme(), Qt::CaseInsensitive) == 0
        && left.host().compare(right.host(), Qt::CaseInsensitive) == 0
        && port(left) == port(right);
}

// Text subtitle formats mpv loads as sidecar files. Jellyfin reports .srt and
// .vtt files under either name, so both spellings are listed.
const QStringList &sidecarSubtitleFormats()
{
    static const QStringList formats{
        QStringLiteral("srt"), QStringLiteral("subrip"), QStringLiteral("ass"),
        QStringLiteral("ssa"), QStringLiteral("vtt"), QStringLiteral("webvtt")};
    return formats;
}

// mpv fetches sidecars itself, so the URL has to carry its own credential.
// On the server's origin any credential already in the query (ApiKey or a
// legacy alias, which servers reject once legacy authorization is off) is
// replaced by the current ApiKey; every other query item is kept byte for
// byte. URLs on another origin never receive the token.
QUrl externalSubtitleUrl(const PlaybackProviderContext &context,
                         const QString &providerUrl)
{
    if (!context.serverUrl.isValid()) {
        return {};
    }
    QUrl url = resolveProviderUrl(context.serverUrl, providerUrl);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
        || url.host().isEmpty()) {
        return {};
    }
    if (sameOrigin(url, context.serverUrl) && !context.accessToken.isEmpty()) {
        QByteArray encoded = JellyfinModelMapper::withoutQueryCredentials(
            QString::fromLatin1(url.toEncoded(QUrl::RemoveFragment))).toLatin1();
        encoded += encoded.contains('?') ? '&' : '?';
        encoded += "ApiKey=" + QUrl::toPercentEncoding(context.accessToken);
        url = QUrl::fromEncoded(encoded);
    }
    return url;
}

Bloom::PlaybackTrack mapTrack(const PlaybackProviderContext &context,
                              const Bloom::MediaRef &media,
                              const QString &mediaSourceId,
                              const QVariantMap &stream)
{
    Bloom::PlaybackTrack track;
    const int index = stream.value(QStringLiteral("index"), -1).toInt();
    track.trackId = QString::number(index);
    track.sourceIndex = index;
    track.kind = stream.value(QStringLiteral("type")).toString().toLower();
    track.language = stream.value(QStringLiteral("language")).toString();
    track.codec = stream.value(QStringLiteral("codec")).toString();
    track.displayTitle = stream.value(QStringLiteral("displayTitle")).toString();
    track.isDefault = stream.value(QStringLiteral("isDefault")).toBool();
    track.isForced = stream.value(QStringLiteral("isForced")).toBool();
    track.isExternal = stream.value(QStringLiteral("isExternal")).toBool();
    track.isHearingImpaired = stream.value(QStringLiteral("isHearingImpaired")).toBool();
    if (track.kind == QStringLiteral("audio"))
        track.audio = Bloom::AudioSourceMetadata::fromVariantMap(stream);
    if (track.kind == QStringLiteral("subtitle") && track.isExternal) {
        const QString deliveryUrl = stream.value(QStringLiteral("externalUrl")).toString().trimmed();
        const QString format = track.codec.trimmed().toLower();
        if (!deliveryUrl.isEmpty()) {
            track.externalUrl = externalSubtitleUrl(context, deliveryUrl);
        }
        if (track.externalUrl.isEmpty() && index >= 0 && !media.itemId.isEmpty()
            && !mediaSourceId.isEmpty() && sidecarSubtitleFormats().contains(format)) {
            // Without a usable DeliveryUrl, GetSubtitle serves the file
            // unchanged when the requested format is the stream's own codec.
            track.externalUrl = externalSubtitleUrl(
                context,
                QStringLiteral("/Videos/%1/%2/Subtitles/%3/Stream.%4")
                    .arg(QString::fromLatin1(QUrl::toPercentEncoding(media.itemId)),
                         QString::fromLatin1(QUrl::toPercentEncoding(mediaSourceId)),
                         QString::number(index), format));
        }
    }
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
    descriptor.durationMs = qMax<qint64>(
        0, providerSource.value(QStringLiteral("durationMs")).toLongLong());
    descriptor.startPositionMs = qMax<qint64>(0, startPositionMs);
    descriptor.selectedAudioTrackId = selectedAudioTrack >= 0
        ? QString::number(selectedAudioTrack) : QString();
    descriptor.selectedSubtitleTrackId = selectedSubtitleTrack >= 0
        ? QString::number(selectedSubtitleTrack) : QString();
    descriptor.reporting = {true, true, true, true};

    for (const QVariant &streamValue : providerSource.value(QStringLiteral("mediaStreams")).toList()) {
        const Bloom::PlaybackTrack track = mapTrack(
            context, media, descriptor.mediaVersionId, streamValue.toMap());
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
        const bool directPlayAvailabilityKnown = providerSource.contains(
            QStringLiteral("directPlayAvailable"));
        if (directPlayAvailabilityKnown
            && providerSource.value(QStringLiteral("directPlayAvailable")).toBool()) {
            selectedUrl = QStringLiteral("/Videos/%1/stream?Container=mp4,mkv&Static=true")
                              .arg(media.itemId);
            descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
        } else {
            selectedUrl = providerSource.value(QStringLiteral("transcodingUrl")).toString().trimmed();
            descriptor.stream.method = Bloom::PlaybackMethod::Transcode;
            if (selectedUrl.isEmpty() && !directPlayAvailabilityKnown) {
                selectedUrl = QStringLiteral("/Videos/%1/stream?Container=mp4,mkv&Static=true")
                                  .arg(media.itemId);
                descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
            }
        }
    }
    if (selectedUrl.isEmpty()) {
        return descriptor;
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
    query.removeAllQueryItems(QStringLiteral("api_key"));
    query.removeAllQueryItems(QStringLiteral("X-Emby-Token"));
    query.removeAllQueryItems(QStringLiteral("X-MediaBrowser-Token"));
    query.removeAllQueryItems(QStringLiteral("deviceProfileId"));
    query.removeAllQueryItems(QStringLiteral("DeviceProfileId"));
    if (!accessToken.isEmpty() && !query.hasQueryItem(QStringLiteral("ApiKey"))) {
        // External mpv cannot use Bloom's request factory. Jellyfin's current
        // query fallback is ApiKey (capitalized); the older aliases are
        // rejected when legacy authentication is disabled.
        query.addQueryItem(QStringLiteral("ApiKey"), accessToken);
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
    // A sidecar subtitle is shown by mpv; asking the stream for it as well
    // would have a transcode burn it in.
    const bool selectedSubtitleIsSidecar = std::any_of(
        descriptor.subtitleTracks.cbegin(), descriptor.subtitleTracks.cend(),
        [&descriptor](const Bloom::PlaybackTrack &track) {
            return track.trackId == descriptor.selectedSubtitleTrackId
                && !track.externalUrl.isEmpty();
        });
    if (selectedSubtitleTrack >= 0 && !selectedSubtitleIsSidecar
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

QUrl JellyfinPlaybackProvider::createTrickplayTileUrl(
    const PlaybackProviderContext &context,
    const QString &itemId,
    int width,
    int tileIndex) const
{
    if (!context.serverUrl.isValid() || itemId.isEmpty() || width <= 0 || tileIndex < 0) {
        return {};
    }

    QUrl url = resolveProviderUrl(
        context.serverUrl,
        QStringLiteral("/Videos/%1/Trickplay/%2/%3.jpg")
            .arg(itemId, QString::number(width), QString::number(tileIndex)));
    QUrlQuery query(url);
    if (!context.accessToken.isEmpty()) {
        query.addQueryItem(QStringLiteral("ApiKey"), context.accessToken);
    }
    url.setQuery(query);
    return url;
}

PlaybackInfoRequest JellyfinPlaybackProvider::createPlaybackInfoRequest(
    const PlaybackProviderContext &context,
    const Bloom::MediaRef &media,
    const QVariantMap &) const
{
    PlaybackInfoRequest request;
    if (media.itemId.isEmpty() || context.profileId.isEmpty()) {
        return request;
    }
    request.endpoint = QStringLiteral("/Items/%1/PlaybackInfo")
                           .arg(QString::fromLatin1(QUrl::toPercentEncoding(media.itemId)));
    request.method = QStringLiteral("POST");
    request.body.insert(QStringLiteral("UserId"), context.profileId);
    request.body.insert(QStringLiteral("MaxStreamingBitrate"), kMaxStreamingBitrate);
    const QStringList audioCodecs = Bloom::AudioPolicy::localDecodeCodecs();
    QJsonArray subtitleProfiles;
    for (const QString &format : {QStringLiteral("ass"), QStringLiteral("ssa"),
             QStringLiteral("subrip"), QStringLiteral("srt"), QStringLiteral("webvtt"),
             QStringLiteral("pgssub"), QStringLiteral("dvdsub"),
             QStringLiteral("dvbsub")}) {
        subtitleProfiles.append(QJsonObject{
            {QStringLiteral("Format"), format},
            {QStringLiteral("Method"), QStringLiteral("Embed")}
        });
    }
    subtitleProfiles.append(QJsonObject{
        {QStringLiteral("Format"), QStringLiteral("mov_text")},
        {QStringLiteral("Method"), QStringLiteral("Embed")},
        {QStringLiteral("Container"), QStringLiteral("mp4,mov")}
    });
    // Jellyfin matches embedded streams against the Embed profiles first, so
    // these only apply to sidecar files. They give each one a DeliveryUrl mpv
    // can load; without them the server plans an external track as Encode,
    // and a default sidecar then costs direct play.
    for (const QString &format : sidecarSubtitleFormats()) {
        subtitleProfiles.append(QJsonObject{
            {QStringLiteral("Format"), format},
            {QStringLiteral("Method"), QStringLiteral("External")}
        });
    }
    request.body.insert(QStringLiteral("DeviceProfile"), QJsonObject{
        {QStringLiteral("Name"), QStringLiteral("Bloom")},
        {QStringLiteral("MaxStreamingBitrate"), kMaxStreamingBitrate},
        {QStringLiteral("DirectPlayProfiles"), QJsonArray{
            QJsonObject{
                {QStringLiteral("Type"), QStringLiteral("Video")},
                {QStringLiteral("AudioCodec"), audioCodecs.join(QLatin1Char(','))}
            }
        }},
        {QStringLiteral("TranscodingProfiles"), QJsonArray{QJsonObject{
            {QStringLiteral("Type"), QStringLiteral("Video")},
            {QStringLiteral("Context"), QStringLiteral("Streaming")},
            {QStringLiteral("Container"), QStringLiteral("mkv")},
            {QStringLiteral("Protocol"), QStringLiteral("http")},
            {QStringLiteral("AudioCodec"), QString::fromLatin1(kTranscodeAudioCodecs)}
        }}},
        {QStringLiteral("SubtitleProfiles"), subtitleProfiles}
    });
    return request;
}

PlaybackInfoParseResult JellyfinPlaybackProvider::parsePlaybackInfoResponse(
    const PlaybackProviderContext &,
    const Bloom::MediaRef &,
    const QJsonObject &wireResponse,
    const QVariantMap &) const
{
    PlaybackInfoParseResult result;
    result.response = JellyfinModelMapper::playbackInfo(wireResponse);
    result.valid = !result.response.mediaSources.isEmpty();
    if (!result.valid) {
        result.error = QStringLiteral("Jellyfin playback info did not contain media sources");
    }
    return result;
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
        break;
    case PlaybackReportEvent::Pause:
        request.endpoint = QStringLiteral("/Sessions/Playing/Progress");
        break;
    case PlaybackReportEvent::Resume:
        request.endpoint = QStringLiteral("/Sessions/Playing/Progress");
        break;
    case PlaybackReportEvent::Stop:
        request.endpoint = QStringLiteral("/Sessions/Playing/Stopped");
        request.deferSessionExpiry = false;
        body = QJsonObject{{QStringLiteral("ItemId"), report.media.itemId}};
        if (report.positionMs >= 0) {
            body.insert(QStringLiteral("PositionTicks"),
                        JellyfinModelMapper::millisecondsToTicks(report.positionMs));
        }
        if (!report.mediaVersionId.isEmpty()) {
            body.insert(QStringLiteral("MediaSourceId"), report.mediaVersionId);
        }
        if (!report.playbackSessionId.isEmpty()) {
            body.insert(QStringLiteral("PlaySessionId"), report.playbackSessionId);
        }
        break;
    }

    request.body = body;
    return request;
}

SubtitleSearchRequest JellyfinPlaybackProvider::createSubtitleSearchRequest(
    const PlaybackProviderContext &, const SubtitleSearchTarget &target,
    const QString &language) const
{
    SubtitleSearchRequest request;
    const QString code = language.trimmed().toLower();
    if (target.media.itemId.isEmpty() || code.isEmpty()) {
        return request;
    }
    // The language is the three-letter code Jellyfin's subtitle providers
    // take (ThreeLetterISOLanguageName). isPerfectMatch is left out, so every
    // result comes back and IsHashMatch marks the ones that match the file.
    request.endpoint = QStringLiteral("/Items/%1/RemoteSearch/Subtitles/%2")
                           .arg(QString::fromLatin1(QUrl::toPercentEncoding(target.media.itemId)),
                                QString::fromLatin1(QUrl::toPercentEncoding(code)));
    request.method = QStringLiteral("GET");
    request.replaySafe = true;
    return request;
}

SubtitleSearchParseResult JellyfinPlaybackProvider::parseSubtitleSearchResponse(
    const QByteArray &body) const
{
    SubtitleSearchParseResult parsed;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        parsed.error = QStringLiteral("The server sent an unreadable subtitle list.");
        return parsed;
    }
    for (const QJsonValue &value : document.array()) {
        const QJsonObject wire = value.toObject();
        Bloom::SubtitleSearchResult result;
        result.id = wire.value(QStringLiteral("Id")).toString();
        if (result.id.isEmpty()) {
            continue;
        }
        result.provider = wire.value(QStringLiteral("ProviderName")).toString();
        result.language = wire.value(QStringLiteral("ThreeLetterISOLanguageName")).toString();
        result.title = wire.value(QStringLiteral("Name")).toString();
        result.format = wire.value(QStringLiteral("Format")).toString().toLower();
        result.downloads = wire.value(QStringLiteral("DownloadCount")).toInteger();
        result.score = wire.value(QStringLiteral("CommunityRating")).toDouble();
        result.hearingImpaired = wire.value(QStringLiteral("HearingImpaired")).toBool();
        result.forced = wire.value(QStringLiteral("Forced")).toBool();
        result.machineTranslated = wire.value(QStringLiteral("MachineTranslated")).toBool();
        result.aiTranslated = wire.value(QStringLiteral("AiTranslated")).toBool();
        result.hashMatch = wire.value(QStringLiteral("IsHashMatch")).toBool();
        result.uploadedAt = QDateTime::fromString(
            wire.value(QStringLiteral("DateCreated")).toString(), Qt::ISODateWithMs);
        parsed.results.append(result);
    }
    parsed.valid = true;
    return parsed;
}

SubtitleSearchRequest JellyfinPlaybackProvider::createSubtitleDownloadRequest(
    const PlaybackProviderContext &, const SubtitleSearchTarget &target,
    const Bloom::SubtitleSearchResult &result) const
{
    SubtitleSearchRequest request;
    if (target.media.itemId.isEmpty() || result.id.isEmpty()) {
        return request;
    }
    request.endpoint = QStringLiteral("/Items/%1/RemoteSearch/Subtitles/%2")
                           .arg(QString::fromLatin1(QUrl::toPercentEncoding(target.media.itemId)),
                                QString::fromLatin1(QUrl::toPercentEncoding(result.id)));
    request.method = QStringLiteral("POST");
    // A replay could fetch from the provider again and spend the user's
    // provider quota, so a download is sent once.
    request.replaySafe = false;
    return request;
}

SubtitleDownloadParseResult JellyfinPlaybackProvider::parseSubtitleDownloadResponse(
    const PlaybackProviderContext &, const SubtitleSearchTarget &,
    const Bloom::SubtitleSearchResult &result, const QByteArray &) const
{
    // 204 No Content: the server saved the file next to the media and
    // refreshes the item, after which PlaybackInfo lists it as an external
    // stream with its own index.
    SubtitleDownloadParseResult parsed;
    parsed.valid = true;
    parsed.needsPlaybackInfo = true;
    parsed.subtitle.title = result.title;
    parsed.subtitle.language = result.language;
    parsed.subtitle.format = result.format;
    return parsed;
}
