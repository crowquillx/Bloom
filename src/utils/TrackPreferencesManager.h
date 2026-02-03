#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QHash>
#include <QPair>

/**
 * @brief Manages audio and subtitle track preferences per season and per movie
 * 
 * Stores track preferences in a separate JSON file (track_preferences.json)
 * to avoid bloating the main config file. Preferences persist across
 * application restarts.
 * 
 * For TV shows: Preferences are stored per season since track configurations
 * can vary between seasons of the same series.
 * 
 * For movies: Preferences are stored per movie ID, allowing users to remember
 * their preferred audio/subtitle tracks for rewatches.
 */
class TrackPreferencesManager : public QObject
{
    Q_OBJECT

public:
    explicit TrackPreferencesManager(QObject *parent = nullptr);
    ~TrackPreferencesManager();

    /// Load preferences from disk
    void load();
    
    /// Save preferences to disk
    void save();

    // ---- Season-based preferences (for TV episodes) ----
    
    /// Get saved audio track index for a season (-1 if no preference)
    Q_INVOKABLE int getAudioTrack(const QString &seasonId) const;
    
    /// Set audio track preference for a season
    Q_INVOKABLE void setAudioTrack(const QString &seasonId, int trackIndex);
    
    /// Get saved subtitle track index for a season (-1 if no preference)
    Q_INVOKABLE int getSubtitleTrack(const QString &seasonId) const;
    
    /// Set subtitle track preference for a season (-1 means "off")
    Q_INVOKABLE void setSubtitleTrack(const QString &seasonId, int trackIndex);
    
    /// Clear all preferences for a season
    Q_INVOKABLE void clearPreferences(const QString &seasonId);
    
    // ---- Movie-based preferences ----
    
    /// Get saved audio track index for a movie (-1 if no preference)
    Q_INVOKABLE int getMovieAudioTrack(const QString &movieId) const;
    
    /// Set audio track preference for a movie
    Q_INVOKABLE void setMovieAudioTrack(const QString &movieId, int trackIndex);
    
    /// Get saved subtitle track index for a movie (-1 if no preference)
    Q_INVOKABLE int getMovieSubtitleTrack(const QString &movieId) const;
    
    /// Set subtitle track preference for a movie (-1 means "off")
    Q_INVOKABLE void setMovieSubtitleTrack(const QString &movieId, int trackIndex);
    
    /// Clear all preferences for a movie
    Q_INVOKABLE void clearMoviePreferences(const QString &movieId);
    
    /// Get the path to the preferences file
    static QString getPreferencesPath();

private:
    // In-memory cache: seasonId -> (audioTrack, subtitleTrack)
    QHash<QString, QPair<int, int>> m_preferences;
    
    // In-memory cache for movies: movieId -> (audioTrack, subtitleTrack)
    QHash<QString, QPair<int, int>> m_moviePreferences;
    
    // Track if we have unsaved changes
    bool m_dirty = false;
    
    // Delayed save timer to batch multiple changes
    void scheduleSave();
};
