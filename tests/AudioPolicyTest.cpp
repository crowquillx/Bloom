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
    void additionalDecodeFormatsKeepAccurateFidelity();
    void dtsHdDoesNotFallBackToDtsCore();
    void mpvArgumentsKeepAutomaticChannelLayout();
    void verifyUsesRuntimeOutputFormat();
    void verifyReportsPcmNegotiationChanges();
    void selectedSourceUsesSelectedTrackAndDefaultFallback();
    void exclusivePlanFollowsPreferenceAndDriver();
    void verifyReportsExclusiveState();
    void resolveDeviceKeepsPreferenceAndFallsBack();
    void outputOptionsFilterToActiveDriver();
    void activeDriverPrefersRuntimeOutput();
    void parseDeviceHelpReadsMpvListing();
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
    const QStringList regressionCodecs{
        QStringLiteral("wmav2"), QStringLiteral("wmapro"), QStringLiteral("wmalossless"),
        QStringLiteral("mlp"), QStringLiteral("pcm_bluray"), QStringLiteral("pcm_dvd"),
        QStringLiteral("pcm_alaw"), QStringLiteral("pcm_s64le")
    };
    const QStringList advertised = AudioPolicy::localDecodeCodecs();
    for (const QString &codec : regressionCodecs) {
        QVERIFY2(advertised.contains(codec), qPrintable(codec));
        const AudioSourceMetadata source = AudioSourceMetadata::fromVariantMap({
            {QStringLiteral("codec"), codec}
        });
        const AudioOutputPlan result = AudioPolicy::plan(source, output, {});
        QVERIFY2(result.localDecodeSupported, qPrintable(codec));
        QVERIFY2(!result.providerConversionRequired, qPrintable(codec));
        QCOMPARE(result.path, QStringLiteral("pcm"));
    }

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

