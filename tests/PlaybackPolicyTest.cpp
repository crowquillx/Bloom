#include <QtTest/QtTest>

#include "player/PlaybackPolicy.h"

namespace PlayerPolicy = Bloom::PlaybackPolicy;

namespace {

QVariantMap stream(const QString &type,
                   int index,
                   const QString &language = {},
                   bool isDefault = false,
                   bool isForced = false,
                   bool isHearingImpaired = false)
{
    return {
        {QStringLiteral("type"), type},
        {QStringLiteral("index"), index},
        {QStringLiteral("language"), language},
        {QStringLiteral("isDefault"), isDefault},
        {QStringLiteral("isForced"), isForced},
        {QStringLiteral("isHearingImpaired"), isHearingImpaired}
    };
}

QVariantMap mediaSource(const QString &id,
                        const QString &name,
                        const QString &path,
                        const QVariantList &streams = {})
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("path"), path},
        {QStringLiteral("mediaStreams"), streams}
    };
}

QVariantMap videoStream(const QString &range,
                        int dolbyVisionProfile = 0,
                        int compatibilityId = 0,
                        const QString &title = {})
{
    QVariantMap result = stream(QStringLiteral("Video"), 0);
    result.insert(QStringLiteral("videoRange"), range);
    result.insert(QStringLiteral("dolbyVisionProfile"), dolbyVisionProfile);
    result.insert(QStringLiteral("dolbyVisionBlSignalCompatibilityId"),
                  compatibilityId);
    result.insert(QStringLiteral("title"), title);
    return result;
}

} // namespace

class PlaybackPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void normalizesLanguageAliases_data();
    void normalizesLanguageAliases();
    void scoresLanguageStreamsDeterministically();
    void resolvesTrackSelectionPrecedence();
    void classifiesHdrAndDolbyVision();
    void evaluatesHdrOutputPolicy();
    void selectsVersionsByForcedIdAndAffinity();
    void filtersMultipartSourcesAndBuildsSubtitles();
    void evaluatesCompletionAndPrefetchThresholds();
    void validatesPrefetchedEpisodesAndContexts();
};

void PlaybackPolicyTest::normalizesLanguageAliases_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("English ISO-639-1") << QStringLiteral(" EN ")
                                        << QStringLiteral("eng");
    QTest::newRow("French terminology") << QStringLiteral("fra")
                                         << QStringLiteral("fre");
    QTest::newRow("German bibliographic") << QStringLiteral("ger")
                                           << QStringLiteral("ger");
    QTest::newRow("unknown normalized") << QStringLiteral(" X-Custom ")
                                         << QStringLiteral("x-custom");
    QTest::newRow("empty") << QString{} << QString{};
}

void PlaybackPolicyTest::normalizesLanguageAliases()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(PlayerPolicy::normalizeLanguageCode(input), expected);
}

void PlaybackPolicyTest::scoresLanguageStreamsDeterministically()
{
    const QVariantList audioStreams{
        stream(QStringLiteral("Audio"), 1, QStringLiteral("eng")),
        stream(QStringLiteral("Audio"), 2, QStringLiteral("en"), true),
        stream(QStringLiteral("Audio"), 3, QStringLiteral("jpn"))
    };
    QCOMPARE(PlayerPolicy::bestLanguageStreamIndex(
                 audioStreams, QStringLiteral("en"), false),
             2);

    const QVariantList subtitleStreams{
        stream(QStringLiteral("Subtitle"), 10, QStringLiteral("eng"),
               false, true),
        stream(QStringLiteral("Subtitle"), 11, QStringLiteral("en"),
               false, false, true),
        stream(QStringLiteral("Subtitle"), 12, QStringLiteral("eng")),
        stream(QStringLiteral("Subtitle"), 13, QStringLiteral("jpn"))
    };
    QCOMPARE(PlayerPolicy::bestLanguageStreamIndex(
                 subtitleStreams, QStringLiteral("eng"), true),
             12);
    QCOMPARE(PlayerPolicy::bestLanguageStreamIndex(
                 subtitleStreams, QStringLiteral("spa"), true),
             -1);
}

