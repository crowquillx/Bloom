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
    void explicitSelectionMatchesLowercaseItemId();
    void explicitSelectionWaitsForTargetSeason();
    void explicitSelectionUsesTargetSeasonEpisode();
    void explicitSelectionDoesNotFallbackWithinTargetSeason();
    void fallbackSelectionChoosesFirstUnplayed();
    void fallbackSelectionSkipsLeadingNullEntries();
    void allNullEpisodesDoNotApplyFallback();

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
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("watched"), false},
        {QStringLiteral("positionMs"), 0}
    });
    episodes.append(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-2")},
        {QStringLiteral("watched"), false},
        {QStringLiteral("positionMs"), 0}
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

void EpisodeSelectionScriptTest::explicitSelectionMatchesLowercaseItemId()
{
    QVariantList episodes;
    episodes.append(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("parentId"), QStringLiteral("season-a")},
        {QStringLiteral("watched"), true},
        {QStringLiteral("positionMs"), 0}
    });
    episodes.append(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-2")},
        {QStringLiteral("parentId"), QStringLiteral("season-a")},
        {QStringLiteral("watched"), false},
        {QStringLiteral("positionMs"), 0}
    });

    const QJSValue result = m_resolveInitialEpisodeSelection.call(QJSValueList{
        m_engine.toScriptValue(episodes),
        QJSValue(QStringLiteral("episode-2")),
        QJSValue(QStringLiteral("season-a"))
    });
    QVERIFY2(!result.isError(), qPrintable(result.toString()));

    QCOMPARE(result.property(QStringLiteral("shouldApply")).toBool(), true);
    QCOMPARE(result.property(QStringLiteral("foundInitialEpisode")).toBool(), true);
    QCOMPARE(result.property(QStringLiteral("targetIndex")).toInt(), 1);
}

void EpisodeSelectionScriptTest::explicitSelectionWaitsForTargetSeason()
{
    QVariantList episodes;
    episodes.append(QVariant());
    episodes.append(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("parentId"), QStringLiteral("season-a")},
        {QStringLiteral("watched"), false},
        {QStringLiteral("positionMs"), 0}
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

void EpisodeSelectionScriptTest::explicitSelectionUsesTargetSeasonEpisode()
{
    QVariantList episodes;
    episodes.append(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("parentId"), QStringLiteral("season-b")},
        {QStringLiteral("watched"), false},
        {QStringLiteral("positionMs"), 0}
    });
    episodes.append(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-2")},
        {QStringLiteral("parentId"), QStringLiteral("season-b")},
        {QStringLiteral("watched"), false},
        {QStringLiteral("positionMs"), 0}
    });

    const QJSValue result = m_resolveInitialEpisodeSelection.call(QJSValueList{
        m_engine.toScriptValue(episodes),
        QJSValue(QStringLiteral("episode-2")),
        QJSValue(QStringLiteral("season-b"))
    });
    QVERIFY2(!result.isError(), qPrintable(result.toString()));

    QCOMPARE(result.property(QStringLiteral("shouldApply")).toBool(), true);
    QCOMPARE(result.property(QStringLiteral("foundInitialEpisode")).toBool(), true);
    QCOMPARE(result.property(QStringLiteral("targetIndex")).toInt(), 1);
    QCOMPARE(result.property(QStringLiteral("currentSeasonId")).toString(), QStringLiteral("season-b"));
}

void EpisodeSelectionScriptTest::explicitSelectionDoesNotFallbackWithinTargetSeason()
{
    QVariantList episodes;
    episodes.append(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("parentId"), QStringLiteral("season-a")},
        {QStringLiteral("watched"), true},
        {QStringLiteral("positionMs"), 0}
    });
    episodes.append(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-2")},
        {QStringLiteral("parentId"), QStringLiteral("season-a")},
        {QStringLiteral("watched"), false},
        {QStringLiteral("positionMs"), 0}
    });

    const QJSValue result = m_resolveInitialEpisodeSelection.call(QJSValueList{
        m_engine.toScriptValue(episodes),
        QJSValue(QStringLiteral("missing-episode")),
        QJSValue(QStringLiteral("season-a"))
    });
    QVERIFY2(!result.isError(), qPrintable(result.toString()));

    QCOMPARE(result.property(QStringLiteral("shouldApply")).toBool(), false);
    QCOMPARE(result.property(QStringLiteral("foundInitialEpisode")).toBool(), false);
    QCOMPARE(result.property(QStringLiteral("waitingForTargetSeason")).toBool(), false);
}

void EpisodeSelectionScriptTest::fallbackSelectionChoosesFirstUnplayed()
{
    QVariantList episodes;
    episodes.append(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("watched"), true},
        {QStringLiteral("positionMs"), 0}
    });
    episodes.append(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-2")},
        {QStringLiteral("watched"), false},
        {QStringLiteral("positionMs"), 0}
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

void EpisodeSelectionScriptTest::fallbackSelectionSkipsLeadingNullEntries()
{
    QVariantList episodes;
    episodes.append(QVariant());
    episodes.append(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-1")},
        {QStringLiteral("parentId"), QStringLiteral("season-a")},
        {QStringLiteral("watched"), true},
        {QStringLiteral("positionMs"), 0}
    });
    episodes.append(QVariantMap{
        {QStringLiteral("itemId"), QStringLiteral("episode-2")},
        {QStringLiteral("parentId"), QStringLiteral("season-a")},
        {QStringLiteral("watched"), false},
        {QStringLiteral("positionMs"), 0}
    });

    const QJSValue result = m_resolveInitialEpisodeSelection.call(QJSValueList{
        m_engine.toScriptValue(episodes),
        QJSValue(QStringLiteral("")),
        QJSValue(QStringLiteral("season-b"))
    });
    QVERIFY2(!result.isError(), qPrintable(result.toString()));

    QCOMPARE(result.property(QStringLiteral("waitingForTargetSeason")).toBool(), true);
    QCOMPARE(result.property(QStringLiteral("currentSeasonId")).toString(), QStringLiteral("season-a"));
    QCOMPARE(result.property(QStringLiteral("shouldApply")).toBool(), true);
    QCOMPARE(result.property(QStringLiteral("targetIndex")).toInt(), 2);
}

void EpisodeSelectionScriptTest::allNullEpisodesDoNotApplyFallback()
{
    QVariantList episodes;
    episodes.append(QVariant());
    episodes.append(QVariant());

    const QJSValue result = m_resolveInitialEpisodeSelection.call(QJSValueList{
        m_engine.toScriptValue(episodes),
        QJSValue(QStringLiteral("")),
        QJSValue(QStringLiteral("season-a"))
    });
    QVERIFY2(!result.isError(), qPrintable(result.toString()));

    QCOMPARE(result.property(QStringLiteral("shouldApply")).toBool(), false);
    QCOMPARE(result.property(QStringLiteral("waitingForTargetSeason")).toBool(), false);
    QVERIFY(result.property(QStringLiteral("currentSeasonId")).isNull());
}

QTEST_MAIN(EpisodeSelectionScriptTest)

#include "EpisodeSelectionScriptTest.moc"
