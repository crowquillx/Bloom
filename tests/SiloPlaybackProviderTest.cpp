#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

#include "providers/silo/SiloPlaybackProvider.h"

class SiloPlaybackProviderTest : public QObject
{
    Q_OBJECT

private slots:
    void legacyStartRequestUsesEnvelopeAndCapabilities();
    void startRequestAcceptsAlternateFileIdentifiers();
    void parsesDirectRemuxAndHlsResponses();
    void rejectsMalformedStartResponses();
    void resolvesAudioSwitchUrlsAndRejectsMalformedResponses();
    void createsAudioSwitchRequestWithNativeRouteAndBody();
    void createsProgressAndStopReports();
    void supportsSubtitleUrlObjectsAndSyntheticIndices();
    void validatesHttpPlaybackUrlsAndDescriptorConstruction();
    void omitsEmptyProfileAndPreservesRecoveryAudio();
};

namespace {
PlaybackProviderContext context()
{
    PlaybackProviderContext result;
    result.serverUrl = QUrl(QStringLiteral("https://silo.example.test:8443/base?ignored=1"));
    result.profileId = QStringLiteral("profile-7");
    return result;
}

Bloom::MediaRef media()
{
    return {QStringLiteral("connection-1"), QStringLiteral("item-42")};
}

QJsonObject playbackResponse(const QString &method, const QString &url)
{
    return {
        {QStringLiteral("session_id"), QStringLiteral("session-42")},
        {QStringLiteral("media_file_id"), QStringLiteral("file-42")},
        {QStringLiteral("play_method"), method},
        {QStringLiteral("position"), 12.345},
        {QStringLiteral("stream_url"), url},
        {QStringLiteral("playback_info"), QJsonObject{
            {QStringLiteral("duration"), 123.456},
            {QStringLiteral("start_position"), 12.345},
            {QStringLiteral("audio_tracks"), QJsonArray{
                QJsonObject{{QStringLiteral("index"), 2},
                             {QStringLiteral("language"), QStringLiteral("eng")},
                             {QStringLiteral("codec"), QStringLiteral("aac")},
                             {QStringLiteral("title"), QStringLiteral("English")},
                             {QStringLiteral("default"), true}},
            }},
            {QStringLiteral("subtitle_tracks"), QJsonArray{
                QJsonObject{{QStringLiteral("index"), 3},
                             {QStringLiteral("language"), QStringLiteral("eng")},
                             {QStringLiteral("title"), QStringLiteral("English CC")},
                             {QStringLiteral("hearing_impaired"), true}},
            }},
        }},
    };
}

void verifyCapabilities(const QJsonObject &capabilities)
{
    QVERIFY(capabilities.value(QStringLiteral("codecs_video")).toArray()
                .contains(QJsonValue(QStringLiteral("h264"))));
    QVERIFY(capabilities.value(QStringLiteral("codecs_audio")).toArray()
                .contains(QJsonValue(QStringLiteral("aac"))));
    QVERIFY(capabilities.value(QStringLiteral("containers")).toArray()
                .contains(QJsonValue(QStringLiteral("mkv"))));
    QCOMPARE(capabilities.value(QStringLiteral("max_resolution")).toString(), QStringLiteral("8k"));
    QVERIFY(capabilities.value(QStringLiteral("hdr")).isBool());
    QVERIFY(capabilities.value(QStringLiteral("hdr")).toBool());
    const QJsonObject passthrough = capabilities.value(QStringLiteral("audio_passthrough")).toObject();
    QVERIFY(passthrough.value(QStringLiteral("passthrough_codecs")).isArray());
    QVERIFY(!passthrough.value(QStringLiteral("passthrough_codecs")).toArray().isEmpty());
    QVERIFY(passthrough.value(QStringLiteral("spatializer_enabled")).isBool());
    QVERIFY(passthrough.value(QStringLiteral("max_channels")).isDouble());
    QVERIFY(capabilities.value(QStringLiteral("supports_bitmap_subtitle_burn_in")).toBool());
}
} // namespace

