#include "DolbyVisionPolicy.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVariantList>

namespace Bloom::PlaybackPolicy {
namespace {

QString lowered(const QVariant &value)
{
    return value.toString().trimmed().toLower();
}

QString streamMetadataText(const QVariantMap &stream, const QVariantMap &mediaSource)
{
    return QStringList{
        stream.value(QStringLiteral("codec")).toString(),
        stream.value(QStringLiteral("title")).toString(),
        stream.value(QStringLiteral("displayTitle")).toString(),
        stream.value(QStringLiteral("videoRange")).toString(),
        stream.value(QStringLiteral("videoRangeType")).toString(),
        stream.value(QStringLiteral("videoDoViTitle")).toString(),
        stream.value(QStringLiteral("codecTag")).toString(),
        stream.value(QStringLiteral("codecTagString")).toString(),
        stream.value(QStringLiteral("profile")).toString(),
        mediaSource.value(QStringLiteral("name")).toString(),
        mediaSource.value(QStringLiteral("path")).toString(),
    }.join(QLatin1Char(' ')).toLower();
}

int profileFromMetadata(const QString &metadata)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"((?:dvhe|dvh1|dvav|dva1|dav1)\.0?(\d)|profile[ ._-]?(\d)\b|\bp([578])\b)"));
    const QRegularExpressionMatch match = pattern.match(metadata);
    if (!match.hasMatch()) {
        return 0;
    }
    for (int group = 1; group <= 3; ++group) {
        if (!match.captured(group).isEmpty()) {
            return match.captured(group).toInt();
        }
    }
    return 0;
}

bool containsWord(const QString &text, const QString &word)
{
    const QRegularExpression pattern(
        QStringLiteral(R"((?:^|[^a-z0-9])%1(?:$|[^a-z0-9]))").arg(word));
    return pattern.match(text).hasMatch();
}

// Filter-list option names that carry filter definitions in their value.
bool isVideoFilterListOption(const QString &name)
{
    return name == QStringLiteral("vf") || name == QStringLiteral("vf-set")
        || name == QStringLiteral("vf-add") || name == QStringLiteral("vf-append")
        || name == QStringLiteral("vf-pre");
}

bool valueDisables(const QString &filterText, const QString &option)
{
    const QRegularExpression pattern(
        QStringLiteral(R"((?:^|[:,=])%1=(?:no|false|0)(?:$|[:,\]]))")
            .arg(QRegularExpression::escape(option)));
    return pattern.match(filterText).hasMatch();
}

QString optionName(const QString &arg)
{
    if (!arg.startsWith(QStringLiteral("--"))) {
        return {};
    }
    const QString option = arg.mid(2);
    const int equalsIndex = option.indexOf(QLatin1Char('='));
    return (equalsIndex >= 0 ? option.left(equalsIndex) : option).toLower();
}

QString optionValue(const QString &arg)
{
    const int equalsIndex = arg.indexOf(QLatin1Char('='));
    return equalsIndex >= 0 ? arg.mid(equalsIndex + 1) : QString();
}

QVersionNumber parseVersionTriplet(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral(R"((\d+)\.(\d+)\.(\d+))"));
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch()) {
        return {};
    }
    return QVersionNumber(match.captured(1).toInt(),
                          match.captured(2).toInt(),
                          match.captured(3).toInt());
}

int bitDepthForPixelFormat(const QString &format)
{
    const QString name = format.trimmed().toLower();
    if (name.isEmpty()) {
        return 0;
    }
    static const QRegularExpression highDepth(QStringLiteral(R"(p(\d{2})(?:le|be)?$)"));
    const QRegularExpressionMatch match = highDepth.match(name);
    if (match.hasMatch()) {
        const int depth = match.captured(1).toInt();
        // p010/p016 are 10/16-bit biplanar formats; yuv420p10 etc. are planar.
        return depth == 1 ? 10 : depth;
    }
    if (name == QStringLiteral("p010")) return 10;
    if (name == QStringLiteral("p012")) return 12;
    if (name == QStringLiteral("p016")) return 16;
    if (name == QStringLiteral("nv12") || name.startsWith(QStringLiteral("yuv"))
        || name.startsWith(QStringLiteral("yuvj"))) {
        return 8;
    }
    return 0;
}

