#include "SiloPlaybackProvider.h"

#include "providers/silo/SiloModelMapper.h"

#include <QJsonArray>
#include <QCryptographicHash>
#include <QUuid>

#include <QJsonValue>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

bool isHttpUrl(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty()) {
        return false;
    }
    const QString scheme = url.scheme().toLower();
    return (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
        && !url.host().isEmpty();
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

QString scopeKey(const PlaybackProviderContext &context)
{
    const QByteArray identity = context.serverUrl.toEncoded() + '\0'
        + context.profileId.toUtf8() + '\0' + context.accountId.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

QJsonObject capabilities()
{
    return {
        {QStringLiteral("video_evidence"), QStringLiteral("declared")},
        {QStringLiteral("audio_evidence"), QStringLiteral("declared")},
        {QStringLiteral("codecs_video"), QJsonArray{"h264", "hevc", "vp8", "vp9", "av1", "mpeg2video", "mpeg4", "vc1", "theora"}},
        {QStringLiteral("codecs_video_hardware"), QJsonArray{}},
        {QStringLiteral("codecs_audio"), QJsonArray{"aac", "ac3", "eac3", "dts", "truehd", "flac", "opus", "vorbis", "mp3", "alac", "pcm_s16le", "pcm_s24le"}},
        {QStringLiteral("containers"), QJsonArray{"mp4", "mkv", "webm", "mov", "ts", "avi", "mpegts", "ogg", "flac"}},
        {QStringLiteral("max_resolution"), QStringLiteral("8k")},
        {QStringLiteral("hdr"), true}
    };
}

QJsonObject playbackContext(const PlaybackProviderContext &context)
{
    const QJsonObject caps = capabilities();
    const QJsonObject subtitles{{"embedded_text", true}, {"sidecar_text", true},
        {"ass_styling", true}, {"embedded_bitmap", true}, {"sidecar_bitmap", false},
        {"font_attachments", true}};
    const QJsonObject direct{{"enabled", true}, {"supported_on_device", true},
        {"containers", caps.value("containers")}, {"video_codecs", caps.value("codecs_video")},
        {"audio_decode_codecs", caps.value("codecs_audio")}, {"audio_passthrough_codecs", QJsonArray{}},
        {"subtitles", subtitles}, {"features", QJsonArray{}}, {"auth_header_refresh", false},
        {"validated_claims", QJsonArray{"client_selected_audio_track_v1", "client_managed_dynamic_range_v1"}},
        {"transformations", QJsonArray{}}};
    return {{"protocol_version", 3}, {"form_factor", "desktop"},
        {"app_version", context.clientVersion.isEmpty() ? QStringLiteral("unknown") : context.clientVersion},
        {"device", QJsonObject{{"platform", context.devicePlatform}}},
        {"output", QJsonObject{}}, {"deliveries", QJsonObject{{"original_http", direct}}}};
}

QString selectedIndex(const QJsonObject &tracks, const QString &kind)
{
    const QJsonValue value = tracks.value(kind).toObject().value(QStringLiteral("index"));
    const int index = value.toInt(-1);
    return index >= 0 ? QString::number(index) : QString();
}

Bloom::PlaybackDescriptor descriptorFromDecision(const PlaybackProviderContext &context,
                                                  const Bloom::MediaRef &media,
                                                  const QJsonObject &wire,
                                                  const QVariantMap &source)
{
    if (wire.value("protocol_version").toInt() != 3
        || wire.value("outcome").toString() != QStringLiteral("playable")) return {};
    const QJsonObject plan = wire.value("playback_plan").toObject();
    const QJsonObject stream = plan.value("stream").toObject();
    const QJsonObject timeline = plan.value("timeline").toObject();
    const QString delivery = plan.value("delivery").toString();
    // Forward-only plans require reanchoring and source/player timeline translation.
    if (plan.value("protocol_version").toInt() != 3
        || delivery != QStringLiteral("original_http")
        || stream.value("protocol").toString() != QStringLiteral("http_progressive")
        || stream.value("header_refresh").toString() != QStringLiteral("none")
        || !timeline.value("can_seek_anywhere").toBool()
        || timeline.value("timeline_offset_seconds").toDouble(-1) != 0
        || plan.value("plan_id").toString().isEmpty()
        || plan.value("plan_attempt_key").toString().isEmpty()) return {};
    Bloom::PlaybackDescriptor descriptor;
    descriptor.media = media;
    descriptor.playbackSessionId = wire.value("session_id").toString();
    if (descriptor.playbackSessionId.isEmpty()) descriptor.playbackSessionId = plan.value("session_id").toString();
    if (!plan.value("session_id").toString().isEmpty()
        && plan.value("session_id").toString() != descriptor.playbackSessionId) return {};
    descriptor.mediaVersionId = plan.value("effective_media_file_id").toString();
    descriptor.stream.method = Bloom::PlaybackMethod::DirectPlay;
    descriptor.stream.url = QUrl(relativeOrAbsoluteUrl(context.serverUrl, stream.value("url").toString()));
    if (!isHttpUrl(descriptor.stream.url)) return {};
    const QJsonObject headers = stream.value("headers").toObject();
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        const QString value = it.value().toString();
        if (!it.value().isString() || it.key().contains('\r') || it.key().contains('\n')
            || it.key().contains(':') || value.contains('\r') || value.contains('\n')) return {};
        descriptor.stream.headers.insert(it.key(), value);
    }
    const QJsonObject planSource = plan.value("source").toObject();
    descriptor.durationMs = planSource.value("duration_seconds").isDouble()
        ? SiloModelMapper::secondsToMilliseconds(planSource.value("duration_seconds").toDouble())
        : source.value("durationMs").toLongLong();
    const double start = timeline.value("player_start_seconds").toDouble(-1);
    if (!std::isfinite(start) || start < 0) return {};
    descriptor.startPositionMs = SiloModelMapper::secondsToMilliseconds(start);
    for (const QVariant &entry : source.value("mediaStreams").toList()) {
        const QVariantMap item = entry.toMap();
        Bloom::PlaybackTrack track;
        track.trackId = QString::number(item.value("index", -1).toInt());
        track.kind = item.value("type").toString().toLower();
        track.language = item.value("language").toString();
        track.codec = item.value("codec").toString();
        track.displayTitle = item.value("displayTitle").toString();
        track.isDefault = item.value("isDefault").toBool();
        track.isForced = item.value("isForced").toBool();
        if (track.kind == QStringLiteral("audio")) descriptor.audioTracks.append(track);
    }
    const QJsonObject selected = plan.value("selected_tracks").toObject();
    descriptor.selectedAudioTrackId = selectedIndex(selected, QStringLiteral("audio"));
    descriptor.selectedSubtitleTrackId = selectedIndex(selected, QStringLiteral("subtitle"));
    const QJsonObject subtitle = plan.value("subtitle").toObject();
    const QString mode = subtitle.value("mode").toString();
    if (mode != QStringLiteral("off") && mode != QStringLiteral("render")) return {};
    for (const QJsonValue &entry : subtitle.value("inventory").toArray()) {
        const QJsonObject item = entry.toObject();
        const int index = item.value("combined_index").toInt(-1);
        if (index < 0) continue;
        Bloom::PlaybackTrack track;
        track.trackId = QString::number(index);
        track.kind = QStringLiteral("subtitle");
        track.language = item.value("language").toString();
        track.codec = item.value("codec").toString();
        track.displayTitle = item.value("label").toString();
        track.isDefault = item.value("default").toBool();
        track.isForced = item.value("forced").toBool();
        track.isHearingImpaired = item.value("hearing_impaired").toBool();
        track.externalUrl = QUrl(relativeOrAbsoluteUrl(context.serverUrl, item.value("url").toString()));
        track.isExternal = !track.externalUrl.isEmpty();
        if (mode == QStringLiteral("render") && item.value("track_id") == subtitle.value("track_id")) {
            const QJsonObject artifact = subtitle.value("artifact").toObject();
            if (!artifact.isEmpty()) {
                track.externalUrl = QUrl(relativeOrAbsoluteUrl(context.serverUrl, artifact.value("url").toString()));
                track.isExternal = true;
            }
        }
        descriptor.subtitleTracks.append(track);
    }
    descriptor.reporting = {false, true, true, true};
    return descriptor;
}

} // namespace

