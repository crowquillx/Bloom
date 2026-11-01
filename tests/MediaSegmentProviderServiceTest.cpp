#include "network/MediaSegmentProviderService.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest/QtTest>

class MediaSegmentProviderServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesTheIntroDbV4Arrays();
    void rejectsMalformedTheIntroDbTimestamps();
    void ignoresTheIntroDbNonTimestampResponses();
    void handlesNullStartAndDurationEnd();
    void dropsOpenEndedSegmentsWithoutDuration();
    void parsesIntroDbSegments();
    void parsesIntroDbClockStringSegments();
    void dropsIntroDbInvalidStringSegments();
    void mergeKeepsServerSegmentAndFillsMissingType();
    void mergeKeepsRepeatedRangesOfEachType();
    void emptyProviderIdsProduceNoSegments();
};

namespace {
MediaSegmentLookupContext episodeContext()
{
    MediaSegmentLookupContext context;
    context.itemId = QStringLiteral("episode-1");
    context.type = QStringLiteral("Episode");
    context.imdbId = QStringLiteral("tt0903747");
    context.tmdbId = QStringLiteral("1396");
    context.seasonNumber = 1;
    context.episodeNumber = 1;
    context.durationMs = 3600LL * 1000LL;
    return context;
}
}

void MediaSegmentProviderServiceTest::parsesTheIntroDbV4Arrays()
{
    const MediaSegmentLookupContext context = episodeContext();
    const QJsonObject payload{
        {QStringLiteral("tvdb_id"), 81189},
        {QStringLiteral("type"), QStringLiteral("tv")},
        {QStringLiteral("season"), 1},
        {QStringLiteral("episode"), 1},
        {QStringLiteral("intro"), QJsonArray{QJsonObject{
            {QStringLiteral("start_ms"), 30000},
            {QStringLiteral("end_ms"), 90000}
        }}},
        {QStringLiteral("recap"), QJsonArray{QJsonObject{
            {QStringLiteral("start_ms"), 0},
            {QStringLiteral("end_ms"), 25000}
        }}},
        {QStringLiteral("credits"), QJsonArray{QJsonObject{
            {QStringLiteral("start_ms"), 3300000},
            {QStringLiteral("end_ms"), 3600000}
        }}},
        {QStringLiteral("preview"), QJsonArray{QJsonObject{
            {QStringLiteral("start_ms"), 3200000},
            {QStringLiteral("end_ms"), 3250000}
        }}}
    };

    const QList<MediaSegmentInfo> segments = MediaSegmentProviderService::parseTheIntroDbSegments(payload, context);
    QCOMPARE(segments.size(), 4);

    const auto findSegment = [&segments](MediaSegmentType type) -> MediaSegmentInfo {
        for (const MediaSegmentInfo &segment : segments) {
            if (segment.type == type) return segment;
        }
        return {};
    };

    const MediaSegmentInfo intro = findSegment(MediaSegmentType::Intro);
    const MediaSegmentInfo recap = findSegment(MediaSegmentType::Recap);
    const MediaSegmentInfo outro = findSegment(MediaSegmentType::Outro);
    const MediaSegmentInfo preview = findSegment(MediaSegmentType::Preview);
    QCOMPARE(intro.type, MediaSegmentType::Intro);
    QCOMPARE(recap.type, MediaSegmentType::Recap);
    QCOMPARE(outro.type, MediaSegmentType::Outro);
    QCOMPARE(preview.type, MediaSegmentType::Preview);
    QCOMPARE(intro.source, QStringLiteral("theintrodb"));
    QCOMPARE(intro.startSeconds(), 30.0);
    QCOMPARE(intro.endSeconds(), 90.0);
}

void MediaSegmentProviderServiceTest::rejectsMalformedTheIntroDbTimestamps()
{
    const QJsonObject payload{
        {QStringLiteral("intro"), QJsonArray{
             QJsonObject{{QStringLiteral("end_ms"), 10000}},
             QJsonObject{{QStringLiteral("start_ms"), 10000}},
             QJsonObject{{QStringLiteral("start_ms"), QStringLiteral("bad")},
                         {QStringLiteral("end_ms"), 10000}},
             QJsonObject{{QStringLiteral("start_ms"), 1000},
                         {QStringLiteral("end_ms"), false}},
             QJsonObject{{QStringLiteral("start_ms"), -1},
                         {QStringLiteral("end_ms"), 10000}},
             QJsonObject{{QStringLiteral("start_ms"), 1000},
                         {QStringLiteral("end_ms"), 1000}},
             QJsonObject{{QStringLiteral("start_ms"), 1.5},
                         {QStringLiteral("end_ms"), 10000}},
             QJsonObject{{QStringLiteral("start_ms"), 1000},
                         {QStringLiteral("end_ms"), 1e100}},
             QJsonValue::Null,
             QJsonObject{{QStringLiteral("start_ms"), 1234},
                         {QStringLiteral("end_ms"), 9876}}}}
    };
    const auto segments = MediaSegmentProviderService::parseTheIntroDbSegments(payload, episodeContext());
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.first().startMs, 1234);
    QCOMPARE(segments.first().endMs, 9876);
}

