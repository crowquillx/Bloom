#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QJSValue>

namespace {

QString scriptPath()
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();  // build-docker
    dir.cdUp();  // repo root
    return dir.filePath(QStringLiteral("src/ui/EpisodeSelection.js"));
}

}  // namespace

class EpisodeSelectionScriptTest : public QObject
{
    Q_OBJECT

private slots:
    void explicitSelectionMustExist();
    void fallbackSelectionChoosesFirstUnplayed();
};

void EpisodeSelectionScriptTest::explicitSelectionMustExist()
{
    QFile scriptFile(scriptPath());
    QVERIFY2(scriptFile.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to open EpisodeSelection.js");

    QJSEngine engine;
    const QJSValue evalResult = engine.evaluate(QString::fromUtf8(scriptFile.readAll()),
                                                QStringLiteral("EpisodeSelection.js"));
    QVERIFY2(!evalResult.isError(), qPrintable(evalResult.toString()));

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

    const QJSValue function = engine.globalObject().property(QStringLiteral("resolveInitialEpisodeSelection"));
    QVERIFY(function.isCallable());

    const QJSValueList args{
        engine.toScriptValue(episodes),
        QJSValue(QStringLiteral("missing-episode"))
    };
    const QJSValue result = function.call(args);
    QVERIFY2(!result.isError(), qPrintable(result.toString()));

    QCOMPARE(result.property(QStringLiteral("shouldApply")).toBool(), false);
    QCOMPARE(result.property(QStringLiteral("foundInitialEpisode")).toBool(), false);
}

void EpisodeSelectionScriptTest::fallbackSelectionChoosesFirstUnplayed()
{
    QFile scriptFile(scriptPath());
    QVERIFY2(scriptFile.open(QIODevice::ReadOnly | QIODevice::Text), "Failed to open EpisodeSelection.js");

    QJSEngine engine;
    const QJSValue evalResult = engine.evaluate(QString::fromUtf8(scriptFile.readAll()),
                                                QStringLiteral("EpisodeSelection.js"));
    QVERIFY2(!evalResult.isError(), qPrintable(evalResult.toString()));

    QVariantList episodes;
    episodes.append(QVariantMap{
        {QStringLiteral("Id"), QStringLiteral("episode-1")},
        {QStringLiteral("isPlayed"), true},
        {QStringLiteral("playbackPositionTicks"), 0}
    });
    episodes.append(QVariantMap{
        {QStringLiteral("Id"), QStringLiteral("episode-2")},
        {QStringLiteral("isPlayed"), false},
        {QStringLiteral("playbackPositionTicks"), 0}
    });

    const QJSValue function = engine.globalObject().property(QStringLiteral("resolveInitialEpisodeSelection"));
    QVERIFY(function.isCallable());

    const QJSValue result = function.call(QJSValueList{
        engine.toScriptValue(episodes),
        QJSValue(QStringLiteral(""))
    });
    QVERIFY2(!result.isError(), qPrintable(result.toString()));

    QCOMPARE(result.property(QStringLiteral("shouldApply")).toBool(), true);
    QCOMPARE(result.property(QStringLiteral("usedFallback")).toBool(), true);
    QCOMPARE(result.property(QStringLiteral("targetIndex")).toInt(), 1);
}

QTEST_MAIN(EpisodeSelectionScriptTest)

#include "EpisodeSelectionScriptTest.moc"