QString SiloPlaybackProvider::capabilitiesEndpoint() const
{
    return QStringLiteral("/api/v2/playback/capabilities");
}

bool SiloPlaybackProvider::acceptCapabilities(const PlaybackProviderContext &context,
                                              const QJsonObject &wire) const
{
    const QString key = scopeKey(context);
    m_installations.remove(key);
    if (wire.value("state").toString() != QStringLiteral("available")
        || !wire.value("allowed").toBool()
        || !wire.value("protocol_versions").toArray().contains(3)
        || !wire.value("features").toArray().contains(QStringLiteral("playback_plan_v3"))
        || !wire.value("deliveries").toArray().contains(QStringLiteral("original_http"))
        || wire.value("installation_id").toString().isEmpty()) return false;
    if (m_installations.size() >= 32) m_installations.clear();
    m_installations.insert(key, wire.value("installation_id").toString());
    return true;
}

Bloom::PlaybackDescriptor SiloPlaybackProvider::createDescriptor(
    const PlaybackProviderContext &, const Bloom::MediaRef &, const QVariantMap &,
    int, int, qint64, const QString &) const
{
    return {};
}

QUrl SiloPlaybackProvider::createTrickplayTileUrl(const PlaybackProviderContext &, const QString &, int, int) const
{
    return {};
}