QString nodeString(const QVariant &value)
{
    if (value.typeId() == QMetaType::QVariantList) {
        QStringList parts;
        for (const QVariant &entry : value.toList()) {
            parts << entry.toString();
        }
        return parts.join(QLatin1Char(','));
    }
    return value.toString();
}

QString orUnavailable(const QString &value)
{
    return value.isEmpty() ? QStringLiteral("unavailable") : value;
}

} // namespace

DolbyVisionSourceInfo describeDolbyVisionSource(const QVariantMap &mediaSource)
{
    DolbyVisionSourceInfo info;
    for (const QVariant &streamVariant : mediaSource.value(QStringLiteral("mediaStreams")).toList()) {
        const QVariantMap stream = streamVariant.toMap();
        if (stream.value(QStringLiteral("type")).toString() != QStringLiteral("Video")) {
            continue;
        }
        const QString metadata = streamMetadataText(stream, mediaSource);
        const int reportedProfile = stream.value(QStringLiteral("dolbyVisionProfile")).toInt();
        const bool isDolbyVision = reportedProfile > 0
            || metadata.contains(QStringLiteral("dovi"))
            || metadata.contains(QStringLiteral("dolby vision"))
            || metadata.contains(QStringLiteral("dvhe"))
            || metadata.contains(QStringLiteral("dvh1"));
        if (!isDolbyVision) {
            continue;
        }

        info.isDolbyVision = true;
        info.profile = reportedProfile > 0 ? reportedProfile : profileFromMetadata(metadata);
        const QString compatibilityKey = QStringLiteral("dolbyVisionBlSignalCompatibilityId");
        if (stream.contains(compatibilityKey) && !stream.value(compatibilityKey).isNull()) {
            info.compatibilityId = stream.value(compatibilityKey).toInt();
        }
        const QString rangeType = lowered(stream.value(QStringLiteral("videoRangeType")));
        info.dualLayer = info.profile == 7 || rangeType.contains(QStringLiteral("withel"));
        if (containsWord(metadata, QStringLiteral("fel"))) {
            info.enhancementLayerHint = QStringLiteral("fel");
        } else if (containsWord(metadata, QStringLiteral("mel"))) {
            info.enhancementLayerHint = QStringLiteral("mel");
        }
        break;
    }
    return info;
}

QString dolbyVisionSourceSummary(const DolbyVisionSourceInfo &info)
{
    if (!info.isDolbyVision) {
        return QStringLiteral("not-dolby-vision");
    }
    QString layers = info.dualLayer ? QStringLiteral("dual-layer") : QStringLiteral("single-layer");
    if (info.dualLayer) {
        layers += info.enhancementLayerHint.isEmpty()
            ? QStringLiteral("(el=mel-or-fel;rpu-decides)")
            : QStringLiteral("(el=%1-by-title)").arg(info.enhancementLayerHint);
    }
    return QStringLiteral("profile=%1 compatibility=%2 %3")
        .arg(info.profile > 0 ? QString::number(info.profile) : QStringLiteral("unknown"),
             info.compatibilityId >= 0 ? QString::number(info.compatibilityId)
                                       : QStringLiteral("unknown"),
             layers);
}

QString formatFilterSupportName(FormatFilterSupport support)
{
    switch (support) {
    case FormatFilterSupport::Unknown: return QStringLiteral("unknown");
    case FormatFilterSupport::Unsupported: return QStringLiteral("unsupported");
    case FormatFilterSupport::DolbyVisionOnly: return QStringLiteral("dolbyvision-only");
    case FormatFilterSupport::DolbyVisionAndEnhancementLayer:
        return QStringLiteral("dolbyvision+enhancement-layer");
    }
    return QStringLiteral("unknown");
}

QString dolbyVisionFormatFilter(FormatFilterSupport support)
{
    const QString label = QStringLiteral("@%1:").arg(QLatin1StringView(kDolbyVisionFilterLabel));
    switch (support) {
    case FormatFilterSupport::DolbyVisionAndEnhancementLayer:
        return label + QStringLiteral("format=dolbyvision=yes:enhancement-layer=yes");
    case FormatFilterSupport::DolbyVisionOnly:
        return label + QStringLiteral("format=dolbyvision=yes");
    case FormatFilterSupport::Unknown:
    case FormatFilterSupport::Unsupported:
        break;
    }
    return {};
}

