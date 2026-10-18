#include <QtTest/QtTest>

#include "models/AudioTypes.h"
#include "player/AudioPolicy.h"

using namespace Bloom;

class AudioPolicyTest final : public QObject
{
    Q_OBJECT

private slots:
    void passthroughCodecMatrix_data();
    void passthroughCodecMatrix();
    void pcmPlansPreserveSourceLayout();
    void passthroughPreferencesAndEndpointAvailability();
    void decodeSupportDoesNotRequirePassthrough();
    void dtsHdDoesNotFallBackToDtsCore();
    void mpvArgumentsKeepAutomaticChannelLayout();
    void verifyUsesRuntimeOutputFormat();
    void verifyReportsPcmNegotiationChanges();
    void selectedSourceUsesSelectedTrackAndDefaultFallback();
};

void AudioPolicyTest::passthroughCodecMatrix_data()
{
    QTest::addColumn<QString>("codec");
    QTest::addColumn<QString>("profile");
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("spatialFormat");
    QTest::addColumn<QString>("transport");
    QTest::addColumn<bool>("available");
    QTest::addColumn<bool>("atmos");
    QTest::addColumn<bool>("dtsX");

    const auto addCodec = [](const char *name, const QString &codec, const QString &profile,
                              const QString &title, const QString &spatialFormat,
                              const QString &transport, bool atmos, bool dtsX) {
        QTest::newRow((QByteArray(name) + "-available").constData())
            << codec << profile << title << spatialFormat << transport << true << atmos << dtsX;
        QTest::newRow((QByteArray(name) + "-unavailable").constData())
            << codec << profile << title << spatialFormat << transport << false << atmos << dtsX;
    };

    addCodec("ac3", "ac3", {}, {}, {}, "ac3", false, false);
    addCodec("eac3", "eac3", {}, {}, {}, "eac3", false, false);
    addCodec("truehd", "truehd", {}, {}, {}, "truehd", false, false);
    addCodec("eac3-joc", "eac3", "E-AC-3 JOC", {}, {}, "eac3", true, false);
    addCodec("eac3-atmos", "eac3", {}, {}, "DolbyAtmos", "eac3", true, false);
    addCodec("truehd-atmos", "truehd", {}, {}, "DolbyAtmos", "truehd", true, false);
    addCodec("dts-core", "dts", {}, {}, {}, "dts", false, false);
    addCodec("dts-hd-ma", "dts", "DTS-HD MA", {}, {}, "dts-hd", false, false);
    addCodec("dts-hd-hra", "dts", "DTS-HD HRA", {}, {}, "dts-hd", false, false);
    addCodec("dtsx", "dts", "DTS:X", {}, {}, "dts-hd", false, true);
}

void AudioPolicyTest::passthroughCodecMatrix()
{
    QFETCH(QString, codec);
    QFETCH(QString, profile);
    QFETCH(QString, title);
    QFETCH(QString, spatialFormat);
    QFETCH(QString, transport);
    QFETCH(bool, available);
    QFETCH(bool, atmos);
    QFETCH(bool, dtsX);

    QVariantMap sourceMap{{QStringLiteral("codec"), codec},
                          {QStringLiteral("profile"), profile},
                          {QStringLiteral("title"), title},
                          {QStringLiteral("audioSpatialFormat"), spatialFormat}};
    const AudioSourceMetadata source = AudioSourceMetadata::fromVariantMap(sourceMap);
    AudioOutputCapabilities output;
    output.passthroughCodecs = available ? QStringList{transport} : QStringList{};
    output.evidence = QStringLiteral("reported");

    const AudioOutputPlan result = AudioPolicy::plan(source, output, {});
    QCOMPARE(AudioPolicy::transportCodec(source), transport);
    QCOMPARE(result.path, available ? QStringLiteral("passthrough") : QStringLiteral("pcm"));
    QCOMPARE(result.passthroughCodec, available ? transport : QString());
    QCOMPARE(result.atmosRetained, available && atmos);
    QCOMPARE(result.dtsXRetained, available && dtsX);
    QVERIFY(result.localDecodeSupported);
    QVERIFY(!result.providerConversionRequired);
}