void SiloPlaybackProviderTest::legacyStartRequestUsesEnvelopeAndCapabilities()
{
    SiloPlaybackProvider provider;
    QVariantMap source{{QStringLiteral("fileId"), QStringLiteral("99")}};
    const PlaybackStartRequest request = provider.createPlaybackStartRequest(
        context(), media(), source, 2, 5, 2501);

    QVERIFY(request.isValid());
    QCOMPARE(request.method, QStringLiteral("POST"));
    QCOMPARE(request.endpoint, QStringLiteral("/api/v1/playback/start"));
    QCOMPARE(request.body.value(QStringLiteral("file_id")).toInteger(), qint64(99));
    QCOMPARE(request.body.value(QStringLiteral("profile_id")).toString(), QStringLiteral("profile-7"));
    QCOMPARE(request.body.value(QStringLiteral("audio_track_index")).toInteger(), qint64(2));
    QCOMPARE(request.body.value(QStringLiteral("start_position")).toDouble(), 2.501);
    QVERIFY(!request.body.contains(QStringLiteral("subtitle_track_index")));
    QVERIFY(!request.body.contains(QStringLiteral("capabilities")));
    verifyCapabilities(request.body);
}

void SiloPlaybackProviderTest::startRequestAcceptsAlternateFileIdentifiers()
{
    SiloPlaybackProvider provider;

    const QVariantMap idSource{{QStringLiteral("id"), QStringLiteral("17")}};
    const PlaybackStartRequest idRequest = provider.createPlaybackStartRequest(
        context(), media(), idSource, -1, -1, 0);
    QVERIFY(idRequest.isValid());
    QCOMPARE(idRequest.body.value(QStringLiteral("file_id")).toInteger(), qint64(17));

    const QVariantMap versionSource{{QStringLiteral("mediaVersionId"), QStringLiteral("7")}};
    const PlaybackStartRequest versionRequest = provider.createPlaybackStartRequest(
        context(), media(), versionSource, -1, -1, 1000);
    QVERIFY(versionRequest.isValid());
    QCOMPARE(versionRequest.body.value(QStringLiteral("file_id")).toInteger(), qint64(7));

    const PlaybackStartRequest missing = provider.createPlaybackStartRequest(
        context(), media(), {}, -1, -1, 0);
    QVERIFY(!missing.isValid());
    QVERIFY(missing.endpoint.isEmpty());
}

