#include "PlaybackPolicy.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <QtGlobal>

#include <limits>
#include <tuple>
#include <utility>

namespace Bloom::PlaybackPolicy {
namespace {

QVariantList mediaStreamsForType(const QVariantMap &mediaSource,
                                 const QString &streamType)
{
    QVariantList result;
    for (const QVariant &streamVariant :
         mediaSource.value(QStringLiteral("mediaStreams")).toList()) {
        const QVariantMap stream = streamVariant.toMap();
        if (stream.value(QStringLiteral("type")).toString() == streamType) {
            result.append(stream);
        }
    }
    return result;
}

// Embedded tracks play from the source the player already has open. Listing them
// first lets every ordinal tie-break below prefer them over external sidecars.
QVariantList embeddedFirst(const QVariantList &streams)
{
    QVariantList embedded;
    QVariantList external;
    for (const QVariant &streamVariant : streams) {
        (streamVariant.toMap().value(QStringLiteral("isExternal")).toBool()
             ? external : embedded).append(streamVariant);
    }
    return embedded + external;
}

bool hasStreamIndex(const QVariantList &streams, int streamIndex)
{
    for (const QVariant &streamVariant : streams) {
        if (streamVariant.toMap().value(QStringLiteral("index"), -1).toInt()
            == streamIndex) {
            return true;
        }
    }
    return false;
}

template<typename Predicate>
int firstMatchingStreamIndex(const QVariantList &streams, Predicate predicate)
{
    for (const QVariant &streamVariant : streams) {
        const QVariantMap stream = streamVariant.toMap();
        if (predicate(stream)) {
            return stream.value(QStringLiteral("index"), -1).toInt();
        }
    }
    return -1;
}

QString normalizedMetadataText(const QVariantMap &stream)
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
        stream.value(QStringLiteral("codecId")).toString(),
        stream.value(QStringLiteral("profile")).toString()
    }.join(QLatin1Char(' ')).trimmed().toLower();
}

QString normalizedMediaSourceMetadataText(const QVariantMap &mediaSource)
{
    return QStringList{
        mediaSource.value(QStringLiteral("name")).toString(),
        mediaSource.value(QStringLiteral("path")).toString(),
        mediaSource.value(QStringLiteral("container")).toString()
    }.join(QLatin1Char(' ')).trimmed().toLower();
}

bool metadataContainsDolbyVisionProfile(const QString &metadata, int profile)
{
    const QString profileText = QString::number(profile);
    return metadata.contains(QStringLiteral("dvhe.0") + profileText)
        || metadata.contains(QStringLiteral("dvh1.0") + profileText)
        || metadata.contains(QStringLiteral("profile ") + profileText)
        || metadata.contains(QStringLiteral("profile-") + profileText)
        || metadata.contains(QStringLiteral("profile.") + profileText)
        || metadata.contains(QStringLiteral(" p") + profileText);
}

bool rangeTokenIndicatesHdr(const QString &token)
{
    const QString normalized = token.trimmed().toUpper();
    return !normalized.isEmpty()
        && normalized != QStringLiteral("UNKNOWN")
        && normalized != QStringLiteral("SDR")
        && normalized != QStringLiteral("0")
        && normalized != QStringLiteral("1");
}

HdrContentKind classifyVideoStream(const QVariantMap &stream,
                                   const QString &mediaSourceMetadata)
{
    const QString metadata = QStringList{
        normalizedMetadataText(stream), mediaSourceMetadata
    }.join(QLatin1Char(' ')).trimmed().toLower();
    const int dvProfile =
        stream.value(QStringLiteral("dolbyVisionProfile")).toInt();
    const int compatibilityId = stream.value(
        QStringLiteral("dolbyVisionBlSignalCompatibilityId")).toInt();
    const QString range = stream.value(
        QStringLiteral("videoRange")).toString().trimmed().toUpper();
    const QString rangeType = stream.value(
        QStringLiteral("videoRangeType")).toString().trimmed().toUpper();
    const bool hasHdr10OrHlgBaseLayer = metadata.contains(QStringLiteral("hdr10"))
        || metadata.contains(QStringLiteral("hlg"))
        || compatibilityId == 1 || compatibilityId == 4 || compatibilityId == 6;
    const bool isDolbyVision = dvProfile > 0
        || metadata.contains(QStringLiteral("dovi"))
        || metadata.contains(QStringLiteral("dolby vision"))
        || metadata.contains(QStringLiteral("dvhe"))
        || metadata.contains(QStringLiteral("dvh1"));

    if (isDolbyVision) {
        if (dvProfile == 7 || dvProfile == 8
            || metadataContainsDolbyVisionProfile(metadata, 7)
            || metadataContainsDolbyVisionProfile(metadata, 8)
            || hasHdr10OrHlgBaseLayer) {
            return HdrContentKind::DolbyVisionCompatible;
        }
        return HdrContentKind::DolbyVisionUnsupported;
    }
    if (rangeTokenIndicatesHdr(range)
        || rangeTokenIndicatesHdr(rangeType)
        || hasHdr10OrHlgBaseLayer) {
        return HdrContentKind::Hdr;
    }
    return HdrContentKind::Sdr;
}