void AudioPolicyTest::pcmPlansPreserveSourceLayout()
{
    const struct Case {
        QString codec;
        int channels;
        QString layout;
        QString fidelity;
    } cases[] = {
        {QStringLiteral("aac"), 2, QStringLiteral("stereo"), QStringLiteral("lossy")},
        {QStringLiteral("aac"), 6, QStringLiteral("5.1"), QStringLiteral("lossy")},
        {QStringLiteral("flac"), 2, QStringLiteral("stereo"), QStringLiteral("lossless")},
        {QStringLiteral("flac"), 6, QStringLiteral("5.1"), QStringLiteral("lossless")}
    };

    for (const Case &entry : cases) {
        const AudioSourceMetadata source = AudioSourceMetadata::fromVariantMap({
            {QStringLiteral("codec"), entry.codec},
            {QStringLiteral("channels"), entry.channels},
            {QStringLiteral("channelLayout"), entry.layout}
        });
        AudioOutputCapabilities output;
        const AudioOutputPlan result = AudioPolicy::plan(source, output, {});
        QCOMPARE(source.fidelity, entry.fidelity);
        QCOMPARE(result.path, QStringLiteral("pcm"));
        QCOMPARE(result.pcmChannels, entry.channels);
        QCOMPARE(result.pcmLayout, entry.layout);
        QVERIFY(result.localDecodeSupported);
        QVERIFY(!result.providerConversionRequired);
    }
}

void AudioPolicyTest::passthroughPreferencesAndEndpointAvailability()
{
    const AudioSourceMetadata source = AudioSourceMetadata::fromVariantMap({
        {QStringLiteral("codec"), QStringLiteral("ac3")}
    });
    AudioOutputCapabilities output;
    output.passthroughCodecs = {QStringLiteral("ac3")};
    output.evidence = QStringLiteral("reported");

    AudioPreferences preferences;
    preferences.mode = AudioPassthroughMode::Disabled;
    QCOMPARE(AudioPolicy::plan(source, output, preferences).path, QStringLiteral("pcm"));

    preferences.mode = AudioPassthroughMode::Custom;
    preferences.customCodecs = {QStringLiteral("ac3")};
    output.passthroughCodecs.clear();
    output.evidence = QStringLiteral("unknown");
    QCOMPARE(AudioPolicy::plan(source, output, preferences).path, QStringLiteral("passthrough"));

    preferences.customCodecs = {QStringLiteral("eac3")};
    const AudioOutputPlan excluded = AudioPolicy::plan(source, output, preferences);
    QCOMPARE(excluded.path, QStringLiteral("pcm"));
    QCOMPARE(excluded.reason, QStringLiteral("Encoded format unavailable or excluded; decode original audio locally"));

    preferences.mode = AudioPassthroughMode::Auto;
    output.passthroughCodecs = {QStringLiteral("ac3")};
    output.endpointAvailable = false;
    QCOMPARE(AudioPolicy::plan(source, output, preferences).path, QStringLiteral("pcm"));

    output.endpointAvailable = true;
    output.evidence = QStringLiteral("unknown");
    output.passthroughCodecs.clear();
    const AudioOutputPlan unknownEndpoint = AudioPolicy::plan(source, output, preferences);
    QCOMPARE(unknownEndpoint.path, QStringLiteral("pcm"));
    QVERIFY(!unknownEndpoint.providerConversionRequired);
    QVERIFY(unknownEndpoint.reason.contains(QStringLiteral("support unknown")));
}