void SiloPlaybackProviderTest::parsesDirectRemuxAndHlsResponses()
{
    SiloPlaybackProvider provider;
    const QString signedAbsolute = QStringLiteral(
        "https://cdn.example.test/movie.mkv?token=abc%2F%2B%3D&sig=a%26b");
    const auto direct = provider.parsePlaybackStartResponse(
        context(), media(), playbackResponse(QStringLiteral("direct"), signedAbsolute));
    QVERIFY(direct.valid);
    QCOMPARE(direct.descriptor.stream.method, Bloom::PlaybackMethod::DirectPlay);
    QCOMPARE(direct.descriptor.stream.url.toString(QUrl::FullyEncoded), signedAbsolute);
    QCOMPARE(direct.descriptor.mediaVersionId, QStringLiteral("file-42"));
    QCOMPARE(direct.descriptor.playbackSessionId, QStringLiteral("session-42"));
    QCOMPARE(direct.descriptor.durationMs, qint64(123456));
    QCOMPARE(direct.descriptor.startPositionMs, qint64(12345));
    QCOMPARE(direct.descriptor.audioTracks.size(), 1);
    QCOMPARE(direct.descriptor.audioTracks.first().trackId, QStringLiteral("2"));
    QCOMPARE(direct.descriptor.selectedAudioTrackId, QStringLiteral("2"));
    QVERIFY(!direct.descriptor.stream.pinsAudioTrack);
    QVERIFY(direct.descriptor.stream.pinnedAudioTrackId.isEmpty());
    QCOMPARE(direct.descriptor.subtitleTracks.size(), 1);
    QCOMPARE(direct.descriptor.subtitleTracks.first().displayTitle, QStringLiteral("English CC"));
    QVERIFY(direct.descriptor.reporting.progress);
    QVERIFY(direct.descriptor.reporting.pause);
    QVERIFY(!direct.descriptor.reporting.start);

    const auto remux = provider.parsePlaybackStartResponse(
        context(), media(), playbackResponse(
            QStringLiteral("remux"),
            QStringLiteral("/media/remux.mkv?token=abc%2F%2B%3D&sig=a%26b")));
    QVERIFY(remux.valid);
    QCOMPARE(remux.descriptor.stream.method, Bloom::PlaybackMethod::DirectStream);
    QCOMPARE(remux.descriptor.stream.url.toString(QUrl::FullyEncoded),
             QStringLiteral("https://silo.example.test:8443/media/remux.mkv?token=abc%2F%2B%3D&sig=a%26b"));

    QJsonObject hls = playbackResponse(QStringLiteral("hls"), QStringLiteral("unused"));
    hls.remove(QStringLiteral("stream_url"));
    hls.insert(QStringLiteral("hls_url"),
               QStringLiteral("/hls/session.m3u8?sig=one%2Ftwo%26three"));
    const auto transcode = provider.parsePlaybackStartResponse(context(), media(), hls);
    QVERIFY(transcode.valid);
    QCOMPARE(transcode.descriptor.stream.method, Bloom::PlaybackMethod::Transcode);
    QCOMPARE(transcode.descriptor.stream.url.toString(QUrl::FullyEncoded),
             QStringLiteral("https://silo.example.test:8443/hls/session.m3u8?sig=one%2Ftwo%26three"));

    QJsonObject external = playbackResponse(QStringLiteral("direct"), signedAbsolute);
    external.insert(QStringLiteral("subtitle_urls"), QJsonArray{
        QJsonObject{{QStringLiteral("index"), 9},
                     {QStringLiteral("url"), QStringLiteral("/subs/movie.fr.vtt")},
                     {QStringLiteral("language"), QStringLiteral("fra")}},
        QStringLiteral("https://cdn.example.test/movie.en.vtt?sig=x%2Fy")});
    const auto withExternal = provider.parsePlaybackStartResponse(context(), media(), external);
    QVERIFY(withExternal.valid);
    QCOMPARE(withExternal.descriptor.subtitleTracks.size(), 3);
    const Bloom::PlaybackTrack &externalObject = withExternal.descriptor.subtitleTracks.at(1);
    QVERIFY(externalObject.isExternal);
    QCOMPARE(externalObject.trackId, QStringLiteral("9"));
    QCOMPARE(externalObject.language, QStringLiteral("fra"));
    const Bloom::PlaybackTrack &externalString = withExternal.descriptor.subtitleTracks.at(2);
    QVERIFY(externalString.isExternal);
    QCOMPARE(externalString.trackId, QStringLiteral("-1000"));
}

void SiloPlaybackProviderTest::rejectsMalformedStartResponses()
{
    SiloPlaybackProvider provider;
    const QJsonObject valid = playbackResponse(QStringLiteral("direct"), QStringLiteral("/stream.mkv"));

    const QList<QString> missingFields{QStringLiteral("session_id"), QStringLiteral("media_file_id"),
                                       QStringLiteral("stream_url"), QStringLiteral("play_method")};
    for (const QString &field : missingFields) {
        QJsonObject malformed = valid;
        malformed.remove(field);
        const auto result = provider.parsePlaybackStartResponse(context(), media(), malformed);
        QVERIFY2(!result.valid, qPrintable(QStringLiteral("field remained optional: %1").arg(field)));
        QVERIFY(!result.descriptor.isValid());
        QVERIFY(!result.error.isEmpty());
    }

    QJsonObject unknownMethod = valid;
    unknownMethod.insert(QStringLiteral("play_method"), QStringLiteral("unsupported"));
    QVERIFY(!provider.parsePlaybackStartResponse(context(), media(), unknownMethod).valid);

    QJsonObject noServerForRelative = valid;
    noServerForRelative.insert(QStringLiteral("stream_url"), QStringLiteral("/stream.mkv"));
    PlaybackProviderContext invalidContext = context();
    invalidContext.serverUrl = QUrl();
    QVERIFY(!provider.parsePlaybackStartResponse(invalidContext, media(), noServerForRelative).valid);
}