void AudioPolicyTest::additionalDecodeFormatsKeepAccurateFidelity()
{
    QCOMPARE(AudioSourceMetadata::fromVariantMap({{"codec", "wmalossless"}}).fidelity, QStringLiteral("lossless"));
    QCOMPARE(AudioSourceMetadata::fromVariantMap({{"codec", "wmapro"}}).fidelity, QStringLiteral("lossy"));
    QCOMPARE(AudioSourceMetadata::fromVariantMap({{"codec", "pcm_alaw"}}).fidelity, QStringLiteral("lossy"));
    QCOMPARE(AudioSourceMetadata::fromVariantMap({{"codec", "pcm_mulaw"}}).fidelity, QStringLiteral("lossy"));
    QCOMPARE(AudioSourceMetadata::fromVariantMap({{"codec", "pcm_s24le"}}).fidelity, QStringLiteral("lossless"));
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

void AudioPolicyTest::exclusivePlanFollowsPreferenceAndDriver()
{
    const auto source = [](int channels, const QString &layout) {
        return AudioSourceMetadata::fromVariantMap({{QStringLiteral("codec"), QStringLiteral("flac")},
                                                    {QStringLiteral("channels"), channels},
                                                    {QStringLiteral("channelLayout"), layout}});
    };
    AudioOutputCapabilities wasapi;
    wasapi.exclusiveModeSupported = true;
    wasapi.evidence = QStringLiteral("wasapi-exclusive-format-query");
    AudioPreferences exclusive;
    exclusive.exclusive = true;

    // Stereo stays stereo in exclusive mode: no upmix is requested.
    auto result = AudioPolicy::plan(source(2, QStringLiteral("stereo")), wasapi, exclusive);
    QVERIFY(result.exclusivePcm);
    QCOMPARE(result.mpvChannels, QStringLiteral("stereo"));
    QVERIFY(AudioPolicy::mpvArguments(result).contains(QStringLiteral("--audio-exclusive=yes")));

    // Unknown endpoint layouts let exclusive negotiation pick an accepted one;
    // auto-safe would force stereo without the system mixer.
    result = AudioPolicy::plan(source(6, QStringLiteral("5.1")), wasapi, exclusive);
    QCOMPARE(result.mpvChannels, QStringLiteral("auto"));

    wasapi.pcmLayouts = {QStringLiteral("stereo"), QStringLiteral("5.1")};
    result = AudioPolicy::plan(source(6, QStringLiteral("5.1")), wasapi, exclusive);
    QCOMPARE(result.mpvChannels, QStringLiteral("5.1,stereo"));

    // Shared mode follows the system mixer layout even when exclusive formats are known.
    result = AudioPolicy::plan(source(6, QStringLiteral("5.1")), wasapi, {});
    QVERIFY(!result.exclusivePcm);
    QCOMPARE(result.mpvChannels, QStringLiteral("auto-safe"));
    QVERIFY(AudioPolicy::mpvArguments(result).contains(QStringLiteral("--audio-exclusive=no")));

    // Drivers without exclusive support (PipeWire, PulseAudio, ALSA) ignore the preference.
    AudioOutputCapabilities pipewire;
    result = AudioPolicy::plan(source(8, QStringLiteral("7.1")), pipewire, exclusive);
    QVERIFY(!result.exclusivePcm);
    QCOMPARE(result.mpvChannels, QStringLiteral("auto-safe"));

    AudioOutputCapabilities missing = wasapi;
    missing.endpointAvailable = false;
    QVERIFY(!AudioPolicy::plan(source(2, QStringLiteral("stereo")), missing, exclusive).exclusivePcm);
}

void AudioPolicyTest::verifyReportsExclusiveState()
{
    AudioOutputPlan plan;
    plan.exclusivePcm = true;
    const QVariantMap pcm{{QStringLiteral("format"), QStringLiteral("s32")},
                          {QStringLiteral("channel-count"), 2}};

    auto result = AudioPolicy::verify(plan, {{QStringLiteral("current-ao"), QStringLiteral("wasapi")},
                                             {QStringLiteral("audio-exclusive"), true},
                                             {QStringLiteral("audio-out-params"), pcm}});
    QVERIFY(result.value(QStringLiteral("exclusiveActive")).toBool());
    QVERIFY(!result.value(QStringLiteral("exclusiveFailed")).toBool());

    // An option changed behind Bloom's back (script/profile) is not reported as exclusive.
    result = AudioPolicy::verify(plan, {{QStringLiteral("current-ao"), QStringLiteral("wasapi")},
                                        {QStringLiteral("audio-exclusive"), false},
                                        {QStringLiteral("audio-out-params"), pcm}});
    QVERIFY(!result.value(QStringLiteral("exclusiveActive")).toBool());

    // Without an open output there is no evidence either way.
    result = AudioPolicy::verify(plan, {{QStringLiteral("current-ao"), QStringLiteral("wasapi")},
                                        {QStringLiteral("audio-exclusive"), true}});
    QVERIFY(!result.value(QStringLiteral("exclusiveActive")).toBool());
    QVERIFY(!result.value(QStringLiteral("exclusiveFailed")).toBool());

    result = AudioPolicy::verify(plan, {{QStringLiteral("current-ao"), QStringLiteral("null")},
                                        {QStringLiteral("audio-exclusive"), true},
                                        {QStringLiteral("audio-out-params"), pcm}});
    QVERIFY(!result.value(QStringLiteral("exclusiveActive")).toBool());
    QVERIFY(result.value(QStringLiteral("exclusiveFailed")).toBool());

    // Encoded carriers on WASAPI always open exclusively.
    AudioOutputPlan passthrough;
    passthrough.path = QStringLiteral("passthrough");
    passthrough.passthroughCodec = QStringLiteral("ac3");
    result = AudioPolicy::verify(passthrough, {{QStringLiteral("current-ao"), QStringLiteral("wasapi")},
                                               {QStringLiteral("audio-out-params"),
                                                QVariantMap{{QStringLiteral("format"), QStringLiteral("spdif-ac3")}}}});
    QVERIFY(result.value(QStringLiteral("exclusiveActive")).toBool());
    QVERIFY(!result.value(QStringLiteral("exclusiveRequested")).toBool());
    QVERIFY(AudioPolicy::diagnosticProperties().contains(QStringLiteral("audio-exclusive")));
}

void AudioPolicyTest::resolveDeviceKeepsPreferenceAndFallsBack()
{
    const auto device = [](const QString &name, const QString &description) {
        return QVariant(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("description"), description}});
    };
    const QVariantList list{device(QStringLiteral("auto"), QStringLiteral("Autoselect device")),
                            device(QStringLiteral("wasapi/{a}"), QStringLiteral("Speakers")),
                            device(QStringLiteral("wasapi/{b}"), QStringLiteral("TV")),
                            device(QStringLiteral("wasapi/{c}"), QStringLiteral("Headset")),
                            device(QStringLiteral("wasapi/{d}"), QStringLiteral("Headset"))};

    auto result = AudioPolicy::resolveDevice(QStringLiteral("auto"), {}, list, true);
    QCOMPARE(result.device, QStringLiteral("auto"));
    QVERIFY(result.preferredAvailable);

    result = AudioPolicy::resolveDevice(QStringLiteral("wasapi/{b}"), QStringLiteral("TV"), list, true);
    QCOMPARE(result.device, QStringLiteral("wasapi/{b}"));
    QCOMPARE(result.description, QStringLiteral("TV"));
    QVERIFY(result.preferredAvailable);

    // Before any list is known, the preference is passed through unchanged.
    result = AudioPolicy::resolveDevice(QStringLiteral("wasapi/{x}"), {}, {}, false);
    QCOMPARE(result.device, QStringLiteral("wasapi/{x}"));

    // A missing device keeps the preference but plays on the system default.
    result = AudioPolicy::resolveDevice(QStringLiteral("wasapi/{x}"), QStringLiteral("Receiver"), list, true);
    QCOMPARE(result.preferred, QStringLiteral("wasapi/{x}"));
    QCOMPARE(result.device, QStringLiteral("auto"));
    QVERIFY(!result.preferredAvailable);

    // A changed id is re-matched only by a unique, identical label under the same driver.
    result = AudioPolicy::resolveDevice(QStringLiteral("wasapi/{old}"), QStringLiteral("TV"), list, true);
    QCOMPARE(result.device, QStringLiteral("wasapi/{b}"));
    QVERIFY(result.rematched);
    QVERIFY(result.preferredAvailable);
    result = AudioPolicy::resolveDevice(QStringLiteral("wasapi/{old}"), QStringLiteral("Headset"), list, true);
    QCOMPARE(result.device, QStringLiteral("auto"));
    result = AudioPolicy::resolveDevice(QStringLiteral("pipewire/old"), QStringLiteral("TV"), list, true);
    QCOMPARE(result.device, QStringLiteral("auto"));

    // An id from another platform's driver never reaches mpv.
    result = AudioPolicy::resolveDevice(QStringLiteral("pulse/alsa_output.usb"), {}, list, true);
    QCOMPARE(result.device, QStringLiteral("auto"));
    QVERIFY(!result.preferredAvailable);

    result = AudioPolicy::resolveDevice(QStringLiteral("wasapi/{b}"), QStringLiteral("TV"), list, true,
                                        {QStringLiteral("wasapi/{b}")});
    QCOMPARE(result.device, QStringLiteral("auto"));
    QVERIFY(!result.preferredAvailable);
}