void AudioPolicyTest::decodeSupportDoesNotRequirePassthrough()
{
    AudioOutputCapabilities output;
    output.evidence = QStringLiteral("unknown");
    for (const QString &codec : AudioPolicy::localDecodeCodecs()) {
        const AudioSourceMetadata source = AudioSourceMetadata::fromVariantMap({
            {QStringLiteral("codec"), codec}
        });
        const AudioOutputPlan result = AudioPolicy::plan(source, output, {});
        QVERIFY2(result.localDecodeSupported, qPrintable(codec));
        QVERIFY2(!result.providerConversionRequired, qPrintable(codec));
        QCOMPARE(result.path, QStringLiteral("pcm"));
    }

    const AudioSourceMetadata unknown = AudioSourceMetadata::fromVariantMap({
        {QStringLiteral("codec"), QStringLiteral("made-up-codec")}
    });
    const AudioOutputPlan unsupported = AudioPolicy::plan(unknown, output, {});
    QVERIFY(!unsupported.localDecodeSupported);
    QVERIFY(unsupported.providerConversionRequired);

    const AudioSourceMetadata absent;
    const AudioOutputPlan noCodec = AudioPolicy::plan(absent, output, {});
    QVERIFY(!noCodec.localDecodeSupported);
    QVERIFY(!noCodec.providerConversionRequired);
}

void AudioPolicyTest::dtsHdDoesNotFallBackToDtsCore()
{
    const AudioSourceMetadata source = AudioSourceMetadata::fromVariantMap({
        {QStringLiteral("codec"), QStringLiteral("dts")},
        {QStringLiteral("profile"), QStringLiteral("DTS-HD MA")}
    });
    AudioOutputCapabilities output;
    output.passthroughCodecs = {QStringLiteral("dts")};
    const AudioOutputPlan result = AudioPolicy::plan(source, output, {});
    QCOMPARE(AudioPolicy::transportCodec(source), QStringLiteral("dts-hd"));
    QCOMPARE(result.path, QStringLiteral("pcm"));
    QVERIFY(result.localDecodeSupported);
    QVERIFY(!result.providerConversionRequired);
}

void AudioPolicyTest::mpvArgumentsKeepAutomaticChannelLayout()
{
    const AudioOutputPlan plan;
    const QStringList args = AudioPolicy::mpvArguments(plan);
    QVERIFY(args.contains(QStringLiteral("--audio-spdif=")));
    QVERIFY(args.contains(QStringLiteral("--audio-channels=auto-safe")));
    QVERIFY(!args.contains(QStringLiteral("--audio-channels=5.1")));
    QVERIFY(!args.contains(QStringLiteral("--audio-channels=7.1")));
}

void AudioPolicyTest::verifyUsesRuntimeOutputFormat()
{
    AudioOutputPlan plan;
    plan.path = QStringLiteral("passthrough");
    plan.passthroughCodec = QStringLiteral("ac3");
    plan.atmosRetained = true;

    QVariantMap runtime{{QStringLiteral("current-ao"), QStringLiteral("alsa")},
                        {QStringLiteral("audio-codec-name"), QStringLiteral("truehd")},
                        {QStringLiteral("audio-out-params"),
                         QVariantMap{{QStringLiteral("format"), QStringLiteral("spdif-ac3")}}}};
    QVariantMap result = AudioPolicy::verify(plan, runtime);
    QVERIFY(result.value(QStringLiteral("outputObservable")).toBool());
    QVERIFY(result.value(QStringLiteral("passthroughActive")).toBool());
    QCOMPARE(result.value(QStringLiteral("encodedFormat")).toString(), QStringLiteral("spdif-ac3"));
    QVERIFY(result.value(QStringLiteral("atmosRetained")).toBool());

    runtime[QStringLiteral("audio-out-params")] =
        QVariantMap{{QStringLiteral("format"), QStringLiteral("spdif-eac3")}};
    result = AudioPolicy::verify(plan, runtime);
    QVERIFY(result.value(QStringLiteral("passthroughActive")).toBool());
    QVERIFY(!result.value(QStringLiteral("atmosRetained")).toBool());
    QVERIFY(result.value(QStringLiteral("needsPcmFallback")).toBool());

    plan.passthroughCodec = QStringLiteral("dts-hd");
    plan.dtsXRetained = true;
    runtime[QStringLiteral("audio-out-params")] =
        QVariantMap{{QStringLiteral("format"), QStringLiteral("spdif-dtshd")}};
    result = AudioPolicy::verify(plan, runtime);
    QCOMPARE(result.value(QStringLiteral("encodedFormat")).toString(), QStringLiteral("spdif-dtshd"));
    QVERIFY(!result.value(QStringLiteral("needsPcmFallback")).toBool());
    QVERIFY(result.value(QStringLiteral("dtsXRetained")).toBool());

    runtime[QStringLiteral("current-ao")] = QStringLiteral("null");
    result = AudioPolicy::verify(plan, runtime);
    QVERIFY(!result.value(QStringLiteral("passthroughActive")).toBool());
    QVERIFY(result.value(QStringLiteral("needsPcmFallback")).toBool());

    runtime = {{QStringLiteral("current-ao"), QStringLiteral("alsa")}};
    result = AudioPolicy::verify(plan, runtime);
    QVERIFY(!result.value(QStringLiteral("outputObservable")).toBool());
    QVERIFY(!result.value(QStringLiteral("passthroughActive")).toBool());
    QVERIFY(!result.value(QStringLiteral("differsFromPlan")).toBool());
}