QStringList appendDolbyVisionFilterArgs(const QStringList &args,
                                        HdrContentKind kind,
                                        FormatFilterSupport support)
{
    const bool isDolbyVision = kind == HdrContentKind::DolbyVisionCompatible
        || kind == HdrContentKind::DolbyVisionUnsupported;
    const QString filter = dolbyVisionFormatFilter(support);
    if (!isDolbyVision || filter.isEmpty()) {
        return args;
    }

    const QString labelPrefix = QStringLiteral("@%1:").arg(QLatin1StringView(kDolbyVisionFilterLabel));
    QStringList result;
    result.reserve(args.size() + 1);
    for (const QString &arg : args) {
        // Drop earlier Bloom-managed entries so the final one is authoritative.
        if (optionName(arg) == QStringLiteral("vf-append")
            && optionValue(arg).startsWith(labelPrefix)) {
            continue;
        }
        result << arg;
    }
    result << QStringLiteral("--vf-append=") + filter;
    return result;
}

QStringList dolbyVisionStrippingFilterArgs(const QStringList &args)
{
    QStringList result;
    for (const QString &arg : args) {
        if (!isVideoFilterListOption(optionName(arg))) {
            continue;
        }
        const QString value = optionValue(arg).toLower();
        if (valueDisables(value, QStringLiteral("dolbyvision"))
            || valueDisables(value, QStringLiteral("enhancement-layer"))) {
            result << arg;
        }
    }
    return result;
}

QString capabilityName(Capability capability)
{
    switch (capability) {
    case Capability::Unknown: return QStringLiteral("unknown");
    case Capability::Supported: return QStringLiteral("yes");
    case Capability::Unsupported: return QStringLiteral("no");
    }
    return QStringLiteral("unknown");
}

QString rendererKindName(RendererKind renderer)
{
    switch (renderer) {
    case RendererKind::Unknown: return QStringLiteral("unknown");
    case RendererKind::GpuNext: return QStringLiteral("gpu-next");
    case RendererKind::LegacyGpu: return QStringLiteral("gpu");
    case RendererKind::LibmpvRenderApi: return QStringLiteral("libmpv-render-api");
    }
    return QStringLiteral("unknown");
}

RendererKind rendererKindForVo(const QString &vo)
{
    const QString normalized = vo.trimmed().toLower();
    if (normalized == QStringLiteral("gpu-next")) return RendererKind::GpuNext;
    if (normalized == QStringLiteral("gpu")) return RendererKind::LegacyGpu;
    if (normalized == QStringLiteral("libmpv")) return RendererKind::LibmpvRenderApi;
    return RendererKind::Unknown;
}

MpvRuntimeInfo parseMpvVersionOutput(const QString &text)
{
    MpvRuntimeInfo info;
    const QStringList lines = text.split(QLatin1Char('\n'));
    bool inLibraryList = false;
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QStringLiteral("mpv "))) {
            const int copyright = line.indexOf(QStringLiteral(" Copyright"));
            info.mpvVersion = (copyright > 0 ? line.left(copyright) : line).trimmed();
            continue;
        }
        if (line.startsWith(QStringLiteral("libplacebo version:"))) {
            info.libplaceboVersion = line.mid(QStringLiteral("libplacebo version:").size()).trimmed();
            continue;
        }
        if (line.startsWith(QStringLiteral("FFmpeg version:"))) {
            QString version = line.mid(QStringLiteral("FFmpeg version:").size()).trimmed();
            static const QRegularExpression runtimePattern(QStringLiteral(R"(\(runtime ([^)]+)\))"));
            const QRegularExpressionMatch runtime = runtimePattern.match(version);
            if (runtime.hasMatch()) {
                version = runtime.captured(1).trimmed();
            }
            info.ffmpegVersion = version;
            continue;
        }
        if (line.startsWith(QStringLiteral("FFmpeg library versions:"))) {
            inLibraryList = true;
            continue;
        }
        if (inLibraryList && line.startsWith(QStringLiteral("libavcodec"))) {
            static const QRegularExpression runtimePattern(
                QStringLiteral(R"(\(runtime (\d+\.\d+\.\d+)\))"));
            const QRegularExpressionMatch runtime = runtimePattern.match(line);
            info.libavcodecVersion = runtime.hasMatch()
                ? parseVersionTriplet(runtime.captured(1))
                : parseVersionTriplet(line);
        }
    }
    return info;
}