void SiloPlaybackProviderTest::resolvesAudioSwitchUrlsAndRejectsMalformedResponses()
{
    SiloPlaybackProvider provider;
    const QString signedRelative = QStringLiteral("/reload.m3u8?token=a%2Fb&sig=x%26y");
    const auto relative = provider.parseAudioSwitchResponse(
        context(), QJsonObject{{QStringLiteral("reload_url"), signedRelative}});
    QVERIFY(relative.valid);
    QCOMPARE(relative.reloadUrl.toString(QUrl::FullyEncoded),
             QStringLiteral("https://silo.example.test:8443/reload.m3u8?token=a%2Fb&sig=x%26y"));

    const QString signedAbsolute = QStringLiteral("https://cdn.example.test/reload.m3u8?sig=x%2Fy");
    const auto absolute = provider.parseAudioSwitchResponse(
        context(), QJsonObject{{QStringLiteral("stream_url"), signedAbsolute}});
    QVERIFY(absolute.valid);
    QCOMPARE(absolute.reloadUrl.toString(QUrl::FullyEncoded), signedAbsolute);
    QVERIFY(!provider.parseAudioSwitchResponse(
                  context(), QJsonObject{{QStringLiteral("reload_url"),
                                          QStringLiteral("file:///tmp/reload.m3u8")}})
                  .valid);
    QVERIFY(!provider.parseAudioSwitchResponse(context(), {}).valid);
    PlaybackProviderContext invalidContext = context();
    invalidContext.serverUrl = QUrl();
    QVERIFY(!provider.parseAudioSwitchResponse(
                  invalidContext, QJsonObject{{QStringLiteral("reload_url"), signedRelative}})
                  .valid);
}

void SiloPlaybackProviderTest::createsAudioSwitchRequestWithNativeRouteAndBody()
{
    SiloPlaybackProvider provider;
    const PlaybackAudioSwitchRequest request = provider.createAudioSwitchRequest(
        context(), QStringLiteral("session/one?x=2"), 7, 12345);
    QVERIFY(request.isValid());
    QCOMPARE(request.method, QStringLiteral("PATCH"));
    QCOMPARE(request.endpoint, QStringLiteral("/api/v1/playback/session%2Fone%3Fx%3D2/audio"));
    QCOMPARE(request.body.value(QStringLiteral("audio_track_index")).toInteger(), qint64(7));
    QCOMPARE(request.body.value(QStringLiteral("position")).toDouble(), 12.345);

    QVERIFY(!provider.createAudioSwitchRequest(context(), {}, 7, 0).isValid());
    QVERIFY(!provider.createAudioSwitchRequest(context(), QStringLiteral("session"), -1, 0).isValid());
}