void AudioPolicyTest::outputOptionsFilterToActiveDriver()
{
    const auto device = [](const QString &name, const QString &description) {
        return QVariant(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("description"), description}});
    };
    const QVariantList list{device(QStringLiteral("auto"), QStringLiteral("Autoselect device")),
                            device(QStringLiteral("pipewire"), QStringLiteral("Default (pipewire)")),
                            device(QStringLiteral("pipewire/dac"), QStringLiteral("USB DAC")),
                            device(QStringLiteral("pipewire/hdmi"), QStringLiteral("HDMI")),
                            device(QStringLiteral("pulse/dac"), QStringLiteral("USB DAC")),
                            device(QStringLiteral("alsa/hw:CARD=DAC"), QStringLiteral("DAC raw"))};

    auto resolution = AudioPolicy::resolveDevice(QStringLiteral("pipewire/hdmi"), {}, list, true);
    auto options = AudioPolicy::outputDeviceOptions(list, QStringLiteral("pipewire"), resolution, {});
    QCOMPARE(options.size(), 3);
    QCOMPARE(options.at(0).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("default"));
    QCOMPARE(options.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("pipewire/dac"));
    const QVariantMap hdmi = options.at(2).toMap();
    QCOMPARE(hdmi.value(QStringLiteral("id")).toString(), QStringLiteral("pipewire/hdmi"));
    QVERIFY(hdmi.value(QStringLiteral("selected")).toBool());
    QVERIFY(hdmi.value(QStringLiteral("active")).toBool());

    // A disconnected preference stays listed as selected but unavailable.
    resolution = AudioPolicy::resolveDevice(QStringLiteral("pipewire/receiver"), QStringLiteral("AVR"), list, true);
    options = AudioPolicy::outputDeviceOptions(list, QStringLiteral("pipewire"), resolution, QStringLiteral("AVR"));
    QCOMPARE(options.size(), 4);
    const QVariantMap missing = options.last().toMap();
    QCOMPARE(missing.value(QStringLiteral("description")).toString(), QStringLiteral("AVR"));
    QVERIFY(missing.value(QStringLiteral("selected")).toBool());
    QVERIFY(!missing.value(QStringLiteral("available")).toBool());
    QVERIFY(!missing.value(QStringLiteral("active")).toBool());
    QVERIFY(options.first().toMap().value(QStringLiteral("active")).toBool());

    // An inactive driver keeps its saved row but cannot bypass the sound server.
    resolution = AudioPolicy::resolveDevice(QStringLiteral("alsa/hw:CARD=DAC"), {}, list, true);
    options = AudioPolicy::outputDeviceOptions(list, QStringLiteral("pipewire"), resolution, {});
    QCOMPARE(resolution.preferred, QStringLiteral("alsa/hw:CARD=DAC"));
    QCOMPARE(resolution.device, QStringLiteral("auto"));
    QVERIFY(!resolution.preferredAvailable);
    QVERIFY(!options.last().toMap().value(QStringLiteral("available")).toBool());
    QVERIFY(!options.last().toMap().value(QStringLiteral("active")).toBool());
    QVERIFY(options.first().toMap().value(QStringLiteral("active")).toBool());

    resolution = AudioPolicy::resolveDevice(QStringLiteral("pipewire/dac"), {}, list, true,
                                           {}, QStringLiteral("pulse"));
    QCOMPARE(resolution.device, QStringLiteral("auto"));
    QVERIFY(!resolution.preferredAvailable);
    resolution = AudioPolicy::resolveDevice(QStringLiteral("alsa/old-dac"), QStringLiteral("DAC raw"), list, true);
    QCOMPARE(resolution.device, QStringLiteral("auto"));
    QVERIFY(!resolution.rematched);

    resolution = AudioPolicy::resolveDevice(QStringLiteral("pipewire/old-dac"), QStringLiteral("USB DAC"), list, true);
    options = AudioPolicy::outputDeviceOptions(list, QStringLiteral("pipewire"), resolution, QStringLiteral("USB DAC"));
    QCOMPARE(options.size(), 3);
    QVERIFY(options.at(1).toMap().value(QStringLiteral("selected")).toBool());
}

