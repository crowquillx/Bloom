#include <QtTest/QtTest>

#include "player/DolbyVisionPolicy.h"
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

QVariantMap dolbyVisionSource(int profile, int compatibilityId, const QString &rangeType,
                              const QString &title = {})
{
    QVariantMap stream = videoStream(QStringLiteral("HDR"), profile, compatibilityId, title);
    stream.insert(QStringLiteral("videoRangeType"), rangeType);
    return mediaSource(QStringLiteral("dv"), title, QStringLiteral("/media/dv.mkv"), {stream});
}

const QString kElFilter =
    QStringLiteral("--vf-append=@bloom-dovi:format=dolbyvision=yes:enhancement-layer=yes");
const QString kDvOnlyFilter = QStringLiteral("--vf-append=@bloom-dovi:format=dolbyvision=yes");
constexpr auto kHighest = PlayerPolicy::VersionPreference::HighestQuality;

} // namespace

class PlaybackPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void normalizesLanguageAliases_data();
    void normalizesLanguageAliases();
    void scoresLanguageStreamsDeterministically();
    void resolvesTrackSelectionPrecedence();
    void prefersEmbeddedSubtitlesOverExternalFiles();
    void classifiesHdrAndDolbyVision();
    void evaluatesHdrOutputPolicy();
    void selectsVersionsByForcedIdAndAffinity();
    void ranksVersionsByPreference();
    void filtersMultipartSourcesAndBuildsSubtitles();
    void evaluatesCompletionAndPrefetchThresholds();
    void validatesPrefetchedEpisodesAndContexts();
    void resolvesAdjacentEpisodesWithinSeason();
    void resolvesAdjacentSeasonsSkippingSpecials();
    void buildsPlaybackVersionOptions();
    void describesDolbyVisionProfiles();
    void dolbyVisionFilterFollowsSourceType_data();
    void dolbyVisionFilterFollowsSourceType();
    void dolbyVisionFilterPreservesExistingFilters();
    void dolbyVisionFilterDegradesWithRuntimeSupport();
    void detectsFiltersThatStripDolbyVision();
    void parsesMpvVersionOutput();
    void evaluatesFelCapabilityByComponent();
    void evaluatesPlaybackDiagnostics();
    void diagnosticsDoNotClaimFelWithoutEvidence();
    void diagnosticsKeepMissingEvidenceUnavailable();
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

void PlaybackPolicyTest::prefersEmbeddedSubtitlesOverExternalFiles()
{
    const auto external = [](QVariantMap value) {
        value.insert(QStringLiteral("isExternal"), true);
        return value;
    };
    // The provider lists an external English file ahead of the embedded one.
    const QVariantMap source = mediaSource(
        QStringLiteral("source"),
        QStringLiteral("Version"),
        QStringLiteral("/media/movie.mkv"),
        {
            stream(QStringLiteral("Audio"), 1, QStringLiteral("eng"), true),
            external(stream(QStringLiteral("Subtitle"), 2, QStringLiteral("eng"))),
            external(stream(QStringLiteral("Subtitle"), 3, QStringLiteral("eng"),
                            false, true)),
            stream(QStringLiteral("Subtitle"), 4, QStringLiteral("eng")),
            stream(QStringLiteral("Subtitle"), 5, QStringLiteral("eng"), false, true)
        });

    auto resolved = PlayerPolicy::resolveTrackSelection(
        source, {}, QStringLiteral("eng"), QStringLiteral("eng"));
    QCOMPARE(resolved.subtitleIndex, 4);
    QCOMPARE(resolved.subtitleSource, QStringLiteral("global-language"));

    resolved = PlayerPolicy::resolveTrackSelection(
        source, {}, QStringLiteral("eng"), QStringLiteral("forced"));
    QCOMPARE(resolved.subtitleIndex, 5);

    // An external file still wins when it is the only match.
    QVariantMap externalOnly = source;
    externalOnly[QStringLiteral("mediaStreams")] = QVariantList{
        stream(QStringLiteral("Subtitle"), 4, QStringLiteral("spa")),
        external(stream(QStringLiteral("Subtitle"), 2, QStringLiteral("eng")))
    };
    resolved = PlayerPolicy::resolveTrackSelection(
        externalOnly, {}, QStringLiteral("eng"), QStringLiteral("eng"));
    QCOMPARE(resolved.subtitleIndex, 2);
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
                 sources, QStringLiteral("signature"), true, affinity, kHighest)
                 .value(QStringLiteral("id")).toString(),
             QStringLiteral("signature"));
    QCOMPARE(PlayerPolicy::selectMediaSource(
                 sources, {}, true, affinity, kHighest)
                 .value(QStringLiteral("id")).toString(),
             QStringLiteral("parent"));

    affinity.parentPath.clear();
    affinity.name = QStringLiteral("Other");
    QCOMPARE(PlayerPolicy::selectMediaSource(sources, {}, true, affinity, kHighest)
                 .value(QStringLiteral("id")).toString(),
             QStringLiteral("parent"));

    affinity.name.clear();
    affinity.signature = PlayerPolicy::mediaSourceSignature(sameSignature);
    QCOMPARE(PlayerPolicy::selectMediaSource(sources, {}, true, affinity, kHighest)
                 .value(QStringLiteral("id")).toString(),
             QStringLiteral("signature"));

    // Without affinity the preference decides: the only HDR source wins.
    QCOMPARE(PlayerPolicy::selectMediaSource(sources, {}, false, affinity, kHighest)
                 .value(QStringLiteral("id")).toString(),
             QStringLiteral("parent"));
    QVERIFY(PlayerPolicy::selectMediaSource({}, {}, true, affinity, kHighest).isEmpty());
}