void PlaybackPolicyTest::resolvesTrackSelectionPrecedence()
{
    QVariantMap source = mediaSource(
        QStringLiteral("source"),
        QStringLiteral("Version"),
        QStringLiteral("/media/show/episode.mkv"),
        {
            stream(QStringLiteral("Audio"), 1, QStringLiteral("eng"), true),
            stream(QStringLiteral("Audio"), 2, QStringLiteral("jpn")),
            stream(QStringLiteral("Subtitle"), 5, QStringLiteral("eng"),
                   false, true),
            stream(QStringLiteral("Subtitle"), 6, QStringLiteral("jpn"), true),
            stream(QStringLiteral("Subtitle"), 7, QStringLiteral("eng"))
        });
    source.insert(QStringLiteral("defaultAudioStreamIndex"), 1);
    source.insert(QStringLiteral("defaultSubtitleStreamIndex"), 5);

    ScopedTrackPreferences preferences;
    preferences.audio.mode = TrackPreferenceMode::ExplicitStream;
    preferences.audio.streamIndex = 2;
    preferences.subtitle.mode = TrackPreferenceMode::ExplicitStream;
    preferences.subtitle.streamIndex = 6;

    auto resolved = PlayerPolicy::resolveTrackSelection(
        source,
        preferences,
        QStringLiteral("eng"),
        QStringLiteral("eng"),
        1,
        -1);
    QCOMPARE(resolved.audioIndex, 1);
    QCOMPARE(resolved.audioSource, QStringLiteral("override"));
    QCOMPARE(resolved.subtitleIndex, -1);
    QCOMPARE(resolved.subtitleSource, QStringLiteral("override-off"));

    resolved = PlayerPolicy::resolveTrackSelection(
        source,
        preferences,
        QStringLiteral("eng"),
        QStringLiteral("eng"));
    QCOMPARE(resolved.audioIndex, 2);
    QCOMPARE(resolved.audioSource, QStringLiteral("explicit"));
    QCOMPARE(resolved.subtitleIndex, 6);
    QCOMPARE(resolved.subtitleSource, QStringLiteral("explicit"));

    preferences = {};
    preferences.subtitle.mode = TrackPreferenceMode::Off;
    resolved = PlayerPolicy::resolveTrackSelection(
        source,
        preferences,
        QStringLiteral("jpn"),
        QStringLiteral("jellyfin-default"));
    QCOMPARE(resolved.audioIndex, 2);
    QCOMPARE(resolved.audioSource, QStringLiteral("global-language"));
    QCOMPARE(resolved.subtitleIndex, -1);
    QCOMPARE(resolved.subtitleSource, QStringLiteral("explicit-off"));

    preferences = {};
    resolved = PlayerPolicy::resolveTrackSelection(
        source,
        preferences,
        QStringLiteral("jellyfin-default"),
        QStringLiteral("forced"));
    QCOMPARE(resolved.audioIndex, 1);
    QCOMPARE(resolved.audioSource, QStringLiteral("jellyfin-default"));
    QCOMPARE(resolved.subtitleIndex, 5);
    QCOMPARE(resolved.subtitleSource, QStringLiteral("global-forced"));

    source.remove(QStringLiteral("defaultSubtitleStreamIndex"));
    source[QStringLiteral("mediaStreams")] = QVariantList{
        stream(QStringLiteral("Audio"), 8),
        stream(QStringLiteral("Subtitle"), 9, QStringLiteral("eng"))
    };
    resolved = PlayerPolicy::resolveTrackSelection(
        source,
        {},
        QStringLiteral("jellyfin-default"),
        QStringLiteral("forced"));
    QCOMPARE(resolved.audioIndex, 8);
    QCOMPARE(resolved.audioSource, QStringLiteral("fallback"));
    QCOMPARE(resolved.subtitleIndex, -1);
    QCOMPARE(resolved.subtitleSource, QStringLiteral("global-forced-off"));
}

void PlaybackPolicyTest::classifiesHdrAndDolbyVision()
{
    using enum PlayerPolicy::HdrContentKind;

    QCOMPARE(PlayerPolicy::classifyMediaSourceHdr(
                 mediaSource(QStringLiteral("sdr"), {}, {},
                             {videoStream(QStringLiteral("SDR"))})),
             Sdr);
    QCOMPARE(PlayerPolicy::classifyMediaSourceHdr(
                 mediaSource(QStringLiteral("hdr"), {}, {},
                             {videoStream(QStringLiteral("HDR10"))})),
             Hdr);
    QCOMPARE(PlayerPolicy::classifyMediaSourceHdr(
                 mediaSource(QStringLiteral("dv5"), {}, {},
                             {videoStream({}, 5)})),
             DolbyVisionUnsupported);
    QCOMPARE(PlayerPolicy::classifyMediaSourceHdr(
                 mediaSource(QStringLiteral("dv7"), {}, {},
                             {videoStream({}, 7)})),
             DolbyVisionCompatible);
    QCOMPARE(PlayerPolicy::classifyMediaSourceHdr(
                 mediaSource(QStringLiteral("dv-compatible"), {}, {},
                             {videoStream({}, 5, 1)})),
             DolbyVisionCompatible);
    QCOMPARE(PlayerPolicy::classifyMediaSourceHdr(
                 mediaSource(QStringLiteral("metadata"),
                             QStringLiteral("Dolby Vision HDR10"),
                             QStringLiteral("/media/episode.mkv"),
                             {videoStream({})})),
             DolbyVisionCompatible);

    QVERIFY(PlayerPolicy::isHdr(DolbyVisionUnsupported));
    QVERIFY(!PlayerPolicy::isHdr(Sdr));
    QVERIFY(PlayerPolicy::shouldToneMapToSdr(
        DolbyVisionUnsupported, QStringLiteral("tone-map")));
    QVERIFY(!PlayerPolicy::shouldToneMapToSdr(
        DolbyVisionUnsupported,
        QStringLiteral("experimental-direct-play")));
    QCOMPARE(PlayerPolicy::hdrContentKindName(DolbyVisionCompatible),
             QStringLiteral("dolby-vision-compatible"));
}