QString normalizedStringKey(const QString &value)
{
    return value.trimmed().toLower();
}

QString mediaSourceVideoDescriptor(const QVariantMap &mediaSource)
{
    QString codec;
    QString profile;
    QString videoRange;
    int width = 0;
    int height = 0;
    const QVariantList streams = mediaStreamsForType(
        mediaSource, QStringLiteral("Video"));
    if (!streams.isEmpty()) {
        const QVariantMap videoStream = streams.first().toMap();
        codec = normalizedStringKey(
            videoStream.value(QStringLiteral("codec")).toString());
        profile = normalizedStringKey(
            videoStream.value(QStringLiteral("profile")).toString());
        videoRange = normalizedStringKey(
            videoStream.value(QStringLiteral("videoRange")).toString());
        width = videoStream.value(QStringLiteral("width")).toInt();
        height = videoStream.value(QStringLiteral("height")).toInt();
    }
    return QStringLiteral("%1|%2|%3|%4x%5|%6|%7")
        .arg(codec,
             profile,
             videoRange,
             QString::number(width),
             QString::number(height),
             normalizedStringKey(
                 mediaSource.value(QStringLiteral("container")).toString()),
             QString::number(
                 mediaSource.value(QStringLiteral("bitRate")).toInt()));
}

} // namespace

QString normalizeLanguageCode(const QString &language)
{
    const QString normalized = language.trimmed().toLower();
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("en"), QStringLiteral("eng")}, {QStringLiteral("eng"), QStringLiteral("eng")},
        {QStringLiteral("ja"), QStringLiteral("jpn")}, {QStringLiteral("jpn"), QStringLiteral("jpn")},
        {QStringLiteral("es"), QStringLiteral("spa")}, {QStringLiteral("spa"), QStringLiteral("spa")},
        {QStringLiteral("fr"), QStringLiteral("fre")}, {QStringLiteral("fre"), QStringLiteral("fre")},
        {QStringLiteral("fra"), QStringLiteral("fre")},
        {QStringLiteral("de"), QStringLiteral("ger")}, {QStringLiteral("ger"), QStringLiteral("ger")},
        {QStringLiteral("deu"), QStringLiteral("ger")},
        {QStringLiteral("it"), QStringLiteral("ita")}, {QStringLiteral("ita"), QStringLiteral("ita")},
        {QStringLiteral("pt"), QStringLiteral("por")}, {QStringLiteral("por"), QStringLiteral("por")},
        {QStringLiteral("ru"), QStringLiteral("rus")}, {QStringLiteral("rus"), QStringLiteral("rus")},
        {QStringLiteral("zh"), QStringLiteral("chi")}, {QStringLiteral("chi"), QStringLiteral("chi")},
        {QStringLiteral("zho"), QStringLiteral("chi")},
        {QStringLiteral("ko"), QStringLiteral("kor")}, {QStringLiteral("kor"), QStringLiteral("kor")},
        {QStringLiteral("ar"), QStringLiteral("ara")}, {QStringLiteral("ara"), QStringLiteral("ara")},
        {QStringLiteral("hi"), QStringLiteral("hin")}, {QStringLiteral("hin"), QStringLiteral("hin")},
        {QStringLiteral("nl"), QStringLiteral("dut")}, {QStringLiteral("dut"), QStringLiteral("dut")},
        {QStringLiteral("nld"), QStringLiteral("dut")},
        {QStringLiteral("sv"), QStringLiteral("swe")}, {QStringLiteral("swe"), QStringLiteral("swe")},
        {QStringLiteral("no"), QStringLiteral("nor")}, {QStringLiteral("nor"), QStringLiteral("nor")},
        {QStringLiteral("da"), QStringLiteral("dan")}, {QStringLiteral("dan"), QStringLiteral("dan")},
        {QStringLiteral("fi"), QStringLiteral("fin")}, {QStringLiteral("fin"), QStringLiteral("fin")},
        {QStringLiteral("pl"), QStringLiteral("pol")}, {QStringLiteral("pol"), QStringLiteral("pol")},
        {QStringLiteral("tr"), QStringLiteral("tur")}, {QStringLiteral("tur"), QStringLiteral("tur")},
        {QStringLiteral("cs"), QStringLiteral("cze")}, {QStringLiteral("cze"), QStringLiteral("cze")},
        {QStringLiteral("ces"), QStringLiteral("cze")},
        {QStringLiteral("el"), QStringLiteral("gre")}, {QStringLiteral("gre"), QStringLiteral("gre")},
        {QStringLiteral("ell"), QStringLiteral("gre")},
        {QStringLiteral("he"), QStringLiteral("heb")}, {QStringLiteral("heb"), QStringLiteral("heb")},
        {QStringLiteral("id"), QStringLiteral("ind")}, {QStringLiteral("ind"), QStringLiteral("ind")},
        {QStringLiteral("tha"), QStringLiteral("tha")}, {QStringLiteral("th"), QStringLiteral("tha")},
        {QStringLiteral("vi"), QStringLiteral("vie")}, {QStringLiteral("vie"), QStringLiteral("vie")}
    };
    return aliases.value(normalized, normalized);
}