int libplaceboApiVersion(const QString &libplaceboVersion)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^v?(\d+)\.(\d+)(?:\.\d+)?)"));
    const QRegularExpressionMatch match = pattern.match(libplaceboVersion.trimmed());
    return match.hasMatch() ? match.captured(2).toInt() : -1;
}

QVersionNumber ffmpegReleaseVersion(const QString &ffmpegVersion)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^n?(\d+)\.(\d+)(?:\.(\d+))?)"));
    const QRegularExpressionMatch match = pattern.match(ffmpegVersion.trimmed());
    if (!match.hasMatch()) {
        return {};
    }
    return QVersionNumber(match.captured(1).toInt(),
                          match.captured(2).toInt(),
                          match.captured(3).isEmpty() ? 0 : match.captured(3).toInt());
}

FelCapability evaluateFelCapability(const MpvRuntimeInfo &info, RendererKind renderer)
{
    FelCapability result;

    switch (info.formatFilter) {
    case FormatFilterSupport::DolbyVisionAndEnhancementLayer:
        result.mpvEnhancementLayer = Capability::Supported;
        break;
    case FormatFilterSupport::DolbyVisionOnly:
    case FormatFilterSupport::Unsupported:
        result.mpvEnhancementLayer = Capability::Unsupported;
        result.reasons << QStringLiteral("mpv predates PR #17932 (vf=format has no enhancement-layer option)");
        break;
    case FormatFilterSupport::Unknown:
        result.reasons << QStringLiteral("mpv vf=format options not probed");
        break;
    }

    const int placeboApi = libplaceboApiVersion(info.libplaceboVersion);
    if (placeboApi >= kFelMinimumLibplaceboApi) {
        result.libplacebo = Capability::Supported;
    } else if (placeboApi >= 0) {
        result.libplacebo = Capability::Unsupported;
        result.reasons << QStringLiteral("libplacebo API %1 < %2 (FEL renderer path compiled out)")
                              .arg(placeboApi)
                              .arg(kFelMinimumLibplaceboApi);
    } else {
        result.reasons << QStringLiteral("libplacebo version unavailable");
    }

    const QVersionNumber minimumLibavcodec(kFelMinimumLibavcodecMajor,
                                           kFelMinimumLibavcodecMinor,
                                           kFelMinimumLibavcodecMicro);
    const QVersionNumber ffmpegRelease = ffmpegReleaseVersion(info.ffmpegVersion);
    if (!info.libavcodecVersion.isNull()) {
        if (info.libavcodecVersion >= minimumLibavcodec && info.matchesVerifiedPackagedStack) {
            result.ffmpegDoviSplit = Capability::Supported;
        } else if (info.libavcodecVersion < minimumLibavcodec) {
            result.ffmpegDoviSplit = Capability::Unsupported;
            result.reasons << QStringLiteral("libavcodec %1 < %2 (no dovi_split BSF; EL cannot be split)")
                                  .arg(info.libavcodecVersion.toString(), minimumLibavcodec.toString());
        }
    } else if (!ffmpegRelease.isNull()) {
        if (ffmpegRelease >= QVersionNumber(9, 0) && info.matchesVerifiedPackagedStack) {
            result.ffmpegDoviSplit = Capability::Supported;
        } else if (ffmpegRelease < QVersionNumber(9, 0)) {
            result.ffmpegDoviSplit = Capability::Unsupported;
            result.reasons << QStringLiteral("FFmpeg %1 < 9.0 (no dovi_split BSF)").arg(info.ffmpegVersion);
        }
    } else if (info.matchesVerifiedPackagedStack) {
        // Git snapshot builds report no release number; the packaged pin was
        // verified to contain dovi_split when Bloom was packaged.
        result.ffmpegDoviSplit = Capability::Supported;
    } else {
        result.reasons << QStringLiteral("FFmpeg %1 is an unversioned snapshot; dovi_split is confirmed only when a Profile 7 EL track appears")
                              .arg(orUnavailable(info.ffmpegVersion));
    }
    if (result.ffmpegDoviSplit == Capability::Unknown
        && (!info.libavcodecVersion.isNull() || !ffmpegRelease.isNull())) {
        result.reasons << QStringLiteral("FFmpeg meets the version floor, but dovi_split availability is unverified; check the packaged libraries or observe EL composition");
    }

    switch (renderer) {
    case RendererKind::GpuNext:
        result.renderer = Capability::Supported;
        break;
    case RendererKind::LegacyGpu:
        result.renderer = Capability::Unsupported;
        result.reasons << QStringLiteral("vo=gpu does not apply Dolby Vision reshaping or the EL");
        break;
    case RendererKind::LibmpvRenderApi:
        result.renderer = Capability::Unsupported;
        result.reasons << QStringLiteral("libmpv render API uses the legacy vo_gpu renderer (no Dolby Vision reshaping or EL)");
        break;
    case RendererKind::Unknown:
        result.reasons << QStringLiteral("renderer unknown");
        break;
    }

    const QList<Capability> parts{result.mpvEnhancementLayer, result.libplacebo,
                                  result.ffmpegDoviSplit, result.renderer};
    if (parts.contains(Capability::Unsupported)) {
        result.overall = Capability::Unsupported;
    } else if (parts.contains(Capability::Unknown)) {
        result.overall = Capability::Unknown;
    } else {
        result.overall = Capability::Supported;
    }
    return result;
}