void SiloPlaybackProviderTest::createsProgressAndStopReports()
{
    SiloPlaybackProvider provider;
    PlaybackReport progress;
    progress.event = PlaybackReportEvent::Progress;
    progress.playbackSessionId = QStringLiteral("session/one?x=2");
    progress.positionMs = 3456;
    const PlaybackReportRequest progressRequest = provider.createReportRequest(progress);
    QVERIFY(progressRequest.isValid());
    QCOMPARE(progressRequest.method, QStringLiteral("POST"));
    QCOMPARE(progressRequest.endpoint,
             QStringLiteral("/api/v1/playback/session%2Fone%3Fx%3D2/progress"));
    QCOMPARE(progressRequest.body.value(QStringLiteral("seconds")).toDouble(), 3.456);
    QVERIFY(!progressRequest.body.contains(QStringLiteral("position")));
    QCOMPARE(progressRequest.body.value(QStringLiteral("is_paused")).toBool(), false);
    QVERIFY(progressRequest.deferSessionExpiry);

    progress.event = PlaybackReportEvent::Pause;
    progress.isPaused = false;
    const PlaybackReportRequest pauseRequest = provider.createReportRequest(progress);
    QCOMPARE(pauseRequest.body.value(QStringLiteral("is_paused")).toBool(), true);
    QCOMPARE(pauseRequest.body.value(QStringLiteral("seconds")).toDouble(), 3.456);
    QVERIFY(!pauseRequest.body.contains(QStringLiteral("position")));

    progress.event = PlaybackReportEvent::Resume;
    const PlaybackReportRequest resumeRequest = provider.createReportRequest(progress);
    QCOMPARE(resumeRequest.body.value(QStringLiteral("seconds")).toDouble(), 3.456);
    QCOMPARE(resumeRequest.body.value(QStringLiteral("is_paused")).toBool(), false);
    QVERIFY(!resumeRequest.body.contains(QStringLiteral("position")));

    progress.event = PlaybackReportEvent::Start;
    QVERIFY(!provider.createReportRequest(progress).isValid());

    progress.event = PlaybackReportEvent::Stop;
    const PlaybackReportRequest stopRequest = provider.createReportRequest(progress);
    QVERIFY(stopRequest.isValid());
    QCOMPARE(stopRequest.method, QStringLiteral("DELETE"));
    QCOMPARE(stopRequest.endpoint, QStringLiteral("/api/v1/playback/session%2Fone%3Fx%3D2"));
    QVERIFY(stopRequest.body.isEmpty());
    QVERIFY(!stopRequest.deferSessionExpiry);
    QVERIFY(!provider.createReportRequest({}).isValid());
}

void SiloPlaybackProviderTest::supportsSubtitleUrlObjectsAndSyntheticIndices()
{
    SiloPlaybackProvider provider;
    QJsonObject arrayResponse = playbackResponse(QStringLiteral("direct"),
                                                 QStringLiteral("/stream.mkv"));
    arrayResponse.insert(QStringLiteral("subtitle_urls"), QJsonArray{
        QStringLiteral("/subs/without-index.vtt"),
        QJsonObject{{QStringLiteral("index"), 3},
                     {QStringLiteral("url"), QStringLiteral("/subs/explicit.vtt")}}});
    const auto arrayResult = provider.parsePlaybackStartResponse(context(), media(), arrayResponse);
    QVERIFY(arrayResult.valid);
    QCOMPARE(arrayResult.descriptor.subtitleTracks.size(), 2);
    QCOMPARE(arrayResult.descriptor.subtitleTracks.at(0).trackId, QStringLiteral("3"));
    QCOMPARE(arrayResult.descriptor.subtitleTracks.at(0).externalUrl.toString(),
             QStringLiteral("https://silo.example.test:8443/subs/explicit.vtt"));
    QCOMPARE(arrayResult.descriptor.subtitleTracks.at(1).trackId, QStringLiteral("-1000"));

    QJsonObject objectResponse = playbackResponse(QStringLiteral("direct"),
                                                   QStringLiteral("/stream.mkv"));
    objectResponse.insert(QStringLiteral("subtitle_urls"),
                          QJsonObject{{QStringLiteral("8"),
                                       QStringLiteral("/subs/object.vtt")}});
    const auto objectResult = provider.parsePlaybackStartResponse(context(), media(), objectResponse);
    QVERIFY(objectResult.valid);
    QCOMPARE(objectResult.descriptor.subtitleTracks.size(), 2);
    QCOMPARE(objectResult.descriptor.subtitleTracks.at(1).trackId, QStringLiteral("8"));
    QVERIFY(objectResult.descriptor.subtitleTracks.at(1).isExternal);
}