int bestLanguageStreamIndex(const QVariantList &streams,
                            const QString &language,
                            bool subtitle)
{
    const QString preference = normalizeLanguageCode(language);
    if (preference.isEmpty()) {
        return -1;
    }
    int bestIndex = -1;
    int bestScore = -1;
    int ordinal = 0;
    for (const QVariant &streamVariant : streams) {
        const QVariantMap stream = streamVariant.toMap();
        if (normalizeLanguageCode(
                stream.value(QStringLiteral("language")).toString())
            != preference) {
            ++ordinal;
            continue;
        }
        int score = 10000 - ordinal;
        if (stream.value(QStringLiteral("isDefault"), false).toBool()) {
            score += 100000;
        } else if (subtitle) {
            const bool forced =
                stream.value(QStringLiteral("isForced"), false).toBool();
            const bool hearingImpaired = stream.value(
                QStringLiteral("isHearingImpaired"), false).toBool();
            score += !forced && !hearingImpaired ? 50000
                : forced ? 30000 : 10000;
        }
        if (score > bestScore) {
            bestScore = score;
            bestIndex = stream.value(QStringLiteral("index"), -1).toInt();
        }
        ++ordinal;
    }
    return bestIndex;
}

ResolvedTrackSelection resolveTrackSelection(
    const QVariantMap &mediaSource,
    const ScopedTrackPreferences &preferences,
    const QString &defaultAudioSelection,
    const QString &defaultSubtitleSelection,
    int preferredAudioIndex,
    int preferredSubtitleIndex)
{
    const QVariantList audioStreams = mediaStreamsForType(
        mediaSource, QStringLiteral("Audio"));
    const QVariantList subtitleStreams = embeddedFirst(mediaStreamsForType(
        mediaSource, QStringLiteral("Subtitle")));
    const auto providerAudio = [&]() -> std::pair<int, QString> {
        const int index = mediaSource.value(
            QStringLiteral("defaultAudioStreamIndex"), -1).toInt();
        return index >= 0 && hasStreamIndex(audioStreams, index)
            ? std::pair{index, QStringLiteral("jellyfin-default")}
            : std::pair{-1, QString{}};
    };
    const auto fileAudio = [&]() -> std::pair<int, QString> {
        const int index = firstMatchingStreamIndex(audioStreams,
            [](const QVariantMap &stream) {
                return stream.value(QStringLiteral("isDefault"), false).toBool();
            });
        return index >= 0 ? std::pair{index, QStringLiteral("file-default")}
                          : std::pair{-1, QString{}};
    };
    const auto builtInAudio = [&]() -> std::pair<int, QString> {
        const auto provider = providerAudio();
        if (provider.first >= 0) return provider;
        const auto file = fileAudio();
        if (file.first >= 0) return file;
        const int index = firstMatchingStreamIndex(audioStreams,
                                                   [](const QVariantMap &) { return true; });
        return {index, index >= 0 ? QStringLiteral("fallback")
                                  : QStringLiteral("none")};
    };
    const auto providerSubtitle = [&]() -> std::pair<int, QString> {
        const int index = mediaSource.value(
            QStringLiteral("defaultSubtitleStreamIndex"), -1).toInt();
        return index >= 0 && hasStreamIndex(subtitleStreams, index)
            ? std::pair{index, QStringLiteral("jellyfin-default")}
            : std::pair{-1, QString{}};
    };
    const auto fileSubtitle = [&]() -> std::pair<int, QString> {
        const int index = firstMatchingStreamIndex(subtitleStreams,
            [](const QVariantMap &stream) {
                return stream.value(QStringLiteral("isDefault"), false).toBool();
            });
        return index >= 0 ? std::pair{index, QStringLiteral("file-default")}
                          : std::pair{-1, QString{}};
    };
    const auto builtInSubtitle = [&]() -> std::pair<int, QString> {
        const auto provider = providerSubtitle();
        if (provider.first >= 0) return provider;
        const auto file = fileSubtitle();
        if (file.first >= 0) return file;
        const int forced = firstMatchingStreamIndex(subtitleStreams,
            [](const QVariantMap &stream) {
                return stream.value(QStringLiteral("isForced"), false).toBool();
            });
        return forced >= 0
            ? std::pair{forced, QStringLiteral("forced-default")}
            : std::pair{-1, QStringLiteral("fallback-off")};
    };
    const auto globalAudio = [&]() -> std::pair<int, QString> {
        if (defaultAudioSelection == QStringLiteral("file-default")) {
            const auto file = fileAudio();
            return file.first >= 0 ? file : builtInAudio();
        }
        if (defaultAudioSelection != QStringLiteral("jellyfin-default")) {
            const int index = bestLanguageStreamIndex(
                audioStreams, defaultAudioSelection, false);
            if (index >= 0) return {index, QStringLiteral("global-language")};
        }
        return builtInAudio();
    };
    const auto globalSubtitle = [&]() -> std::pair<int, QString> {
        if (defaultSubtitleSelection == QStringLiteral("off")) {
            return {-1, QStringLiteral("global-off")};
        }
        if (defaultSubtitleSelection == QStringLiteral("forced")) {
            const int forced = firstMatchingStreamIndex(subtitleStreams,
                [](const QVariantMap &stream) {
                    return stream.value(QStringLiteral("isForced"), false).toBool();
                });
            return forced >= 0
                ? std::pair{forced, QStringLiteral("global-forced")}
                : std::pair{-1, QStringLiteral("global-forced-off")};
        }
        if (defaultSubtitleSelection == QStringLiteral("file-default")) {
            const auto file = fileSubtitle();
            return file.first >= 0 ? file : builtInSubtitle();
        }
        if (defaultSubtitleSelection != QStringLiteral("jellyfin-default")) {
            const int index = bestLanguageStreamIndex(
                subtitleStreams, defaultSubtitleSelection, true);
            if (index >= 0) return {index, QStringLiteral("global-language")};
        }
        return builtInSubtitle();
    };

    std::pair<int, QString> audio;
    if (preferredAudioIndex >= 0
        && hasStreamIndex(audioStreams, preferredAudioIndex)) {
        audio = {preferredAudioIndex, QStringLiteral("override")};
    } else if (preferences.audio.mode == TrackPreferenceMode::ExplicitStream
               && hasStreamIndex(audioStreams,
                                 preferences.audio.streamIndex)) {
        audio = {preferences.audio.streamIndex, QStringLiteral("explicit")};
    } else {
        audio = globalAudio();
    }

    std::pair<int, QString> subtitle;
    if (preferredSubtitleIndex == -1) {
        subtitle = {-1, QStringLiteral("override-off")};
    } else if (preferredSubtitleIndex >= 0
               && hasStreamIndex(subtitleStreams, preferredSubtitleIndex)) {
        subtitle = {preferredSubtitleIndex, QStringLiteral("override")};
    } else if (preferences.subtitle.mode == TrackPreferenceMode::Off) {
        subtitle = {-1, QStringLiteral("explicit-off")};
    } else if (preferences.subtitle.mode == TrackPreferenceMode::ExplicitStream
               && hasStreamIndex(subtitleStreams,
                                 preferences.subtitle.streamIndex)) {
        subtitle = {preferences.subtitle.streamIndex, QStringLiteral("explicit")};
    } else {
        subtitle = globalSubtitle();
    }
    return {audio.first, subtitle.first, audio.second, subtitle.second};
}

