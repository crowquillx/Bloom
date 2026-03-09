#include "TrackPreferencesManager.h"
#include "ConfigManager.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>

namespace {
constexpr auto kVersionKey = "version";
constexpr auto kEpisodesKey = "episodes";
constexpr auto kMoviesKey = "movies";
constexpr auto kAudioKey = "audio";
constexpr auto kSubtitleKey = "subtitle";
constexpr auto kModeKey = "mode";
constexpr auto kStreamIndexKey = "streamIndex";
constexpr auto kPreferredLanguageKey = "preferredLanguage";
constexpr auto kForcedOnlyKey = "forcedOnly";
constexpr auto kHearingImpairedKey = "hearingImpaired";
constexpr auto kStrategyKey = "strategy";

QString modeToString(TrackPreferenceMode mode)
{
    switch (mode) {
    case TrackPreferenceMode::Off:
        return QStringLiteral("off");
    case TrackPreferenceMode::ExplicitStream:
        return QStringLiteral("explicit");
    case TrackPreferenceMode::Unset:
    default:
        return QStringLiteral("unset");
    }
}

TrackPreferenceMode modeFromString(const QString &mode)
{
    const QString normalized = mode.trimmed().toLower();
    if (normalized == QStringLiteral("off")) {
        return TrackPreferenceMode::Off;
    }
    if (normalized == QStringLiteral("explicit")) {
        return TrackPreferenceMode::ExplicitStream;
    }
    return TrackPreferenceMode::Unset;
}

QJsonObject preferenceToJson(const TrackSelectionPreference &preference)
{
    QJsonObject json;
    json[kModeKey] = modeToString(preference.mode);
    if (preference.mode == TrackPreferenceMode::ExplicitStream && preference.streamIndex >= 0) {
        json[kStreamIndexKey] = preference.streamIndex;
    }
    if (!preference.preferredLanguage.isEmpty()) {
        json[kPreferredLanguageKey] = preference.preferredLanguage;
    }
    if (preference.forcedOnly) {
        json[kForcedOnlyKey] = true;
    }
    if (preference.hearingImpaired) {
        json[kHearingImpairedKey] = true;
    }
    if (!preference.strategy.isEmpty()) {
        json[kStrategyKey] = preference.strategy;
    }
    return json;
}

TrackSelectionPreference preferenceFromJson(const QJsonValue &value)
{
    TrackSelectionPreference preference;
    if (!value.isObject()) {
        return preference;
    }

    const QJsonObject object = value.toObject();
    preference.mode = modeFromString(object.value(kModeKey).toString());
    preference.streamIndex = object.value(kStreamIndexKey).toInt(-1);
    preference.preferredLanguage = object.value(kPreferredLanguageKey).toString();
    preference.forcedOnly = object.value(kForcedOnlyKey).toBool(false);
    preference.hearingImpaired = object.value(kHearingImpairedKey).toBool(false);
    preference.strategy = object.value(kStrategyKey).toString();

    if (preference.mode != TrackPreferenceMode::ExplicitStream) {
        preference.streamIndex = -1;
    } else if (preference.streamIndex < 0) {
        preference.mode = TrackPreferenceMode::Unset;
    }

    return preference;
}

QJsonObject scopedPreferencesToJson(const ScopedTrackPreferences &preferences)
{
    QJsonObject json;
    json[kAudioKey] = preferenceToJson(preferences.audio);
    json[kSubtitleKey] = preferenceToJson(preferences.subtitle);
    return json;
}

ScopedTrackPreferences scopedPreferencesFromJson(const QJsonValue &value)
{
    ScopedTrackPreferences preferences;
    if (!value.isObject()) {
        return preferences;
    }

    const QJsonObject object = value.toObject();
    preferences.audio = preferenceFromJson(object.value(kAudioKey));
    preferences.subtitle = preferenceFromJson(object.value(kSubtitleKey));
    return preferences;
}

template<typename MapType>
void loadPreferenceSection(const QJsonObject &section, MapType &target)
{
    target.clear();
    for (auto it = section.begin(); it != section.end(); ++it) {
        const ScopedTrackPreferences preferences = scopedPreferencesFromJson(it.value());
        if (!preferences.isEmpty()) {
            target.insert(it.key(), preferences);
        }
    }
}

template<typename MapType>
QJsonObject savePreferenceSection(const MapType &source)
{
    QJsonObject section;
    for (auto it = source.begin(); it != source.end(); ++it) {
        if (!it.value().isEmpty()) {
            section[it.key()] = scopedPreferencesToJson(it.value());
        }
    }
    return section;
}
}

TrackPreferencesManager::TrackPreferencesManager(QObject *parent)
    : QObject(parent)
{
    load();
}

TrackPreferencesManager::~TrackPreferencesManager()
{
    if (m_dirty) {
        save();
    }
}

