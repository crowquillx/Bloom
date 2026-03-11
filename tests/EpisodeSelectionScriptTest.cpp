#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QJSEngine>
#include <QJSValue>

class EpisodeSelectionScriptTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void explicitSelectionMustExist();
    void explicitSelectionWaitsForTargetSeason();
    void fallbackSelectionChoosesFirstUnplayed();

private:
    QJSEngine m_engine;
    QJSValue m_resolveInitialEpisodeSelection;
};

void EpisodeSelectionScriptTest::initTestCase()
{
    QFile scriptFile(QCoreApplication::applicationDirPath()
                     + QLatin1Char('/')
                     + QString::fromLatin1(EPISODE_SELECTION_SCRIPT_PATH));
    QVERIFY2(scriptFile.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to open EpisodeSelection.js");

    const QJSValue evalResult = m_engine.evaluate(QString::fromUtf8(scriptFile.readAll()),
                                                QStringLiteral("EpisodeSelection.js"));
    QVERIFY2(!evalResult.isError(), qPrintable(evalResult.toString()));

    m_resolveInitialEpisodeSelection =
        m_engine.globalObject().property(QStringLiteral("resolveInitialEpisodeSelection"));
    QVERIFY(m_resolveInitialEpisodeSelection.isCallable());
}

void EpisodeSelectionScriptTest::explicitSelectionMustExist()
{

    QVariantList episodes;
    episodes.append(QVariantMap{
        {QStringLiteral("Id"), QStringLiteral("episode-1")},
        {QStringLiteral("isPlayed"), false},
        {QStringLiteral("playbackPositionTicks"), 0}
    });
    episodes.append(QVariantMap{
        {QStringLiteral("Id"), QStringLiteral("episode-2")},
        {QStringLiteral("isPlayed"), false},
        {QStringLiteral("playbackPositionTicks"), 0}
    });

    const QJSValueList args{
        m_engine.toScriptValue(episodes),
        QJSValue(QStringLiteral("missing-episode"))
    };
    const QJSValue result = m_resolveInitialEpisodeSelection.call(args);
    QVERIFY2(!result.isError(), qPrintable(result.toString()));

    QCOMPARE(result.property(QStringLiteral("shouldApply")).toBool(), false);
    QCOMPARE(result.property(QStringLiteral("foundInitialEpisode")).toBool(), false);
}

void EpisodeSelectionScriptTest::explicitSelectionWaitsForTargetSeason()
{
    QVariantList episodes;
    episodes.append(QVariantMap{
        {QStringLiteral("Id"), QStringLiteral("episode-1")},
        {QStringLiteral("ParentId"), QStringLiteral("season-a")},
        {QStringLiteral("isPlayed"), false},
        {QStringLiteral("playbackPositionTicks"), 0}
    });

    const QJSValue result = m_resolveInitialEpisodeSelection.call(QJSValueList{
        m_engine.toScriptValue(episodes),
        QJSValue(QStringLiteral("missing-episode")),
        QJSValue(QStringLiteral("season-b"))
    });
    QVERIFY2(!result.isError(), qPrintable(result.toString()));

    QCOMPARE(result.property(QStringLiteral("shouldApply")).toBool(), false);
    QCOMPARE(result.property(QStringLiteral("waitingForTargetSeason")).toBool(), true);
    QCOMPARE(result.property(QStringLiteral("currentSeasonId")).toString(), QStringLiteral("season-a"));
}

void EpisodeSelectionScriptTest::fallbackSelectionChoosesFirstUnplayed()
{
    QVariantList episodes;
    episodes.append(QVariantMap{
        {QStringLiteral("Id"), QStringLiteral("episode-1")},
        {QStringLiteral("UserData"), QVariantMap{
            {QStringLiteral("Played"), true},
            {QStringLiteral("PlaybackPositionTicks"), 0}
        }}
    });
    episodes.append(QVariantMap{
        {QStringLiteral("Id"), QStringLiteral("episode-2")},
        {QStringLiteral("UserData"), QVariantMap{
            {QStringLiteral("Played"), false},
            {QStringLiteral("PlaybackPositionTicks"), 0}
        }}
    });

    const QJSValue result = m_resolveInitialEpisodeSelection.call(QJSValueList{
        m_engine.toScriptValue(episodes),
        QJSValue(QStringLiteral("")),
        QJSValue(QStringLiteral(""))
    });
    QVERIFY2(!result.isError(), qPrintable(result.toString()));

    QCOMPARE(result.property(QStringLiteral("shouldApply")).toBool(), true);
    QCOMPARE(result.property(QStringLiteral("usedFallback")).toBool(), true);
    QCOMPARE(result.property(QStringLiteral("targetIndex")).toInt(), 1);
}

QTEST_MAIN(EpisodeSelectionScriptTest)

#include "EpisodeSelectionScriptTest.moc"