HdrContentKind classifyMediaSourceHdr(const QVariantMap &mediaSource)
{
    HdrContentKind result = HdrContentKind::Sdr;
    const QString sourceMetadata = normalizedMediaSourceMetadataText(mediaSource);
    for (const QVariant &streamVariant : mediaStreamsForType(
             mediaSource, QStringLiteral("Video"))) {
        const HdrContentKind kind = classifyVideoStream(
            streamVariant.toMap(), sourceMetadata);
        if (kind == HdrContentKind::DolbyVisionUnsupported) return kind;
        if (kind == HdrContentKind::DolbyVisionCompatible) result = kind;
        else if (kind == HdrContentKind::Hdr
                 && result == HdrContentKind::Sdr) result = kind;
    }
    return result;
}

bool isHdr(HdrContentKind kind)
{
    return kind != HdrContentKind::Sdr;
}

bool shouldToneMapToSdr(HdrContentKind kind,
                        const QString &dolbyVisionFallbackMode)
{
    return kind == HdrContentKind::DolbyVisionUnsupported
        && dolbyVisionFallbackMode != QStringLiteral("experimental-direct-play");
}

QString hdrContentKindName(HdrContentKind kind)
{
    switch (kind) {
    case HdrContentKind::Sdr: return QStringLiteral("sdr");
    case HdrContentKind::Hdr: return QStringLiteral("hdr");
    case HdrContentKind::DolbyVisionCompatible:
        return QStringLiteral("dolby-vision-compatible");
    case HdrContentKind::DolbyVisionUnsupported:
        return QStringLiteral("dolby-vision-unsupported");
    }
    return QStringLiteral("unknown");
}

