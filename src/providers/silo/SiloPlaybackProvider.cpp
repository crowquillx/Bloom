#include "SiloPlaybackProvider.h"

#include "providers/silo/SiloModelMapper.h"
#include "player/AudioPolicy.h"

#include <QJsonArray>
#include <QCryptographicHash>
#include <QUuid>

#include <QJsonValue>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

const QString kOriginalHttp = QStringLiteral("original_http");
const QString kRemuxHls = QStringLiteral("server_remux_hls");
const QString kTranscodeHls = QStringLiteral("server_transcode_hls");
const QString kSeekReanchor = QStringLiteral("seek_reanchor_v1");
const QString kHlsRenditions = QStringLiteral("hls_subtitle_renditions_v1");
const QString kHlsRemuxRenditions = QStringLiteral("hls_remux_subtitle_renditions_v1");

bool isHttpUrl(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty()) {
        return false;
    }
    const QString scheme = url.scheme().toLower();
    return (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
        && !url.host().isEmpty() && url.userInfo().isEmpty();
}

QString relativeOrAbsoluteUrl(const QUrl &serverUrl, const QString &wireUrl)
{
    const QString trimmed = wireUrl.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    const QUrl url(trimmed);
    QUrl resolved;
    if (url.isRelative()) {
        if (!isHttpUrl(serverUrl)) {
            return {};
        }
        QUrl origin = serverUrl;
        origin.setPath(QStringLiteral("/"));
        origin.setQuery(QString());
        origin.setFragment(QString());
        resolved = origin.resolved(url);
    } else {
        resolved = url;
    }
    if (!isHttpUrl(resolved)) {
        return {};
    }
    return resolved.toString(QUrl::FullyEncoded);
}

bool sameOrigin(const PlaybackProviderContext &context, const QUrl &url)
{
    const auto port = [](const QUrl &value)
    { return value.port(value.scheme() == QStringLiteral("https") ? 443 : 80); };
    return url.scheme().compare(context.serverUrl.scheme(), Qt::CaseInsensitive) == 0
        && url.host().compare(context.serverUrl.host(), Qt::CaseInsensitive) == 0
        && port(url) == port(context.serverUrl);
}

QUrl mediaConsumerUrl(const PlaybackProviderContext &context, const QString &wireUrl)
{
    QUrl url(relativeOrAbsoluteUrl(context.serverUrl, wireUrl));
    if (!isHttpUrl(url))
        return {};
    if (sameOrigin(context, url))
    {
        // mpv's URL-only path uses the contract's account-token fallback. The st
        // reference reconstructs a stream but does not authorize the account.
        if (context.accessToken.isEmpty())
            return {};
        if (!QUrlQuery(url).hasQueryItem(QStringLiteral("token")))
        {
            QByteArray encoded = url.toEncoded(QUrl::RemoveFragment);
            encoded += url.hasQuery() ? '&' : '?';
            encoded += "token=" + QUrl::toPercentEncoding(context.accessToken);
            url = QUrl::fromEncoded(encoded);
        }
    }
    return url;
}

QString scopeKey(const PlaybackProviderContext &context)
{
    const QByteArray identity = context.serverUrl.toEncoded() + '\0' + context.profileId.toUtf8()
        + '\0' + context.accountId.toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

// /api/v2 sends identities as strings; the protocol fixtures use integers.
QString identityString(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toInteger());
    return {};
}

QJsonObject capabilities()
{
    return { { QStringLiteral("video_evidence"), QStringLiteral("declared") },
        { QStringLiteral("audio_evidence"), QStringLiteral("declared") },
        { QStringLiteral("codecs_video"),
            QJsonArray {
                "h264", "hevc", "vp8", "vp9", "av1", "mpeg2video", "mpeg4", "vc1", "theora" } },
        { QStringLiteral("codecs_video_hardware"), QJsonArray {} },
        { QStringLiteral("codecs_audio"),
            QJsonArray::fromStringList(Bloom::AudioPolicy::localDecodeCodecs()) },
        { QStringLiteral("containers"),
            QJsonArray { "mp4", "mkv", "webm", "mov", "ts", "m2ts", "avi", "mpegts", "mpeg", "ogg",
                "flac", "wmv", "flv" } },
        { QStringLiteral("max_resolution"), QStringLiteral("8k") },
        { QStringLiteral("hdr"), true } };
}

