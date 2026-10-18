#include "AudioPolicy.h"

namespace Bloom::AudioPolicy {

QStringList localDecodeCodecs()
{
    return {"aac", "ac3", "eac3", "dts", "dts-hd", "truehd", "flac", "opus",
            "vorbis", "mp3", "mp2", "alac", "pcm_s16le", "pcm_s24le", "pcm_s32le",
            "pcm_f32le", "pcm_f64le", "pcm_s16be", "pcm_s24be", "pcm_s32be"};
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
    if (source.channels > 0 && source.channels <= 2) {
        result.mpvChannels = source.channels == 1 ? "mono" : "stereo";
    } else if (!layout.isEmpty() && output.pcmLayouts.contains(layout)) {
        result.mpvChannels = layout + ",stereo";
        result.exclusivePcm = output.evidence == "wasapi-exclusive-format-query";
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
            "audio-params", "audio-out-params", "audio-spdif", "audio-channels"};
}

QVariantMap verify(const AudioOutputPlan &plan, const QVariantMap &properties)
{
    const QVariantMap out = properties.value("audio-out-params").toMap();
    const QString format = out.value("format").toString();
    const QString ao = properties.value("current-ao").toString();
    const bool observable = !format.isEmpty() && !ao.isEmpty();
    const bool encoded = observable && ao != "null" && format.startsWith("spdif-");
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
            {"objectMetadataEvidence", matching ? "encoded carrier active; receiver confirmation required"
                                                : "not retained by PCM or unverified output"}};
}
}