HdrPlaybackPolicy evaluateHdrPlayback(bool contentIsHdr,
                                      bool contentShouldToneMapToSdr,
                                      bool hdrEnabled,
                                      const QString &hdrOutputMode)
{
    HdrPlaybackPolicy policy;
    if (!contentIsHdr) return policy;
    policy.toneMapToSdr = contentShouldToneMapToSdr
        || !hdrEnabled
        || hdrOutputMode == QStringLiteral("tone-map-to-sdr");
    policy.outputHdr = hdrEnabled
        && !policy.toneMapToSdr
        && (hdrOutputMode == QStringLiteral("match-content")
            || hdrOutputMode == QStringLiteral("force-hdr-experimental"));
    policy.shouldToggleDisplayHdr = policy.outputHdr;
    return policy;
}

QString mediaSourceParentPath(const QVariantMap &mediaSource)
{
    QFileInfo info(mediaSource.value(QStringLiteral("path")).toString());
    return normalizedStringKey(
        info.dir().absolutePath().replace(QLatin1Char('\\'), QLatin1Char('/')));
}

QString mediaSourceSignature(const QVariantMap &mediaSource)
{
    return mediaSourceVideoDescriptor(mediaSource);
}

QString buildVersionSubtitle(const QVariantMap &mediaSource)
{
    QStringList parts;
    const QVariantList videos = mediaStreamsForType(mediaSource,
                                                    QStringLiteral("Video"));
    if (!videos.isEmpty()) {
        const QVariantMap video = videos.first().toMap();
        const int width = video.value(QStringLiteral("width")).toInt();
        const int height = video.value(QStringLiteral("height")).toInt();
        if (width > 0 && height > 0) {
            parts.append(QStringLiteral("%1x%2").arg(width).arg(height));
        }
        const QString range = video.value(
            QStringLiteral("videoRange")).toString().trimmed();
        if (!range.isEmpty()
            && range.compare(QStringLiteral("SDR"), Qt::CaseInsensitive) != 0) {
            parts.append(range.toUpper());
        }
        const QString codec = video.value(
            QStringLiteral("codec")).toString().trimmed();
        if (!codec.isEmpty()) parts.append(codec.toUpper());
        const QString profile = video.value(
            QStringLiteral("profile")).toString().trimmed();
        if (!profile.isEmpty()) parts.append(profile);
    }
    const QString container = mediaSource.value(
        QStringLiteral("container")).toString().trimmed();
    if (!container.isEmpty()) parts.append(container.toUpper());
    const int bitrate = mediaSource.value(QStringLiteral("bitRate")).toInt();
    if (bitrate > 0) {
        parts.append(QStringLiteral("%1 Mbps").arg(
            QString::number(double(bitrate) / 1000000.0, 'f', 1)));
    }
    return parts.join(QStringLiteral(" • "));
}