// mpv selects any of these embedded streams from the original file by its ffmpeg
// index, so Silo never needs to extract or burn in an embedded track.
QJsonArray nativeEmbeddedSubtitles()
{
    const QJsonArray codecs { "subrip", "ass", "ssa", "webvtt", "mov_text", "text",
        "hdmv_pgs_subtitle", "dvd_subtitle", "dvb_subtitle" };
    QJsonArray result;
    for (const char *container : { "mkv", "mp4", "ts", "m2ts", "avi", "ogg" })
        result.append(QJsonObject { { "container", container }, { "codecs", codecs },
            { "track_identity", "ffmpeg_stream_index" }, { "ass_styling", true },
            { "font_attachments", true } });
    return result;
}

QJsonObject playbackContext(const PlaybackProviderContext &context)
{
    const QJsonObject caps = capabilities();
    const QJsonObject subtitles { { "embedded_text", true }, { "sidecar_text", true },
        { "ass_styling", true }, { "embedded_bitmap", true }, { "sidecar_bitmap", false },
        { "font_attachments", true }, { "native_embedded", nativeEmbeddedSubtitles() } };
    // Listed first so the server keeps the original file whenever it can.
    const QJsonObject direct { { "enabled", true }, { "supported_on_device", true },
        { "containers", caps.value("containers") }, { "video_codecs", caps.value("codecs_video") },
        { "audio_decode_codecs", caps.value("codecs_audio") },
        { "audio_passthrough_codecs", QJsonArray {} }, { "subtitles", subtitles },
        { "features", QJsonArray {} }, { "auth_header_refresh", false },
        { "validated_claims",
            QJsonArray { "client_selected_audio_track_v1", "client_managed_dynamic_range_v1" } },
        { "transformations", QJsonArray {} } };
    // Server remux and transcode reach mpv as HLS. An HLS stream holds only text
    // renditions, so embedded subtitles otherwise arrive as sidecar files mpv
    // adds: text, ASS and PGS (.sup), without the JSON font bundle. mpv
    // tone-maps HDR itself, so the server need not tone-map to change packaging.
    const QJsonObject hlsSubtitles { { "embedded_text", false }, { "sidecar_text", true },
        { "ass_styling", true }, { "embedded_bitmap", false }, { "sidecar_bitmap", true },
        { "font_attachments", false } };
    const QJsonObject hls { { "enabled", true }, { "supported_on_device", true },
        { "containers", QJsonArray {} }, { "video_codecs", caps.value("codecs_video") },
        { "audio_decode_codecs", caps.value("codecs_audio") },
        { "audio_passthrough_codecs", QJsonArray {} },
        { "hdr_details",
            QJsonObject { { "hdr10", true }, { "hdr10_plus", true }, { "hlg", true },
                { "dolby_vision_profiles", QJsonArray {} } } },
        { "subtitles", hlsSubtitles }, { "features", QJsonArray {} },
        { "auth_header_refresh", false }, { "validated_claims", QJsonArray {} },
        { "transformations", QJsonArray {} } };
    return { { "protocol_version", 3 }, { "form_factor", "desktop" },
        { "app_version",
            context.clientVersion.isEmpty() ? QStringLiteral("unknown") : context.clientVersion },
        { "device", QJsonObject { { "platform", context.devicePlatform } } },
        { "output", QJsonObject {} },
        { "deliveries", QJsonObject { { kOriginalHttp, direct }, { "hls", hls } } } };
}

struct SubtitleOrdinals
{
    QHash<int, int> ordinalByStreamIndex;
    QHash<int, int> streamIndexByOrdinal;
    QList<int> externalStreamIndexes;
};

// Silo numbers subtitles by one combined ordinal: external files first, then
// embedded streams, then downloads. The /watch source lists embedded streams
// (keyed by ffmpeg index) before external files, each group in Silo's order.
SubtitleOrdinals subtitleOrdinals(const QVariantMap &source)
{
    QList<int> embedded;
    SubtitleOrdinals result;
    for (const QVariant &entry : source.value(QStringLiteral("mediaStreams")).toList())
    {
        const QVariantMap stream = entry.toMap();
        const int index = stream.value(QStringLiteral("index"), -1).toInt();
        if (index < 0
            || stream.value(QStringLiteral("type")).toString().compare(
                   QStringLiteral("Subtitle"), Qt::CaseInsensitive)
                != 0)
            continue;
        (stream.value(QStringLiteral("isExternal")).toBool() ? result.externalStreamIndexes
                                                             : embedded)
            .append(index);
    }
    const QList<int> ordered = result.externalStreamIndexes + embedded;
    for (int i = 0; i < ordered.size(); ++i)
    {
        result.ordinalByStreamIndex.insert(ordered.at(i), i);
        result.streamIndexByOrdinal.insert(i, ordered.at(i));
    }
    return result;
}