void MediaSegmentProviderServiceTest::ignoresTheIntroDbNonTimestampResponses()
{
    const QJsonObject versions{
        {QStringLiteral("tvdb_id"), 81189},
        {QStringLiteral("type"), QStringLiteral("tv")},
        {QStringLiteral("versions"), QJsonArray{QJsonObject{
             {QStringLiteral("duration_ms"), 3600000},
             {QStringLiteral("submission_count"), 10}}}}
    };
    const QJsonObject episodes{
        {QStringLiteral("tvdb_id"), 81189},
        {QStringLiteral("type"), QStringLiteral("tv")},
        {QStringLiteral("episodes"), QJsonArray{QJsonObject{
             {QStringLiteral("season"), 1},
             {QStringLiteral("episode"), 1},
             {QStringLiteral("intro"), 10}}}}
    };
    QVERIFY(MediaSegmentProviderService::parseTheIntroDbSegments(versions, episodeContext()).isEmpty());
    QVERIFY(MediaSegmentProviderService::parseTheIntroDbSegments(episodes, episodeContext()).isEmpty());
}

void MediaSegmentProviderServiceTest::handlesNullStartAndDurationEnd()
{
    const MediaSegmentLookupContext context = episodeContext();
    const QJsonObject payload{
        {QStringLiteral("recap"), QJsonArray{QJsonObject{
            {QStringLiteral("start_ms"), QJsonValue::Null},
            {QStringLiteral("end_ms"), 10000}
        }}},
        {QStringLiteral("credits"), QJsonArray{QJsonObject{
            {QStringLiteral("start_ms"), 3300000},
            {QStringLiteral("end_ms"), QJsonValue::Null}
        }}}
    };

    const QList<MediaSegmentInfo> segments = MediaSegmentProviderService::parseTheIntroDbSegments(payload, context);
    QCOMPARE(segments.size(), 2);
    QCOMPARE(segments.at(0).type, MediaSegmentType::Recap);
    QCOMPARE(segments.at(0).startSeconds(), 0.0);
    QCOMPARE(segments.at(0).endSeconds(), 10.0);
    QCOMPARE(segments.at(1).type, MediaSegmentType::Outro);
    QCOMPARE(segments.at(1).startSeconds(), 3300.0);
    QCOMPARE(segments.at(1).endSeconds(), 3600.0);
}

void MediaSegmentProviderServiceTest::dropsOpenEndedSegmentsWithoutDuration()
{
    MediaSegmentLookupContext context = episodeContext();
    const QJsonObject payload{
        {QStringLiteral("intro"), QJsonArray{QJsonObject{}}},
        {QStringLiteral("credits"), QJsonArray{QJsonObject{
            {QStringLiteral("start_ms"), 1000},
            {QStringLiteral("end_ms"), QJsonValue::Null}
        }}}
    };

    context.durationMs = 0;
    const QList<MediaSegmentInfo> segments = MediaSegmentProviderService::parseTheIntroDbSegments(payload, context);
    QVERIFY(segments.isEmpty());
}

void MediaSegmentProviderServiceTest::parsesIntroDbSegments()
{
    const MediaSegmentLookupContext context = episodeContext();
    const QJsonObject payload{
        {QStringLiteral("intro"), QJsonObject{
            {QStringLiteral("start_sec"), 5.5},
            {QStringLiteral("end_sec"), 75.0},
            {QStringLiteral("confidence"), 0.91},
            {QStringLiteral("submission_count"), 8}
        }},
        {QStringLiteral("outro"), QJsonObject{
            {QStringLiteral("start_ms"), 3300000},
            {QStringLiteral("end_ms"), 3600000}
        }}
    };

    const QList<MediaSegmentInfo> segments = MediaSegmentProviderService::parseIntroDbSegments(payload, context);
    QCOMPARE(segments.size(), 2);
    QCOMPARE(segments.at(0).type, MediaSegmentType::Intro);
    QCOMPARE(segments.at(1).type, MediaSegmentType::Outro);
    QCOMPARE(segments.at(0).source, QStringLiteral("introdb"));
    QCOMPARE(segments.at(0).startSeconds(), 5.5);
    QCOMPARE(segments.at(0).submissionCount, 8);
}

void MediaSegmentProviderServiceTest::parsesIntroDbClockStringSegments()
{
    const MediaSegmentLookupContext context = episodeContext();
    const QJsonObject payload{
        {QStringLiteral("intro"), QJsonObject{
            {QStringLiteral("start_sec"), QStringLiteral("1:05")},
            {QStringLiteral("end_sec"), QStringLiteral("0:02:15")}
        }}
    };

    const QList<MediaSegmentInfo> segments = MediaSegmentProviderService::parseIntroDbSegments(payload, context);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.first().type, MediaSegmentType::Intro);
    QCOMPARE(segments.first().startSeconds(), 65.0);
    QCOMPARE(segments.first().endSeconds(), 135.0);
}

