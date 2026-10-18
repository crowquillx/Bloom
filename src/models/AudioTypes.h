#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace Bloom {

struct AudioSourceMetadata {
    QString codec;
    QString profile;
    QString channelLayout;
    int channels = 0;
    int sampleRate = 0;
    int bitDepth = 0;
    qint64 bitRate = 0;
    QString fidelity = QStringLiteral("unknown");
    bool atmos = false;
    bool dtsX = false;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("codec"), codec},
                {QStringLiteral("profile"), profile},
                {QStringLiteral("channelLayout"), channelLayout},
                {QStringLiteral("channels"), channels},
                {QStringLiteral("sampleRate"), sampleRate},
                {QStringLiteral("bitDepth"), bitDepth},
                {QStringLiteral("bitRate"), bitRate},
                {QStringLiteral("fidelity"), fidelity},
                {QStringLiteral("atmos"), atmos},
                {QStringLiteral("dtsX"), dtsX}};
    }

    static AudioSourceMetadata fromVariantMap(const QVariantMap &map)
    {
        AudioSourceMetadata metadata;
        metadata.codec = map.value(QStringLiteral("codec")).toString().trimmed();
        metadata.profile = map.value(QStringLiteral("profile")).toString().trimmed();
        metadata.channelLayout = map.value(QStringLiteral("channelLayout")).toString().trimmed();
        metadata.channels = map.value(QStringLiteral("channels")).toInt();
        metadata.sampleRate = map.value(QStringLiteral("sampleRate")).toInt();
        metadata.bitDepth = map.value(QStringLiteral("bitDepth")).toInt();
        metadata.bitRate = map.value(QStringLiteral("bitRate")).toLongLong();

        // Only structured stream profile/title metadata and Jellyfin's explicit
        // spatial-format field identify Atmos or DTS:X; file names are excluded.
        const QString structured = QStringList{
            metadata.profile,
            map.value(QStringLiteral("title")).toString(),
            map.value(QStringLiteral("embeddedTitle")).toString(),
            map.value(QStringLiteral("audioSpatialFormat")).toString()
        }.join(QLatin1Char(' ')).toLower();
        metadata.atmos = map.value(QStringLiteral("atmos")).toBool()
            || structured.contains(QStringLiteral("dolbyatmos"))
            || structured.contains(QStringLiteral("dolby atmos"))
            || structured.contains(QStringLiteral("atmos"))
            || (metadata.codec.toLower().contains(QStringLiteral("eac3"))
                && structured.contains(QStringLiteral("joc")));
        metadata.dtsX = map.value(QStringLiteral("dtsX")).toBool()
            || structured.contains(QStringLiteral("dtsx"))
            || structured.contains(QStringLiteral("dts-x"))
            || structured.contains(QStringLiteral("dts:x"));

        const QString normalizedCodec = metadata.codec.toLower();
        const QString normalizedProfile = metadata.profile.toLower();
        const bool dtsMasterAudio = normalizedCodec.contains(QStringLiteral("dts"))
            && (normalizedProfile.contains(QStringLiteral("ma"))
                || normalizedProfile.contains(QStringLiteral("master audio")));
        const bool dtsHighResolutionAudio = normalizedCodec.contains(QStringLiteral("dts"))
            && (normalizedProfile.contains(QStringLiteral("hra"))
                || normalizedProfile.contains(QStringLiteral("high resolution audio")));
        const bool losslessCodec = normalizedCodec.contains(QStringLiteral("truehd"))
            || normalizedCodec.contains(QStringLiteral("mlp"))
            || normalizedCodec.contains(QStringLiteral("flac"))
            || normalizedCodec.contains(QStringLiteral("alac"))
            || normalizedCodec.contains(QStringLiteral("pcm"))
            || dtsMasterAudio;
        const bool lossyCodec = normalizedCodec.contains(QStringLiteral("aac"))
            || normalizedCodec.contains(QStringLiteral("ac3"))
            || normalizedCodec.contains(QStringLiteral("eac3"))
            || (normalizedCodec == QStringLiteral("dts") && !dtsMasterAudio
                && !metadata.dtsX && !normalizedProfile.contains(QStringLiteral("hd")))
            || dtsHighResolutionAudio
            || normalizedCodec.contains(QStringLiteral("mp3"))
            || normalizedCodec.contains(QStringLiteral("opus"))
            || normalizedCodec.contains(QStringLiteral("vorbis"))
            || normalizedCodec.contains(QStringLiteral("wma"));
        metadata.fidelity = losslessCodec ? QStringLiteral("lossless")
            : lossyCodec ? QStringLiteral("lossy") : QStringLiteral("unknown");
        return metadata;
    }
};

struct AudioOutputCapabilities {
    QString device = QStringLiteral("auto");
    QString endpointId;
    QStringList passthroughCodecs;
    QStringList pcmLayouts;
    QString evidence = QStringLiteral("unknown");
    bool endpointAvailable = true;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("device"), device},
                {QStringLiteral("endpointId"), endpointId},
                {QStringLiteral("passthroughCodecs"), passthroughCodecs},
                {QStringLiteral("pcmLayouts"), pcmLayouts},
                {QStringLiteral("evidence"), evidence},
                {QStringLiteral("endpointAvailable"), endpointAvailable}};
    }
};

enum class AudioPassthroughMode { Auto, Disabled, Custom };

struct AudioPreferences {
    AudioPassthroughMode mode = AudioPassthroughMode::Auto;
    QStringList customCodecs;

    QVariantMap toVariantMap() const
    {
        QString modeName = QStringLiteral("auto");
        if (mode == AudioPassthroughMode::Disabled)
            modeName = QStringLiteral("disabled");
        else if (mode == AudioPassthroughMode::Custom)
            modeName = QStringLiteral("custom");
        return {{QStringLiteral("mode"), modeName},
                {QStringLiteral("customCodecs"), customCodecs}};
    }
};

struct AudioOutputPlan {
    AudioSourceMetadata source;
    QString device = QStringLiteral("auto");
    QString path = QStringLiteral("pcm");
    QString passthroughCodec;
    QString pcmLayout;
    QString mpvChannels = QStringLiteral("auto-safe");
    bool exclusivePcm = false;
    int pcmChannels = 0;
    bool localDecodeSupported = false;
    bool providerConversionRequired = false;
    bool atmosRetained = false;
    bool dtsXRetained = false;
    bool runtimeRevalidationRequired = true;
    QString reason;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("source"), source.toVariantMap()},
                {QStringLiteral("device"), device},
                {QStringLiteral("path"), path},
                {QStringLiteral("passthroughCodec"), passthroughCodec},
                {QStringLiteral("pcmLayout"), pcmLayout},
                {QStringLiteral("mpvChannels"), mpvChannels},
                {QStringLiteral("exclusivePcm"), exclusivePcm},
                {QStringLiteral("pcmChannels"), pcmChannels},
                {QStringLiteral("localDecodeSupported"), localDecodeSupported},
                {QStringLiteral("providerConversionRequired"), providerConversionRequired},
                {QStringLiteral("atmosRetained"), atmosRetained},
                {QStringLiteral("dtsXRetained"), dtsXRetained},
                {QStringLiteral("runtimeRevalidationRequired"), runtimeRevalidationRequired},
                {QStringLiteral("reason"), reason}};
    }
};

} // namespace Bloom
