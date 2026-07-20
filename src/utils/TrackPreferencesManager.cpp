#include "TrackPreferencesManager.h"
#include "ConfigManager.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include "BloomLogging.h"

namespace {
constexpr auto kVersionKey = "version";
constexpr auto kScopesKey = "scopes";
constexpr auto kLocalScope = "_local";
constexpr auto kPendingScope = "_pending";
constexpr auto kEpisodesKey = "episodes";
constexpr auto kMoviesKey = "movies";
constexpr auto kAudioKey = "audio";
constexpr auto kSubtitleKey = "subtitle";
constexpr auto kSubtitleDelayMsKey = "subtitleDelayMs";
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
    if (preferences.subtitleDelayMs != 0) {
        json[kSubtitleDelayMsKey] = preferences.subtitleDelayMs;
    }
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
    preferences.subtitleDelayMs = object.value(kSubtitleDelayMsKey).toInt(0);
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
    : TrackPreferencesManager(nullptr, parent)
{
}

TrackPreferencesManager::TrackPreferencesManager(ConfigManager *configManager, QObject *parent)
    : QObject(parent)
    , m_configManager(configManager)
{
    load();
    if (m_configManager) {
        connect(m_configManager, &ConfigManager::connectionsChanged,
                this, &TrackPreferencesManager::adoptPendingScope);
        adoptPendingScope();
    }
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
            qCWarning(lcConfig) << "TrackPreferencesManager: Failed to remove invalid preferences file:" << path;
        }
    };

    m_episodePreferences.clear();
    m_moviePreferences.clear();

    if (!file.exists()) {
        qCDebug(lcConfig) << "TrackPreferencesManager: No preferences file found at" << path;
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcConfig) << "TrackPreferencesManager: Failed to open preferences file:" << path;
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcConfig) << "TrackPreferencesManager: JSON parse error:" << parseError.errorString()
                   << "- clearing saved track preferences";
        discardPersistedPreferences();
        return;
    }

    if (!document.isObject()) {
        qCWarning(lcConfig) << "TrackPreferencesManager: Invalid preferences format - clearing saved track preferences";
        discardPersistedPreferences();
        return;
    }

    const QJsonObject root = document.object();
    const int version = root.value(kVersionKey).toInt(-1);
    if (version != 2 && version != 3 && version != kCurrentSchemaVersion) {
        qCWarning(lcConfig) << "TrackPreferencesManager: Resetting legacy track preferences schema version"
                   << version << "expected" << kCurrentSchemaVersion;
        discardPersistedPreferences();
        return;
    }

    if (version == kCurrentSchemaVersion) {
        const QJsonObject scopes = root.value(kScopesKey).toObject();
        for (const QString &scopeId : scopes.keys()) {
            const QJsonObject scope = scopes.value(scopeId).toObject();
            loadPreferenceSection(scope.value(kEpisodesKey).toObject(),
                                  m_episodePreferences[scopeId]);
            loadPreferenceSection(scope.value(kMoviesKey).toObject(),
                                  m_moviePreferences[scopeId]);
        }
    } else {
        const QString scopeId = migrationScopeId();
        loadPreferenceSection(root.value(kEpisodesKey).toObject(),
                              m_episodePreferences[scopeId]);
        loadPreferenceSection(root.value(kMoviesKey).toObject(),
                              m_moviePreferences[scopeId]);
        m_dirty = true;
        save();
    }

    const QString scopeId = activeScopeId();
    qCDebug(lcConfig) << "TrackPreferencesManager: Loaded preferences for connection scope"
             << scopeId << "with" << m_episodePreferences.value(scopeId).size()
             << "episode scopes and" << m_moviePreferences.value(scopeId).size()
             << "movie scopes";
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
    QJsonObject scopes;
    QSet<QString> scopeIds;
    for (auto it = m_episodePreferences.cbegin(); it != m_episodePreferences.cend(); ++it) {
        scopeIds.insert(it.key());
    }
    for (auto it = m_moviePreferences.cbegin(); it != m_moviePreferences.cend(); ++it) {
        scopeIds.insert(it.key());
    }
    for (const QString &scopeId : scopeIds) {
        const QJsonObject episodes = savePreferenceSection(m_episodePreferences.value(scopeId));
        const QJsonObject movies = savePreferenceSection(m_moviePreferences.value(scopeId));
        if (episodes.isEmpty() && movies.isEmpty()) {
            continue;
        }
        QJsonObject scope;
        scope[kEpisodesKey] = episodes;
        scope[kMoviesKey] = movies;
        scopes[scopeId] = scope;
    }
    root[kScopesKey] = scopes;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcConfig) << "TrackPreferencesManager: Failed to save preferences to" << path;
        scheduleRetrySave();
        return;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qCWarning(lcConfig) << "TrackPreferencesManager: Failed to commit preferences to" << path;
        scheduleRetrySave();
        return;
    }

    m_dirty = false;
    m_saveRetryAttempts = 0;
    qCDebug(lcConfig) << "TrackPreferencesManager: Saved preferences for"
             << scopes.size() << "connection scopes";
}