void AudioPolicyTest::verifyReportsPcmNegotiationChanges()
{
    auto source = AudioSourceMetadata::fromVariantMap({{"codec", "flac"}, {"channels", 6}, {"sampleRate", 96000}});
    const auto planned = AudioPolicy::plan(source, {}, {});
    auto result = AudioPolicy::verify(planned, {{"current-ao", "pipewire"},
        {"audio-out-params", QVariantMap{{"format", "float"}, {"channel-count", 2}, {"samplerate", 48000}}}});
    QVERIFY(result.value("differsFromPlan").toBool());
    QVERIFY(result.value("pcmChannelCountChanged").toBool());
    QVERIFY(result.value("pcmSampleRateChanged").toBool());
    QVERIFY(!result.value("needsPcmFallback").toBool());
    QVERIFY(!planned.providerConversionRequired);
}

void AudioPolicyTest::selectedSourceUsesSelectedTrackAndDefaultFallback()
{
    const QVariantMap source{{QStringLiteral("mediaStreams"), QVariantList{
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")},
                    {QStringLiteral("index"), 1},
                    {QStringLiteral("codec"), QStringLiteral("aac")},
                    {QStringLiteral("channels"), 2}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Audio")},
                    {QStringLiteral("index"), 4},
                    {QStringLiteral("codec"), QStringLiteral("flac")},
                    {QStringLiteral("channels"), 6},
                    {QStringLiteral("channelLayout"), QStringLiteral("5.1")},
                    {QStringLiteral("sampleRate"), 96000},
                    {QStringLiteral("isDefault"), true}},
        QVariantMap{{QStringLiteral("type"), QStringLiteral("Subtitle")},
                    {QStringLiteral("index"), 8},
                    {QStringLiteral("codec"), QStringLiteral("ass")}}
    }}};

    const AudioSourceMetadata selected = AudioPolicy::selectedSource(source, 1);
    QCOMPARE(selected.codec, QStringLiteral("aac"));
    QCOMPARE(selected.channels, 2);

    const AudioSourceMetadata fallback = AudioPolicy::selectedSource(source, 99);
    QCOMPARE(fallback.codec, QStringLiteral("flac"));
    QCOMPARE(fallback.channels, 6);
    QCOMPARE(fallback.channelLayout, QStringLiteral("5.1"));
    QCOMPARE(fallback.sampleRate, 96000);

    const AudioSourceMetadata none = AudioPolicy::selectedSource({}, 1);
    QCOMPARE(none.fidelity, QStringLiteral("unknown"));
    QVERIFY(none.codec.isEmpty());
}

QTEST_GUILESS_MAIN(AudioPolicyTest)
#include "AudioPolicyTest.moc"