QString selectedIndex(const QJsonObject &tracks, const QString &kind)
{
    const QJsonValue value = tracks.value(kind).toObject().value(QStringLiteral("index"));
    const int index = value.toInt(-1);
    return index >= 0 ? QString::number(index) : QString();
}

Bloom::PlaybackTrack subtitleTrack(const QJsonObject &item, int sourceIndex)
{
    Bloom::PlaybackTrack track;
    track.trackId = QString::number(item.value("combined_index").toInt(-1));
    track.kind = QStringLiteral("subtitle");
    track.language = item.value("language").toString();
    track.codec = item.value("codec").toString();
    track.displayTitle = item.value("label").toString();
    track.isDefault = item.value("default").toBool();
    track.isForced = item.value("forced").toBool();
    track.isHearingImpaired = item.value("hearing_impaired").toBool();
    track.sourceIndex = sourceIndex;
    return track;
}

// Turns a playable v3 decision into a descriptor, or explains in *rejection why
// Bloom cannot execute it. allowReanchor says the server can move a
// server-anchored stream, which forward-only plans need for every seek.
Bloom::PlaybackDescriptor descriptorFromDecision(const PlaybackProviderContext &context,
    const Bloom::MediaRef &media, const QJsonObject &wire, const QVariantMap &source,
    bool allowReanchor, QString *rejection)
{
    const auto reject = [rejection](const QString &reason)
    {
        if (rejection)
            *rejection = reason;
        return Bloom::PlaybackDescriptor {};
    };
    if (wire.value("protocol_version").toInt() != 3
        || wire.value("outcome").toString() != QStringLiteral("playable"))
        return {};
    const QJsonObject plan = wire.value("playback_plan").toObject();
    const QJsonObject stream = plan.value("stream").toObject();
    const QJsonObject timeline = plan.value("timeline").toObject();
    const QString delivery = plan.value("delivery").toString();
    const bool original = delivery == kOriginalHttp;
    if (delivery.isEmpty())
        return {};
    if (!original && delivery != kRemuxHls && delivery != kTranscodeHls)
        return reject(
            QStringLiteral("Silo chose a %1 stream, which Bloom does not request").arg(delivery));
    if (plan.value("protocol_version").toInt() != 3 || plan.value("plan_id").toString().isEmpty()
        || plan.value("plan_attempt_key").toString().isEmpty()
        || stream.value("protocol").toString()
            != (original ? QStringLiteral("http_progressive") : QStringLiteral("hls")))
        return {};
    if (stream.value("header_refresh").toString() != QStringLiteral("none")
        || !stream.value("headers").isObject() || !stream.value("headers").toObject().isEmpty())
        return reject(QStringLiteral("Silo's %1 stream needs request headers that Bloom cannot "
                                     "refresh")
                .arg(delivery));
    const double offset = timeline.value("timeline_offset_seconds").toDouble(-1);
    const double start = timeline.value("player_start_seconds").toDouble(-1);
    const bool seekAnywhere = timeline.value("can_seek_anywhere").toBool();
    if (!std::isfinite(offset) || offset < 0 || !std::isfinite(start) || start < 0)
        return {};
    // The original file always carries its whole timeline.
    if (original && (!seekAnywhere || offset != 0))
        return {};
    if (!seekAnywhere && !allowReanchor)
        return reject(QStringLiteral("Silo's %1 stream can only seek through the server, which "
                                     "this server does not offer")
                .arg(delivery));
    Bloom::PlaybackDescriptor descriptor;
    descriptor.media = media;
    descriptor.playbackSessionId = wire.value("session_id").toString();
    if (descriptor.playbackSessionId.isEmpty())
        descriptor.playbackSessionId = plan.value("session_id").toString();
    if (!plan.value("session_id").toString().isEmpty()
        && plan.value("session_id").toString() != descriptor.playbackSessionId)
        return {};
    descriptor.mediaVersionId = identityString(plan.value("effective_media_file_id"));
    descriptor.stream.method = original ? Bloom::PlaybackMethod::DirectPlay
        : delivery == kRemuxHls         ? Bloom::PlaybackMethod::DirectStream
                                        : Bloom::PlaybackMethod::Transcode;
    descriptor.stream.url = mediaConsumerUrl(context, stream.value("url").toString());
    if (!isHttpUrl(descriptor.stream.url))
        return {};
    // A long playlist carries the stream query once through EXT-X-DEFINE,
    // which FFmpeg's HLS demuxer does not expand, so mpv authenticates its
    // playlist and segment requests with the account bearer as well.
    if (!original && sameOrigin(context, descriptor.stream.url))
        descriptor.stream.headers.insert(
            QStringLiteral("Authorization"), QStringLiteral("Bearer ") + context.accessToken);
    descriptor.stream.timelineOffsetMs = SiloModelMapper::secondsToMilliseconds(offset);
    descriptor.stream.seekRequiresReanchor = !seekAnywhere;
    descriptor.stream.carriesSourceTracks = original;
    const QJsonObject planSource = plan.value("source").toObject();
    descriptor.durationMs = planSource.value("duration_seconds").isDouble()
        ? SiloModelMapper::secondsToMilliseconds(planSource.value("duration_seconds").toDouble())
        : source.value("durationMs").toLongLong();
    // Bloom's positions are source positions; the player starts at start.
    descriptor.startPositionMs = SiloModelMapper::secondsToMilliseconds(start + offset);
    for (const QVariant &entry : source.value("mediaStreams").toList())
    {
        const QVariantMap item = entry.toMap();
        Bloom::PlaybackTrack track;
        track.trackId = QString::number(item.value("index", -1).toInt());
        track.kind = item.value("type").toString().toLower();
        track.language = item.value("language").toString();
        track.codec = item.value("codec").toString();
        track.displayTitle = item.value("displayTitle").toString();
        track.isDefault = item.value("isDefault").toBool();
        track.isForced = item.value("isForced").toBool();
        if (track.kind == QStringLiteral("audio")) {
            track.audio = Bloom::AudioSourceMetadata::fromVariantMap(item);
            descriptor.audioTracks.append(track);
        }
    }
    const QJsonObject selected = plan.value("selected_tracks").toObject();
    descriptor.selectedAudioTrackId = selectedIndex(selected, QStringLiteral("audio"));
    descriptor.selectedSubtitleTrackId = selectedIndex(selected, QStringLiteral("subtitle"));
    if (!original && !descriptor.selectedAudioTrackId.isEmpty())
    {
        // The packaged stream carries only the selected audio, so another
        // track needs a new stream.
        descriptor.stream.deliveredAudioTracks = { descriptor.selectedAudioTrackId.toInt() };
        descriptor.stream.pinsAudioTrack = true;
        descriptor.stream.pinnedAudioTrackId = descriptor.selectedAudioTrackId;
    }
    const QJsonObject subtitle = plan.value("subtitle").toObject();
    const QString mode = subtitle.value("mode").toString();
    const QJsonObject artifact = subtitle.value("artifact").toObject();
    const QJsonObject rendition = subtitle.value("hls_rendition").toObject();
    if (mode != QStringLiteral("off") && mode != QStringLiteral("render")
        && mode != QStringLiteral("convert") && mode != QStringLiteral("burn_in"))
        return {};
    // Native embedded selection belongs to the original file, burn-in and HLS
    // renditions to packaged streams.
    if ((original && (mode == QStringLiteral("burn_in") || !rendition.isEmpty()))
        || (!original && subtitle.contains("embedded")))
        return {};
    if (mode == QStringLiteral("off"))
        descriptor.selectedSubtitleTrackId.clear();
    const SubtitleOrdinals ordinals = subtitleOrdinals(source);
    const QString selectedTrack = mode == QStringLiteral("off")
        ? QString()
        : subtitle.value("track_id").toString();
    for (const QJsonValue &entry : subtitle.value("inventory").toArray())
    {
        const QJsonObject item = entry.toObject();
        const int index = item.value("combined_index").toInt(-1);
        if (index < 0)
            continue;
        const bool embedded = item.value("source").toString() == QStringLiteral("embedded");
        const bool isSelected
            = !selectedTrack.isEmpty() && item.value("track_id").toString() == selectedTrack;
        Bloom::PlaybackTrack track = subtitleTrack(item,
            item.value("source").toString() == QStringLiteral("downloaded")
                ? -1
                : ordinals.streamIndexByOrdinal.value(index, -1));
        if (original)
        {
            // Embedded tracks are already in the original stream mpv opens. Their
            // inventory sidecars would duplicate them and cost a server extraction.
            if (embedded)
                continue;
            track.externalUrl = mediaConsumerUrl(context, item.value("url").toString());
        }
        else if (isSelected && !rendition.isEmpty())
        {
            // The HLS stream carries this track as its subtitle rendition.
            if (track.sourceIndex >= 0)
                descriptor.stream.deliveredSubtitleTracks.append(track.sourceIndex);
        }
        else if (isSelected && mode == QStringLiteral("burn_in"))
        {
            descriptor.stream.subtitleBurnedIn = true;
            descriptor.stream.pinsSubtitleTrack = true;
            if (track.sourceIndex >= 0)
                descriptor.stream.pinnedSubtitleTrackId = QString::number(track.sourceIndex);
        }
        else if (!isSelected && (embedded || item.value("url").toString().isEmpty()))
        {
            // Whole-track extraction of an embedded stream is slow on network
            // storage; asking for the track lets Silo put it in the HLS
            // stream, extract it, or burn it in, in that order.
            track.requiresReplan = true;
        }
        else
        {
            track.externalUrl = mediaConsumerUrl(context, item.value("url").toString());
        }
        if (isSelected && !artifact.isEmpty())
            track.externalUrl = mediaConsumerUrl(context, artifact.value("url").toString());
        track.isExternal = !track.externalUrl.isEmpty();
        descriptor.subtitleTracks.append(track);
    }
    descriptor.reporting = { false, true, true, true };
    return descriptor;
}

} // namespace

