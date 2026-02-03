#include "TrackPreferencesManager.h"
#include "ConfigManager.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QTimer>
#include <QDebug>

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
    QString path = getPreferencesPath();
    QFile file(path);
    
    if (!file.exists()) {
        qDebug() << "TrackPreferencesManager: No preferences file found at" << path;
        return;
    }
    
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "TrackPreferencesManager: Failed to open preferences file:" << path;
        return;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "TrackPreferencesManager: JSON parse error:" << parseError.errorString();
        return;
    }
    
    if (!doc.isObject()) {
        qWarning() << "TrackPreferencesManager: Invalid preferences format";
        return;
    }
    
    QJsonObject root = doc.object();
    m_preferences.clear();
    m_moviePreferences.clear();
    
    // Load season preferences (for backwards compatibility, top-level keys that aren't "movies")
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key() == "movies") continue;  // Skip movies section
        
        QString seasonId = it.key();
        QJsonObject prefs = it.value().toObject();
        
        int audioTrack = prefs.value("audio").toInt(-1);
        int subtitleTrack = prefs.value("subtitle").toInt(-1);
        
        m_preferences[seasonId] = qMakePair(audioTrack, subtitleTrack);
    }
    
    // Load movie preferences from "movies" section
    if (root.contains("movies")) {
        QJsonObject moviesObj = root.value("movies").toObject();
        for (auto it = moviesObj.begin(); it != moviesObj.end(); ++it) {
            QString movieId = it.key();
            QJsonObject prefs = it.value().toObject();
            
            int audioTrack = prefs.value("audio").toInt(-1);
            int subtitleTrack = prefs.value("subtitle").toInt(-1);
            
            m_moviePreferences[movieId] = qMakePair(audioTrack, subtitleTrack);
        }
    }
    
    qDebug() << "TrackPreferencesManager: Loaded preferences for" << m_preferences.size() << "seasons and" << m_moviePreferences.size() << "movies";
}

void TrackPreferencesManager::save()
{
    QString path = getPreferencesPath();
    
    // Ensure config directory exists
    QDir dir = QFileInfo(path).dir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    QJsonObject root;
    
    // Save season preferences (top-level keys)
    for (auto it = m_preferences.begin(); it != m_preferences.end(); ++it) {
        QJsonObject prefs;
        prefs["audio"] = it.value().first;
        prefs["subtitle"] = it.value().second;
        root[it.key()] = prefs;
    }
    
    // Save movie preferences under "movies" key
    if (!m_moviePreferences.isEmpty()) {
        QJsonObject moviesObj;
        for (auto it = m_moviePreferences.begin(); it != m_moviePreferences.end(); ++it) {
            QJsonObject prefs;
            prefs["audio"] = it.value().first;
            prefs["subtitle"] = it.value().second;
            moviesObj[it.key()] = prefs;
        }
        root["movies"] = moviesObj;
    }
    
    QJsonDocument doc(root);
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "TrackPreferencesManager: Failed to save preferences to" << path;
        return;
    }
    
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    
    m_dirty = false;
    qDebug() << "TrackPreferencesManager: Saved preferences for" << m_preferences.size() << "seasons";
}

void TrackPreferencesManager::scheduleSave()
{
    if (m_dirty) return;  // Already scheduled
    
    m_dirty = true;
    
    // Delay save by 1 second to batch multiple changes
    QTimer::singleShot(1000, this, [this]() {
        if (m_dirty) {
            save();
        }
    });
}

int TrackPreferencesManager::getAudioTrack(const QString &seasonId) const
{
    if (m_preferences.contains(seasonId)) {
        return m_preferences[seasonId].first;
    }
    return -1;
}

void TrackPreferencesManager::setAudioTrack(const QString &seasonId, int trackIndex)
{
    if (seasonId.isEmpty()) return;
    
    m_preferences[seasonId].first = trackIndex;
    scheduleSave();
}

int TrackPreferencesManager::getSubtitleTrack(const QString &seasonId) const
{
    if (m_preferences.contains(seasonId)) {
        return m_preferences[seasonId].second;
    }
    return -1;
}

void TrackPreferencesManager::setSubtitleTrack(const QString &seasonId, int trackIndex)
{
    if (seasonId.isEmpty()) return;
    
    m_preferences[seasonId].second = trackIndex;
    scheduleSave();
}

void TrackPreferencesManager::clearPreferences(const QString &seasonId)
{
    if (m_preferences.remove(seasonId) > 0) {
        scheduleSave();
    }
}

// ---- Movie-based preferences ----

int TrackPreferencesManager::getMovieAudioTrack(const QString &movieId) const
{
    if (m_moviePreferences.contains(movieId)) {
        return m_moviePreferences[movieId].first;
    }
    return -1;
}

void TrackPreferencesManager::setMovieAudioTrack(const QString &movieId, int trackIndex)
{
    if (movieId.isEmpty()) return;
    
    m_moviePreferences[movieId].first = trackIndex;
    scheduleSave();
}

int TrackPreferencesManager::getMovieSubtitleTrack(const QString &movieId) const
{
    if (m_moviePreferences.contains(movieId)) {
        return m_moviePreferences[movieId].second;
    }
    return -1;
}

void TrackPreferencesManager::setMovieSubtitleTrack(const QString &movieId, int trackIndex)
{
    if (movieId.isEmpty()) return;
    
    m_moviePreferences[movieId].second = trackIndex;
    scheduleSave();
}

void TrackPreferencesManager::clearMoviePreferences(const QString &movieId)
{
    if (m_moviePreferences.remove(movieId) > 0) {
        scheduleSave();
    }
}