QString felCapabilitySummary(const FelCapability &capability)
{
    QString summary = QStringLiteral("fel=%1 (mpv-el=%2 ffmpeg-dovi_split=%3 libplacebo=%4 renderer=%5)")
                          .arg(capabilityName(capability.overall),
                               capabilityName(capability.mpvEnhancementLayer),
                               capabilityName(capability.ffmpegDoviSplit),
                               capabilityName(capability.libplacebo),
                               capabilityName(capability.renderer));
    if (!capability.reasons.isEmpty()) {
        summary += QStringLiteral(" reasons=[") + capability.reasons.join(QStringLiteral("; "))
            + QLatin1Char(']');
    }
    return summary;
}

QString describeVideoFilterChain(const QVariant &vfProperty)
{
    if (vfProperty.typeId() == QMetaType::QString) {
        return vfProperty.toString();
    }
    QStringList filters;
    for (const QVariant &entryVariant : vfProperty.toList()) {
        const QVariantMap entry = entryVariant.toMap();
        QString text;
        const QString label = entry.value(QStringLiteral("label")).toString();
        if (!label.isEmpty()) {
            text += QLatin1Char('@') + label + QLatin1Char(':');
        }
        if (entry.contains(QStringLiteral("enabled"))
            && !entry.value(QStringLiteral("enabled")).toBool()) {
            text += QLatin1Char('!');
        }
        text += entry.value(QStringLiteral("name")).toString();
        const QVariantMap params = entry.value(QStringLiteral("params")).toMap();
        QStringList paramTexts;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            paramTexts << it.key() + QLatin1Char('=') + it.value().toString();
        }
        if (!paramTexts.isEmpty()) {
            text += QLatin1Char('=') + paramTexts.join(QLatin1Char(':'));
        }
        filters << text;
    }
    return filters.join(QLatin1Char(','));
}