namespace {

struct VersionMetrics
{
    qint64 pixels = 0;
    bool hdr = false;
    qint64 bitRate = 0;
    qint64 size = 0;
};

VersionMetrics versionMetrics(const QVariantMap &mediaSource)
{
    VersionMetrics metrics;
    const QVariantList videos = mediaStreamsForType(mediaSource,
                                                    QStringLiteral("Video"));
    if (!videos.isEmpty()) {
        const QVariantMap video = videos.first().toMap();
        metrics.pixels =
            qint64(qMax(0, video.value(QStringLiteral("width")).toInt()))
            * qMax(0, video.value(QStringLiteral("height")).toInt());
    }
    metrics.hdr = isHdr(classifyMediaSourceHdr(mediaSource));
    metrics.bitRate = qMax<qint64>(
        0, mediaSource.value(QStringLiteral("bitRate")).toLongLong());
    metrics.size = qMax<qint64>(
        0, mediaSource.value(QStringLiteral("size")).toLongLong());
    return metrics;
}

// Smallest ranks ascending, so an unknown (zero) metric must sort last.
qint64 knownOrMax(qint64 value)
{
    return value > 0 ? value : std::numeric_limits<qint64>::max();
}

bool ranksAbove(const VersionMetrics &candidate,
                const VersionMetrics &best,
                VersionPreference preference)
{
    if (preference == VersionPreference::Smallest) {
        return std::tuple(knownOrMax(candidate.size),
                          knownOrMax(candidate.bitRate),
                          knownOrMax(candidate.pixels))
            < std::tuple(knownOrMax(best.size),
                         knownOrMax(best.bitRate),
                         knownOrMax(best.pixels));
    }
    return std::tuple(candidate.pixels, candidate.hdr,
                      candidate.bitRate, candidate.size)
        > std::tuple(best.pixels, best.hdr, best.bitRate, best.size);
}

} // namespace

VersionPreference parseVersionPreference(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("smallest")) {
        return VersionPreference::Smallest;
    }
    if (normalized == QStringLiteral("ask")) {
        return VersionPreference::Ask;
    }
    return VersionPreference::HighestQuality;
}

QString versionPreferenceName(VersionPreference preference)
{
    switch (preference) {
    case VersionPreference::Smallest: return QStringLiteral("smallest");
    case VersionPreference::Ask: return QStringLiteral("ask");
    case VersionPreference::HighestQuality: break;
    }
    return QStringLiteral("highest-quality");
}

QVariantMap preferredMediaSource(const QVariantList &mediaSources,
                                 VersionPreference preference)
{
    if (mediaSources.isEmpty()) return QVariantMap{};
    qsizetype bestIndex = 0;
    VersionMetrics best = versionMetrics(mediaSources.first().toMap());
    for (qsizetype i = 1; i < mediaSources.size(); ++i) {
        const VersionMetrics candidate =
            versionMetrics(mediaSources.at(i).toMap());
        if (ranksAbove(candidate, best, preference)) {
            bestIndex = i;
            best = candidate;
        }
    }
    return mediaSources.at(bestIndex).toMap();
}

QVariantMap selectMediaSource(const QVariantList &mediaSources,
                              const QString &forcedMediaSourceId,
                              bool useAffinityFallback,
                              const VersionAffinity &affinity,
                              VersionPreference preference)
{
    if (!forcedMediaSourceId.isEmpty()) {
        for (const QVariant &value : mediaSources) {
            const QVariantMap source = value.toMap();
            if (source.value(QStringLiteral("id")).toString()
                == forcedMediaSourceId) return source;
        }
    }
    if (useAffinityFallback) {
        const QString parentPath = normalizedStringKey(affinity.parentPath);
        if (!parentPath.isEmpty()) {
            for (const QVariant &value : mediaSources) {
                const QVariantMap source = value.toMap();
                if (mediaSourceParentPath(source) == parentPath) return source;
            }
        }
        const QString name = normalizedStringKey(affinity.name);
        if (!name.isEmpty()) {
            for (const QVariant &value : mediaSources) {
                const QVariantMap source = value.toMap();
                if (normalizedStringKey(
                        source.value(QStringLiteral("name")).toString()) == name) {
                    return source;
                }
            }
        }
        if (!affinity.signature.isEmpty()) {
            for (const QVariant &value : mediaSources) {
                const QVariantMap source = value.toMap();
                if (mediaSourceSignature(source) == affinity.signature) return source;
            }
        }
    }
    return preferredMediaSource(mediaSources, preference);
}

QVariantList primaryPresentationSources(const QVariantList &mediaSources)
{
    int firstPart = std::numeric_limits<int>::max();
    for (const QVariant &value : mediaSources) {
        const int part = value.toMap().value(
            QStringLiteral("presentationPartIndex")).toInt();
        if (part > 0) firstPart = qMin(firstPart, part);
    }
    if (firstPart == std::numeric_limits<int>::max()) return mediaSources;
    QVariantList result;
    for (const QVariant &value : mediaSources) {
        if (value.toMap().value(QStringLiteral("presentationPartIndex")).toInt()
            == firstPart) result.append(value);
    }
    return result;
}

