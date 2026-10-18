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
    void replansAudioWithDurableRequestIdentity();
    void sequencesReportsAndFinalizesStop();
    void isolatesAccountsAndConnections();
    void mapsTrickplayWithVersionAndExpiryGuards();
    void startBodyMatchesPinnedV2Schema();
    void mediaAuthorizationStaysOnItsOriginAndRejectedPlansStop();
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

void SiloPlaybackProviderTest::replansAudioWithDurableRequestIdentity()
{
    SiloPlaybackProvider provider;
    prime(provider);
    const auto request
        = provider.createAudioSwitchRequest(context(), QStringLiteral("session-42"), 2, 23456);
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
    QVERIFY(provider.parseAudioSwitchResponse(context(), decision()).valid);
    QVERIFY(!provider.createAudioSwitchRequest(context(), QStringLiteral("unknown-session"), 2, 0)
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
    QVERIFY(!provider.createAudioSwitchRequest(ctx, QStringLiteral("session-42"), 0, 0).isValid());
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
    verify(playback.value("deliveries").toObject().value("original_http").toObject(),
        schemas.value("DeliveryCapabilityV3").toObject());
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

QTEST_GUILESS_MAIN(SiloPlaybackProviderTest)
#include "SiloPlaybackProviderTest.moc"