QString SiloPlaybackProvider::capabilitiesEndpoint() const
{
    return QStringLiteral("/api/v2/playback/capabilities");
}

bool SiloPlaybackProvider::acceptCapabilities(
    const PlaybackProviderContext &context, const QJsonObject &wire) const
{
    const QString key = scopeKey(context);
    m_installations.remove(key);
    m_previewScopes.remove(key);
    m_serverFeatures.remove(key);
    if (wire.value("state").toString() != QStringLiteral("available")
        || !wire.value("allowed").toBool() || !wire.value("protocol_versions").toArray().contains(3)
        || !wire.value("features").toArray().contains(QStringLiteral("playback_plan_v3"))
        || !wire.value("deliveries").toArray().contains(QStringLiteral("original_http"))
        || wire.value("installation_id").toString().isEmpty())
        return false;
    if (m_installations.size() >= 32)
    {
        m_installations.clear();
        m_previewScopes.clear();
        m_serverFeatures.clear();
    }
    QSet<QString> features;
    for (const QJsonValue &feature : wire.value("features").toArray())
        features.insert(feature.toString());
    m_serverFeatures.insert(key, features);
    if (wire.value("features").toArray().contains(QStringLiteral("trickplay_v1")))
        m_previewScopes.insert(key);
    m_installations.insert(key, wire.value("installation_id").toString());
    return true;
}

