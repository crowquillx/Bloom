#include "PlaybackPolicy.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
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

QString streamLanguage(const QVariantList &streams, int streamIndex)
{
    for (const QVariant &streamVariant : streams) {
        const QVariantMap stream = streamVariant.toMap();
        if (stream.value(QStringLiteral("index"), -1).toInt() == streamIndex) {
            return stream.value(QStringLiteral("language")).toString();
        }
    }
    return {};
}

bool sameText(const QString &left, const QString &right)
{
    return left.trimmed().compare(right.trimmed(), Qt::CaseInsensitive) == 0;
}

// A tag that names no spoken language: empty, und, mul, zxx, mis or private use.
bool isUndeterminedLanguage(const QString &language)
{
    return languageMatchRank(language, language) < 0;
}

// A subtitle track with only part of the dialogue: flagged forced, or titled
// as signs, songs or forced (fansub releases rarely set the flag).
bool isPartialSubtitle(const QVariantMap &stream)
{
    static const QRegularExpression partial(
        QStringLiteral("\\b(signs?|songs?|forced)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return stream.value(QStringLiteral("isForced")).toBool()
        || partial.match(stream.value(QStringLiteral("title")).toString()).hasMatch()
        || partial.match(stream.value(QStringLiteral("displayTitle")).toString()).hasMatch();
}

} // namespace

int languageMatchRank(const QString &candidate, const QString &preferred)
{
    const auto split = [](const QString &tag) {
        QString normalized = tag.trimmed().toLower();
        normalized.replace(QLatin1Char('_'), QLatin1Char('-'));
        const qsizetype dash = normalized.indexOf(QLatin1Char('-'));
        const QString primary = dash < 0 ? normalized : normalized.left(dash);
        return std::pair{primary, dash < 0 ? QString{} : normalized.mid(dash)};
    };
    const auto [candidatePrimary, candidateRest] = split(candidate);
    const auto [preferredPrimary, preferredRest] = split(preferred);
    // und/mul/zxx/mis name no spoken language; x-/i- tags are private use.
    static const QStringList nonLanguages{
        QStringLiteral("und"), QStringLiteral("mul"), QStringLiteral("zxx"),
        QStringLiteral("mis")};
    const auto isLanguage = [](const QString &primary) {
        return primary.size() >= 2 && primary.size() <= 3 && !nonLanguages.contains(primary);
    };
    if (!isLanguage(candidatePrimary) || !isLanguage(preferredPrimary)
        || normalizeLanguageCode(candidatePrimary) != normalizeLanguageCode(preferredPrimary)) {
        return -1;
    }
    return candidateRest == preferredRest ? 0 : 1;
}

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
    int preferredSubtitleIndex,
    bool useServerPreferences)
{
    const Bloom::ServerTrackPreferences server = useServerPreferences
        ? Bloom::ServerTrackPreferences::fromVariantMap(
              mediaSource.value(QStringLiteral("serverTrackPreferences")).toMap())
        : Bloom::ServerTrackPreferences{};
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
    } else if (const int local = explicitStreamIndex(audioStreams, preferences.audio, false);
               local >= 0) {
        audio = {local, QStringLiteral("explicit")};
    } else if (const ServerTrackChoice choice = serverAudioChoice(audioStreams, server);
               !choice.source.isEmpty()) {
        audio = {choice.index, choice.source};
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
    } else if (const int local = explicitStreamIndex(subtitleStreams, preferences.subtitle, true);
               local >= 0) {
        subtitle = {local, QStringLiteral("explicit")};
    } else if (const ServerTrackChoice choice = serverSubtitleChoice(
                   subtitleStreams, server, streamLanguage(audioStreams, audio.first));
               !choice.source.isEmpty()) {
        subtitle = {choice.index, choice.source};
    } else {
        subtitle = globalSubtitle();
    }
    return {audio.first, subtitle.first, audio.second, subtitle.second};
}