PlaybackInfoRequest SiloPlaybackProvider::createPlaybackInfoRequest(
    const PlaybackProviderContext &, const Bloom::MediaRef &media, const QVariantMap &) const
{
    PlaybackInfoRequest request;
    if (!media.itemId.isEmpty()) request.endpoint = QStringLiteral("/api/v2/watch/%1")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(media.itemId)));
    return request;
}

PlaybackInfoParseResult SiloPlaybackProvider::parsePlaybackInfoResponse(
    const PlaybackProviderContext &, const Bloom::MediaRef &, const QJsonObject &wire,
    const QVariantMap &) const
{
    PlaybackInfoParseResult result;
    result.response = SiloModelMapper::playbackInfo(wire);
    result.valid = !result.response.mediaSources.isEmpty();
    if (!result.valid) result.error = QStringLiteral("Silo watch state has no playable files");
    return result;
}

PlaybackStartRequest SiloPlaybackProvider::createPlaybackStartRequest(
    const PlaybackProviderContext &context, const Bloom::MediaRef &, const QVariantMap &source,
    int audio, int subtitle, qint64 positionMs) const
{
    PlaybackStartRequest request;
    const QString fileId = source.value("fileId", source.value("id", source.value("mediaVersionId"))).toString();
    const QString installation = m_installations.value(scopeKey(context));
    if (fileId.isEmpty() || installation.isEmpty() || context.profileId.isEmpty()
        || context.playbackAttemptId.isEmpty()) return request;
    request.endpoint = QStringLiteral("/api/v2/playback/start");
    request.replaySafe = true;
    request.body = {{"installation_id", installation}, {"protocol_version", 3},
        {"client_features", QJsonArray{"playback_plan_v3", "neutral_playback_v3_contract_v1", "sequenced_progress_v1", "plan_source_duration_v1"}},
        {"file_id", fileId}, {"profile_id", context.profileId},
        {"playback_attempt_id", context.playbackAttemptId}, {"allow_alternate_versions", false},
        {"quality_preference", "auto"}, {"subtitle_fidelity_preference", "compatible"},
        {"start_position", qMax<qint64>(0, positionMs) / 1000.0},
        {"client_capabilities", capabilities()}, {"client_playback_context", playbackContext(context)}};
    if (audio >= 0) request.body.insert(QStringLiteral("audio_track_index"), audio);
    if (subtitle >= 0) request.body.insert(QStringLiteral("subtitle_track_index"), subtitle);
    return request;
}

PlaybackStartParseResult SiloPlaybackProvider::parsePlaybackStartResponse(
    const PlaybackProviderContext &context, const Bloom::MediaRef &media,
    const QJsonObject &wire, const QVariantMap &source) const
{
    PlaybackStartParseResult result;
    result.descriptor = descriptorFromDecision(context, media, wire, source);
    result.valid = result.descriptor.isValid() && !m_installations.value(scopeKey(context)).isEmpty()
        && !context.playbackAttemptId.isEmpty() && m_sessions.size() < 64;
    if (result.valid) {
        m_sessions.insert(result.descriptor.playbackSessionId,
            Session{media, context, m_installations.value(scopeKey(context)), wire.value("playback_plan").toObject(), 0});
    } else {
        result.error = QStringLiteral("Silo returned an unsupported or malformed playback decision");
    }
    return result;
}