QString TrackPreferencesManager::getPreferencesPath()
{
    return ConfigManager::getConfigDir() + "/track_preferences.json";
}

void TrackPreferencesManager::load()
{
    const QString path = getPreferencesPath();
    QFile file(path);
    const auto discardPersistedPreferences = [&path]() {
        if (!QFile::remove(path)) {
            qWarning() << "TrackPreferencesManager: Failed to remove invalid preferences file:" << path;
        }
    };

    m_episodePreferences.clear();
    m_moviePreferences.clear();

    if (!file.exists()) {
        qDebug() << "TrackPreferencesManager: No preferences file found at" << path;
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "TrackPreferencesManager: Failed to open preferences file:" << path;
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "TrackPreferencesManager: JSON parse error:" << parseError.errorString()
                   << "- clearing saved track preferences";
        discardPersistedPreferences();
        return;
    }

    if (!document.isObject()) {
        qWarning() << "TrackPreferencesManager: Invalid preferences format - clearing saved track preferences";
        discardPersistedPreferences();
        return;
    }

    const QJsonObject root = document.object();
    const int version = root.value(kVersionKey).toInt(-1);
    if (version != kCurrentSchemaVersion) {
        qWarning() << "TrackPreferencesManager: Resetting legacy track preferences schema version"
                   << version << "expected" << kCurrentSchemaVersion;
        discardPersistedPreferences();
        return;
    }

    loadPreferenceSection(root.value(kEpisodesKey).toObject(), m_episodePreferences);
    loadPreferenceSection(root.value(kMoviesKey).toObject(), m_moviePreferences);

    qDebug() << "TrackPreferencesManager: Loaded preferences for"
             << m_episodePreferences.size() << "episode scopes and"
             << m_moviePreferences.size() << "movie scopes";
}

void TrackPreferencesManager::save()
{
    const QString path = getPreferencesPath();
    QDir dir = QFileInfo(path).dir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QJsonObject root;
    root[kVersionKey] = kCurrentSchemaVersion;
    root[kEpisodesKey] = savePreferenceSection(m_episodePreferences);
    root[kMoviesKey] = savePreferenceSection(m_moviePreferences);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "TrackPreferencesManager: Failed to save preferences to" << path;
        return;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning() << "TrackPreferencesManager: Failed to commit preferences to" << path;
        return;
    }

    m_dirty = false;
    qDebug() << "TrackPreferencesManager: Saved preferences for"
             << m_episodePreferences.size() << "episode scopes and"
             << m_moviePreferences.size() << "movie scopes";
}

void TrackPreferencesManager::scheduleSave()
{
    if (m_saveScheduled) {
        return;
    }

    m_dirty = true;
    m_saveScheduled = true;
    QTimer::singleShot(1000, this, [this]() {
        m_saveScheduled = false;
        if (m_dirty) {
            save();
        }
    });
}

ScopedTrackPreferences TrackPreferencesManager::getSeasonPreferences(const QString &seasonId) const
{
    return m_episodePreferences.value(seasonId);
}

void TrackPreferencesManager::setSeasonPreferences(const QString &seasonId, const ScopedTrackPreferences &preferences)
{
    if (seasonId.isEmpty()) {
        return;
    }

    if (preferences.isEmpty()) {
        clearSeasonPreferences(seasonId);
        return;
    }

    if (m_episodePreferences.value(seasonId) == preferences) {
        return;
    }

    m_episodePreferences.insert(seasonId, preferences);
    scheduleSave();
}

void TrackPreferencesManager::clearSeasonPreferences(const QString &seasonId)
{
    if (m_episodePreferences.remove(seasonId) > 0) {
        scheduleSave();
    }
}

ScopedTrackPreferences TrackPreferencesManager::getMoviePreferences(const QString &movieId) const
{
    return m_moviePreferences.value(movieId);
}

void TrackPreferencesManager::setMoviePreferences(const QString &movieId, const ScopedTrackPreferences &preferences)
{
    if (movieId.isEmpty()) {
        return;
    }

    if (preferences.isEmpty()) {
        clearMoviePreferences(movieId);
        return;
    }

    if (m_moviePreferences.value(movieId) == preferences) {
        return;
    }

    m_moviePreferences.insert(movieId, preferences);
    scheduleSave();
}

void TrackPreferencesManager::clearMoviePreferences(const QString &movieId)
{
    if (m_moviePreferences.remove(movieId) > 0) {
        scheduleSave();
    }
}

void TrackPreferencesManager::clearAllPreferences()
{
    const bool hadData = !m_episodePreferences.isEmpty() || !m_moviePreferences.isEmpty();
    m_episodePreferences.clear();
    m_moviePreferences.clear();
    if (hadData) {
        scheduleSave();
    }
}