namespace {

// Which subtitle tracks a language lookup considers.
enum class SubtitleTracks { Any, Forced, FullDialogue };

// Best stream in `language`: closest language tag first, then (audio) the
// default flag or (subtitles) full dialogue over forced over hearing
// impaired, then stream order, so an embedded track beats an external file.
int serverLanguageStreamIndex(const QVariantList &streams, const QString &language,
                              bool subtitle, SubtitleTracks tracks = SubtitleTracks::Any)
{
    int bestIndex = -1;
    std::tuple<int, int, int> bestKey{std::numeric_limits<int>::max(), 0, 0};
    int ordinal = 0;
    for (const QVariant &streamVariant : streams) {
        const QVariantMap stream = streamVariant.toMap();
        const int position = ordinal++;
        const bool forced = stream.value(QStringLiteral("isForced")).toBool();
        if ((tracks == SubtitleTracks::Forced && !forced)
            || (tracks == SubtitleTracks::FullDialogue && isPartialSubtitle(stream))) {
            continue;
        }
        const int rank = languageMatchRank(
            stream.value(QStringLiteral("language")).toString(), language);
        if (rank < 0) {
            continue;
        }
        const int priority = subtitle
            ? (forced ? 2 : 0) + (stream.value(QStringLiteral("isHearingImpaired")).toBool() ? 1 : 0)
            : (stream.value(QStringLiteral("isDefault")).toBool() ? 0 : 1);
        const std::tuple<int, int, int> key{rank, priority, position};
        if (bestIndex < 0 || key < bestKey) {
            bestKey = key;
            bestIndex = stream.value(QStringLiteral("index"), -1).toInt();
        }
    }
    return bestIndex;
}

bool matchesSubtitleSignature(const QVariantMap &stream,
                              const Bloom::ServerSubtitleSignature &signature)
{
    const QString source = stream.value(QStringLiteral("isExternal")).toBool()
        ? QStringLiteral("external") : QStringLiteral("embedded");
    const QString signatureSource = sameText(signature.source, QStringLiteral("downloaded"))
        ? QStringLiteral("external") : signature.source;
    const QString title = stream.value(QStringLiteral("title")).toString();
    const QString label = title.isEmpty()
        ? stream.value(QStringLiteral("language")).toString() : title;
    return sameText(source, signatureSource)
        && sameText(stream.value(QStringLiteral("language")).toString(), signature.language)
        && sameText(stream.value(QStringLiteral("codec")).toString(), signature.codec)
        && (signature.label.trimmed().isEmpty() || sameText(label, signature.label)
            || sameText(stream.value(QStringLiteral("displayTitle")).toString(), signature.label))
        && stream.value(QStringLiteral("isForced")).toBool() == signature.forced
        && stream.value(QStringLiteral("isHearingImpaired")).toBool() == signature.hearingImpaired;
}

int signatureStreamIndex(const QVariantList &streams,
                         const Bloom::ServerSubtitleSignature &signature)
{
    if (signature.isEmpty()) {
        return -1;
    }
    return firstMatchingStreamIndex(streams, [&signature](const QVariantMap &stream) {
        return matchesSubtitleSignature(stream, signature);
    });
}

} // namespace

ServerTrackChoice serverAudioChoice(const QVariantList &audioStreams,
                                    const Bloom::ServerTrackPreferences &server)
{
    if (server.audioStreamIndex >= 0 && hasStreamIndex(audioStreams, server.audioStreamIndex)) {
        return {server.audioStreamIndex, QStringLiteral("server")};
    }
    if (server.audioLanguage.trimmed().isEmpty()) {
        return {};
    }
    if (server.playDefaultAudioTrack) {
        const int index = firstMatchingStreamIndex(audioStreams, [](const QVariantMap &stream) {
            return stream.value(QStringLiteral("isDefault")).toBool();
        });
        if (index >= 0) {
            return {index, QStringLiteral("server-default-track")};
        }
    }
    const int index = serverLanguageStreamIndex(audioStreams, server.audioLanguage, false);
    return index >= 0 ? ServerTrackChoice{index, QStringLiteral("server-language")}
                      : ServerTrackChoice{};
}