void PlaybackPolicyTest::evaluatesHdrOutputPolicy()
{
    auto policy = PlayerPolicy::evaluateHdrPlayback(
        false, false, true, QStringLiteral("match-content"));
    QVERIFY(!policy.toneMapToSdr);
    QVERIFY(!policy.outputHdr);
    QVERIFY(!policy.shouldToggleDisplayHdr);

    policy = PlayerPolicy::evaluateHdrPlayback(
        true, false, true, QStringLiteral("match-content"));
    QVERIFY(!policy.toneMapToSdr);
    QVERIFY(policy.outputHdr);
    QVERIFY(policy.shouldToggleDisplayHdr);

    policy = PlayerPolicy::evaluateHdrPlayback(
        true, false, false, QStringLiteral("match-content"));
    QVERIFY(policy.toneMapToSdr);
    QVERIFY(!policy.outputHdr);

    policy = PlayerPolicy::evaluateHdrPlayback(
        true, true, true, QStringLiteral("force-hdr-experimental"));
    QVERIFY(policy.toneMapToSdr);
    QVERIFY(!policy.outputHdr);
}

void PlaybackPolicyTest::selectsVersionsByForcedIdAndAffinity()
{
    QVariantMap first = mediaSource(QStringLiteral("first"),
                                    QStringLiteral("1080p"),
                                    QStringLiteral("/media/show/1080/ep.mkv"),
                                    {videoStream(QStringLiteral("SDR"))});
    QVariantMap sameParent = mediaSource(QStringLiteral("parent"),
                                         QStringLiteral("Other"),
                                         QStringLiteral("/media/show/4k/next.mkv"),
                                         {videoStream(QStringLiteral("HDR10"))});
    QVariantMap sameName = mediaSource(QStringLiteral("name"),
                                       QStringLiteral("1080P"),
                                       QStringLiteral("/elsewhere/next.mkv"),
                                       {videoStream(QStringLiteral("SDR"))});
    QVariantMap sameSignature = mediaSource(QStringLiteral("signature"),
                                            QStringLiteral("Different"),
                                            QStringLiteral("/third/next.mkv"),
                                            {videoStream(QStringLiteral("SDR"))});
    QVariantList signatureStreams = sameSignature.value(
        QStringLiteral("mediaStreams")).toList();
    QVariantMap signatureVideo = signatureStreams.first().toMap();
    signatureVideo.insert(QStringLiteral("codec"), QStringLiteral("av1"));
    signatureStreams[0] = signatureVideo;
    sameSignature[QStringLiteral("mediaStreams")] = signatureStreams;
    const QVariantList sources{first, sameParent, sameName, sameSignature};

    PlayerPolicy::VersionAffinity affinity{
        PlayerPolicy::mediaSourceParentPath(sameParent),
        QStringLiteral("1080p"),
        PlayerPolicy::mediaSourceSignature(first)
    };
    QCOMPARE(PlayerPolicy::selectMediaSource(
                 sources, QStringLiteral("signature"), true, affinity)
                 .value(QStringLiteral("id")).toString(),
             QStringLiteral("signature"));
    QCOMPARE(PlayerPolicy::selectMediaSource(
                 sources, {}, true, affinity)
                 .value(QStringLiteral("id")).toString(),
             QStringLiteral("parent"));

    affinity.parentPath.clear();
    affinity.name = QStringLiteral("Other");
    QCOMPARE(PlayerPolicy::selectMediaSource(sources, {}, true, affinity)
                 .value(QStringLiteral("id")).toString(),
             QStringLiteral("parent"));

    affinity.name.clear();
    affinity.signature = PlayerPolicy::mediaSourceSignature(sameSignature);
    QCOMPARE(PlayerPolicy::selectMediaSource(sources, {}, true, affinity)
                 .value(QStringLiteral("id")).toString(),
             QStringLiteral("signature"));

    QCOMPARE(PlayerPolicy::selectMediaSource(sources, {}, false, affinity)
                 .value(QStringLiteral("id")).toString(),
             QStringLiteral("first"));
    QVERIFY(PlayerPolicy::selectMediaSource({}, {}, true, affinity).isEmpty());
}