PlaybackAudioSwitchRequest SiloPlaybackProvider::createAudioSwitchRequest(
    const PlaybackProviderContext &context, const QString &sessionId, int audio, qint64 positionMs) const
{
    PlaybackAudioSwitchRequest request;
    const auto it = m_sessions.constFind(sessionId);
    if (it == m_sessions.cend() || audio < 0 || scopeKey(it->context) != scopeKey(context)) return request;
    const Session &session = *it;
    QJsonObject selected = session.plan.value("selected_tracks").toObject();
    selected.insert(QStringLiteral("audio"), QJsonObject{
        {"id", QStringLiteral("file:%1:audio:%2").arg(session.plan.value("effective_media_file_id").toString()).arg(audio)},
        {"index", audio}});
    request.endpoint = QStringLiteral("/api/v2/playback/%1/replan")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(sessionId)));
    request.method = QStringLiteral("POST");
    request.replaySafe = true;
    request.body = {{"installation_id", session.installationId}, {"protocol_version", 3},
        {"playback_attempt_id", session.context.playbackAttemptId},
        {"replan_request_id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"failed_plan_id", session.plan.value("plan_id")},
        {"plan_attempt_id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"plan_attempt_key", session.plan.value("plan_attempt_key")},
        {"attempted_plan_keys", QJsonArray{}}, {"attempt_count", 1},
        {"operation", "track_change"}, {"quality_preference", "auto"},
        {"position_seconds", qMax<qint64>(0, positionMs) / 1000.0},
        {"selected_tracks", selected}, {"client_capabilities", capabilities()},
        {"client_playback_context", playbackContext(context)}};
    return request;
}

PlaybackAudioSwitchParseResult SiloPlaybackProvider::parseAudioSwitchResponse(
    const PlaybackProviderContext &context, const QJsonObject &wire) const
{
    PlaybackAudioSwitchParseResult result;
    const QString sessionId = wire.value("session_id").toString(
        wire.value("playback_plan").toObject().value("session_id").toString());
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || scopeKey(it->context) != scopeKey(context)) return result;
    const auto descriptor = descriptorFromDecision(context, it->media, wire, {});
    result.valid = descriptor.isValid();
    if (result.valid) {
        it->plan = wire.value("playback_plan").toObject();
        result.reloadUrl = descriptor.stream.url;
    } else {
        result.error = QStringLiteral("Silo returned an unsupported audio replan");
    }
    return result;
}

PlaybackRecoveryRequest SiloPlaybackProvider::createPlaybackRecoveryRequest(
    const PlaybackProviderContext &context, const Bloom::MediaRef &media,
    const QVariantMap &source, int audio, qint64 positionMs) const
{
    const auto start = createPlaybackStartRequest(context, media, source, audio, -1, positionMs);
    return {start.endpoint, start.method, start.body, start.replaySafe};
}

PlaybackStartParseResult SiloPlaybackProvider::parsePlaybackRecoveryResponse(
    const PlaybackProviderContext &context, const Bloom::MediaRef &media,
    const QJsonObject &wire, const QVariantMap &source) const
{
    return parsePlaybackStartResponse(context, media, wire, source);
}

PlaybackReportRequest SiloPlaybackProvider::createReportRequest(const PlaybackReport &report) const
{
    PlaybackReportRequest request;
    auto it = m_sessions.find(report.playbackSessionId);
    if (report.event == PlaybackReportEvent::Start || it == m_sessions.end()
        || it->media.connectionId != report.media.connectionId || it->media.itemId != report.media.itemId) return request;
    request.endpoint = QStringLiteral("/api/v2/playback/%1")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(report.playbackSessionId)));
    request.replaySafe = true;
    request.body = {{"installation_id", it->installationId}, {"sequence", ++it->sequence},
        {"position", qMax<qint64>(0, report.positionMs) / 1000.0},
        {"is_paused", report.event == PlaybackReportEvent::Pause || report.isPaused}};
    if (report.event == PlaybackReportEvent::Stop) {
        request.method = QStringLiteral("DELETE");
        request.deferSessionExpiry = false;
        request.body.insert(QStringLiteral("stop_id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        m_sessions.erase(it);
    } else {
        request.endpoint += QStringLiteral("/progress");
    }
    return request;
}