ServerTrackChoice serverSubtitleChoice(const QVariantList &subtitleStreams,
                                       const Bloom::ServerTrackPreferences &server,
                                       const QString &audioLanguage)
{
    const QString mode = server.subtitleMode;
    if (mode.isEmpty()) {
        return {};
    }
    const bool showForced = server.showForcedSubtitles.value_or(true);
    const auto forcedForAudio = [&]() -> ServerTrackChoice {
        if (showForced) {
            const int index = serverLanguageStreamIndex(subtitleStreams, audioLanguage, true,
                                                        SubtitleTracks::Forced);
            if (index >= 0) {
                return {index, QStringLiteral("server-forced")};
            }
        }
        return {-1, QStringLiteral("server-off")};
    };
    if (mode == QStringLiteral("off")) {
        return forcedForAudio();
    }
    const QString language = server.subtitleLanguage.trimmed();
    if (mode == QStringLiteral("forced")) {
        // Jellyfin's OnlyForced: a forced track in the subtitle language or
        // in no named language, then one for the audio, then any.
        int index = serverLanguageStreamIndex(subtitleStreams, language, true,
                                              SubtitleTracks::Forced);
        if (index < 0) {
            index = firstMatchingStreamIndex(subtitleStreams, [](const QVariantMap &stream) {
                return stream.value(QStringLiteral("isForced")).toBool()
                    && isUndeterminedLanguage(stream.value(QStringLiteral("language")).toString());
            });
        }
        if (index < 0) {
            index = serverLanguageStreamIndex(subtitleStreams, audioLanguage, true,
                                              SubtitleTracks::Forced);
        }
        if (index < 0) {
            index = firstMatchingStreamIndex(subtitleStreams, [](const QVariantMap &stream) {
                return stream.value(QStringLiteral("isForced")).toBool();
            });
        }
        return index >= 0 ? ServerTrackChoice{index, QStringLiteral("server-forced")}
                          : ServerTrackChoice{-1, QStringLiteral("server-forced-off")};
    }
    if (mode == QStringLiteral("always")) {
        const int signature = signatureStreamIndex(subtitleStreams, server.subtitleSignature);
        if (signature >= 0) {
            return {signature, QStringLiteral("server-signature")};
        }
    } else if (mode == QStringLiteral("auto")) {
        // Audio already in the subtitle language needs only its forced track.
        if (language.isEmpty() || languageMatchRank(audioLanguage, language) >= 0) {
            return forcedForAudio();
        }
        const int signature = signatureStreamIndex(subtitleStreams, server.subtitleSignature);
        if (signature >= 0
            && languageMatchRank(streamLanguage(subtitleStreams, signature), language) >= 0) {
            return {signature, QStringLiteral("server-signature")};
        }
    } else {
        return {};
    }
    // Full dialogue only: a signs or songs track in the language does not
    // translate the dialogue, so the file cannot meet the preference then.
    const int index = language.isEmpty()
        ? -1 : serverLanguageStreamIndex(subtitleStreams, language, true,
                                         SubtitleTracks::FullDialogue);
    // A preference this file cannot meet says nothing about it.
    return index >= 0 ? ServerTrackChoice{index, QStringLiteral("server-language")}
                      : ServerTrackChoice{};
}

TrackSelectionPreference explicitTrackPreference(const QVariantMap &mediaSource,
                                                 int streamIndex,
                                                 bool subtitle)
{
    TrackSelectionPreference result;
    if (subtitle && streamIndex == -1) {
        result.mode = TrackPreferenceMode::Off;
        return result;
    }
    if (streamIndex < 0) {
        return result;
    }
    result.mode = TrackPreferenceMode::ExplicitStream;
    result.streamIndex = streamIndex;
    const QVariantList streams = mediaStreamsForType(
        mediaSource, subtitle ? QStringLiteral("Subtitle") : QStringLiteral("Audio"));
    for (const QVariant &streamVariant : streams) {
        const QVariantMap stream = streamVariant.toMap();
        if (stream.value(QStringLiteral("index"), -1).toInt() != streamIndex) {
            continue;
        }
        result.preferredLanguage = stream.value(QStringLiteral("language")).toString().trimmed();
        result.title = stream.value(QStringLiteral("title")).toString().trimmed();
        result.codec = stream.value(QStringLiteral("codec")).toString().trimmed();
        if (subtitle) {
            result.forcedOnly = stream.value(QStringLiteral("isForced")).toBool();
            result.hearingImpaired = stream.value(QStringLiteral("isHearingImpaired")).toBool();
        } else {
            result.channels = qMax(0, stream.value(QStringLiteral("channels")).toInt());
        }
        break;
    }
    return result;
}