void SiloPlaybackProviderTest::validatesHttpPlaybackUrlsAndDescriptorConstruction()
{
    SiloPlaybackProvider provider;
    QJsonObject malicious = playbackResponse(QStringLiteral("direct"),
                                             QStringLiteral("file:///tmp/movie.mkv"));
    QVERIFY(!provider.parsePlaybackStartResponse(context(), media(), malicious).valid);

    QJsonObject maliciousSubtitle = playbackResponse(QStringLiteral("direct"),
                                                     QStringLiteral("/stream.mkv"));
    maliciousSubtitle.insert(QStringLiteral("subtitle_urls"),
                             QJsonArray{QStringLiteral("file:///tmp/subtitle.vtt")});
    const auto subtitleResult =
        provider.parsePlaybackStartResponse(context(), media(), maliciousSubtitle);
    QVERIFY(subtitleResult.valid);
    QCOMPARE(subtitleResult.descriptor.subtitleTracks.size(), 1);

    const QVariantList streams{
        QVariantMap{{QStringLiteral("index"), 2},
                     {QStringLiteral("type"), QStringLiteral("Audio")},
                     {QStringLiteral("language"), QStringLiteral("fra")}},
        QVariantMap{{QStringLiteral("index"), 1},
                     {QStringLiteral("type"), QStringLiteral("Audio")},
                     {QStringLiteral("language"), QStringLiteral("eng")},
                     {QStringLiteral("isDefault"), true}}};
    const QVariantMap source{{QStringLiteral("fileId"), QStringLiteral("99")},
                             {QStringLiteral("streamUrl"), QStringLiteral("/native.mkv")},
                             {QStringLiteral("mediaStreams"), streams}};
    const auto descriptor = provider.createDescriptor(context(), media(), source, -1, -1, 0);
    QVERIFY(descriptor.isValid());
    QCOMPARE(descriptor.stream.url.toString(QUrl::FullyEncoded),
             QStringLiteral("https://silo.example.test:8443/native.mkv"));
    QCOMPARE(descriptor.selectedAudioTrackId, QStringLiteral("1"));
    QVERIFY(!descriptor.stream.pinsAudioTrack);
    QVERIFY(descriptor.stream.pinnedAudioTrackId.isEmpty());

    const QVariantMap invalidSource{{QStringLiteral("fileId"), QStringLiteral("99")},
                                    {QStringLiteral("streamUrl"), QStringLiteral("javascript:alert(1)")}};
    QVERIFY(!provider.createDescriptor(context(), media(), invalidSource, -1, -1, 0).isValid());
}

void SiloPlaybackProviderTest::omitsEmptyProfileAndPreservesRecoveryAudio()
{
    SiloPlaybackProvider provider;
    PlaybackProviderContext noProfile = context();
    noProfile.profileId.clear();
    const QVariantMap source{{QStringLiteral("fileId"), QStringLiteral("99")}};
    const PlaybackStartRequest start =
        provider.createPlaybackStartRequest(noProfile, media(), source, -1, -1, 0);
    QVERIFY(start.isValid());
    QVERIFY(!start.body.contains(QStringLiteral("profile_id")));

    const PlaybackRecoveryRequest recovery =
        provider.createPlaybackRecoveryRequest(context(), media(), source, 7, 2501);
    QVERIFY(recovery.isValid());
    QCOMPARE(recovery.body.value(QStringLiteral("audio_track_index")).toInteger(), qint64(7));
    QCOMPARE(recovery.body.value(QStringLiteral("start_position")).toDouble(), 2.501);
}

QTEST_MAIN(SiloPlaybackProviderTest)
#include "SiloPlaybackProviderTest.moc"