void PlaybackPolicyTest::ranksVersionsByPreference()
{
    const auto version = [](const QString &id, int width, int height,
                            const QString &range, qint64 bitRate, qint64 size) {
        QVariantMap video = videoStream(range);
        video.insert(QStringLiteral("width"), width);
        video.insert(QStringLiteral("height"), height);
        QVariantMap source = mediaSource(id, id, QStringLiteral("/media/%1.mkv").arg(id),
                                         {video});
        source.insert(QStringLiteral("bitRate"), bitRate);
        source.insert(QStringLiteral("size"), size);
        return source;
    };
    const auto pick = [](const QVariantList &sources, PlayerPolicy::VersionPreference preference) {
        return PlayerPolicy::preferredMediaSource(sources, preference)
            .value(QStringLiteral("id")).toString();
    };

    const QVariantMap hd = version(QStringLiteral("hd"), 1920, 1080, QStringLiteral("SDR"),
                                   8'000'000, 4'000'000'000);
    const QVariantMap uhdSdr = version(QStringLiteral("uhd-sdr"), 3840, 2160,
                                       QStringLiteral("SDR"), 40'000'000, 30'000'000'000);
    const QVariantMap uhdHdr = version(QStringLiteral("uhd-hdr"), 3840, 2160,
                                       QStringLiteral("HDR10"), 30'000'000, 25'000'000'000);
    const QVariantMap unknown = version(QStringLiteral("unknown"), 0, 0, {}, 0, 0);
    const QVariantList sources{unknown, hd, uhdSdr, uhdHdr};

    // Resolution first, then HDR beats a higher-bitrate SDR encode.
    QCOMPARE(pick(sources, PlayerPolicy::VersionPreference::HighestQuality),
             QStringLiteral("uhd-hdr"));
    // Ask falls back to highest quality when no prompt is allowed.
    QCOMPARE(pick(sources, PlayerPolicy::VersionPreference::Ask), QStringLiteral("uhd-hdr"));
    // Smallest file wins; a source without metrics never counts as smallest.
    QCOMPARE(pick(sources, PlayerPolicy::VersionPreference::Smallest), QStringLiteral("hd"));

    // Without sizes, Smallest falls back to bitrate.
    QVariantMap hdNoSize = hd;
    hdNoSize.insert(QStringLiteral("size"), 0);
    QVariantMap uhdNoSize = uhdHdr;
    uhdNoSize.insert(QStringLiteral("size"), 0);
    QCOMPARE(pick({uhdNoSize, hdNoSize}, PlayerPolicy::VersionPreference::Smallest),
             QStringLiteral("hd"));

    // Ties keep server order.
    QVariantMap hdCopy = hd;
    hdCopy.insert(QStringLiteral("id"), QStringLiteral("hd-copy"));
    QCOMPARE(pick({hd, hdCopy}, PlayerPolicy::VersionPreference::HighestQuality),
             QStringLiteral("hd"));
    QCOMPARE(pick({hd, hdCopy}, PlayerPolicy::VersionPreference::Smallest), QStringLiteral("hd"));
    QVERIFY(PlayerPolicy::preferredMediaSource({}, PlayerPolicy::VersionPreference::Smallest)
                .isEmpty());

    QCOMPARE(PlayerPolicy::parseVersionPreference(QStringLiteral(" ASK ")),
             PlayerPolicy::VersionPreference::Ask);
    QCOMPARE(PlayerPolicy::parseVersionPreference(QStringLiteral("smallest")),
             PlayerPolicy::VersionPreference::Smallest);
    QCOMPARE(PlayerPolicy::parseVersionPreference(QStringLiteral("bogus")),
             PlayerPolicy::VersionPreference::HighestQuality);
    QCOMPARE(PlayerPolicy::versionPreferenceName(PlayerPolicy::VersionPreference::Smallest),
             QStringLiteral("smallest"));
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

namespace {

QVariantMap libraryRow(const QString &id, const QString &mediaType, int indexNumber, int childCount = -1)
{
    QVariantMap row{
        {QStringLiteral("itemId"), id},
        {QStringLiteral("mediaType"), mediaType},
        {QStringLiteral("indexNumber"), indexNumber}
    };
    if (childCount >= 0) row.insert(QStringLiteral("childCount"), childCount);
    return row;
}

} // namespace

void PlaybackPolicyTest::resolvesAdjacentEpisodesWithinSeason()
{
    const QVariantList season{
        libraryRow(QStringLiteral("e1"), QStringLiteral("Episode"), 1),
        libraryRow(QStringLiteral("folder"), QStringLiteral("Folder"), 0),
        libraryRow(QStringLiteral("e2"), QStringLiteral("Episode"), 2),
        libraryRow(QStringLiteral("e3"), QStringLiteral("Episode"), 3)
    };

    const auto middle = PlayerPolicy::resolveAdjacentEpisodes(season, QStringLiteral("e2"));
    QVERIFY(middle.currentFound);
    QCOMPARE(middle.previous.value(QStringLiteral("itemId")).toString(), QStringLiteral("e1"));
    QCOMPARE(middle.next.value(QStringLiteral("itemId")).toString(), QStringLiteral("e3"));
    QVERIFY(!middle.previousInAdjacentSeason);
    QVERIFY(!middle.nextInAdjacentSeason);

    const auto first = PlayerPolicy::resolveAdjacentEpisodes(season, QStringLiteral("e1"));
    QVERIFY(first.previous.isEmpty());
    QVERIFY(first.previousInAdjacentSeason);
    QCOMPARE(first.next.value(QStringLiteral("itemId")).toString(), QStringLiteral("e2"));

    const auto last = PlayerPolicy::resolveAdjacentEpisodes(season, QStringLiteral("e3"));
    QVERIFY(last.next.isEmpty());
    QVERIFY(last.nextInAdjacentSeason);

    const auto missing = PlayerPolicy::resolveAdjacentEpisodes(season, QStringLiteral("other"));
    QVERIFY(!missing.currentFound);
    QVERIFY(!missing.previousInAdjacentSeason);
    QVERIFY(!missing.nextInAdjacentSeason);

    QCOMPARE(PlayerPolicy::boundaryEpisode(season, false).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("e1"));
    QCOMPARE(PlayerPolicy::boundaryEpisode(season, true).value(QStringLiteral("itemId")).toString(),
             QStringLiteral("e3"));
    QVERIFY(PlayerPolicy::boundaryEpisode({}, true).isEmpty());
}

void PlaybackPolicyTest::resolvesAdjacentSeasonsSkippingSpecials()
{
    const QVariantList series{
        libraryRow(QStringLiteral("specials"), QStringLiteral("Season"), 0, 4),
        libraryRow(QStringLiteral("s1"), QStringLiteral("Season"), 1, 10),
        libraryRow(QStringLiteral("empty"), QStringLiteral("Season"), 2, 0),
        libraryRow(QStringLiteral("s3"), QStringLiteral("Season"), 3, 8)
    };

    QVERIFY(PlayerPolicy::adjacentSeasonId(series, QStringLiteral("s1"), -1).isEmpty());
    QCOMPARE(PlayerPolicy::adjacentSeasonId(series, QStringLiteral("s1"), 1), QStringLiteral("s3"));
    QCOMPARE(PlayerPolicy::adjacentSeasonId(series, QStringLiteral("s3"), -1), QStringLiteral("s1"));
    QVERIFY(PlayerPolicy::adjacentSeasonId(series, QStringLiteral("s3"), 1).isEmpty());
    // Inside specials, the regular seasons are still reachable.
    QCOMPARE(PlayerPolicy::adjacentSeasonId(series, QStringLiteral("specials"), 1), QStringLiteral("s1"));
    QVERIFY(PlayerPolicy::adjacentSeasonId(series, QStringLiteral("unknown"), 1).isEmpty());
}

void PlaybackPolicyTest::buildsPlaybackVersionOptions()
{
    const QVariantList sources{
        mediaSource(QStringLiteral("v1"), QStringLiteral(" 1080p "), QStringLiteral("/a.mkv")),
        mediaSource(QString(), QStringLiteral("broken"), QStringLiteral("/b.mkv")),
        mediaSource(QStringLiteral("v2"), QStringLiteral("4K"), QStringLiteral("/c.mkv"))
    };

    const QVariantList options = PlayerPolicy::playbackVersionOptions(sources, QStringLiteral("v2"));
    QCOMPARE(options.size(), 2);
    QCOMPARE(options.at(0).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("1080p"));
    QVERIFY(!options.at(0).toMap().value(QStringLiteral("selected")).toBool());
    QCOMPARE(options.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("v2"));
    QVERIFY(options.at(1).toMap().value(QStringLiteral("selected")).toBool());

    const QVariantList unselected = PlayerPolicy::playbackVersionOptions(sources, QString());
    QVERIFY(!unselected.at(0).toMap().value(QStringLiteral("selected")).toBool());
    QVERIFY(!unselected.at(1).toMap().value(QStringLiteral("selected")).toBool());
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

void PlaybackPolicyTest::describesDolbyVisionProfiles()
{
    const auto p5 = PlayerPolicy::describeDolbyVisionSource(
        dolbyVisionSource(5, 0, QStringLiteral("DOVI")));
    QVERIFY(p5.isDolbyVision);
    QCOMPARE(p5.profile, 5);
    QCOMPARE(p5.compatibilityId, 0);
    QVERIFY(!p5.dualLayer);

    const auto p7 = PlayerPolicy::describeDolbyVisionSource(
        dolbyVisionSource(7, 6, QStringLiteral("DOVIWithHDR10")));
    QCOMPARE(p7.profile, 7);
    QVERIFY(p7.dualLayer);
    // Provider metadata cannot tell MEL from FEL; Bloom must not claim FEL.
    QVERIFY(p7.enhancementLayerHint.isEmpty());

    const auto p7Fel = PlayerPolicy::describeDolbyVisionSource(
        dolbyVisionSource(7, 6, QStringLiteral("DOVIWithEL"),
                          QStringLiteral("Movie (2024) BluRay Remux DV P7 FEL")));
    QVERIFY(p7Fel.dualLayer);
    QCOMPARE(p7Fel.enhancementLayerHint, QStringLiteral("fel"));

    const auto p8 = PlayerPolicy::describeDolbyVisionSource(
        dolbyVisionSource(8, 1, QStringLiteral("DOVIWithHDR10")));
    QCOMPARE(p8.profile, 8);
    QCOMPARE(p8.compatibilityId, 1);
    QVERIFY(!p8.dualLayer);

    QVariantMap codecOnly = videoStream(QStringLiteral("HDR"));
    codecOnly.insert(QStringLiteral("codecTag"), QStringLiteral("dvhe.08.06"));
    const auto fromCodec = PlayerPolicy::describeDolbyVisionSource(
        mediaSource(QStringLiteral("c"), {}, {}, {codecOnly}));
    QVERIFY(fromCodec.isDolbyVision);
    QCOMPARE(fromCodec.profile, 8);

    QVERIFY(!PlayerPolicy::describeDolbyVisionSource(
                 mediaSource(QStringLiteral("hdr10"), {}, {},
                             {videoStream(QStringLiteral("HDR10"))}))
                 .isDolbyVision);
    QVERIFY(!PlayerPolicy::describeDolbyVisionSource(
                 mediaSource(QStringLiteral("sdr"), {}, {},
                             {videoStream(QStringLiteral("SDR"))}))
                 .isDolbyVision);
}

void PlaybackPolicyTest::dolbyVisionFilterFollowsSourceType_data()
{
    QTest::addColumn<QVariantMap>("source");
    QTest::addColumn<bool>("expectFilter");

    QTest::newRow("SDR") << mediaSource(QStringLiteral("sdr"), {}, {},
                                        {videoStream(QStringLiteral("SDR"))})
                         << false;
    QTest::newRow("HDR10") << mediaSource(QStringLiteral("hdr10"), {}, {},
                                          {videoStream(QStringLiteral("HDR10"))})
                           << false;
    QVariantMap hlg = videoStream(QStringLiteral("HDR"));
    hlg.insert(QStringLiteral("videoRangeType"), QStringLiteral("HLG"));
    QTest::newRow("HLG") << mediaSource(QStringLiteral("hlg"), {}, {}, {hlg}) << false;
    QTest::newRow("DV profile 5") << dolbyVisionSource(5, 0, QStringLiteral("DOVI")) << true;
    QTest::newRow("DV profile 7 MEL/FEL")
        << dolbyVisionSource(7, 6, QStringLiteral("DOVIWithHDR10")) << true;
    QTest::newRow("DV profile 8.1") << dolbyVisionSource(8, 1, QStringLiteral("DOVIWithHDR10"))
                                    << true;
    QTest::newRow("DV profile 8.4 HLG") << dolbyVisionSource(8, 4, QStringLiteral("DOVIWithHLG"))
                                        << true;
}

void PlaybackPolicyTest::dolbyVisionFilterFollowsSourceType()
{
    QFETCH(QVariantMap, source);
    QFETCH(bool, expectFilter);

    const QStringList base{QStringLiteral("--vo=gpu-next"), QStringLiteral("--hwdec=auto-safe")};
    const PlayerPolicy::HdrContentKind kind = PlayerPolicy::classifyMediaSourceHdr(source);
    const QStringList args = PlayerPolicy::appendDolbyVisionFilterArgs(
        base, kind, PlayerPolicy::FormatFilterSupport::DolbyVisionAndEnhancementLayer);
    if (expectFilter) {
        QCOMPARE(args, QStringList(base) << kElFilter);
    } else {
        // SDR/HDR10/HLG keep Bloom's existing path byte-for-byte.
        QCOMPARE(args, base);
    }
}

void PlaybackPolicyTest::dolbyVisionFilterPreservesExistingFilters()
{
    const QStringList base{
        QStringLiteral("--vf=lavfi=[bwdif]"),
        QStringLiteral("--vf-append=@user:format=colorlevels=limited"),
        QStringLiteral("--glsl-shaders-append=~~/shaders/ArtCNN_C4F16.glsl"),
        QStringLiteral("--target-colorspace-hint=auto"),
    };
    const QStringList args = PlayerPolicy::appendDolbyVisionFilterArgs(
        base, PlayerPolicy::HdrContentKind::DolbyVisionCompatible,
        PlayerPolicy::FormatFilterSupport::DolbyVisionAndEnhancementLayer);
    QCOMPARE(args.mid(0, base.size()), base);
    QCOMPARE(args.last(), kElFilter);
    QCOMPARE(args.size(), base.size() + 1);

    // Re-applying (replacement playback, recovery) never duplicates the entry.
    const QStringList again = PlayerPolicy::appendDolbyVisionFilterArgs(
        args, PlayerPolicy::HdrContentKind::DolbyVisionCompatible,
        PlayerPolicy::FormatFilterSupport::DolbyVisionAndEnhancementLayer);
    QCOMPARE(again, args);
    QCOMPARE(again.filter(QStringLiteral("@bloom-dovi:")).size(), 1);
}

void PlaybackPolicyTest::dolbyVisionFilterDegradesWithRuntimeSupport()
{
    const QStringList base{QStringLiteral("--vo=gpu-next")};
    using Support = PlayerPolicy::FormatFilterSupport;
    QCOMPARE(PlayerPolicy::appendDolbyVisionFilterArgs(
                 base, PlayerPolicy::HdrContentKind::DolbyVisionCompatible,
                 Support::DolbyVisionOnly),
             QStringList(base) << kDvOnlyFilter);
    // Unknown (probe pending) and Unsupported (very old mpv) emit nothing:
    // an unknown suboption would make external mpv refuse to start.
    QCOMPARE(PlayerPolicy::appendDolbyVisionFilterArgs(
                 base, PlayerPolicy::HdrContentKind::DolbyVisionCompatible, Support::Unknown),
             base);
    QCOMPARE(PlayerPolicy::appendDolbyVisionFilterArgs(
                 base, PlayerPolicy::HdrContentKind::DolbyVisionUnsupported, Support::Unsupported),
             base);
}

void PlaybackPolicyTest::detectsFiltersThatStripDolbyVision()
{
    const QStringList args{
        QStringLiteral("--vf=lavfi=[bwdif]"),
        QStringLiteral("--vf-append=format=dolbyvision=no"),
        QStringLiteral("--vf-add=format=enhancement-layer=no:hdr10plus=yes"),
        QStringLiteral("--vf-append=@bloom-dovi:format=dolbyvision=yes:enhancement-layer=yes"),
        QStringLiteral("--sub-filter-regex-append=dolbyvision=no"),
    };
    QCOMPARE(PlayerPolicy::dolbyVisionStrippingFilterArgs(args),
             QStringList({args.at(1), args.at(2)}));
}

void PlaybackPolicyTest::parsesMpvVersionOutput()
{
    // Captured from a distribution mpv 0.41.0 (pre-PR-#17932).
    const QString legacy = QStringLiteral(
        "mpv v0.41.0 Copyright \u00a9 2000-2025 mpv/MPlayer/mplayer2 projects\n"
        " built on Jan  1 1980 00:00:00\n"
        "libplacebo version: v7.360.1\n"
        "FFmpeg version: 9.0.1\n"
        "FFmpeg library versions:\n"
        "   libavcodec      63.1.101\n"
        "   libavdevice     63.1.101\n");
    const auto legacyInfo = PlayerPolicy::parseMpvVersionOutput(legacy);
    QCOMPARE(legacyInfo.mpvVersion, QStringLiteral("mpv v0.41.0"));
    QCOMPARE(legacyInfo.libplaceboVersion, QStringLiteral("v7.360.1"));
    QCOMPARE(legacyInfo.ffmpegVersion, QStringLiteral("9.0.1"));
    QCOMPARE(legacyInfo.libavcodecVersion, QVersionNumber(63, 1, 101));
    QCOMPARE(PlayerPolicy::libplaceboApiVersion(legacyInfo.libplaceboVersion), 360);

    // Runtime FFmpeg differs from the build-time one: the runtime one counts.
    const QString mixed = QStringLiteral(
        "mpv v0.41.0-1200-gdde067107 Copyright 2000-2026\n"
        "libplacebo version: v7.376.0 (v7.360.0-171-g0df80ae-dirty)\n"
        "FFmpeg version: N-127602-g2d49d0e62 (runtime 8.1.2)\n"
        "FFmpeg library versions:\n"
        "   libavcodec      63.1.100 (runtime 62.28.100)\n");
    const auto mixedInfo = PlayerPolicy::parseMpvVersionOutput(mixed);
    QCOMPARE(mixedInfo.ffmpegVersion, QStringLiteral("8.1.2"));
    QCOMPARE(mixedInfo.libavcodecVersion, QVersionNumber(62, 28, 100));
    QCOMPARE(PlayerPolicy::libplaceboApiVersion(mixedInfo.libplaceboVersion), 376);
    QVERIFY(PlayerPolicy::ffmpegReleaseVersion(QStringLiteral("N-127602-g2d49d0e62")).isNull());
    QCOMPARE(PlayerPolicy::ffmpegReleaseVersion(QStringLiteral("n9.0.2")), QVersionNumber(9, 0, 2));
}

void PlaybackPolicyTest::evaluatesFelCapabilityByComponent()
{
    using PlayerPolicy::Capability;
    using PlayerPolicy::RendererKind;

    PlayerPolicy::MpvRuntimeInfo pinned;
    pinned.mpvVersion = QStringLiteral("mpv v0.41.0-1200-gdde067107");
    pinned.libplaceboVersion = QStringLiteral("v7.376.0");
    pinned.ffmpegVersion = QStringLiteral("9.0.2");
    pinned.libavcodecVersion = QVersionNumber(63, 1, 102);
    pinned.formatFilter = PlayerPolicy::FormatFilterSupport::DolbyVisionAndEnhancementLayer;
    pinned.matchesVerifiedPackagedStack = true;
    QCOMPARE(PlayerPolicy::evaluateFelCapability(pinned, RendererKind::GpuNext).overall,
             Capability::Supported);

    // Same runtime, but the libmpv render API (linux-libmpv-opengl) cannot
    // apply DV reshaping or the EL.
    const auto renderApi = PlayerPolicy::evaluateFelCapability(pinned, RendererKind::LibmpvRenderApi);
    QCOMPARE(renderApi.overall, Capability::Unsupported);
    QCOMPARE(renderApi.renderer, Capability::Unsupported);

    // mpv has PR #17932 but libplacebo is a release without FEL composition.
    PlayerPolicy::MpvRuntimeInfo oldPlacebo = pinned;
    oldPlacebo.libplaceboVersion = QStringLiteral("v7.360.1");
    const auto placebo = PlayerPolicy::evaluateFelCapability(oldPlacebo, RendererKind::GpuNext);
    QCOMPARE(placebo.overall, Capability::Unsupported);
    QCOMPARE(placebo.libplacebo, Capability::Unsupported);
    QCOMPARE(placebo.mpvEnhancementLayer, Capability::Supported);

    // FFmpeg without dovi_split cannot split the in-band EL.
    PlayerPolicy::MpvRuntimeInfo oldFfmpeg = pinned;
    oldFfmpeg.libavcodecVersion = QVersionNumber(62, 35, 100);
    QCOMPARE(PlayerPolicy::evaluateFelCapability(oldFfmpeg, RendererKind::GpuNext).ffmpegDoviSplit,
             Capability::Unsupported);
    oldFfmpeg.libavcodecVersion = QVersionNumber(62, 36, 100);
    QCOMPARE(PlayerPolicy::evaluateFelCapability(oldFfmpeg, RendererKind::GpuNext).ffmpegDoviSplit,
             Capability::Supported);

    // Embedded libmpv with a git FFmpeg reports no lavc numbers: unknown
    // unless it is the build-time verified packaged pin.
    PlayerPolicy::MpvRuntimeInfo embedded = pinned;
    embedded.libavcodecVersion = {};
    embedded.ffmpegVersion = QStringLiteral("N-127602-g2d49d0e62");
    embedded.matchesVerifiedPackagedStack = false;
    QCOMPARE(PlayerPolicy::evaluateFelCapability(embedded, RendererKind::GpuNext).overall,
             Capability::Unknown);
    embedded.matchesVerifiedPackagedStack = true;
    QCOMPARE(PlayerPolicy::evaluateFelCapability(embedded, RendererKind::GpuNext).overall,
             Capability::Supported);

    auto unverified = pinned;
    unverified.matchesVerifiedPackagedStack = false;
    QCOMPARE(PlayerPolicy::evaluateFelCapability(unverified, RendererKind::GpuNext).overall,
             Capability::Unknown);

    // Old mpv: no EL option, whatever the libraries are.
    PlayerPolicy::MpvRuntimeInfo oldMpv = pinned;
    oldMpv.formatFilter = PlayerPolicy::FormatFilterSupport::DolbyVisionOnly;
    QCOMPARE(PlayerPolicy::evaluateFelCapability(oldMpv, RendererKind::GpuNext).overall,
             Capability::Unsupported);
}

void PlaybackPolicyTest::evaluatesPlaybackDiagnostics()
{
    const QVariantMap properties{
        {QStringLiteral("current-vo"), QStringLiteral("gpu-next")},
        {QStringLiteral("current-gpu-context"), QStringLiteral("d3d11")},
        {QStringLiteral("options/hwdec"), QStringLiteral("auto-safe")},
        {QStringLiteral("hwdec-current"), QStringLiteral("no")},
        {QStringLiteral("options/target-colorspace-hint"), QStringLiteral("auto")},
        {QStringLiteral("options/target-colorspace-hint-mode"), QStringLiteral("target")},
        {QStringLiteral("vf"), QVariantList{
             QVariantMap{{QStringLiteral("name"), QStringLiteral("lavfi")},
                         {QStringLiteral("enabled"), true},
                         {QStringLiteral("params"),
                          QVariantMap{{QStringLiteral("graph"), QStringLiteral("[bwdif]")}}}},
             QVariantMap{{QStringLiteral("name"), QStringLiteral("format")},
                         {QStringLiteral("label"), QStringLiteral("bloom-dovi")},
                         {QStringLiteral("enabled"), true},
                         {QStringLiteral("params"),
                          QVariantMap{{QStringLiteral("dolbyvision"), QStringLiteral("yes")},
                                      {QStringLiteral("enhancement-layer"), QStringLiteral("yes")}}}},
         }},
        {QStringLiteral("track-list"), QVariantList{
             QVariantMap{{QStringLiteral("type"), QStringLiteral("video")},
                         {QStringLiteral("selected"), true},
                         {QStringLiteral("codec"), QStringLiteral("hevc")},
                         {QStringLiteral("decoder"), QStringLiteral("hevc")},
                         {QStringLiteral("dolby-vision-profile"), 7}},
             QVariantMap{{QStringLiteral("type"), QStringLiteral("video")},
                         {QStringLiteral("dependent"), true},
                         {QStringLiteral("dolby-vision-profile"), 7}},
         }},
        {QStringLiteral("options/show-dependent-tracks"), true},
        {QStringLiteral("video-dec-params"), QVariantMap{
             {QStringLiteral("pixelformat"), QStringLiteral("yuv420p10")},
             {QStringLiteral("gamma"), QStringLiteral("pq")},
             {QStringLiteral("primaries"), QStringLiteral("bt.2020")},
             {QStringLiteral("colormatrix"), QStringLiteral("dolbyvision")}}},
        {QStringLiteral("video-out-params"), QVariantMap{
             {QStringLiteral("colormatrix"), QStringLiteral("dolbyvision")}}},
        {QStringLiteral("video-target-params"), QVariantMap{
             {QStringLiteral("gamma"), QStringLiteral("pq")},
             {QStringLiteral("primaries"), QStringLiteral("bt.2020")},
             {QStringLiteral("sig-peak"), 10.0}}},
        {QStringLiteral("vo-passes"), QVariantMap{
             {QStringLiteral("fresh"), QVariantList{
                  QVariantMap{{QStringLiteral("desc"),
                               QStringLiteral("decoding (dovi) + enhancement layer + tone mapping")}}}}}},
        {QStringLiteral("frame-drop-count"), 0},
        {QStringLiteral("decoder-frame-drop-count"), 3},
    };

    const auto d = PlayerPolicy::evaluatePlaybackDiagnostics(properties);
    QCOMPARE(d.vo, QStringLiteral("gpu-next"));
    QVERIFY(d.softwareDecoding);
    QVERIFY(d.hardwareFallbackToSoftware);
    QCOMPARE(d.targetColorspaceHintMode, QStringLiteral("target"));
    QCOMPARE(d.videoFilterChain,
             QStringLiteral("lavfi=graph=[bwdif],@bloom-dovi:format=dolbyvision=yes:enhancement-layer=yes"));
    QCOMPARE(d.dolbyVisionProfile, 7);
    QCOMPARE(d.bitDepth, 10);
    QVERIFY(d.dolbyVisionMetadataApplied);
    QVERIFY(!d.dolbyVisionMetadataStripped);
    QVERIFY(d.enhancementLayerTrackObservable);
    QVERIFY(d.enhancementLayerTrackPresent);
    QVERIFY(d.enhancementLayerEnabled);
    QVERIFY(d.enhancementLayerRendered);
    QCOMPARE(d.outputTransfer, QStringLiteral("pq"));
    QCOMPARE(d.decoderDroppedFrames, 3);
    QCOMPARE(d.delayedFrames, -1);

    const QString summary = PlayerPolicy::playbackDiagnosticsSummary(
        d, PlayerPolicy::describeDolbyVisionSource(
               dolbyVisionSource(7, 6, QStringLiteral("DOVIWithHDR10"))));
    QVERIFY(summary.contains(QStringLiteral("el-rendered=yes")));
    QVERIFY(summary.contains(QStringLiteral("hwdec=auto-safe->software(fallback)")));
}

void PlaybackPolicyTest::diagnosticsDoNotClaimFelWithoutEvidence()
{
    // EL deliberately disabled and stripped DV metadata: nothing may claim FEL.
    const QVariantMap properties{
        {QStringLiteral("current-vo"), QStringLiteral("gpu-next")},
        {QStringLiteral("hwdec-current"), QStringLiteral("d3d11va")},
        {QStringLiteral("vf"), QStringLiteral("format=dolbyvision=no:enhancement-layer=no")},
        {QStringLiteral("video-out-params"),
         QVariantMap{{QStringLiteral("colormatrix"), QStringLiteral("bt.2020-ncl")}}},
        {QStringLiteral("vo-passes"), QVariantMap{
             {QStringLiteral("fresh"), QVariantList{
                  QVariantMap{{QStringLiteral("desc"), QStringLiteral("decoding + tone mapping")}}}}}},
        {QStringLiteral("track-list"), QVariantList{
             QVariantMap{{QStringLiteral("type"), QStringLiteral("video")},
                         {QStringLiteral("selected"), true},
                         {QStringLiteral("dolby-vision-profile"), 7}}}},
    };
    const auto d = PlayerPolicy::evaluatePlaybackDiagnostics(properties);
    QVERIFY(!d.softwareDecoding);
    QVERIFY(d.dolbyVisionMetadataStripped);
    QVERIFY(!d.dolbyVisionMetadataApplied);
    QVERIFY(!d.enhancementLayerEnabled);
    QVERIFY(!d.enhancementLayerRendered);
    QVERIFY(!d.enhancementLayerTrackObservable);
    const QString summary = PlayerPolicy::playbackDiagnosticsSummary(d, {});
    QVERIFY(summary.contains(QStringLiteral("el-rendered=no")));
    QVERIFY(summary.contains(QStringLiteral("el-track=unobservable")));
    QVERIFY(summary.contains(QStringLiteral("(stripped-by-filter)")));

    // Without vo-passes the renderer evidence is unobservable, not "no".
    QVariantMap noPasses = properties;
    noPasses.remove(QStringLiteral("vo-passes"));
    QVERIFY(PlayerPolicy::playbackDiagnosticsSummary(
                PlayerPolicy::evaluatePlaybackDiagnostics(noPasses), {})
                .contains(QStringLiteral("el-rendered=unobservable")));
}

void PlaybackPolicyTest::diagnosticsKeepMissingEvidenceUnavailable()
{
    const auto source = PlayerPolicy::describeDolbyVisionSource(
        dolbyVisionSource(7, 6, QStringLiteral("DOVIWithHDR10")));
    QVariantMap properties{
        {QStringLiteral("options/hwdec"), QStringLiteral("auto-safe")},
        {QStringLiteral("video-dec-params"), QVariantMap{
             {QStringLiteral("colormatrix"), QStringLiteral("dolbyvision")}}},
    };
    const auto checkUnavailable = [&source](const QVariantMap &snapshot) {
        const auto diagnostics = PlayerPolicy::evaluatePlaybackDiagnostics(snapshot);
        QVERIFY(!diagnostics.hwdecCurrentObservable);
        QVERIFY(!diagnostics.softwareDecoding);
        QVERIFY(!diagnostics.hardwareFallbackToSoftware);
        QVERIFY(!diagnostics.videoFilterChainObservable);
        QVERIFY(!diagnostics.enhancementLayerEnabled);
        QVERIFY(!diagnostics.dolbyVisionMetadataObservable);
        QVERIFY(!diagnostics.dolbyVisionMetadataApplied);
        const auto summary = PlayerPolicy::playbackDiagnosticsSummary(diagnostics, source);
        QVERIFY(summary.contains(QStringLiteral("hwdec=auto-safe->unavailable")));
        QVERIFY(!summary.contains(QStringLiteral("(fallback)")));
        QVERIFY(summary.contains(QStringLiteral("vf=[unavailable]")));
        QVERIFY(summary.contains(QStringLiteral("el-enabled=unavailable")));
        QVERIFY(summary.contains(QStringLiteral("dv-metadata-applied=unavailable")));
    };
    checkUnavailable(properties);
    properties.insert(QStringLiteral("hwdec-current"), QVariant());
    properties.insert(QStringLiteral("vf"), QVariant());
    properties.insert(QStringLiteral("video-out-params"), QVariant());
    checkUnavailable(properties);
    properties.insert(QStringLiteral("hwdec-current"), QString());
    properties.insert(QStringLiteral("video-out-params"), QVariantMap{});
    checkUnavailable(properties);

    properties.insert(QStringLiteral("hwdec-current"), QStringLiteral("no"));
    properties.insert(QStringLiteral("vf"), QVariantList{});
    properties.insert(QStringLiteral("video-out-params"), QVariantMap{
        {QStringLiteral("colormatrix"), QStringLiteral("bt.2020-ncl")}});
    const auto observed = PlayerPolicy::evaluatePlaybackDiagnostics(properties);
    QVERIFY(observed.hwdecCurrentObservable);
    QVERIFY(observed.softwareDecoding);
    QVERIFY(observed.hardwareFallbackToSoftware);
    QVERIFY(observed.videoFilterChainObservable);
    QVERIFY(observed.enhancementLayerEnabled);
    QVERIFY(observed.dolbyVisionMetadataObservable);
    QVERIFY(!observed.dolbyVisionMetadataApplied);
}

#include "PlaybackPolicyTest.moc"