Bloom::PlaybackDescriptor SiloPlaybackProvider::createDescriptor(const PlaybackProviderContext &,
    const Bloom::MediaRef &, const QVariantMap &, int, int, qint64, const QString &) const
{
    return {};
}

PlaybackInfoRequest SiloPlaybackProvider::createTrickplayInfoRequest(
    const PlaybackProviderContext &context, const Bloom::MediaRef &media,
    const QString &fileId) const
{
    PlaybackInfoRequest request;
    const QString key = scopeKey(context) + QLatin1Char('/') + media.itemId;
    m_previews.remove(key);
    if (!m_previewScopes.contains(scopeKey(context)) || fileId.isEmpty()
        || !m_previewFiles.value(key).contains(fileId))
        return request;
    QUrl url(QStringLiteral("/api/v2/watch/%1/trickplay")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(media.itemId))));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("file_id"), fileId);
    url.setQuery(query);
    request.endpoint = url.toString(QUrl::FullyEncoded);
    return request;
}

TrickplayTileInfoMap SiloPlaybackProvider::parseTrickplayInfoResponse(
    const PlaybackProviderContext &context, const Bloom::MediaRef &media, const QJsonObject &wire,
    const QString &fileId) const
{
    const QString key = scopeKey(context) + QLatin1Char('/') + media.itemId;
    m_previews.remove(key);
    if (wire.value("file_id").toString() != fileId)
        return {};
    const auto mapped = SiloModelMapper::trickplayInfo(wire);
    const QDateTime expiry
        = QDateTime::fromString(wire.value("expires_at").toString(), Qt::ISODateWithMs);
    if (mapped.isEmpty() || !expiry.isValid() || expiry <= QDateTime::currentDateTimeUtc())
        return {};
    const QJsonArray sheets = wire.value("sheets").toArray();
    QList<QUrl> urls(sheets.size());
    QSet<int> seen;
    for (const auto &entry : sheets)
    {
        const QJsonObject sheet = entry.toObject();
        const int index = sheet.value("index").toInt(-1);
        const QUrl url(relativeOrAbsoluteUrl(context.serverUrl, sheet.value("url").toString()));
        if (index < 0 || index >= urls.size() || seen.contains(index) || !isHttpUrl(url))
            return {};
        seen.insert(index);
        urls[index] = url;
    }
    if (m_previews.size() >= 32)
        m_previews.clear();
    m_previews.insert(key, { mapped.first().width, urls, expiry });
    return mapped;
}