PlaybackDiagnostics evaluatePlaybackDiagnostics(const QVariantMap &properties)
{
    PlaybackDiagnostics result;
    result.vo = properties.value(QStringLiteral("current-vo")).toString();
    result.gpuContext = properties.value(QStringLiteral("current-gpu-context")).toString();
    result.gpuApi = nodeString(properties.value(QStringLiteral("options/gpu-api")));
    result.hwdecRequested = nodeString(properties.value(QStringLiteral("options/hwdec")));
    result.hwdecCurrent = properties.value(QStringLiteral("hwdec-current")).toString();
    result.softwareDecoding = result.hwdecCurrent.isEmpty()
        || result.hwdecCurrent == QStringLiteral("no");
    result.hardwareFallbackToSoftware = result.softwareDecoding
        && !result.hwdecRequested.isEmpty()
        && result.hwdecRequested != QStringLiteral("no");
    result.targetColorspaceHint =
        properties.value(QStringLiteral("options/target-colorspace-hint")).toString();
    result.targetColorspaceHintMode =
        properties.value(QStringLiteral("options/target-colorspace-hint-mode")).toString();
    result.videoFilterChain = describeVideoFilterChain(properties.value(QStringLiteral("vf")));

    const QString chain = result.videoFilterChain.toLower();
    result.dolbyVisionMetadataStripped = valueDisables(chain, QStringLiteral("dolbyvision"));
    result.enhancementLayerEnabled = !valueDisables(chain, QStringLiteral("enhancement-layer"));

    result.enhancementLayerTrackObservable =
        properties.value(QStringLiteral("options/show-dependent-tracks")).toBool();
    for (const QVariant &trackVariant : properties.value(QStringLiteral("track-list")).toList()) {
        const QVariantMap track = trackVariant.toMap();
        if (track.value(QStringLiteral("type")).toString() != QStringLiteral("video")) {
            continue;
        }
        if (track.value(QStringLiteral("dependent")).toBool()) {
            if (track.contains(QStringLiteral("dolby-vision-profile"))) {
                result.enhancementLayerTrackPresent = true;
            }
            continue;
        }
        if (!track.value(QStringLiteral("selected")).toBool()) {
            continue;
        }
        result.videoCodec = track.value(QStringLiteral("codec")).toString();
        result.videoDecoder = track.value(QStringLiteral("decoder")).toString();
        result.dolbyVisionProfile = track.value(QStringLiteral("dolby-vision-profile")).toInt();
    }

    const QVariantMap decoded = properties.value(QStringLiteral("video-dec-params")).toMap();
    const QVariantMap output = properties.value(QStringLiteral("video-out-params")).toMap();
    const QVariantMap source = decoded.isEmpty()
        ? properties.value(QStringLiteral("video-params")).toMap()
        : decoded;
    result.pixelFormat = source.value(QStringLiteral("hw-pixelformat")).toString();
    if (result.pixelFormat.isEmpty()) {
        result.pixelFormat = source.value(QStringLiteral("pixelformat")).toString();
    }
    result.bitDepth = bitDepthForPixelFormat(result.pixelFormat);
    result.sourceTransfer = source.value(QStringLiteral("gamma")).toString();
    result.sourcePrimaries = source.value(QStringLiteral("primaries")).toString();
    result.sourceColormatrix = source.value(QStringLiteral("colormatrix")).toString();
    // video-out-params is what the VO receives after the filter chain; a
    // dolbyvision colormatrix there means the RPU mapping reached the renderer.
    const QVariantMap delivered = output.isEmpty() ? source : output;
    result.dolbyVisionMetadataApplied =
        delivered.value(QStringLiteral("colormatrix")).toString() == QStringLiteral("dolbyvision");

    const QVariantMap target = properties.value(QStringLiteral("video-target-params")).toMap();
    result.outputTransfer = target.value(QStringLiteral("gamma")).toString();
    result.outputPrimaries = target.value(QStringLiteral("primaries")).toString();
    result.outputSignalPeak = target.value(QStringLiteral("sig-peak")).toDouble();

    const QVariant passes = properties.value(QStringLiteral("vo-passes"));
    if (passes.isValid() && !passes.isNull()) {
        result.renderPassesObservable = true;
        const QVariantMap passMap = passes.toMap();
        for (const QString &kind : {QStringLiteral("fresh"), QStringLiteral("redraw")}) {
            for (const QVariant &pass : passMap.value(kind).toList()) {
                if (pass.toMap().value(QStringLiteral("desc")).toString().contains(
                        QStringLiteral("enhancement layer"), Qt::CaseInsensitive)) {
                    result.enhancementLayerRendered = true;
                }
            }
        }
    }

    auto counter = [&properties](const QString &name) -> qint64 {
        const QVariant value = properties.value(name);
        return value.isValid() && !value.isNull() ? value.toLongLong() : -1;
    };
    result.droppedFrames = counter(QStringLiteral("frame-drop-count"));
    result.decoderDroppedFrames = counter(QStringLiteral("decoder-frame-drop-count"));
    result.delayedFrames = counter(QStringLiteral("vo-delayed-frame-count"));
    return result;
}

