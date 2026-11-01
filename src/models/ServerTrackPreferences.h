#pragma once

#include <QString>
#include <QVariantMap>

#include <optional>

namespace Bloom {

/// A subtitle track as the server remembers it, so the choice survives files
/// whose track order differs. Matching is by every field below; an empty
/// label matches any label.
struct ServerSubtitleSignature
{
    QString source; // embedded or external
    QString language;
    QString codec;
    QString label;
    bool forced = false;
    bool hearingImpaired = false;

    [[nodiscard]] bool isEmpty() const
    {
        return source.isEmpty() && language.isEmpty() && codec.isEmpty() && label.isEmpty()
            && !forced && !hearingImpaired;
    }
};

/// The audio and subtitle preferences a server resolved for one file (Silo)
/// or holds for the account (Jellyfin). Every member is optional: an empty
/// member means the server has no preference for it, and Bloom's own
/// defaults decide. The media source map carries it as
/// `serverTrackPreferences`.
struct ServerTrackPreferences
{
    /// The server's chosen audio track as Bloom's source stream index.
    int audioStreamIndex = -1;
    /// Preferred audio language (a BCP 47 tag or ISO 639 code).
    QString audioLanguage;
    /// With an audio language, the file's default track still wins
    /// (Jellyfin's PlayDefaultAudioTrack).
    bool playDefaultAudioTrack = false;

    /// "" (no preference), "off", "always", "auto" (full subtitles only when
    /// the audio is in another language) or "forced" (forced tracks only).
    QString subtitleMode;
    QString subtitleLanguage;
    /// Whether forced subtitles show in "off" and "auto"; unset means yes.
    std::optional<bool> showForcedSubtitles;
    ServerSubtitleSignature subtitleSignature;

    [[nodiscard]] bool hasAudioPreference() const
    {
        return audioStreamIndex >= 0 || !audioLanguage.trimmed().isEmpty();
    }

    [[nodiscard]] bool hasSubtitlePreference() const
    {
        return !subtitleMode.isEmpty();
    }

    [[nodiscard]] bool isEmpty() const
    {
        return !hasAudioPreference() && !hasSubtitlePreference();
    }

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap result;
        if (audioStreamIndex >= 0) {
            result.insert(QStringLiteral("audioStreamIndex"), audioStreamIndex);
        }
        if (!audioLanguage.isEmpty()) {
            result.insert(QStringLiteral("audioLanguage"), audioLanguage);
            result.insert(QStringLiteral("playDefaultAudioTrack"), playDefaultAudioTrack);
        }
        if (!subtitleMode.isEmpty()) {
            result.insert(QStringLiteral("subtitleMode"), subtitleMode);
            result.insert(QStringLiteral("subtitleLanguage"), subtitleLanguage);
            if (showForcedSubtitles.has_value()) {
                result.insert(QStringLiteral("showForcedSubtitles"), *showForcedSubtitles);
            }
            if (!subtitleSignature.isEmpty()) {
                result.insert(QStringLiteral("subtitleSignature"),
                              QVariantMap{
                                  {QStringLiteral("source"), subtitleSignature.source},
                                  {QStringLiteral("language"), subtitleSignature.language},
                                  {QStringLiteral("codec"), subtitleSignature.codec},
                                  {QStringLiteral("label"), subtitleSignature.label},
                                  {QStringLiteral("forced"), subtitleSignature.forced},
                                  {QStringLiteral("hearingImpaired"),
                                   subtitleSignature.hearingImpaired}});
            }
        }
        return result;
    }

    [[nodiscard]] static ServerTrackPreferences fromVariantMap(const QVariantMap &map)
    {
        ServerTrackPreferences result;
        result.audioStreamIndex = map.value(QStringLiteral("audioStreamIndex"), -1).toInt();
        result.audioLanguage = map.value(QStringLiteral("audioLanguage")).toString();
        result.playDefaultAudioTrack =
            map.value(QStringLiteral("playDefaultAudioTrack")).toBool();
        result.subtitleMode = map.value(QStringLiteral("subtitleMode")).toString();
        result.subtitleLanguage = map.value(QStringLiteral("subtitleLanguage")).toString();
        if (map.contains(QStringLiteral("showForcedSubtitles"))) {
            result.showForcedSubtitles =
                map.value(QStringLiteral("showForcedSubtitles")).toBool();
        }
        const QVariantMap signature = map.value(QStringLiteral("subtitleSignature")).toMap();
        result.subtitleSignature.source = signature.value(QStringLiteral("source")).toString();
        result.subtitleSignature.language =
            signature.value(QStringLiteral("language")).toString();
        result.subtitleSignature.codec = signature.value(QStringLiteral("codec")).toString();
        result.subtitleSignature.label = signature.value(QStringLiteral("label")).toString();
        result.subtitleSignature.forced = signature.value(QStringLiteral("forced")).toBool();
        result.subtitleSignature.hearingImpaired =
            signature.value(QStringLiteral("hearingImpaired")).toBool();
        return result;
    }
};

} // namespace Bloom