QUrl SiloPlaybackProvider::createTrickplayTileUrl(
    const PlaybackProviderContext &context, const QString &itemId, int width, int tileIndex) const
{
    const auto it = m_previews.constFind(scopeKey(context) + QLatin1Char('/') + itemId);
    if (it == m_previews.cend() || it->width != width || tileIndex < 0
        || tileIndex >= it->sheets.size() || it->expiresAt <= QDateTime::currentDateTimeUtc())
        return {};
    return it->sheets.at(tileIndex);
}

PlaybackInfoRequest SiloPlaybackProvider::createPlaybackInfoRequest(
    const PlaybackProviderContext &, const Bloom::MediaRef &media, const QVariantMap &) const
{
    PlaybackInfoRequest request;
    if (!media.itemId.isEmpty())
        request.endpoint = QStringLiteral("/api/v2/watch/%1")
                               .arg(QString::fromLatin1(QUrl::toPercentEncoding(media.itemId)));
    return request;
}

PlaybackInfoParseResult SiloPlaybackProvider::parsePlaybackInfoResponse(
    const PlaybackProviderContext &context, const Bloom::MediaRef &media, const QJsonObject &wire,
    const QVariantMap &) const
{
    if (m_previewFiles.size() >= 64)
        m_previewFiles.clear();
    QSet<QString> files;
    for (const auto &entry : wire.value("versions").toArray())
    {
        const QJsonObject version = entry.toObject();
        if (version.value("trickplay_available").toBool())
            files.insert(version.value("file_id").toString());
    }
    m_previewFiles.insert(scopeKey(context) + QLatin1Char('/') + media.itemId, files);
    PlaybackInfoParseResult result;
    result.response = SiloModelMapper::playbackInfo(wire);
    result.valid = !result.response.mediaSources.isEmpty();
    if (!result.valid)
        result.error = QStringLiteral("Silo watch state has no playable files");
    return result;
}

PlaybackStartRequest SiloPlaybackProvider::createPlaybackStartRequest(
    const PlaybackProviderContext &context, const Bloom::MediaRef &, const QVariantMap &source,
    int audio, int subtitle, qint64 positionMs) const
{
    PlaybackStartRequest request;
    const QString fileId
        = source.value("fileId", source.value("id", source.value("mediaVersionId"))).toString();
    const QString installation = m_installations.value(scopeKey(context));
    if (fileId.isEmpty() || installation.isEmpty() || context.profileId.isEmpty()
        || context.playbackAttemptId.isEmpty())
        return request;
    request.endpoint = QStringLiteral("/api/v2/playback/start");
    request.replaySafe = true;
    QJsonArray features { "playback_plan_v3", "neutral_playback_v3_contract_v1",
        "sequenced_progress_v1", "plan_source_duration_v1", "embedded_subtitles_v1" };
    // An embedded text subtitle then travels inside the HLS stream, where mpv
    // selects it as the stream's subtitle track, instead of as a sidecar that
    // Silo must extract from the whole file first.
    const QSet<QString> serverFeatures = m_serverFeatures.value(scopeKey(context));
    if (serverFeatures.contains(kHlsRenditions))
    {
        features.append(kHlsRenditions);
        if (serverFeatures.contains(kHlsRemuxRenditions))
            features.append(kHlsRemuxRenditions);
    }
    request.body = { { "installation_id", installation }, { "protocol_version", 3 },
        { "client_features", features },
        { "file_id", fileId }, { "profile_id", context.profileId },
        { "playback_attempt_id", context.playbackAttemptId }, { "allow_alternate_versions", false },
        { "quality_preference", "auto" }, { "subtitle_fidelity_preference", "compatible" },
        { "start_position", qMax<qint64>(0, positionMs) / 1000.0 },
        { "client_capabilities", capabilities() },
        { "client_playback_context", playbackContext(context) } };
    if (audio >= 0)
        request.body.insert(QStringLiteral("audio_track_index"), audio);
    const QHash<int, int> ordinals = subtitleOrdinals(source).ordinalByStreamIndex;
    if (subtitle >= 0 && ordinals.contains(subtitle))
        request.body.insert(QStringLiteral("subtitle_track_index"), ordinals.value(subtitle));
    return request;
}

