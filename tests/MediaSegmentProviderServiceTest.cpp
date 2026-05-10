#include "network/MediaSegmentProviderService.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest/QtTest>

class MediaSegmentProviderServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesTheIntroDbV2Arrays();
    void handlesNullStartAndDurationEnd();
    void dropsOpenEndedSegmentsWithoutDuration();
    void parsesIntroDbSegments();
    void mergeKeepsServerSegmentAndFillsMissingType();
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
    context.durationTicks = 3600LL * 10000000LL;
    return context;
}
}

void MediaSegmentProviderServiceTest::parsesTheIntroDbV2Arrays()
{
    const MediaSegmentLookupContext context = episodeContext();
    const QJsonObject payload{
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
    QCOMPARE(segments.at(0).type, MediaSegmentType::Intro);
    QCOMPARE(segments.at(1).type, MediaSegmentType::Recap);
    QCOMPARE(segments.at(2).type, MediaSegmentType::Outro);
    QCOMPARE(segments.at(3).type, MediaSegmentType::Preview);
    QCOMPARE(segments.at(0).source, QStringLiteral("theintrodb"));
    QCOMPARE(segments.at(0).startSeconds(), 30.0);
    QCOMPARE(segments.at(0).endSeconds(), 90.0);
}

void MediaSegmentProviderServiceTest::handlesNullStartAndDurationEnd()
{
    const MediaSegmentLookupContext context = episodeContext();
    const QJsonObject payload{
        {QStringLiteral("credits"), QJsonArray{QJsonObject{
            {QStringLiteral("start_ms"), QJsonValue::Null},
            {QStringLiteral("end_ms"), QJsonValue::Null}
        }}}
    };

    const QList<MediaSegmentInfo> segments = MediaSegmentProviderService::parseTheIntroDbSegments(payload, context);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.first().type, MediaSegmentType::Outro);
    QCOMPARE(segments.first().startSeconds(), 0.0);
    QCOMPARE(segments.first().endSeconds(), 3600.0);
}

void MediaSegmentProviderServiceTest::dropsOpenEndedSegmentsWithoutDuration()
{
    MediaSegmentLookupContext context = episodeContext();
    context.durationTicks = 0;
    const QJsonObject payload{
        {QStringLiteral("credits"), QJsonArray{QJsonObject{
            {QStringLiteral("start_ms"), 1000},
            {QStringLiteral("end_ms"), QJsonValue::Null}
        }}}
    };

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

void MediaSegmentProviderServiceTest::mergeKeepsServerSegmentAndFillsMissingType()
{
    MediaSegmentInfo serverIntro;
    serverIntro.itemId = QStringLiteral("episode-1");
    serverIntro.type = MediaSegmentType::Intro;
    serverIntro.typeString = QStringLiteral("Intro");
    serverIntro.source = QStringLiteral("jellyfin");
    serverIntro.startTicks = 10 * 10000000LL;
    serverIntro.endTicks = 70 * 10000000LL;

    MediaSegmentInfo externalIntro = serverIntro;
    externalIntro.source = QStringLiteral("theintrodb");
    externalIntro.startTicks = 30 * 10000000LL;
    externalIntro.endTicks = 90 * 10000000LL;

    MediaSegmentInfo externalOutro;
    externalOutro.itemId = QStringLiteral("episode-1");
    externalOutro.type = MediaSegmentType::Outro;
    externalOutro.typeString = QStringLiteral("Outro");
    externalOutro.source = QStringLiteral("theintrodb");
    externalOutro.startTicks = 3300 * 10000000LL;
    externalOutro.endTicks = 3600 * 10000000LL;

    const QList<MediaSegmentInfo> merged = MediaSegmentProviderService::mergeSegmentsByType(
        {serverIntro},
        {externalIntro, externalOutro});

    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.at(0).source, QStringLiteral("jellyfin"));
    QCOMPARE(merged.at(0).startSeconds(), 10.0);
    QCOMPARE(merged.at(1).type, MediaSegmentType::Outro);
    QCOMPARE(merged.at(1).source, QStringLiteral("theintrodb"));
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