QString TrackPreferencesManager::activeScopeId() const
{
    if (m_configManager) {
        const auto connection = m_configManager->getActiveConnection();
        if (connection.has_value() && !connection->connectionId.isEmpty()) {
            return connection->connectionId;
        }
        return QString::fromLatin1(kPendingScope);
    }
    return QString::fromLatin1(kLocalScope);
}

QString TrackPreferencesManager::migrationScopeId() const
{
    if (!m_configManager) {
        return QString::fromLatin1(kLocalScope);
    }
    const auto active = m_configManager->getActiveConnection();
    if (active.has_value() && !active->connectionId.isEmpty()) {
        return active->connectionId;
    }
    const QList<ServerConnection> connections = m_configManager->getConnections();
    if (connections.size() == 1 && !connections.first().connectionId.isEmpty()) {
        return connections.first().connectionId;
    }
    return QString::fromLatin1(kPendingScope);
}

void TrackPreferencesManager::adoptPendingScope()
{
    if (!m_configManager) {
        return;
    }
    const auto active = m_configManager->getActiveConnection();
    if (!active.has_value() || active->connectionId.isEmpty()) {
        return;
    }

    const QString pendingId = QString::fromLatin1(kPendingScope);
    const QString targetId = active->connectionId;
    if (targetId == pendingId || targetId == QString::fromLatin1(kLocalScope)) {
        return;
    }
    bool changed = false;
    const auto adopt = [&changed, &pendingId, &targetId](auto &scopes) {
        auto pendingIt = scopes.find(pendingId);
        if (pendingIt == scopes.end()) {
            return;
        }
        const auto pending = pendingIt.value();
        scopes.erase(pendingIt);
        auto &target = scopes[targetId];
        for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
            if (!target.contains(it.key())) {
                target.insert(it.key(), it.value());
            }
        }
        changed = true;
    };
    adopt(m_episodePreferences);
    adopt(m_moviePreferences);
    if (changed) {
        scheduleSave();
    }
}

void TrackPreferencesManager::scheduleSave()
{
    m_dirty = true;
    m_saveRetryAttempts = 0;
    if (m_saveScheduled) {
        return;
    }

    scheduleSaveAfter(kInitialSaveDelayMs);
}

void TrackPreferencesManager::scheduleRetrySave()
{
    if (!m_dirty || m_saveScheduled) {
        return;
    }

    if (m_saveRetryAttempts >= kMaxSaveRetryAttempts) {
        qCWarning(lcConfig) << "TrackPreferencesManager: Giving up on autosave retries until preferences change again";
        return;
    }

    const int delayMs = kInitialSaveDelayMs << m_saveRetryAttempts;
    ++m_saveRetryAttempts;
    scheduleSaveAfter(delayMs);
}

void TrackPreferencesManager::scheduleSaveAfter(int delayMs)
{
    m_saveScheduled = true;
    QTimer::singleShot(delayMs, this, [this]() {
        m_saveScheduled = false;
        if (m_dirty) {
            save();
        }
    });
}

ScopedTrackPreferences TrackPreferencesManager::getSeasonPreferences(const QString &seasonId) const
{
    return m_episodePreferences.value(activeScopeId()).value(seasonId);
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

    auto &scope = m_episodePreferences[activeScopeId()];
    if (scope.value(seasonId) == preferences) {
        return;
    }

    scope.insert(seasonId, preferences);
    scheduleSave();
}

void TrackPreferencesManager::clearSeasonPreferences(const QString &seasonId)
{
    const QString scopeId = activeScopeId();
    auto scopeIt = m_episodePreferences.find(scopeId);
    if (scopeIt != m_episodePreferences.end() && scopeIt->remove(seasonId) > 0) {
        if (scopeIt->isEmpty()) {
            m_episodePreferences.erase(scopeIt);
        }
        scheduleSave();
    }
}

ScopedTrackPreferences TrackPreferencesManager::getMoviePreferences(const QString &movieId) const
{
    return m_moviePreferences.value(activeScopeId()).value(movieId);
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

    auto &scope = m_moviePreferences[activeScopeId()];
    if (scope.value(movieId) == preferences) {
        return;
    }

    scope.insert(movieId, preferences);
    scheduleSave();
}

void TrackPreferencesManager::clearMoviePreferences(const QString &movieId)
{
    const QString scopeId = activeScopeId();
    auto scopeIt = m_moviePreferences.find(scopeId);
    if (scopeIt != m_moviePreferences.end() && scopeIt->remove(movieId) > 0) {
        if (scopeIt->isEmpty()) {
            m_moviePreferences.erase(scopeIt);
        }
        scheduleSave();
    }
}

void TrackPreferencesManager::clearAllPreferences()
{
    const QString scopeId = activeScopeId();
    const bool hadEpisodes = m_episodePreferences.remove(scopeId) > 0;
    const bool hadMovies = m_moviePreferences.remove(scopeId) > 0;
    const bool hadData = hadEpisodes || hadMovies;
    if (hadData) {
        scheduleSave();
    }
}
