#include "AudioPolicy.h"

#include <QRegularExpression>

namespace Bloom::AudioPolicy {

QStringList localDecodeCodecs()
{
    return {"aac", "aac_latm", "ac3", "eac3", "dts", "dts-hd", "truehd", "mlp",
            "flac", "alac", "ape", "wavpack", "tta", "tak", "shorten",
            "opus", "vorbis", "speex", "mp1", "mp2", "mp3", "wmav1", "wmav2",
            "wmapro", "wmalossless", "wmavoice", "atrac3", "atrac3plus",
            "pcm_alaw", "pcm_mulaw", "pcm_bluray", "pcm_dvd", "pcm_s8", "pcm_u8",
            "pcm_s16le", "pcm_s16be", "pcm_s24le", "pcm_s24be", "pcm_s32le",
            "pcm_s32be", "pcm_s64le", "pcm_s64be", "pcm_f32le", "pcm_f32be",
            "pcm_f64le", "pcm_f64be"};
}

QString transportCodec(const AudioSourceMetadata &source)
{
    const QString codec = source.codec.trimmed().toLower();
    const QString profile = source.profile.toLower();
    if (codec == "dts-hd" || codec == "dtshd" || codec == "dts_hd_ma"
        || codec == "dts-hd ma" || codec == "dts-hd hra" || codec == "dts:x"
        || (codec == "dts" && (profile.contains("hd") || profile.contains("ma")
                              || profile.contains("hra") || source.dtsX))) {
        return "dts-hd";
    }
    if (codec == "ac-3") return "ac3";
    if (codec == "e-ac-3" || codec == "e-ac3") return "eac3";
    if (codec == "ac3" || codec == "eac3" || codec == "truehd" || codec == "dts")
        return codec;
    return {};
}

AudioOutputPlan plan(const AudioSourceMetadata &source,
                     const AudioOutputCapabilities &output,
                     const AudioPreferences &preferences)
{
    AudioOutputPlan result;
    result.source = source;
    result.device = output.device;
    result.pcmChannels = source.channels;
    result.pcmLayout = source.channelLayout;
    QString layout = source.channelLayout;
    if (layout.isEmpty()) {
        if (source.channels == 1) layout = "mono";
        if (source.channels == 2) layout = "stereo";
        if (source.channels == 6) layout = "5.1";
        if (source.channels == 8) layout = "7.1";
    }
    result.exclusivePcm = preferences.exclusive && output.exclusiveModeSupported
        && output.endpointAvailable;
    if (source.channels > 0 && source.channels <= 2) {
        result.mpvChannels = source.channels == 1 ? "mono" : "stereo";
    } else if (result.exclusivePcm && !layout.isEmpty() && output.pcmLayouts.contains(layout)) {
        // Exclusive format queries proved this layout; stereo remains the fallback.
        result.mpvChannels = layout + ",stereo";
    } else if (result.exclusivePcm) {
        // Exclusive negotiation asks the driver for each layout and downmixes only
        // to one it accepts. auto-safe would force stereo without a system mixer.
        result.mpvChannels = "auto";
    }
    const QString transport = transportCodec(source);
    const QString codec = source.codec.trimmed().toLower();
    result.localDecodeSupported = localDecodeCodecs().contains(codec)
        || !transport.isEmpty() || codec == "pcm";
    // Unknown metadata is not evidence that the server must convert the source.
    result.providerConversionRequired = !codec.isEmpty() && !result.localDecodeSupported;
    if (!output.endpointAvailable) {
        result.reason = "Selected endpoint unavailable; local decode with null/fallback output";
    } else if (preferences.mode == AudioPassthroughMode::Disabled) {
        result.reason = "Passthrough disabled";
    } else if (transport.isEmpty()) {
        result.reason = "PCM output for locally decoded audio";
    } else {
        const bool custom = preferences.mode == AudioPassthroughMode::Custom;
        const bool allowed = custom ? preferences.customCodecs.contains(transport)
                                    : output.passthroughCodecs.contains(transport);
        if (allowed) {
            result.path = "passthrough";
            result.passthroughCodec = transport;
            result.atmosRetained = source.atmos && (transport == "eac3" || transport == "truehd");
            result.dtsXRetained = source.dtsX && transport == "dts-hd";
            result.reason = custom ? "Explicit user passthrough override"
                                   : "Endpoint reports encoded format support: " + output.evidence;
        } else {
            result.reason = output.evidence == "unknown" && !custom
                ? "Encoded endpoint support unknown; decode original audio locally"
                : "Encoded format unavailable or excluded; decode original audio locally";
        }
    }
    return result;
}

QStringList mpvArguments(const AudioOutputPlan &plan)
{
    return {"--audio-spdif=" + plan.passthroughCodec,
            "--audio-device=" + plan.device,
            "--audio-channels=" + plan.mpvChannels,
            "--audio-exclusive=" + QString(plan.exclusivePcm ? "yes" : "no"),
            "--audio-fallback-to-null=yes"};
}

AudioSourceMetadata selectedSource(const QVariantMap &source, int trackIndex)
{
    AudioSourceMetadata fallback;
    bool found = false;
    for (const QVariant &value : source.value("mediaStreams").toList()) {
        const auto stream = value.toMap();
        if (stream.value("type").toString().compare("audio", Qt::CaseInsensitive) != 0)
            continue;
        if (stream.value("index", -1).toInt() == trackIndex)
            return AudioSourceMetadata::fromVariantMap(stream);
        if (!found || stream.value("isDefault").toBool()) {
            fallback = AudioSourceMetadata::fromVariantMap(stream);
            found = true;
        }
    }
    return fallback;
}

QStringList diagnosticProperties()
{
    return {"audio-device", "current-ao", "audio-codec-name", "current-tracks/audio/decoder",
            "current-tracks/audio/codec", "current-tracks/audio/id",
            "audio-params", "audio-out-params", "audio-spdif", "audio-channels",
            "audio-exclusive"};
}

QVariantMap verify(const AudioOutputPlan &plan, const QVariantMap &properties)
{
    const QVariantMap out = properties.value("audio-out-params").toMap();
    const QString format = out.value("format").toString();
    const QString ao = properties.value("current-ao").toString();
    const bool observable = !format.isEmpty() && !ao.isEmpty();
    const bool encoded = observable && ao != "null" && format.startsWith("spdif-");
    const QVariant exclusiveOption = properties.value("audio-exclusive");
    const bool exclusiveOptionSet = exclusiveOption.typeId() == QMetaType::QString
        ? exclusiveOption.toString() == "yes" : exclusiveOption.toBool();
    // mpv's WASAPI AO never falls back to shared mode on its own: with the
    // option set (or an spdif carrier) it opens exclusively or fails to null.
    const bool exclusiveActive = observable && ao == "wasapi"
        && (encoded || (plan.exclusivePcm && exclusiveOptionSet));
    const bool exclusiveFailed = plan.exclusivePcm && ao == "null";
    const QString expected = plan.passthroughCodec == "dts-hd" ? "spdif-dtshd"
                                                             : "spdif-" + plan.passthroughCodec;
    const bool matching = encoded && format == expected;
    const int channels = out.value("channel-count").toInt();
    const int sampleRate = out.value("samplerate").toInt();
    const bool channelChange = observable && !encoded && plan.pcmChannels > 0
        && channels > 0 && channels != plan.pcmChannels;
    const bool rateChange = observable && !encoded && plan.source.sampleRate > 0
        && sampleRate > 0 && sampleRate != plan.source.sampleRate;
    return {{"source", plan.source.toVariantMap()}, {"plan", plan.toVariantMap()},
            {"runtime", properties}, {"outputObservable", observable},
            {"passthroughActive", encoded}, {"encodedFormat", encoded ? format : QString()},
            {"atmosRetained", matching && plan.atmosRetained},
            {"dtsXRetained", matching && plan.dtsXRetained},
            {"pcmChannelCountChanged", channelChange}, {"pcmSampleRateChanged", rateChange},
            {"differsFromPlan", ao == "null" || channelChange || rateChange
                || (observable && ((plan.path == "passthrough") != matching || (plan.path == "pcm" && encoded)))},
            {"needsPcmFallback", plan.path == "passthrough" && (ao == "null" || (observable && !matching))},
            {"exclusiveRequested", plan.exclusivePcm}, {"exclusiveActive", exclusiveActive},
            {"exclusiveFailed", exclusiveFailed},
            {"objectMetadataEvidence", matching ? "encoded carrier active; receiver confirmation required"
                                                : "not retained by PCM or unverified output"}};
}

QString deviceDriver(const QString &device)
{
    const QString trimmed = device.trimmed();
    if (trimmed.isEmpty() || trimmed == "auto") return {};
    return trimmed.section('/', 0, 0);
}

QString activeDriver(const QVariantList &devices, const QString &runtimeDriver)
{
    if (!runtimeDriver.isEmpty() && runtimeDriver != "null") return runtimeDriver;
    for (const QVariant &value : devices) {
        const QString driver = deviceDriver(value.toMap().value("name").toString());
        if (!driver.isEmpty()) return driver;
    }
    return {};
}

bool driverSupportsExclusive(const QString &driver)
{
    return driver == "wasapi";
}

AudioDeviceResolution resolveDevice(const QString &preferred,
                                    const QString &preferredDescription,
                                    const QVariantList &devices,
                                    bool devicesKnown,
                                    const QStringList &unusableDevices)
{
    AudioDeviceResolution result;
    const QString wanted = preferred.trimmed();
    if (wanted.isEmpty() || wanted == "auto") return result;
    result.preferred = wanted;
    result.description = preferredDescription;
    if (unusableDevices.contains(wanted)) {
        result.preferredAvailable = false;
        result.reason = "Preferred output failed to open; using system default";
        return result;
    }
    if (!devicesKnown) {
        result.device = wanted;
        result.reason = "Device list unknown; using preferred output";
        return result;
    }
    const QString driver = deviceDriver(wanted);
    QVariantMap match;
    int matches = 0;
    for (const QVariant &value : devices) {
        const QVariantMap device = value.toMap();
        const QString name = device.value("name").toString();
        if (name == wanted) {
            result.device = wanted;
            result.description = device.value("description").toString();
            return result;
        }
        if (!preferredDescription.isEmpty() && deviceDriver(name) == driver
            && name.contains('/') && !unusableDevices.contains(name)
            && device.value("description").toString() == preferredDescription) {
            match = device;
            ++matches;
        }
    }
    if (matches == 1) {
        result.device = match.value("name").toString();
        result.rematched = true;
        result.reason = "Preferred output id changed; matched by unique description";
        return result;
    }
    result.preferredAvailable = false;
    result.reason = "Preferred output unavailable; using system default";
    return result;
}

QVariantList outputDeviceOptions(const QVariantList &devices,
                                 const QString &driver,
                                 const AudioDeviceResolution &resolution,
                                 const QString &preferredDescription)
{
    const bool preferAuto = resolution.preferred == "auto";
    QVariantList options{QVariantMap{{"id", "auto"}, {"description", QString()},
                                     {"kind", "default"}, {"available", true},
                                     {"selected", preferAuto}, {"active", resolution.device == "auto"}}};
    bool preferredListed = preferAuto;
    bool preferredReported = false;
    for (const QVariant &value : devices) {
        const QVariantMap device = value.toMap();
        const QString name = device.value("name").toString();
        if (name == resolution.preferred) preferredReported = true;
        // Bare driver defaults duplicate System Default; other drivers would
        // bypass the system's routing (for example raw ALSA under PipeWire).
        if (!name.contains('/') || (!driver.isEmpty() && deviceDriver(name) != driver))
            continue;
        const bool selected = name == resolution.preferred
            || (resolution.rematched && name == resolution.device);
        preferredListed = preferredListed || selected;
        options.append(QVariantMap{{"id", name}, {"description", device.value("description").toString()},
                                   {"kind", "device"}, {"available", true}, {"selected", selected},
                                   {"active", name == resolution.device}});
    }
    if (!preferredListed) {
        // Keep a saved endpoint visible while it is disconnected or belongs to
        // another driver so the preference is not silently replaced.
        options.append(QVariantMap{{"id", resolution.preferred}, {"description", preferredDescription},
                                   {"kind", "device"}, {"available", preferredReported},
                                   {"selected", true}, {"active", resolution.device == resolution.preferred}});
    }
    return options;
}

QVariantList parseDeviceHelp(const QString &output)
{
    static const QRegularExpression line(QStringLiteral(R"(^\s*'(.+)'\s+\((.*)\)\s*$)"));
    QVariantList devices;
    for (const QString &text : output.split('\n')) {
        const auto match = line.match(text);
        if (match.hasMatch())
            devices.append(QVariantMap{{"name", match.captured(1)}, {"description", match.captured(2)}});
    }
    return devices;
}
}