QString playbackDiagnosticsSummary(const PlaybackDiagnostics &d,
                                   const DolbyVisionSourceInfo &source)
{
    QStringList parts{
        QStringLiteral("vo=%1").arg(orUnavailable(d.vo)),
        QStringLiteral("gpu-context=%1").arg(orUnavailable(d.gpuContext)),
        QStringLiteral("gpu-api=%1").arg(orUnavailable(d.gpuApi)),
        QStringLiteral("hwdec=%1->%2%3")
            .arg(orUnavailable(d.hwdecRequested),
                 d.softwareDecoding ? QStringLiteral("software") : d.hwdecCurrent,
                 d.hardwareFallbackToSoftware ? QStringLiteral("(fallback)") : QString()),
        QStringLiteral("target-colorspace-hint=%1/%2")
            .arg(orUnavailable(d.targetColorspaceHint), orUnavailable(d.targetColorspaceHintMode)),
        QStringLiteral("vf=[%1]").arg(d.videoFilterChain),
        QStringLiteral("codec=%1 decoder=%2 depth=%3 pixfmt=%4")
            .arg(orUnavailable(d.videoCodec), orUnavailable(d.videoDecoder),
                 d.bitDepth > 0 ? QString::number(d.bitDepth) : QStringLiteral("unknown"),
                 orUnavailable(d.pixelFormat)),
        QStringLiteral("source=%1/%2/%3")
            .arg(orUnavailable(d.sourceTransfer), orUnavailable(d.sourcePrimaries),
                 orUnavailable(d.sourceColormatrix)),
        QStringLiteral("output=%1/%2 peak=%3")
            .arg(orUnavailable(d.outputTransfer), orUnavailable(d.outputPrimaries))
            .arg(d.outputSignalPeak, 0, 'f', 2),
        QStringLiteral("drops=%1 decoder-drops=%2 delayed=%3")
            .arg(d.droppedFrames).arg(d.decoderDroppedFrames).arg(d.delayedFrames),
    };
    if (source.isDolbyVision || d.dolbyVisionProfile > 0) {
        parts << QStringLiteral("dv-source=[%1] mpv-dv-profile=%2 dv-metadata-applied=%3%4")
                     .arg(dolbyVisionSourceSummary(source))
                     .arg(d.dolbyVisionProfile > 0 ? QString::number(d.dolbyVisionProfile)
                                                   : QStringLiteral("unavailable"))
                     .arg(d.dolbyVisionMetadataApplied ? QStringLiteral("yes") : QStringLiteral("no"),
                          d.dolbyVisionMetadataStripped ? QStringLiteral(" (stripped-by-filter)") : QString());
        const bool profile7 = d.dolbyVisionProfile == 7 || source.profile == 7;
        if (profile7) {
            parts << QStringLiteral("el-track=%1 el-enabled=%2 el-rendered=%3 el-decoder=not-exposed-by-mpv")
                         .arg(d.enhancementLayerTrackObservable
                                  ? (d.enhancementLayerTrackPresent ? QStringLiteral("detected")
                                                                    : QStringLiteral("absent"))
                                  : QStringLiteral("unobservable"),
                              d.enhancementLayerEnabled ? QStringLiteral("yes") : QStringLiteral("no"),
                              d.renderPassesObservable
                                  ? (d.enhancementLayerRendered ? QStringLiteral("yes")
                                                                : QStringLiteral("no"))
                                  : QStringLiteral("unobservable"));
        }
    }
    return parts.join(QLatin1Char(' '));
}

QStringList playbackDiagnosticProperties()
{
    return {
        QStringLiteral("current-vo"),
        QStringLiteral("current-gpu-context"),
        QStringLiteral("options/gpu-api"),
        QStringLiteral("options/hwdec"),
        QStringLiteral("hwdec-current"),
        QStringLiteral("options/target-colorspace-hint"),
        QStringLiteral("options/target-colorspace-hint-mode"),
        QStringLiteral("options/show-dependent-tracks"),
        QStringLiteral("vf"),
        QStringLiteral("track-list"),
        QStringLiteral("video-dec-params"),
        QStringLiteral("video-out-params"),
        QStringLiteral("video-target-params"),
        QStringLiteral("vo-passes"),
        QStringLiteral("frame-drop-count"),
        QStringLiteral("decoder-frame-drop-count"),
        QStringLiteral("vo-delayed-frame-count"),
    };
}

QStringList runtimeProvenanceProperties()
{
    return {
        QStringLiteral("mpv-version"),
        QStringLiteral("ffmpeg-version"),
        QStringLiteral("libplacebo-version"),
    };
}

} // namespace Bloom::PlaybackPolicy