void PlaybackPolicyTest::filtersMultipartSourcesAndBuildsSubtitles()
{
    QVariantMap partTwo = mediaSource(QStringLiteral("part-two"), {}, {});
    partTwo.insert(QStringLiteral("presentationPartIndex"), 2);
    QVariantMap partOneA = mediaSource(QStringLiteral("part-one-a"), {}, {});
    partOneA.insert(QStringLiteral("presentationPartIndex"), 1);
    QVariantMap partOneB = mediaSource(QStringLiteral("part-one-b"), {}, {});
    partOneB.insert(QStringLiteral("presentationPartIndex"), 1);

    const QVariantList primary = PlayerPolicy::primaryPresentationSources(
        {partTwo, partOneA, partOneB});
    QCOMPARE(primary.size(), 2);
    QCOMPARE(primary.at(0).toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("part-one-a"));

    QVariantMap source = mediaSource(
        QStringLiteral("source"), {}, {},
        {QVariantMap{
            {QStringLiteral("type"), QStringLiteral("Video")},
            {QStringLiteral("width"), 3840},
            {QStringLiteral("height"), 2160},
            {QStringLiteral("videoRange"), QStringLiteral("HDR10")},
            {QStringLiteral("codec"), QStringLiteral("hevc")},
            {QStringLiteral("profile"), QStringLiteral("Main 10")}
        }});
    source.insert(QStringLiteral("container"), QStringLiteral("mkv"));
    source.insert(QStringLiteral("bitRate"), 25000000);
    QCOMPARE(PlayerPolicy::buildVersionSubtitle(source),
             QStringLiteral("3840x2160 • HDR10 • HEVC • Main 10 • MKV • 25.0 Mbps"));
}

void PlaybackPolicyTest::evaluatesCompletionAndPrefetchThresholds()
{
    PlayerPolicy::CompletionInput completion{
        false, QStringLiteral("episode"), QStringLiteral("series"),
        90.0, 100.0, 90
    };
    QVERIFY(PlayerPolicy::meetsCompletionThreshold(completion));
    completion.positionSeconds = 89.999;
    QVERIFY(!PlayerPolicy::meetsCompletionThreshold(completion));
    completion.positionSeconds = 100.0;
    completion.alreadyEvaluated = true;
    QVERIFY(!PlayerPolicy::meetsCompletionThreshold(completion));
    completion.alreadyEvaluated = false;
    completion.seriesId.clear();
    QVERIFY(!PlayerPolicy::meetsCompletionThreshold(completion));

    PlayerPolicy::PrefetchInput prefetch{
        false, true, QStringLiteral("series"), QStringLiteral("episode"),
        75.0, 100.0, 75.0
    };
    QVERIFY(PlayerPolicy::shouldPrefetchNextEpisode(prefetch));
    prefetch.playbackActive = false;
    QVERIFY(!PlayerPolicy::shouldPrefetchNextEpisode(prefetch));
    prefetch.playbackActive = true;
    prefetch.alreadyRequested = true;
    QVERIFY(!PlayerPolicy::shouldPrefetchNextEpisode(prefetch));
}

void PlaybackPolicyTest::validatesPrefetchedEpisodesAndContexts()
{
    PlayerPolicy::PrefetchedEpisodeInput input{
        true,
        QVariantMap{{QStringLiteral("itemId"), QStringLiteral("next")}},
        QStringLiteral("series"),
        QStringLiteral("current"),
        QStringLiteral("series"),
        QStringLiteral("current")
    };
    QVERIFY(PlayerPolicy::isUsablePrefetchedEpisode(input));
    input.episode[QStringLiteral("itemId")] = QStringLiteral("current");
    QVERIFY(!PlayerPolicy::isUsablePrefetchedEpisode(input));
    input.episode[QStringLiteral("itemId")] = QStringLiteral("next");
    input.prefetchedSeriesId = QStringLiteral("stale-series");
    QVERIFY(!PlayerPolicy::isUsablePrefetchedEpisode(input));

    QCOMPARE(PlayerPolicy::nextEpisodeRequestContext(
                 QStringLiteral("prefetch"),
                 QStringLiteral("series"),
                 QStringLiteral("episode")),
             QStringLiteral("player:prefetch:series:episode"));
    QVERIFY(PlayerPolicy::nextEpisodeRequestContext(
                {}, QStringLiteral("series"), QStringLiteral("episode"))
                .isEmpty());
}

QTEST_APPLESS_MAIN(PlaybackPolicyTest)

#include "PlaybackPolicyTest.moc"