QVariantList playbackVersionOptions(const QVariantList &primarySources,
                                    const QString &activeSourceId)
{
    QVariantList options;
    for (const QVariant &value : primarySources) {
        const QVariantMap source = value.toMap();
        const QString id = source.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) continue;
        options.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("title"), source.value(QStringLiteral("name")).toString().trimmed()},
            {QStringLiteral("subtitle"), buildVersionSubtitle(source)},
            {QStringLiteral("selected"), !activeSourceId.isEmpty() && id == activeSourceId}
        });
    }
    return options;
}

namespace {

QVariantList episodeRows(const QVariantList &items)
{
    QVariantList episodes;
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("mediaType")).toString() == QStringLiteral("Episode")
            && !item.value(QStringLiteral("itemId")).toString().isEmpty()) {
            episodes.append(item);
        }
    }
    return episodes;
}

} // namespace

AdjacentEpisodes resolveAdjacentEpisodes(const QVariantList &seasonItems,
                                         const QString &currentItemId)
{
    AdjacentEpisodes result;
    if (currentItemId.isEmpty()) return result;
    const QVariantList episodes = episodeRows(seasonItems);
    for (int i = 0; i < episodes.size(); ++i) {
        if (episodes.at(i).toMap().value(QStringLiteral("itemId")).toString() != currentItemId) {
            continue;
        }
        result.currentFound = true;
        if (i > 0) result.previous = episodes.at(i - 1).toMap();
        else result.previousInAdjacentSeason = true;
        if (i + 1 < episodes.size()) result.next = episodes.at(i + 1).toMap();
        else result.nextInAdjacentSeason = true;
        break;
    }
    return result;
}

QString adjacentSeasonId(const QVariantList &seriesItems,
                         const QString &currentSeasonId,
                         int direction)
{
    if (currentSeasonId.isEmpty() || direction == 0) return QString();
    QList<QVariantMap> seasons;
    int currentIndex = -1;
    for (const QVariant &value : seriesItems) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("mediaType")).toString() != QStringLiteral("Season")) continue;
        const QString id = item.value(QStringLiteral("itemId")).toString();
        if (id.isEmpty()) continue;
        if (id == currentSeasonId) currentIndex = int(seasons.size());
        seasons.append(item);
    }
    if (currentIndex < 0) return QString();
    const bool currentIsSpecials =
        seasons.at(currentIndex).value(QStringLiteral("indexNumber"), -1).toInt() == 0;
    const int step = direction < 0 ? -1 : 1;
    for (int i = currentIndex + step; i >= 0 && i < seasons.size(); i += step) {
        const QVariantMap season = seasons.at(i);
        if (season.contains(QStringLiteral("childCount"))
            && season.value(QStringLiteral("childCount")).toInt() == 0) continue;
        if (!currentIsSpecials && season.value(QStringLiteral("indexNumber"), -1).toInt() == 0) continue;
        return season.value(QStringLiteral("itemId")).toString();
    }
    return QString();
}

QVariantMap boundaryEpisode(const QVariantList &seasonItems, bool fromEnd)
{
    const QVariantList episodes = episodeRows(seasonItems);
    if (episodes.isEmpty()) return QVariantMap{};
    return (fromEnd ? episodes.last() : episodes.first()).toMap();
}

PlaybackContentKind playbackContentKindFromName(const QString &name)
{
    return name.trimmed().compare(QStringLiteral("extra"), Qt::CaseInsensitive) == 0
        ? PlaybackContentKind::Extra
        : PlaybackContentKind::Feature;
}

QString playbackContentKindName(PlaybackContentKind kind)
{
    return kind == PlaybackContentKind::Extra ? QStringLiteral("extra")
                                              : QStringLiteral("feature");
}

PlaybackContentRules playbackContentRules(PlaybackContentKind kind)
{
    PlaybackContentRules rules;
    if (kind == PlaybackContentKind::Extra) {
        rules.resumesFromRequestedPosition = false;
        rules.keepsSeriesContext = false;
        rules.allowsNextEpisode = false;
        rules.allowsUpNext = false;
        rules.persistsTrackPreferences = false;
        rules.persistsSubtitleDelay = false;
    }
    return rules;
}

bool meetsCompletionThreshold(const CompletionInput &input)
{
    return !input.alreadyEvaluated
        && !input.itemId.isEmpty()
        && !input.seriesId.isEmpty()
        && input.durationSeconds > 0.0
        && (input.positionSeconds / input.durationSeconds) * 100.0
            >= input.thresholdPercent;
}