void AudioPolicyTest::activeDriverPrefersRuntimeOutput()
{
    const QVariantList list{QVariantMap{{QStringLiteral("name"), QStringLiteral("auto")}},
                            QVariantMap{{QStringLiteral("name"), QStringLiteral("pipewire")}},
                            QVariantMap{{QStringLiteral("name"), QStringLiteral("pulse/sink")}}};
    QCOMPARE(AudioPolicy::activeDriver(list, {}), QStringLiteral("pipewire"));
    QCOMPARE(AudioPolicy::activeDriver(list, QStringLiteral("pulse")), QStringLiteral("pulse"));
    QCOMPARE(AudioPolicy::activeDriver(list, QStringLiteral("null")), QStringLiteral("pipewire"));
    QVERIFY(AudioPolicy::activeDriver({}, {}).isEmpty());
    QVERIFY(AudioPolicy::driverSupportsExclusive(QStringLiteral("wasapi")));
    QVERIFY(!AudioPolicy::driverSupportsExclusive(QStringLiteral("pipewire")));
    QVERIFY(!AudioPolicy::driverSupportsExclusive(QStringLiteral("alsa")));
}

void AudioPolicyTest::parseDeviceHelpReadsMpvListing()
{
    const QString output = QStringLiteral(
        "List of detected audio devices:\r\n"
        "  'auto' (Autoselect device)\r\n"
        "  'pipewire' (Default (pipewire))\n"
        "  'pipewire/alsa_output.usb-DAC-00.analog-stereo' (USB DAC Analog Stereo)\n"
        "  'wasapi/{0.0.0.00000000}.{guid}' (Speakers (USB Audio))\n"
        "unrelated line\n");
    const QVariantList devices = AudioPolicy::parseDeviceHelp(output);
    QCOMPARE(devices.size(), 4);
    QCOMPARE(devices.at(1).toMap().value(QStringLiteral("description")).toString(),
             QStringLiteral("Default (pipewire)"));
    QCOMPARE(devices.at(2).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("pipewire/alsa_output.usb-DAC-00.analog-stereo"));
    QCOMPARE(devices.at(3).toMap().value(QStringLiteral("description")).toString(),
             QStringLiteral("Speakers (USB Audio)"));
}
QTEST_GUILESS_MAIN(AudioPolicyTest)
#include "AudioPolicyTest.moc"