PlaybackStartParseResult SiloPlaybackProvider::parsePlaybackStartResponse(
    const PlaybackProviderContext &context, const Bloom::MediaRef &media, const QJsonObject &wire,
    const QVariantMap &source) const
{
    PlaybackStartParseResult result;
    QString rejection;
    result.descriptor = descriptorFromDecision(context, media, wire, source,
        m_serverFeatures.value(scopeKey(context)).contains(kSeekReanchor), &rejection);
    const QString sessionId
        = wire.value("session_id")
              .toString(wire.value("playback_plan").toObject().value("session_id").toString());
    const QString installation = m_installations.value(scopeKey(context));
    const bool knownSession = wire.value("protocol_version").toInt() == 3 && !sessionId.isEmpty()
        && !installation.isEmpty() && !context.playbackAttemptId.isEmpty();
    if (knownSession)
    {
        // Abandoned sessions (superseded starts, parts never reached) are never
        // stopped by the player; Silo reaps them, so forget the oldest here.
        if (m_sessions.size() >= 64 && !m_sessions.contains(sessionId))
        {
            auto oldest = m_sessions.begin();
            for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it)
                if (it->order < oldest->order)
                    oldest = it;
            m_sessions.erase(oldest);
        }
        m_sessions.insert(sessionId,
            Session { media, context, installation, wire.value("playback_plan").toObject(), source,
                0, ++m_sessionOrder });
    }
    result.valid = result.descriptor.isValid() && knownSession;
    if (!result.valid) {
        result.descriptor.media = media;
        if (knownSession)
            result.descriptor.playbackSessionId = sessionId;
        const QJsonObject terminal = wire.value("terminal").toObject();
        if (!terminal.value("message").toString().trimmed().isEmpty())
            result.error = terminal.value("message").toString().trimmed();
        else if (!rejection.isEmpty())
            result.error = rejection;
        else
            result.error
                = QStringLiteral("Silo returned an unsupported or malformed playback decision");
    }
    return result;
}

PlaybackReplanRequest SiloPlaybackProvider::createReplanRequest(
    const PlaybackProviderContext &context, const QString &sessionId,
    const PlaybackReplan &replan) const
{
    PlaybackReplanRequest request;
    const auto it = m_sessions.constFind(sessionId);
    if (it == m_sessions.cend() || scopeKey(it->context) != scopeKey(context))
        return request;
    const Session &session = *it;
    const QString fileId = identityString(session.plan.value("effective_media_file_id"));
    const QJsonObject current = session.plan.value("selected_tracks").toObject();
    const bool seek = replan.operation == PlaybackReplan::Operation::SeekReanchor;
    QJsonObject selected;
    const int audio = replan.audioTrackIndex >= 0
        ? replan.audioTrackIndex
        : current.value("audio").toObject().value("index").toInt(-1);
    if (audio >= 0)
        selected.insert(QStringLiteral("audio"),
            QJsonObject { { "id", QStringLiteral("file:%1:audio:%2").arg(fileId).arg(audio) },
                { "index", audio } });
    if (seek)
    {
        // A reanchor moves the timeline only; the server keeps its tracks.
        if (current.contains("subtitle"))
            selected.insert(QStringLiteral("subtitle"), current.value("subtitle"));
    }
    else
    {
        // A track change that omits the subtitle turns subtitles off.
        bool numeric = false;
        int ordinal = replan.subtitleTrackId.toInt(&numeric);
        if (!numeric)
            ordinal = subtitleOrdinals(session.source)
                          .ordinalByStreamIndex.value(replan.subtitleTrackIndex, -1);
        if (ordinal >= 0)
        {
            QString trackId = QStringLiteral("file:%1:subtitle:%2").arg(fileId).arg(ordinal);
            for (const QJsonValue &entry :
                session.plan.value("subtitle").toObject().value("inventory").toArray())
                if (entry.toObject().value("combined_index").toInt(-1) == ordinal)
                    trackId = entry.toObject().value("track_id").toString(trackId);
            selected.insert(
                QStringLiteral("subtitle"), QJsonObject { { "id", trackId }, { "index", ordinal } });
        }
    }
    request.endpoint = QStringLiteral("/api/v2/playback/%1/replan")
                           .arg(QString::fromLatin1(QUrl::toPercentEncoding(sessionId)));
    request.replaySafe = true;
    request.body = { { "installation_id", session.installationId }, { "protocol_version", 3 },
        { "playback_attempt_id", session.context.playbackAttemptId },
        { "replan_request_id", QUuid::createUuid().toString(QUuid::WithoutBraces) },
        { "failed_plan_id", session.plan.value("plan_id") },
        { "plan_attempt_id", QUuid::createUuid().toString(QUuid::WithoutBraces) },
        { "plan_attempt_key", session.plan.value("plan_attempt_key") },
        { "attempted_plan_keys", QJsonArray {} }, { "attempt_count", 1 },
        { "operation", seek ? "seek_reanchor" : "track_change" },
        { "quality_preference", "auto" },
        { "position_seconds", qMax<qint64>(0, replan.positionMs) / 1000.0 },
        { "selected_tracks", selected }, { "client_capabilities", capabilities() },
        { "client_playback_context", playbackContext(context) } };
    return request;
}