int explicitStreamIndex(const QVariantList &streams,
                        const TrackSelectionPreference &preference,
                        bool subtitle)
{
    if (preference.mode != TrackPreferenceMode::ExplicitStream) {
        return -1;
    }
    const bool knowsTrack = !preference.preferredLanguage.isEmpty()
        || !preference.title.isEmpty() || !preference.codec.isEmpty()
        || preference.channels > 0 || preference.forcedOnly || preference.hearingImpaired;
    if (!knowsTrack) {
        return hasStreamIndex(streams, preference.streamIndex) ? preference.streamIndex : -1;
    }
    const bool pickedUndetermined = isUndeterminedLanguage(preference.preferredLanguage);
    const bool pickedPartial = subtitle
        && isPartialSubtitle(QVariantMap{{QStringLiteral("isForced"), preference.forcedOnly},
                                         {QStringLiteral("title"), preference.title}});
    int bestIndex = -1;
    // Larger is better, compared in order.
    using Key = std::tuple<bool, bool, bool, bool, bool, bool, int>;
    Key bestKey{};
    int ordinal = 0;
    for (const QVariant &streamVariant : streams) {
        const QVariantMap stream = streamVariant.toMap();
        const int position = ordinal++;
        const QString language = stream.value(QStringLiteral("language")).toString();
        const int rank = pickedUndetermined
            ? (isUndeterminedLanguage(language) ? 0 : -1)
            : languageMatchRank(language, preference.preferredLanguage);
        if (rank < 0
            || (subtitle
                && (stream.value(QStringLiteral("isForced")).toBool() != preference.forcedOnly
                    || isPartialSubtitle(stream) != pickedPartial))) {
            continue;
        }
        const int index = stream.value(QStringLiteral("index"), -1).toInt();
        const int channels = stream.value(QStringLiteral("channels")).toInt();
        const Key key{
            sameText(stream.value(QStringLiteral("title")).toString(), preference.title),
            !subtitle
                || stream.value(QStringLiteral("isHearingImpaired")).toBool()
                    == preference.hearingImpaired,
            preference.codec.isEmpty()
                || sameText(stream.value(QStringLiteral("codec")).toString(), preference.codec),
            preference.channels <= 0 || channels == preference.channels,
            rank == 0,
            index == preference.streamIndex,
            -position};
        if (bestIndex < 0 || key > bestKey) {
            bestKey = key;
            bestIndex = index;
        }
    }
    return bestIndex;
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

QList<int> stillWatchingThresholdChoices()
{
    return {0, 2, 3, 4, 5};
}

int normalizeStillWatchingThreshold(int threshold)
{
    if (threshold <= 0) {
        return 0;
    }
    return std::clamp(threshold, 2, 5);
}

int autoplayChainCountAfterStart(int currentCount, bool startedByAutoplay)
{
    return startedByAutoplay ? std::max(0, currentCount) + 1 : 0;
}

bool shouldAskStillWatching(int chainCount, int threshold)
{
    const int normalized = normalizeStillWatchingThreshold(threshold);
    return normalized > 0 && chainCount >= normalized;
}

QList<int> sleepTimerChoices()
{
    return {kSleepTimerOff, 15, 30, 45, 60, 90, kSleepTimerEndOfItem};
}

int normalizeSleepTimerChoice(int choice)
{
    return sleepTimerChoices().contains(choice) ? choice : kSleepTimerOff;
}

SleepTimerKind sleepTimerKind(int choice)
{
    const int normalized = normalizeSleepTimerChoice(choice);
    if (normalized == kSleepTimerEndOfItem) {
        return SleepTimerKind::EndOfItem;
    }
    return normalized > 0 ? SleepTimerKind::Duration : SleepTimerKind::Off;
}

int sleepTimerDurationSeconds(int choice)
{
    return sleepTimerKind(choice) == SleepTimerKind::Duration
        ? normalizeSleepTimerChoice(choice) * 60
        : 0;
}

bool sleepTimerSurvivesPlaybackEnd(int choice, SleepTimerPlaybackEnd end)
{
    switch (sleepTimerKind(choice)) {
    case SleepTimerKind::Off:
        return false;
    case SleepTimerKind::EndOfItem:
        // Recovery continues the same item, so its end is still ahead.
        return end == SleepTimerPlaybackEnd::Failed;
    case SleepTimerKind::Duration:
        return end != SleepTimerPlaybackEnd::Stopped;
    }
    return false;
}

bool sleepTimerFiresAtPlaybackEnd(int choice, SleepTimerPlaybackEnd end)
{
    return sleepTimerKind(choice) == SleepTimerKind::EndOfItem
        && end == SleepTimerPlaybackEnd::Finished;
}

int sleepTimerChoiceForNewPlayback(int currentChoice, int defaultChoice, SleepTimerStart start)
{
    const SleepTimerKind kind = sleepTimerKind(currentChoice);
    switch (start) {
    case SleepTimerStart::New:
        break;
    case SleepTimerStart::UpNext:
        return kind == SleepTimerKind::Duration ? normalizeSleepTimerChoice(currentChoice) : kSleepTimerOff;
    case SleepTimerStart::EpisodeSkip:
        return normalizeSleepTimerChoice(currentChoice);
    }
    return normalizeSleepTimerChoice(defaultChoice);
}

} // namespace Bloom::PlaybackPolicy
