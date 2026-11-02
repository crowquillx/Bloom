#include <QtTest/QtTest>
#include <QJsonArray>
#include <QUrl>
#include <QFile>
#include <QJsonDocument>

#include "providers/silo/SiloPlaybackProvider.h"

namespace {
PlaybackProviderContext context()
{
    PlaybackProviderContext result;
    result.serverUrl = QUrl(QStringLiteral("https://silo.example.test/base"));
    result.profileId = QStringLiteral("profile-7");
    result.accountId = QStringLiteral("account-1");
    result.accessToken = QStringLiteral("account-token");
    result.playbackAttemptId = QStringLiteral("attempt-0001");
    return result;
}
Bloom::MediaRef media()
{
    return {QStringLiteral("connection-1"), QStringLiteral("item-42")};
}
QJsonObject capability()
{
    return { { "state", "available" }, { "allowed", true }, { "installation_id", "installation-1" },
        { "protocol_versions", QJsonArray { 3 } },
        { "features", QJsonArray { "playback_plan_v3" } },
        { "deliveries", QJsonArray { "original_http" } }, { "future_field", true } };
}
QVariantMap source() { return { { "fileId", "opaque/file-42" }, { "durationMs", 120000 } }; }
QJsonObject decision()
{
    return { { "protocol_version", 3 }, { "outcome", "playable" }, { "session_id", "session-42" },
        { "playback_plan",
            QJsonObject { { "protocol_version", 3 }, { "plan_id", "plan:00000001" },
                { "plan_attempt_key", "v3:00000001" }, { "session_id", "session-42" },
                { "delivery", "original_http" }, { "requested_media_file_id", "opaque/file-42" },
                { "effective_media_file_id", "opaque/file-42" },
                { "stream",
                    QJsonObject { { "url", "/api/v2/stream/session-42?st=opaque%2Bvalue" },
                        { "protocol", "http_progressive" }, { "headers", QJsonObject {} },
                        { "header_refresh", "none" } } },
                { "timeline",
                    QJsonObject { { "source_start_seconds", 12.345 },
                        { "player_start_seconds", 12.345 }, { "stream_origin_seconds", 0 },
                        { "timeline_offset_seconds", 0 }, { "can_seek_anywhere", true } } },
                { "source",
                    QJsonObject {
                        { "media_file_id", "opaque/file-42" }, { "duration_seconds", 123.456 } } },
                { "selected_tracks",
                    QJsonObject { { "audio",
                        QJsonObject {
                            { "id", "file:opaque/file-42:audio:0" }, { "index", 0 } } } } },
                { "subtitle", QJsonObject { { "mode", "off" }, { "inventory", QJsonArray {} } } },
                { "future_field", 1 } } },
        { "future_field", 2 } };
}
QVariantMap stream(const QString &type, int index, const QString &language, bool external = false)
{
    return { { "type", type }, { "index", index }, { "language", language },
        { "isExternal", external } };
}
// /watch order: embedded subtitles by ffmpeg index, then external files.
QVariantMap sourceWithSubtitles()
{
    QVariantMap result = source();
    result["mediaStreams"] = QVariantList { stream("Video", 0, {}), stream("Audio", 0, "eng"),
        stream("Subtitle", 2, "eng"), stream("Subtitle", 3, "spa"), stream("Subtitle", 4, "fre"),
        stream("Subtitle", 5, "eng", true), stream("Subtitle", 6, "ger", true) };
    return result;
}
QJsonObject inventoryEntry(int combinedIndex, const QString &source, const QString &language)
{
    return { { "track_id", QStringLiteral("file:opaque/file-42:subtitle:%1").arg(combinedIndex) },
        { "combined_index", combinedIndex }, { "source", source }, { "language", language },
        { "codec", "subrip" }, { "forced", false }, { "default", false },
        { "hearing_impaired", false }, { "delivery", "sidecar" },
        { "url", QStringLiteral("/api/v2/stream/session-42/subtitles/%1.vtt").arg(combinedIndex) } };
}
PlaybackReplan audioReplan(int audio, qint64 positionMs)
{
    PlaybackReplan replan;
    replan.audioTrackIndex = audio;
    replan.positionMs = positionMs;
    return replan;
}
QJsonObject fixture(const QString &name)
{
    QFile file(QFINDTESTDATA(QStringLiteral("contracts/silo-playback-v3/") + name));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
// The protocol fixture's features with the /api/v2 capability envelope.
QJsonObject fixtureCapability()
{
    auto result = capability();
    result["features"] = fixture(QStringLiteral("capability_response.json")).value("features");
    return result;
}
// /watch for the fixture's file 42: embedded subtitles at ffmpeg indexes 3-5
// (combined ordinals 1-3) after one external file (index 6, ordinal 0).
QVariantMap fixtureSource()
{
    QVariantMap result = source();
    result["fileId"] = "42";
    result["mediaStreams"] = QVariantList { stream("Video", 0, {}), stream("Audio", 0, "eng"),
        stream("Subtitle", 3, "eng"), stream("Subtitle", 4, "jpn"), stream("Subtitle", 5, "fre"),
        stream("Subtitle", 6, "eng", true) };
    return result;
}
void prime(SiloPlaybackProvider &provider)
{
    QVERIFY(provider.acceptCapabilities(context(), capability()));
    QVERIFY(provider.parsePlaybackStartResponse(context(), media(), decision(), source()).valid);
}
}

class SiloPlaybackProviderTest : public QObject
{
    Q_OBJECT
private slots:
    void gatesStartOnCapabilitiesAndProfile();
    void createsV3StartWithOpaqueIdentity();
    void parsesDecisionAndOpaqueStream();
    void rejectsUnknownAndUnsafePlans();
    void mapsWatchStateAndSeconds();
    void mapsServerTrackPreferences();
    void writesTrackPreferencesWithSiloIdentities();
    void storedAudioPreferenceTrustsTheResolvedTrack();
    void audioSignatureKeepsSiloTitles();
    void replansAudioWithDurableRequestIdentity();
    void sequencesReportsAndFinalizesStop();
    void isolatesAccountsAndConnections();
    void mapsTrickplayWithVersionAndExpiryGuards();
    void startBodyMatchesPinnedV2Schema();
    void mediaAuthorizationStaysOnItsOriginAndRejectedPlansStop();
    void sendsSubtitleSelectionAsCombinedOrdinal();
    void claimsNativeEmbeddedSubtitles();
    void keepsEmbeddedSubtitlesInTheOriginalStream();
    void evictsOldestSessionInsteadOfRefusingStarts();
    void classifiesLostSessionsAndSurfacesTerminals();
    void negotiatesHlsAfterTheOriginal();
    void acceptsTranscodeWithSubtitleRendition();
    void anchorsRemuxTimelineAndRoutesSubtitles();
    void replansSeeksAndTrackChanges();
};

void SiloPlaybackProviderTest::gatesStartOnCapabilitiesAndProfile()
{
    SiloPlaybackProvider provider;
    QVERIFY(!provider.createPlaybackStartRequest(context(), media(), source(), 0, -1, 0).isValid());
    for (const QString &state : { QStringLiteral("disabled"), QStringLiteral("future_state") })
    {
        auto wire = capability();
        wire["state"] = state;
        QVERIFY(!provider.acceptCapabilities(context(), wire));
    }
    auto wire = capability();
    wire["allowed"] = false;
    QVERIFY(!provider.acceptCapabilities(context(), wire));
    QVERIFY(provider.acceptCapabilities(context(), capability()));
    auto ctx = context();
    ctx.profileId.clear();
    QVERIFY(!provider.createPlaybackStartRequest(ctx, media(), source(), 0, -1, 0).isValid());
    wire = capability();
    wire["protocol_versions"] = QJsonArray { 4 };
    QVERIFY(!provider.acceptCapabilities(context(), wire));
    QVERIFY(!provider.createPlaybackStartRequest(context(), media(), source(), 0, -1, 0).isValid());
}

void SiloPlaybackProviderTest::createsV3StartWithOpaqueIdentity()
{
    SiloPlaybackProvider provider;
    QVERIFY(provider.acceptCapabilities(context(), capability()));
    const auto request
        = provider.createPlaybackStartRequest(context(), media(), source(), 2, -1, 12345);
    QCOMPARE(request.endpoint, QStringLiteral("/api/v2/playback/start"));
    QCOMPARE(request.body.value("file_id").toString(), QStringLiteral("opaque/file-42"));
    QCOMPARE(request.body.value("protocol_version").toInt(), 3);
    QCOMPARE(request.body.value("installation_id").toString(), QStringLiteral("installation-1"));
    QCOMPARE(request.body.value("playback_attempt_id").toString(), context().playbackAttemptId);
    QCOMPARE(request.body.value("start_position").toDouble(), 12.345);
    QCOMPARE(request.body.value("audio_track_index").toInt(), 2);
    QVERIFY(!request.body.contains("subtitle_track_index"));
    const auto caps = request.body.value("client_capabilities").toObject();
    QCOMPARE(caps.value("video_evidence").toString(), QStringLiteral("declared"));
    QVERIFY(caps.value("codecs_video").toArray().contains(QStringLiteral("hevc")));
    const QJsonArray audioCodecs = caps.value("codecs_audio").toArray();
    QVERIFY(audioCodecs.contains(QStringLiteral("wmav2")));
    QVERIFY(audioCodecs.contains(QStringLiteral("wmapro")));
    QVERIFY(audioCodecs.contains(QStringLiteral("mlp")));
    QVERIFY(audioCodecs.contains(QStringLiteral("pcm_bluray")));
    QVERIFY(request.body.value("client_playback_context")
            .toObject()
            .value("deliveries")
            .toObject()
            .contains("original_http"));
    QVERIFY(!request.body.value("allow_alternate_versions").toBool(true));
}

void SiloPlaybackProviderTest::parsesDecisionAndOpaqueStream()
{
    SiloPlaybackProvider provider;
    QVERIFY(provider.acceptCapabilities(context(), capability()));
    const auto parsed
        = provider.parsePlaybackStartResponse(context(), media(), decision(), source());
    QVERIFY(parsed.valid);
    QCOMPARE(parsed.descriptor.mediaVersionId, QStringLiteral("opaque/file-42"));
    QCOMPARE(parsed.descriptor.playbackSessionId, QStringLiteral("session-42"));
    QCOMPARE(parsed.descriptor.durationMs, 123456);
    QCOMPARE(parsed.descriptor.startPositionMs, 12345);
    QCOMPARE(parsed.descriptor.stream.method, Bloom::PlaybackMethod::DirectPlay);
    QCOMPARE(parsed.descriptor.stream.url.toEncoded(),
        QByteArrayLiteral("https://silo.example.test/api/v2/stream/"
                          "session-42?st=opaque%2Bvalue&token=account-token"));
    QVERIFY(parsed.descriptor.reporting.progress);
    QVERIFY(!parsed.descriptor.reporting.start);
}

void SiloPlaybackProviderTest::rejectsUnknownAndUnsafePlans()
{
    SiloPlaybackProvider provider;
    QVERIFY(provider.acceptCapabilities(context(), capability()));
    auto wire = decision();
    wire["outcome"] = "future_outcome";
    QVERIFY(!provider.parsePlaybackStartResponse(context(), media(), wire, source()).valid);
    wire = decision();
    auto plan = wire.value("playback_plan").toObject();
    plan["delivery"] = "future_delivery";
    wire["playback_plan"] = plan;
    QVERIFY(!provider.parsePlaybackStartResponse(context(), media(), wire, source()).valid);
    wire = decision();
    plan = wire.value("playback_plan").toObject();
    auto stream = plan.value("stream").toObject();
    stream["url"] = "file:///etc/passwd";
    plan["stream"] = stream;
    wire["playback_plan"] = plan;
    QVERIFY(!provider.parsePlaybackStartResponse(context(), media(), wire, source()).valid);
    wire = decision();
    plan = wire.value("playback_plan").toObject();
    auto timeline = plan.value("timeline").toObject();
    timeline["timeline_offset_seconds"] = 12;
    plan["timeline"] = timeline;
    wire["playback_plan"] = plan;
    QVERIFY(!provider.parsePlaybackStartResponse(context(), media(), wire, source()).valid);
    QVERIFY(!provider
            .parsePlaybackStartResponse(context(), media(),
                QJsonObject { { "stream_url", "https://example.test" } }, source())
            .valid);
}

void SiloPlaybackProviderTest::mapsWatchStateAndSeconds()
{
    SiloPlaybackProvider provider;
    QCOMPARE(provider.createPlaybackInfoRequest(context(), media(), {}).endpoint,
        QStringLiteral("/api/v2/watch/item-42"));
    const auto parsed = provider.parsePlaybackInfoResponse(context(), media(),
        QJsonObject { { "versions",
            QJsonArray { QJsonObject { { "file_id", "file-1" }, { "duration_seconds", 123 },
                { "audio_tracks",
                    QJsonArray {
                        QJsonObject { { "codec", "aac" }, { "default", true } } } } } } } });
    QVERIFY(parsed.valid);
    QCOMPARE(parsed.response.mediaSources.first().id, QStringLiteral("file-1"));
    QCOMPARE(parsed.response.mediaSources.first().durationMs, 123000);
}

void SiloPlaybackProviderTest::mapsServerTrackPreferences()
{
    SiloPlaybackProvider provider;
    const QJsonArray audio {
        QJsonObject { { "codec", "truehd" }, { "language", "en" }, { "default", true },
            { "title", "TrueHD 7.1" }, { "embedded_title", "Main" } },
        QJsonObject { { "codec", "aac" }, { "language", "ja" }, { "default", false },
            { "title", "AAC Stereo" } } };
    const QJsonArray subtitles { QJsonObject { { "index", 3 }, { "language", "en" },
        { "codec", "subrip" }, { "forced", false }, { "hearing_impaired", false } } };
    const auto version = [&](int effectiveAudio)
    {
        return QJsonObject { { "file_id", "file-1" }, { "audio_tracks", audio },
            { "subtitle_tracks", subtitles }, { "effective_audio_track_index", effectiveAudio },
            { "effective_audio_language", "ja" } };
    };
    auto parsed = provider.parsePlaybackInfoResponse(context(), media(),
        QJsonObject { { "versions", QJsonArray { version(1) } },
            { "effective_subtitle_mode", "always" }, { "effective_subtitle_language", "en" },
            { "effective_show_forced_subtitles", false },
            { "effective_subtitle_track_signature",
                QJsonObject { { "source", "embedded" }, { "language", "en" },
                    { "codec", "subrip" }, { "label", "English" }, { "forced", false },
                    { "hearing_impaired", true } } } });
    QVERIFY(parsed.valid);
    const MediaSourceInfo source = parsed.response.mediaSources.first();
    // The ordinal into audio_tracks is Bloom's Silo audio stream index.
    QCOMPARE(source.serverTrackPreferences.audioStreamIndex, 1);
    QCOMPARE(source.serverTrackPreferences.subtitleMode, QStringLiteral("always"));
    QCOMPARE(source.serverTrackPreferences.subtitleLanguage, QStringLiteral("en"));
    QVERIFY(source.serverTrackPreferences.showForcedSubtitles == std::optional<bool>(false));
    QCOMPARE(source.serverTrackPreferences.subtitleSignature.label, QStringLiteral("English"));
    QVERIFY(source.serverTrackPreferences.subtitleSignature.hearingImpaired);
    QCOMPARE(source.mediaStreams.at(0).embeddedTitle, QStringLiteral("Main"));
    QCOMPARE(source.mediaStreams.at(0).title, QStringLiteral("TrueHD 7.1"));
    const QVariantMap mapped =
        parsed.response.getMediaSourcesVariant().first().toMap().value("serverTrackPreferences").toMap();
    QCOMPARE(mapped.value("audioStreamIndex").toInt(), 1);
    QCOMPARE(mapped.value("subtitleMode").toString(), QStringLiteral("always"));

    // Silo resolves the file's default track even without a preference: no
    // server choice then, and no stored subtitle preference either.
    parsed = provider.parsePlaybackInfoResponse(
        context(), media(), QJsonObject { { "versions", QJsonArray { version(0) } } });
    QVERIFY(parsed.valid);
    QVERIFY(parsed.response.mediaSources.first().serverTrackPreferences.isEmpty());
    QVERIFY(!parsed.response.getMediaSourcesVariant().first().toMap().contains(
        "serverTrackPreferences"));

    // A track FFmpeg cannot decode is not Silo's fallback.
    QJsonArray undecodable = audio;
    undecodable[0] = QJsonObject { { "codec", "unknown" }, { "default", true } };
    QJsonObject withUndecodable = version(1);
    withUndecodable["audio_tracks"] = undecodable;
    parsed = provider.parsePlaybackInfoResponse(
        context(), media(), QJsonObject { { "versions", QJsonArray { withUndecodable } } });
    QCOMPARE(parsed.response.mediaSources.first().serverTrackPreferences.audioStreamIndex, -1);

    // The track Silo resolved is kept aside for the stored-preference read.
    QCOMPARE(parsed.response.mediaSources.first().serverTrackPreferences.resolvedAudioStreamIndex,
        1);

    // A stored language without a mode follows Silo's default mode, auto.
    parsed = provider.parsePlaybackInfoResponse(context(), media(),
        QJsonObject { { "versions", QJsonArray { version(0) } },
            { "effective_subtitle_language", "fr" } });
    QCOMPARE(parsed.response.mediaSources.first().serverTrackPreferences.subtitleMode,
        QStringLiteral("auto"));
}

void SiloPlaybackProviderTest::writesTrackPreferencesWithSiloIdentities()
{
    SiloPlaybackProvider provider;
    QVariantMap source = sourceWithSubtitles();
    QVariantList streams = source.value("mediaStreams").toList();
    QVariantMap english = stream("Audio", 0, "en");
    english["title"] = "TrueHD 7.1";
    english["embeddedTitle"] = "Main";
    english["codec"] = "truehd";
    english["channelLayout"] = "7.1";
    english["channels"] = 8;
    QVariantMap japanese = stream("Audio", 1, "und");
    japanese["codec"] = "aac";
    streams[1] = english;
    streams.insert(2, japanese);
    source["mediaStreams"] = streams;

    TrackPreferenceWrite write;
    write.contentId = QStringLiteral("series/7");
    write.mediaSource = source;
    write.streamIndex = 0;
    auto request = provider.createTrackPreferenceWriteRequest(write);
    QVERIFY(request.isValid());
    QCOMPARE(request.method, QStringLiteral("PUT"));
    QCOMPARE(request.endpoint, QStringLiteral("/api/v2/audio-prefs/series%2F7"));
    QVERIFY(request.replaySafe);
    QCOMPARE(request.body.value("audio_track_index").toInt(), 0);
    QCOMPARE(request.body.value("audio_language").toString(), QStringLiteral("en"));
    const QJsonObject signature = request.body.value("track_signature").toObject();
    QCOMPARE(signature.value("title").toString(), QStringLiteral("TrueHD 7.1"));
    QCOMPARE(signature.value("embedded_title").toString(), QStringLiteral("Main"));
    QCOMPARE(signature.value("codec").toString(), QStringLiteral("truehd"));
    QCOMPARE(signature.value("layout").toString(), QStringLiteral("7.1"));
    QCOMPARE(signature.value("channels").toInt(), 8);
    QVERIFY(!signature.contains("default"));

    // An undetermined language is no preference; the ordinal still counts.
    write.streamIndex = 1;
    request = provider.createTrackPreferenceWriteRequest(write);
    QCOMPARE(request.body.value("audio_track_index").toInt(), 1);
    QVERIFY(!request.body.contains("audio_language"));

    // Subtitles use Silo's combined ordinal: external files, then embedded.
    write.kind = TrackPreferenceWrite::Kind::Subtitle;
    write.streamIndex = 6;
    request = provider.createTrackPreferenceWriteRequest(write);
    QCOMPARE(request.endpoint, QStringLiteral("/api/v2/subtitle-prefs/series%2F7"));
    QCOMPARE(request.body.value("subtitle_track_index").toInt(), 1);
    QCOMPARE(request.body.value("subtitle_mode").toString(), QStringLiteral("always"));
    QCOMPARE(request.body.value("subtitle_language").toString(), QStringLiteral("ger"));
    QCOMPARE(request.body.value("track_signature").toObject().value("source").toString(),
        QStringLiteral("external"));
    QVERIFY(!request.body.contains("show_forced_subtitles"));
    write.streamIndex = 2;
    request = provider.createTrackPreferenceWriteRequest(write);
    QCOMPARE(request.body.value("subtitle_track_index").toInt(), 2);
    QCOMPARE(request.body.value("track_signature").toObject().value("source").toString(),
        QStringLiteral("embedded"));

    write.streamIndex = -1;
    request = provider.createTrackPreferenceWriteRequest(write);
    QCOMPARE(request.body.value("subtitle_track_index").toInt(), -1);
    QCOMPARE(request.body.value("subtitle_mode").toString(), QStringLiteral("off"));
    QVERIFY(!request.body.contains("subtitle_language"));

    // Tracks Silo does not know, and items outside a series, are not written.
    write.streamIndex = 9;
    QVERIFY(!provider.createTrackPreferenceWriteRequest(write).isValid());
    write.streamIndex = 2;
    write.contentId.clear();
    QVERIFY(!provider.createTrackPreferenceWriteRequest(write).isValid());

    // A movie's preference lives under the movie's own content ID.
    write.contentId = QStringLiteral("movie:heat-1995");
    request = provider.createTrackPreferenceWriteRequest(write);
    QCOMPARE(request.endpoint, QStringLiteral("/api/v2/subtitle-prefs/movie%3Aheat-1995"));
    write.kind = TrackPreferenceWrite::Kind::Audio;
    write.streamIndex = 0;
    request = provider.createTrackPreferenceWriteRequest(write);
    QCOMPARE(request.endpoint, QStringLiteral("/api/v2/audio-prefs/movie%3Aheat-1995"));
}

void SiloPlaybackProviderTest::storedAudioPreferenceTrustsTheResolvedTrack()
{
    SiloPlaybackProvider provider;
    const QJsonArray audio {
        QJsonObject { { "codec", "truehd" }, { "language", "en" }, { "default", true } },
        QJsonObject { { "codec", "aac" }, { "language", "ja" }, { "default", false } } };
    const auto watch = [&](int effectiveAudio, const QJsonObject &item)
    {
        QJsonObject result = item;
        result["versions"] = QJsonArray { QJsonObject { { "file_id", "file-1" },
            { "audio_tracks", audio }, { "effective_audio_track_index", effectiveAudio } } };
        return result;
    };
    const QJsonObject episode { { "content_id", "episode-2" }, { "type", "episode" },
        { "series_id", "series-1" } };

    // On season 1 the user picked the English track, which is also the file's
    // default. On season 2 (or another device) Silo resolves to it again, and
    // the watch response alone cannot tell it from no preference.
    auto parsed = provider.parsePlaybackInfoResponse(context(), media(), watch(0, episode));
    QVERIFY(parsed.valid);
    QCOMPARE(parsed.response.trackPreferenceContentId, QStringLiteral("series-1"));
    QCOMPARE(parsed.response.mediaSources.first().serverTrackPreferences.audioStreamIndex, -1);
    const PlaybackInfoRequest read = provider.createTrackPreferenceReadRequest(parsed.response);
    QVERIFY(read.isValid());
    QCOMPARE(read.method, QStringLiteral("GET"));
    QCOMPARE(read.endpoint, QStringLiteral("/api/v2/audio-prefs/series-1"));

    // The series has a stored row naming that track: the resolved track is
    // the profile's choice.
    const auto row = [](const QJsonObject &body) { return QJsonDocument(body).toJson(); };
    PlaybackInfoResponse stored = parsed.response;
    provider.applyTrackPreferenceRead(stored, true,
        row(QJsonObject { { "audio_track_index", 0 }, { "audio_language", "en" } }));
    QCOMPARE(stored.mediaSources.first().serverTrackPreferences.audioStreamIndex, 0);
    QCOMPARE(stored.getMediaSourcesVariant()
                 .first()
                 .toMap()
                 .value("serverTrackPreferences")
                 .toMap()
                 .value("audioStreamIndex")
                 .toInt(),
        0);
    // No row (404): the file default stays no preference.
    PlaybackInfoResponse missing = parsed.response;
    provider.applyTrackPreferenceRead(missing, false, {});
    QCOMPARE(missing.mediaSources.first().serverTrackPreferences.audioStreamIndex, -1);

    // The row picked a track this file lacks (French): Silo resolved past it
    // to the default, which is no choice of the profile's, so Bloom's own
    // defaults still decide.
    PlaybackInfoResponse elsewhere = parsed.response;
    provider.applyTrackPreferenceRead(elsewhere, true,
        row(QJsonObject { { "audio_track_index", 0 }, { "audio_language", "fr" },
            { "track_signature", QJsonObject { { "language", "fr" }, { "codec", "ac3" } } } }));
    QCOMPARE(elsewhere.mediaSources.first().serverTrackPreferences.audioStreamIndex, -1);
    // A row without a language counts only by its exact track signature.
    PlaybackInfoResponse bySignature = parsed.response;
    provider.applyTrackPreferenceRead(bySignature, true,
        row(QJsonObject { { "audio_track_index", 3 },
            { "track_signature", QJsonObject { { "language", "en-US" }, { "codec", "TrueHD" } } } }));
    QCOMPARE(bySignature.mediaSources.first().serverTrackPreferences.audioStreamIndex, 0);
    PlaybackInfoResponse indexOnly = parsed.response;
    provider.applyTrackPreferenceRead(indexOnly, true,
        row(QJsonObject { { "audio_track_index", 0 } }));
    QCOMPARE(indexOnly.mediaSources.first().serverTrackPreferences.audioStreamIndex, -1);

    // A track other than the fallback is the server's choice already (a
    // profile-wide language), so nothing needs reading.
    parsed = provider.parsePlaybackInfoResponse(context(), media(), watch(1, episode));
    QCOMPARE(parsed.response.mediaSources.first().serverTrackPreferences.audioStreamIndex, 1);
    QVERIFY(!provider.createTrackPreferenceReadRequest(parsed.response).isValid());

    // A movie reads its own row; extras keep none.
    parsed = provider.parsePlaybackInfoResponse(context(), media(),
        watch(0, QJsonObject { { "content_id", "movie:heat-1995" }, { "type", "movie" } }));
    QCOMPARE(provider.createTrackPreferenceReadRequest(parsed.response).endpoint,
        QStringLiteral("/api/v2/audio-prefs/movie%3Aheat-1995"));
    parsed = provider.parsePlaybackInfoResponse(context(), media(),
        watch(0, QJsonObject { { "content_id", "extra-1" }, { "type", "extra" } }));
    QVERIFY(!provider.createTrackPreferenceReadRequest(parsed.response).isValid());
}

void SiloPlaybackProviderTest::audioSignatureKeepsSiloTitles()
{
    // Silo sends no title for the first track, only its container title.
    // Bloom shows the container title, but the signature written back must
    // name the track exactly as Silo does, or Silo's exact match fails.
    SiloPlaybackProvider provider;
    const QJsonArray audio {
        QJsonObject { { "codec", "truehd" }, { "language", "en" }, { "default", true },
            { "embedded_title", "Main" }, { "layout", "7.1" }, { "channels", 8 } },
        QJsonObject { { "codec", "ac3" }, { "language", "en" }, { "default", false },
            { "title", "Commentary" }, { "embedded_title", "Director" } } };
    const auto parsed = provider.parsePlaybackInfoResponse(context(), media(),
        QJsonObject { { "content_id", "episode-2" }, { "type", "episode" },
            { "series_id", "series-1" },
            { "versions",
                QJsonArray { QJsonObject { { "file_id", "file-1" }, { "audio_tracks", audio },
                    { "effective_audio_track_index", 0 } } } } });
    QVERIFY(parsed.valid);
    const MediaSourceInfo source = parsed.response.mediaSources.first();
    QCOMPARE(source.mediaStreams.at(0).title, QStringLiteral("Main"));
    QVERIFY(source.mediaStreams.at(0).titleFromEmbeddedTitle);
    QVERIFY(!source.mediaStreams.at(1).titleFromEmbeddedTitle);

    TrackPreferenceWrite write;
    write.contentId = QStringLiteral("series-1");
    write.mediaSource = parsed.response.getMediaSourcesVariant().first().toMap();
    write.streamIndex = source.mediaStreams.at(0).index;
    QJsonObject signature =
        provider.createTrackPreferenceWriteRequest(write).body.value("track_signature").toObject();
    QVERIFY(!signature.contains("title"));
    QCOMPARE(signature.value("embedded_title").toString(), QStringLiteral("Main"));
    QCOMPARE(signature.value("codec").toString(), QStringLiteral("truehd"));
    QCOMPARE(signature.value("layout").toString(), QStringLiteral("7.1"));
    QCOMPARE(signature.value("channels").toInt(), 8);
    // A track with both titles sends both.
    write.streamIndex = source.mediaStreams.at(1).index;
    const QJsonObject commentary =
        provider.createTrackPreferenceWriteRequest(write).body.value("track_signature").toObject();
    QCOMPARE(commentary.value("title").toString(), QStringLiteral("Commentary"));
    QCOMPARE(commentary.value("embedded_title").toString(), QStringLiteral("Director"));

    // The stored row Silo hands back with that signature names the track.
    const auto row = [](const QJsonObject &sig)
    {
        return QJsonDocument(QJsonObject { { "audio_track_index", 0 }, { "track_signature", sig } })
            .toJson();
    };
    PlaybackInfoResponse stored = parsed.response;
    provider.applyTrackPreferenceRead(stored, true, row(signature));
    QCOMPARE(stored.mediaSources.first().serverTrackPreferences.audioStreamIndex, 0);
    // A signature carrying the display title is not Silo's track.
    signature.insert(QStringLiteral("title"), QStringLiteral("Main"));
    PlaybackInfoResponse displayTitle = parsed.response;
    provider.applyTrackPreferenceRead(displayTitle, true, row(signature));
    QCOMPARE(displayTitle.mediaSources.first().serverTrackPreferences.audioStreamIndex, -1);
}

void SiloPlaybackProviderTest::replansAudioWithDurableRequestIdentity()
{
    SiloPlaybackProvider provider;
    prime(provider);
    const auto request
        = provider.createReplanRequest(context(), QStringLiteral("session-42"), audioReplan(2, 23456));
    QCOMPARE(request.endpoint, QStringLiteral("/api/v2/playback/session-42/replan"));
    QCOMPARE(request.method, QStringLiteral("POST"));
    QCOMPARE(request.body.value("operation").toString(), QStringLiteral("track_change"));
    QCOMPARE(request.body.value("playback_attempt_id").toString(), context().playbackAttemptId);
    QVERIFY(!request.body.value("replan_request_id").toString().isEmpty());
    QCOMPARE(request.body.value("position_seconds").toDouble(), 23.456);
    QCOMPARE(request.body.value("selected_tracks")
                 .toObject()
                 .value("audio")
                 .toObject()
                 .value("index")
                 .toInt(),
        2);
    QVERIFY(provider.parseReplanResponse(context(), decision()).valid);
    QVERIFY(!provider.createReplanRequest(context(), QStringLiteral("unknown-session"), audioReplan(2, 0))
            .isValid());
}

void SiloPlaybackProviderTest::sequencesReportsAndFinalizesStop()
{
    SiloPlaybackProvider provider;
    prime(provider);
    PlaybackReport report;
    report.media = media();
    report.playbackSessionId = QStringLiteral("session-42");
    report.positionMs = 12345;
    const auto progress = provider.createReportRequest(report);
    QCOMPARE(progress.body.value("position").toDouble(), 12.345);
    QCOMPARE(progress.body.value("sequence").toInteger(), 1);
    QCOMPARE(progress.body.value("installation_id").toString(), QStringLiteral("installation-1"));
    QVERIFY(!progress.body.contains("seconds"));
    report.event = PlaybackReportEvent::Pause;
    const auto paused = provider.createReportRequest(report);
    QCOMPARE(paused.body.value("sequence").toInteger(), 2);
    QVERIFY(paused.body.value("is_paused").toBool());
    report.event = PlaybackReportEvent::Stop;
    const auto stop = provider.createReportRequest(report);
    QCOMPARE(stop.method, QStringLiteral("DELETE"));
    QCOMPARE(stop.body.value("sequence").toInteger(), 3);
    QVERIFY(!stop.body.value("stop_id").toString().isEmpty());
    QVERIFY(!provider.createReportRequest(report).isValid());
}

void SiloPlaybackProviderTest::isolatesAccountsAndConnections()
{
    SiloPlaybackProvider provider;
    prime(provider);
    auto ctx = context();
    ctx.accountId = QStringLiteral("another-account");
    QVERIFY(!provider.createReplanRequest(ctx, QStringLiteral("session-42"), audioReplan(0, 0)).isValid());
    PlaybackReport report;
    report.media = media();
    report.media.connectionId = QStringLiteral("another-connection");
    report.playbackSessionId = QStringLiteral("session-42");
    QVERIFY(!provider.createReportRequest(report).isValid());
}

void SiloPlaybackProviderTest::mapsTrickplayWithVersionAndExpiryGuards()
{
    SiloPlaybackProvider provider;
    auto caps = capability();
    caps["features"] = QJsonArray { "playback_plan_v3", "trickplay_v1" };
    QVERIFY(provider.acceptCapabilities(context(), caps));
    const auto watch = QJsonObject { { "versions",
        QJsonArray {
            QJsonObject { { "file_id", "opaque/file-1" }, { "trickplay_available", true } },
            QJsonObject { { "file_id", "file-2" }, { "trickplay_available", false } } } } };
    QVERIFY(provider.parsePlaybackInfoResponse(context(), media(), watch).valid);
    const auto request
        = provider.createTrickplayInfoRequest(context(), media(), QStringLiteral("opaque/file-1"));
    QCOMPARE(
        request.endpoint, QStringLiteral("/api/v2/watch/item-42/trickplay?file_id=opaque/file-1"));
    QVERIFY(!provider.createTrickplayInfoRequest(context(), media(), QStringLiteral("file-2"))
            .isValid());
    QJsonObject previews {
        { "file_id", "opaque/file-1" }, { "interval_ms", 10000 }, { "thumbnail_width", 300 },
        { "thumbnail_height", 169 }, { "tile_columns", 10 }, { "tile_rows", 10 },
        { "thumbnail_count", 120 },
        { "expires_at", QDateTime::currentDateTimeUtc().addSecs(60).toString(Qt::ISODateWithMs) },
        { "sheets",
            QJsonArray {
                QJsonObject { { "index", 1 }, { "url", "/sheet-1.jpg?token=opaque%2Bvalue" } },
                QJsonObject { { "index", 0 }, { "url", "/sheet-0.jpg?token=opaque%2Fvalue" } } } }
    };
    const auto mapped = provider.parseTrickplayInfoResponse(
        context(), media(), previews, QStringLiteral("opaque/file-1"));
    QCOMPARE(mapped.size(), 1);
    QCOMPARE(mapped.first().width, 300);
    QCOMPARE(mapped.first().interval, 10000);
    QCOMPARE(mapped.first().getTileIndex(101), 1);
    QCOMPARE(provider.createTrickplayTileUrl(context(), media().itemId, 300, 0).toEncoded(),
        QByteArrayLiteral("https://silo.example.test/sheet-0.jpg?token=opaque%2Fvalue"));
    QVERIFY(provider.createTrickplayTileUrl(context(), media().itemId, 300, 2).isEmpty());
    auto other = context();
    other.accountId = QStringLiteral("another-account");
    QVERIFY(provider.createTrickplayTileUrl(other, media().itemId, 300, 0).isEmpty());
    QVERIFY(
        provider.parseTrickplayInfoResponse(context(), media(), previews, QStringLiteral("file-2"))
            .isEmpty());
    previews["tile_columns"] = 2147483647;
    QVERIFY(provider
            .parseTrickplayInfoResponse(
                context(), media(), previews, QStringLiteral("opaque/file-1"))
            .isEmpty());
    QVERIFY(provider.createTrickplayTileUrl(context(), media().itemId, 300, 0).isEmpty());
    previews["tile_columns"] = 10;
    previews["expires_at"]
        = QDateTime::currentDateTimeUtc().addSecs(-1).toString(Qt::ISODateWithMs);
    QVERIFY(provider
            .parseTrickplayInfoResponse(
                context(), media(), previews, QStringLiteral("opaque/file-1"))
            .isEmpty());
}

void SiloPlaybackProviderTest::startBodyMatchesPinnedV2Schema()
{
    QFile file(QFINDTESTDATA("contracts/silo-v2-openapi-manifest.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto schemas
        = QJsonDocument::fromJson(file.readAll()).object().value("schemas").toObject();
    SiloPlaybackProvider provider;
    QVERIFY(provider.acceptCapabilities(context(), capability()));
    const auto body
        = provider.createPlaybackStartRequest(context(), media(), source(), 0, -1, 1000).body;
    const auto verify = [](const QJsonObject &object, const QJsonObject &schema)
    {
        for (const auto &key : schema.value("required").toArray())
            QVERIFY2(object.contains(key.toString()), qPrintable(key.toString()));
        const auto properties = schema.value("properties").toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            QVERIFY2(properties.contains(it.key()), qPrintable(it.key()));
    };
    verify(body, schemas.value("PlaybackStartBody").toObject());
    verify(body.value("client_capabilities").toObject(),
        schemas.value("ClientCodecCapabilitiesV3").toObject());
    const auto playback = body.value("client_playback_context").toObject();
    verify(playback, schemas.value("ClientPlaybackContextV3").toObject());
    const auto direct = playback.value("deliveries").toObject().value("original_http").toObject();
    verify(direct, schemas.value("DeliveryCapabilityV3").toObject());
    const auto subtitles = direct.value("subtitles").toObject();
    verify(subtitles, schemas.value("DeliverySubtitleCapabilitiesV3").toObject());
    for (const auto &native : subtitles.value("native_embedded").toArray())
        verify(native.toObject(), schemas.value("NativeEmbeddedSubtitleCapabilityV3").toObject());
    const auto hls = playback.value("deliveries").toObject().value("hls").toObject();
    verify(hls, schemas.value("DeliveryCapabilityV3").toObject());
    verify(hls.value("subtitles").toObject(),
        schemas.value("DeliverySubtitleCapabilitiesV3").toObject());
    verify(hls.value("hdr_details").toObject(), schemas.value("HDRCapabilitiesV3").toObject());
    // Replans carry the same capability bodies plus their own envelope.
    QVERIFY(provider.parsePlaybackStartResponse(context(), media(), decision(), source()).valid);
    PlaybackReplan replan;
    replan.operation = PlaybackReplan::Operation::SeekReanchor;
    verify(provider.createReplanRequest(context(), QStringLiteral("session-42"), replan).body,
        schemas.value("PlaybackReplanBody").toObject());
    QCOMPARE(schemas.value("PlaybackStartBody")
                 .toObject()
                 .value("properties")
                 .toObject()
                 .value("file_id")
                 .toObject()
                 .value("type")
                 .toString(),
        QStringLiteral("string"));
    QVERIFY(body.value("file_id").isString());
}

void SiloPlaybackProviderTest::mediaAuthorizationStaysOnItsOriginAndRejectedPlansStop()
{
    SiloPlaybackProvider provider;
    QVERIFY(provider.acceptCapabilities(context(), capability()));
    auto wire = decision();
    auto plan = wire.value("playback_plan").toObject();
    auto stream = plan.value("stream").toObject();
    stream["url"] = "https://proxy.example.test/opaque?st=signed%2Bvalue";
    plan["stream"] = stream;
    wire["playback_plan"] = plan;
    const auto external = provider.parsePlaybackStartResponse(context(), media(), wire, source());
    QVERIFY(external.valid);
    QVERIFY(!external.descriptor.stream.url.toString().contains("account-token"));
    QCOMPARE(external.descriptor.stream.url.toEncoded(),
        QByteArrayLiteral("https://proxy.example.test/opaque?st=signed%2Bvalue"));
    wire = decision();
    plan = wire.value("playback_plan").toObject();
    plan["delivery"] = "future_delivery";
    wire["playback_plan"] = plan;
    const auto rejected = provider.parsePlaybackStartResponse(context(), media(), wire, source());
    QVERIFY(!rejected.valid);
    QCOMPARE(rejected.descriptor.playbackSessionId, QStringLiteral("session-42"));
    PlaybackReport stop;
    stop.event = PlaybackReportEvent::Stop;
    stop.media = media();
    stop.playbackSessionId = rejected.descriptor.playbackSessionId;
    const auto cleanup = provider.createReportRequest(stop);
    QVERIFY(cleanup.isValid());
    QVERIFY(cleanup.replaySafe);
    QVERIFY(!cleanup.body.contains("position"));
    QVERIFY(!cleanup.body.contains("sequence"));
}

void SiloPlaybackProviderTest::sendsSubtitleSelectionAsCombinedOrdinal()
{
    SiloPlaybackProvider provider;
    QVERIFY(provider.acceptCapabilities(context(), capability()));
    const auto ordinalFor = [&](int streamIndex)
    {
        return provider
            .createPlaybackStartRequest(context(), media(), sourceWithSubtitles(), 0, streamIndex, 0)
            .body.value("subtitle_track_index");
    };
    // Silo counts external files first: 5 -> 0, 6 -> 1, then embedded 2 -> 2.
    QCOMPARE(ordinalFor(5).toInt(), 0);
    QCOMPARE(ordinalFor(6).toInt(), 1);
    QCOMPARE(ordinalFor(2).toInt(), 2);
    QCOMPARE(ordinalFor(4).toInt(), 4);
    QVERIFY(ordinalFor(9).isUndefined());
    QVERIFY(ordinalFor(-1).isUndefined());
}

void SiloPlaybackProviderTest::claimsNativeEmbeddedSubtitles()
{
    SiloPlaybackProvider provider;
    QVERIFY(provider.acceptCapabilities(context(), capability()));
    const auto body = provider.createPlaybackStartRequest(context(), media(), source(), 0, -1, 0).body;
    QVERIFY(body.value("client_features").toArray().contains(QStringLiteral("embedded_subtitles_v1")));
    const auto native = body.value("client_playback_context")
                            .toObject()
                            .value("deliveries")
                            .toObject()
                            .value("original_http")
                            .toObject()
                            .value("subtitles")
                            .toObject()
                            .value("native_embedded")
                            .toArray();
    QVERIFY(!native.isEmpty());
    QVERIFY(native.size() <= 16);
    QStringList containers;
    for (const auto &entry : native)
    {
        const auto capability = entry.toObject();
        QCOMPARE(capability.value("track_identity").toString(),
            QStringLiteral("ffmpeg_stream_index"));
        QVERIFY(capability.value("codecs").toArray().contains(QStringLiteral("hdmv_pgs_subtitle")));
        QVERIFY(capability.value("codecs").toArray().contains(QStringLiteral("ass")));
        containers.append(capability.value("container").toString());
    }
    QVERIFY(containers.contains(QStringLiteral("mkv")));
    QVERIFY(containers.contains(QStringLiteral("mp4")));
    const auto direct = body.value("client_capabilities").toObject().value("containers").toArray();
    QVERIFY(direct.contains(QStringLiteral("m2ts")));
}

void SiloPlaybackProviderTest::keepsEmbeddedSubtitlesInTheOriginalStream()
{
    SiloPlaybackProvider provider;
    QVERIFY(provider.acceptCapabilities(context(), capability()));
    auto wire = decision();
    auto plan = wire.value("playback_plan").toObject();
    plan["selected_tracks"] = QJsonObject { { "subtitle",
        QJsonObject { { "id", "file:opaque/file-42:subtitle:1" }, { "index", 1 } } } };
    plan["subtitle"] = QJsonObject { { "mode", "render" },
        { "track_id", "file:opaque/file-42:subtitle:1" },
        { "inventory",
            QJsonArray { inventoryEntry(0, "external", "eng"), inventoryEntry(1, "external", "ger"),
                inventoryEntry(2, "embedded", "eng"), inventoryEntry(3, "embedded", "spa"),
                inventoryEntry(4, "embedded", "fre"), inventoryEntry(5, "downloaded", "ita") } } };
    wire["playback_plan"] = plan;
    const auto parsed
        = provider.parsePlaybackStartResponse(context(), media(), wire, sourceWithSubtitles());
    QVERIFY(parsed.valid);
    const auto tracks = parsed.descriptor.subtitleTracks;
    QCOMPARE(tracks.size(), 3);
    for (const auto &track : tracks)
        QVERIFY(track.isExternal);
    QCOMPARE(tracks.at(0).sourceIndex, 5);
    QCOMPARE(tracks.at(1).sourceIndex, 6);
    QCOMPARE(tracks.at(2).sourceIndex, -1);
    QCOMPARE(tracks.at(2).language, QStringLiteral("ita"));
    QCOMPARE(parsed.descriptor.selectedSubtitleTrackId, QStringLiteral("1"));
}

void SiloPlaybackProviderTest::evictsOldestSessionInsteadOfRefusingStarts()
{
    SiloPlaybackProvider provider;
    QVERIFY(provider.acceptCapabilities(context(), capability()));
    for (int i = 0; i < 70; ++i)
    {
        auto wire = decision();
        const QString id = QStringLiteral("session-%1").arg(i);
        auto plan = wire.value("playback_plan").toObject();
        plan["session_id"] = id;
        wire["session_id"] = id;
        wire["playback_plan"] = plan;
        QVERIFY(provider.parsePlaybackStartResponse(context(), media(), wire, source()).valid);
    }
    QVERIFY(provider.createPlaybackStartRequest(context(), media(), source(), 0, -1, 0).isValid());
    PlaybackReport report;
    report.media = media();
    report.positionMs = 1000;
    report.playbackSessionId = QStringLiteral("session-0");
    QVERIFY(!provider.createReportRequest(report).isValid());
    report.playbackSessionId = QStringLiteral("session-69");
    QVERIFY(provider.createReportRequest(report).isValid());
}

void SiloPlaybackProviderTest::classifiesLostSessionsAndSurfacesTerminals()
{
    SiloPlaybackProvider provider;
    QVERIFY(provider.queuedSessionsExpire());
    PlaybackReport report;
    report.event = PlaybackReportEvent::Progress;
    QCOMPARE(provider.classifyReportFailure(report, 404), PlaybackSessionLoss::Expired);
    QCOMPARE(provider.classifyReportFailure(report, 410), PlaybackSessionLoss::Ended);
    QCOMPARE(provider.classifyReportFailure(report, 500), PlaybackSessionLoss::None);
    report.event = PlaybackReportEvent::Pause;
    QCOMPARE(provider.classifyReportFailure(report, 410), PlaybackSessionLoss::Ended);
    report.event = PlaybackReportEvent::Stop;
    QCOMPARE(provider.classifyReportFailure(report, 404), PlaybackSessionLoss::None);

    QVERIFY(provider.acceptCapabilities(context(), capability()));
    const QJsonObject terminal { { "protocol_version", 3 }, { "outcome", "terminal" },
        { "terminal",
            QJsonObject { { "reason", "subtitle_codec_unsupported" },
                { "message", "Subtitle format arib_caption has no route." },
                { "retryable", false } } } };
    const auto refused = provider.parsePlaybackStartResponse(context(), media(), terminal, source());
    QVERIFY(!refused.valid);
    QCOMPARE(refused.error, QStringLiteral("Subtitle format arib_caption has no route."));
}

void SiloPlaybackProviderTest::negotiatesHlsAfterTheOriginal()
{
    SiloPlaybackProvider provider;
    QVERIFY(provider.acceptCapabilities(context(), capability()));
    auto body = provider.createPlaybackStartRequest(context(), media(), source(), 0, -1, 0).body;
    const auto deliveries
        = body.value("client_playback_context").toObject().value("deliveries").toObject();
    QCOMPARE(deliveries.keys(), QStringList({ "hls", "original_http" }));
    const auto hls = deliveries.value("hls").toObject();
    QVERIFY(hls.value("enabled").toBool());
    QVERIFY(hls.value("supported_on_device").toBool());
    QVERIFY(hls.value("hdr_details").toObject().value("hdr10").toBool());
    QVERIFY(!hls.value("subtitles").toObject().value("font_attachments").toBool(true));
    QVERIFY(!deliveries.contains("progressive"));
    QVERIFY(!body.value("client_features").toArray().contains(QStringLiteral("hls_subtitle_renditions_v1")));

    // Renditions are requested only from a server that offers them.
    auto caps = capability();
    caps["features"] = QJsonArray { "playback_plan_v3", "hls_subtitle_renditions_v1",
        "hls_remux_subtitle_renditions_v1" };
    QVERIFY(provider.acceptCapabilities(context(), caps));
    body = provider.createPlaybackStartRequest(context(), media(), source(), 0, -1, 0).body;
    const auto features = body.value("client_features").toArray();
    QVERIFY(features.contains(QStringLiteral("hls_subtitle_renditions_v1")));
    QVERIFY(features.contains(QStringLiteral("hls_remux_subtitle_renditions_v1")));
}

void SiloPlaybackProviderTest::acceptsTranscodeWithSubtitleRendition()
{
    SiloPlaybackProvider provider;
    QVERIFY(provider.acceptCapabilities(context(), fixtureCapability()));
    QVariantMap watch = source();
    watch["mediaStreams"] = QVariantList { stream("Video", 0, {}), stream("Audio", 0, "eng"),
        stream("Subtitle", 3, "eng") };
    const auto parsed = provider.parsePlaybackStartResponse(
        context(), media(), fixture("hls-subtitle-rendition-decision_response.json"), watch);
    QVERIFY2(parsed.valid, qPrintable(parsed.error));
    const auto &descriptor = parsed.descriptor;
    QCOMPARE(descriptor.stream.method, Bloom::PlaybackMethod::Transcode);
    QCOMPARE(descriptor.stream.url.toEncoded(),
        QByteArrayLiteral("https://silo.example.test/playback/transcode/"
                          "11111111-1111-4111-8111-111111111111/master.m3u8?token=account-token"));
    // Long playlists carry their query through EXT-X-DEFINE, which FFmpeg
    // does not expand; mpv sends the bearer with every playlist and segment.
    QCOMPARE(descriptor.stream.headers.value("Authorization").toString(),
        QStringLiteral("Bearer account-token"));
    QCOMPARE(descriptor.mediaVersionId, QStringLiteral("42"));
    QCOMPARE(descriptor.startPositionMs, 12500);
    QCOMPARE(descriptor.stream.timelineOffsetMs, 0);
    QVERIFY(!descriptor.stream.seekRequiresReanchor);
    QVERIFY(!descriptor.stream.carriesSourceTracks);
    QCOMPARE(descriptor.stream.deliveredAudioTracks, QList<int>({ 0 }));
    QVERIFY(descriptor.stream.pinsAudioTrack);
    QCOMPARE(descriptor.stream.pinnedAudioTrackId, QStringLiteral("0"));
    // The embedded track rides in the HLS stream as its only subtitle.
    QCOMPARE(descriptor.stream.deliveredSubtitleTracks, QList<int>({ 3 }));
    QCOMPARE(descriptor.subtitleTracks.size(), 1);
    QVERIFY(!descriptor.subtitleTracks.first().isExternal);
    QVERIFY(!descriptor.subtitleTracks.first().requiresReplan);
    QCOMPARE(descriptor.subtitleTracks.first().sourceIndex, 3);
    QCOMPARE(descriptor.selectedSubtitleTrackId, QStringLiteral("0"));

    const auto plain = provider.parsePlaybackStartResponse(
        context(), media(), fixture("learned-limit-decision_response.json"), watch);
    QVERIFY2(plain.valid, qPrintable(plain.error));
    QVERIFY(plain.descriptor.stream.deliveredSubtitleTracks.isEmpty());
    QVERIFY(plain.descriptor.selectedSubtitleTrackId.isEmpty());
}

void SiloPlaybackProviderTest::anchorsRemuxTimelineAndRoutesSubtitles()
{
    SiloPlaybackProvider provider;
    auto wire = fixture("decision_response.json");
    auto plan = wire.value("playback_plan").toObject();
    plan["delivery"] = "server_remux_hls";
    auto stream = plan.value("stream").toObject();
    stream["url"] = "/playback/transcode/11111111-1111-4111-8111-111111111111/master.m3u8";
    stream["protocol"] = "hls";
    plan["stream"] = stream;
    // A copy remux resumed at 312.5 s starts at the preceding keyframe, 300 s.
    plan["timeline"] = QJsonObject { { "source_start_seconds", 312.5 },
        { "stream_origin_seconds", 300 }, { "player_start_seconds", 12.5 },
        { "timeline_offset_seconds", 300 }, { "seek_window_start_seconds", 300 },
        { "can_seek_anywhere", false }, { "seek_restoration", "source_position" } };
    wire["playback_plan"] = plan;

    auto caps = capability();
    caps["features"] = QJsonArray { "playback_plan_v3" };
    QVERIFY(provider.acceptCapabilities(context(), caps));
    const auto unanchored
        = provider.parsePlaybackStartResponse(context(), media(), wire, fixtureSource());
    QVERIFY(!unanchored.valid);
    QVERIFY(unanchored.error.contains(QStringLiteral("server_remux_hls")));

    QVERIFY(provider.acceptCapabilities(context(), fixtureCapability()));
    const auto parsed
        = provider.parsePlaybackStartResponse(context(), media(), wire, fixtureSource());
    QVERIFY2(parsed.valid, qPrintable(parsed.error));
    const auto &descriptor = parsed.descriptor;
    QCOMPARE(descriptor.stream.method, Bloom::PlaybackMethod::DirectStream);
    QCOMPARE(descriptor.stream.timelineOffsetMs, 300000);
    QVERIFY(descriptor.stream.seekRequiresReanchor);
    QCOMPARE(descriptor.startPositionMs, 312500);
    QCOMPARE(descriptor.durationMs, 7200000);
    // External and downloaded sidecars play locally; embedded tracks, which
    // the stream lacks, are asked for; nothing is selected under "off".
    QStringList tracks;
    for (const auto &track : descriptor.subtitleTracks)
        tracks.append(QStringLiteral("%1:%2:%3:%4")
                          .arg(track.trackId)
                          .arg(track.sourceIndex)
                          .arg(int(track.isExternal))
                          .arg(int(track.requiresReplan)));
    QCOMPARE(tracks,
        QStringList({ "0:6:1:0", "1:3:0:1", "2:4:0:1", "3:5:0:1", "4:-1:1:0" }));
    QVERIFY(descriptor.selectedSubtitleTrackId.isEmpty());

    // Burn-in pins the track; any other subtitle needs a new stream.
    auto subtitle = plan.value("subtitle").toObject();
    subtitle["mode"] = "burn_in";
    subtitle["track_id"] = "file:42:subtitle:3";
    plan["subtitle"] = subtitle;
    plan["selected_tracks"] = QJsonObject { { "audio", QJsonObject { { "id", "file:42:audio:0" },
                                                        { "index", 0 } } },
        { "subtitle", QJsonObject { { "id", "file:42:subtitle:3" }, { "index", 3 } } } };
    wire["playback_plan"] = plan;
    const auto burned
        = provider.parsePlaybackStartResponse(context(), media(), wire, fixtureSource());
    QVERIFY2(burned.valid, qPrintable(burned.error));
    QVERIFY(burned.descriptor.stream.subtitleBurnedIn);
    QVERIFY(burned.descriptor.stream.pinsSubtitleTrack);
    QCOMPARE(burned.descriptor.stream.pinnedSubtitleTrackId, QStringLiteral("5"));
    QCOMPARE(burned.descriptor.selectedSubtitleTrackId, QStringLiteral("3"));

    // An extracted embedded track arrives as the plan's sidecar artifact.
    subtitle["mode"] = "render";
    subtitle["track_id"] = "file:42:subtitle:1";
    subtitle["artifact"] = QJsonObject { { "url", "/stream/session/subtitles/1.ass?file_id=42" },
        { "mime_type", "text/x-ssa" }, { "format", "ass" }, { "timing_origin_seconds", 0 } };
    plan["subtitle"] = subtitle;
    wire["playback_plan"] = plan;
    const auto extracted
        = provider.parsePlaybackStartResponse(context(), media(), wire, fixtureSource());
    QVERIFY2(extracted.valid, qPrintable(extracted.error));
    const auto selected = extracted.descriptor.subtitleTracks.at(1);
    QVERIFY(selected.isExternal);
    QVERIFY(!selected.requiresReplan);
    QCOMPARE(selected.sourceIndex, 3);
    QVERIFY(selected.externalUrl.toString().contains(QStringLiteral("/subtitles/1.ass")));
    QVERIFY(!extracted.descriptor.stream.subtitleBurnedIn);

    // Bloom never asks for progressive remux, so it never plays one.
    plan["delivery"] = "server_remux_progressive";
    stream["protocol"] = "http_progressive";
    plan["stream"] = stream;
    wire["playback_plan"] = plan;
    const auto progressive
        = provider.parsePlaybackStartResponse(context(), media(), wire, fixtureSource());
    QVERIFY(!progressive.valid);
    QVERIFY(progressive.error.contains(QStringLiteral("server_remux_progressive")));
}

void SiloPlaybackProviderTest::replansSeeksAndTrackChanges()
{
    SiloPlaybackProvider provider;
    QVERIFY(provider.acceptCapabilities(context(), fixtureCapability()));
    auto wire = fixture("decision_response.json");
    auto plan = wire.value("playback_plan").toObject();
    plan["selected_tracks"] = QJsonObject {
        { "audio", QJsonObject { { "id", "file:42:audio:0" }, { "index", 0 } } },
        { "subtitle", QJsonObject { { "id", "file:42:subtitle:0" }, { "index", 0 } } } };
    wire["playback_plan"] = plan;
    QVERIFY(provider.parsePlaybackStartResponse(context(), media(), wire, fixtureSource()).valid);
    const QString session = QStringLiteral("11111111-1111-4111-8111-111111111111");

    PlaybackReplan seek;
    seek.operation = PlaybackReplan::Operation::SeekReanchor;
    seek.positionMs = 1234500;
    auto request = provider.createReplanRequest(context(), session, seek);
    QVERIFY(request.isValid());
    QCOMPARE(request.body.value("operation").toString(), QStringLiteral("seek_reanchor"));
    QCOMPARE(request.body.value("position_seconds").toDouble(), 1234.5);
    QCOMPARE(request.body.value("failed_plan_id"), plan.value("plan_id"));
    // A reanchor keeps the server's tracks.
    QCOMPARE(request.body.value("selected_tracks").toObject(),
        plan.value("selected_tracks").toObject());

    PlaybackReplan change;
    change.subtitleTrackIndex = 4; // embedded PGS, combined ordinal 2
    change.positionMs = 60000;
    request = provider.createReplanRequest(context(), session, change);
    QCOMPARE(request.body.value("operation").toString(), QStringLiteral("track_change"));
    const auto selected = request.body.value("selected_tracks").toObject();
    QCOMPARE(selected.value("audio").toObject().value("index").toInt(), 0);
    QCOMPARE(selected.value("subtitle").toObject(),
        QJsonObject({ { "id", "file:42:subtitle:2" }, { "index", 2 } }));
    change.subtitleTrackIndex = -1000;
    change.subtitleTrackId = QStringLiteral("4"); // downloaded, absent from the source
    request = provider.createReplanRequest(context(), session, change);
    QCOMPARE(request.body.value("selected_tracks").toObject().value("subtitle").toObject(),
        QJsonObject({ { "id", "file:42:subtitle:4" }, { "index", 4 } }));
    change.subtitleTrackId.clear();
    change.subtitleTrackIndex = -1;
    request = provider.createReplanRequest(context(), session, change);
    QVERIFY(!request.body.value("selected_tracks").toObject().contains("subtitle"));

    // The replacement plan becomes the one the next replan replaces.
    auto replacement = fixture("hls-subtitle-rendition-decision_response.json");
    auto next = replacement.value("playback_plan").toObject();
    next["plan_id"] = "plan:replacement";
    replacement["playback_plan"] = next;
    const auto parsed = provider.parseReplanResponse(context(), replacement);
    QVERIFY2(parsed.valid, qPrintable(parsed.error));
    QCOMPARE(parsed.descriptor.stream.method, Bloom::PlaybackMethod::Transcode);
    request = provider.createReplanRequest(context(), session, seek);
    QCOMPARE(request.body.value("failed_plan_id").toString(), QStringLiteral("plan:replacement"));

    const QJsonObject terminal { { "protocol_version", 3 }, { "outcome", "adaptation_unavailable" },
        { "session_id", session },
        { "terminal", QJsonObject { { "reason", "adaptation_exhausted" },
                          { "message", "No route remains." }, { "retryable", false } } } };
    const auto refused = provider.parseReplanResponse(context(), terminal);
    QVERIFY(!refused.valid);
    QCOMPARE(refused.error, QStringLiteral("No route remains."));
}

QTEST_GUILESS_MAIN(SiloPlaybackProviderTest)
#include "SiloPlaybackProviderTest.moc"