PlaybackReplanParseResult SiloPlaybackProvider::parseReplanResponse(
    const PlaybackProviderContext &context, const QJsonObject &wire) const
{
    PlaybackReplanParseResult result;
    const QString sessionId
        = wire.value("session_id")
              .toString(wire.value("playback_plan").toObject().value("session_id").toString());
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || scopeKey(it->context) != scopeKey(context))
    {
        result.error = QStringLiteral("Silo answered for an unknown playback session");
        return result;
    }
    QString rejection;
    result.descriptor = descriptorFromDecision(context, it->media, wire, it->source,
        m_serverFeatures.value(scopeKey(context)).contains(kSeekReanchor), &rejection);
    result.valid = result.descriptor.isValid();
    if (result.valid)
    {
        it->plan = wire.value("playback_plan").toObject();
        return result;
    }
    const QString terminal
        = wire.value("terminal").toObject().value("message").toString().trimmed();
    result.error = !terminal.isEmpty() ? terminal
        : !rejection.isEmpty()         ? rejection
                                       : QStringLiteral("Silo returned an unsupported replan");
    return result;
}

PlaybackReportRequest SiloPlaybackProvider::createReportRequest(const PlaybackReport &report) const
{
    PlaybackReportRequest request;
    auto it = m_sessions.find(report.playbackSessionId);
    if (report.event == PlaybackReportEvent::Start || it == m_sessions.end()
        || it->media.connectionId != report.media.connectionId
        || it->media.itemId != report.media.itemId)
        return request;
    request.endpoint
        = QStringLiteral("/api/v2/playback/%1")
              .arg(QString::fromLatin1(QUrl::toPercentEncoding(report.playbackSessionId)));
    request.replaySafe = true;
    request.body = { { "installation_id", it->installationId } };
    if (report.positionMs >= 0 || report.event != PlaybackReportEvent::Stop)
    {
        request.body.insert(QStringLiteral("sequence"), ++it->sequence);
        request.body.insert(
            QStringLiteral("position"), qMax<qint64>(0, report.positionMs) / 1000.0);
        request.body.insert(QStringLiteral("is_paused"),
            report.event == PlaybackReportEvent::Pause || report.isPaused);
    }
    if (report.event == PlaybackReportEvent::Stop) {
        request.method = QStringLiteral("DELETE");
        request.deferSessionExpiry = false;
        request.body.insert(
            QStringLiteral("stop_id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        m_sessions.erase(it);
    }
    else
    {
        request.endpoint += QStringLiteral("/progress");
    }
    return request;
}

PlaybackSessionLoss SiloPlaybackProvider::classifyReportFailure(
    const PlaybackReport &report, int httpStatus) const
{
    // 410 playback_session_ended: an administrator ended it, so leave playback.
    // 404: the attempt expired or was reaped; Silo recovers with a new session.
    if (report.event == PlaybackReportEvent::Stop || report.event == PlaybackReportEvent::Start)
        return PlaybackSessionLoss::None;
    if (httpStatus == 410)
        return PlaybackSessionLoss::Ended;
    if (httpStatus == 404)
        return PlaybackSessionLoss::Expired;
    return PlaybackSessionLoss::None;
}