bool shouldPrefetchNextEpisode(const PrefetchInput &input)
{
    return !input.alreadyRequested
        && input.playbackActive
        && !input.seriesId.isEmpty()
        && !input.itemId.isEmpty()
        && input.durationSeconds > 0.0
        && (input.positionSeconds / input.durationSeconds) * 100.0
            >= input.triggerPercent;
}

bool isUsablePrefetchedEpisode(const PrefetchedEpisodeInput &input)
{
    const QString episodeId = input.episode.value(
        QStringLiteral("itemId")).toString();
    return input.ready
        && !input.episode.isEmpty()
        && !episodeId.isEmpty()
        && !input.prefetchedSeriesId.isEmpty()
        && input.prefetchedSeriesId == input.pendingSeriesId
        && !input.prefetchedForItemId.isEmpty()
        && input.prefetchedForItemId == input.pendingItemId
        && episodeId != input.pendingItemId;
}

QString nextEpisodeRequestContext(const QString &mode,
                                  const QString &seriesId,
                                  const QString &itemId)
{
    if (mode.isEmpty() || seriesId.isEmpty() || itemId.isEmpty()) return {};
    return QStringLiteral("player:%1:%2:%3").arg(mode, seriesId, itemId);
}

EndFileReason endFileReasonFromName(const QString &name)
{
    const QString reason = name.trimmed().toLower();
    if (reason == QStringLiteral("eof")) return EndFileReason::Eof;
    if (reason == QStringLiteral("stop")) return EndFileReason::Stop;
    if (reason == QStringLiteral("quit")) return EndFileReason::Quit;
    if (reason == QStringLiteral("error")) return EndFileReason::Error;
    if (reason == QStringLiteral("redirect")) return EndFileReason::Redirect;
    return EndFileReason::Unknown;
}

EndFileReason endFileReasonFromLibmpv(int reason)
{
    // mpv_end_file_reason values from mpv/client.h (stable client API).
    switch (reason) {
    case 0: return EndFileReason::Eof;
    case 2: return EndFileReason::Stop;
    case 3: return EndFileReason::Quit;
    case 4: return EndFileReason::Error;
    case 5: return EndFileReason::Redirect;
    default: return EndFileReason::Unknown;
    }
}

QString endFileReasonName(EndFileReason reason)
{
    switch (reason) {
    case EndFileReason::Eof: return QStringLiteral("eof");
    case EndFileReason::Stop: return QStringLiteral("stop");
    case EndFileReason::Quit: return QStringLiteral("quit");
    case EndFileReason::Error: return QStringLiteral("error");
    case EndFileReason::Redirect: return QStringLiteral("redirect");
    case EndFileReason::Unknown: break;
    }
    return QStringLiteral("unknown");
}

EndFileOutcome classifyEndFile(EndFileReason reason, bool hasRemainingPlaylistItems)
{
    if (reason == EndFileReason::Redirect || hasRemainingPlaylistItems) {
        return EndFileOutcome::Continue;
    }
    return reason == EndFileReason::Error ? EndFileOutcome::Failed : EndFileOutcome::Ended;
}

QList<SkipRange> mergeSkipRanges(QList<SkipRange> ranges)
{
    ranges.removeIf([](const SkipRange &range) { return range.endMs <= range.startMs; });
    std::sort(ranges.begin(), ranges.end(), [](const SkipRange &a, const SkipRange &b) {
        return a.startMs < b.startMs;
    });
    QList<SkipRange> merged;
    for (const SkipRange &range : std::as_const(ranges)) {
        if (!merged.isEmpty() && range.startMs <= merged.last().endMs) {
            merged.last().endMs = std::max(merged.last().endMs, range.endMs);
        } else {
            merged.append(range);
        }
    }
    return merged;
}

int skipRangeIndex(const QList<SkipRange> &ranges, qint64 positionMs, qint64 aheadToleranceMs)
{
    int ahead = -1;
    for (int index = 0; index < ranges.size(); ++index) {
        const SkipRange &range = ranges.at(index);
        if (range.endMs <= range.startMs || range.endMs <= positionMs) {
            continue;
        }
        if (range.startMs <= positionMs) {
            return index;
        }
        if (range.startMs - positionMs > aheadToleranceMs) {
            continue;
        }
        if (ahead < 0 || range.startMs < ranges.at(ahead).startMs) {
            ahead = index;
        }
    }
    return ahead;
}

} // namespace Bloom::PlaybackPolicy