void MediaSegmentProviderServiceTest::dropsIntroDbInvalidStringSegments()
{
    const MediaSegmentLookupContext context = episodeContext();
    const QJsonObject payload{
        {QStringLiteral("intro"), QJsonObject{
            {QStringLiteral("start_sec"), QStringLiteral("not-a-time")},
            {QStringLiteral("end_sec"), QStringLiteral("1:00")}
        }}
    };

    const QList<MediaSegmentInfo> segments = MediaSegmentProviderService::parseIntroDbSegments(payload, context);
    QVERIFY(segments.isEmpty());
}

void MediaSegmentProviderServiceTest::mergeKeepsServerSegmentAndFillsMissingType()
{
    MediaSegmentInfo serverIntro;
    serverIntro.itemId = QStringLiteral("episode-1");
    serverIntro.type = MediaSegmentType::Intro;
    serverIntro.typeString = QStringLiteral("Intro");
    serverIntro.source = QStringLiteral("jellyfin");
    serverIntro.startMs = 10 * 1000LL;
    serverIntro.endMs = 70 * 1000LL;

    MediaSegmentInfo externalIntro = serverIntro;
    externalIntro.source = QStringLiteral("theintrodb");
    externalIntro.startMs = 30 * 1000LL;
    externalIntro.endMs = 90 * 1000LL;

    MediaSegmentInfo externalOutro;
    externalOutro.itemId = QStringLiteral("episode-1");
    externalOutro.type = MediaSegmentType::Outro;
    externalOutro.typeString = QStringLiteral("Outro");
    externalOutro.source = QStringLiteral("theintrodb");
    externalOutro.startMs = 3300 * 1000LL;
    externalOutro.endMs = 3600 * 1000LL;

    const QList<MediaSegmentInfo> merged = MediaSegmentProviderService::mergeSegmentsByType(
        {serverIntro},
        {externalIntro, externalOutro});

    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.at(0).source, QStringLiteral("jellyfin"));
    QCOMPARE(merged.at(0).startSeconds(), 10.0);
    QCOMPARE(merged.at(1).type, MediaSegmentType::Outro);
    QCOMPARE(merged.at(1).source, QStringLiteral("theintrodb"));
}

void MediaSegmentProviderServiceTest::mergeKeepsRepeatedRangesOfEachType()
{
    const auto segment = [](MediaSegmentType type, const QString &source, qint64 startSeconds,
                            qint64 endSeconds) {
        MediaSegmentInfo info;
        info.itemId = QStringLiteral("episode-1");
        info.type = type;
        info.source = source;
        info.startMs = startSeconds * 1000LL;
        info.endMs = endSeconds * 1000LL;
        return info;
    };
    // Silo can return several ranges of one type (an intro after a cold
    // open and another midway); all of them stay.
    const QList<MediaSegmentInfo> server{
        segment(MediaSegmentType::Intro, QStringLiteral("scanner"), 60, 90),
        segment(MediaSegmentType::Intro, QString(), 1300, 1330)};
    // An external provider fills only types the server lacks, with every
    // range it has of that type.
    const QList<MediaSegmentInfo> external{
        segment(MediaSegmentType::Intro, QStringLiteral("theintrodb"), 0, 30),
        segment(MediaSegmentType::Outro, QStringLiteral("theintrodb"), 1700, 1750),
        segment(MediaSegmentType::Outro, QStringLiteral("theintrodb"), 3500, 3600)};

    QList<MediaSegmentInfo> merged = MediaSegmentProviderService::mergeSegmentsByType(server, external);
    QCOMPARE(merged.size(), 4);
    QCOMPARE(merged.at(0).startMs, 60000);
    QCOMPARE(merged.at(1).startMs, 1300000);
    QCOMPARE(merged.at(2).type, MediaSegmentType::Outro);
    QCOMPARE(merged.at(3).type, MediaSegmentType::Outro);
    QCOMPARE(merged.at(3).startMs, 3500000);
    QVERIFY(MediaSegmentProviderService::hasMissingSupportedSegmentTypes(server));

    // PlaybackService merges the provider chain's result into the server
    // list again; that repeats nothing.
    merged = MediaSegmentProviderService::mergeSegmentsByType(server, merged);
    QCOMPARE(merged.size(), 4);

    // A later provider adds nothing for a type an earlier one filled.
    const QList<MediaSegmentInfo> later{
        segment(MediaSegmentType::Outro, QStringLiteral("introdb"), 3400, 3600)};
    QCOMPARE(MediaSegmentProviderService::mergeSegmentsByType(merged, later).size(), 4);
}

void MediaSegmentProviderServiceTest::emptyProviderIdsProduceNoSegments()
{
    MediaSegmentLookupContext context = episodeContext();
    context.imdbId.clear();
    context.tmdbId.clear();

    const QList<MediaSegmentInfo> segments = MediaSegmentProviderService::parseIntroDbSegments(QJsonObject{}, context);
    QVERIFY(segments.isEmpty());
}

QTEST_MAIN(MediaSegmentProviderServiceTest)

#include "MediaSegmentProviderServiceTest.moc"
