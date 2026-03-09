#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QHash>

enum class TrackPreferenceMode {
    Unset,
    Off,
    ExplicitStream
};

struct TrackSelectionPreference
{
    TrackPreferenceMode mode = TrackPreferenceMode::Unset;
    int streamIndex = -1;
    QString preferredLanguage;
    bool forcedOnly = false;
    bool hearingImpaired = false;
    QString strategy;

    [[nodiscard]] bool isMeaningful() const
    {
        return mode != TrackPreferenceMode::Unset
            || !preferredLanguage.isEmpty()
            || forcedOnly
            || hearingImpaired
            || !strategy.isEmpty();
    }
};

inline bool operator==(const TrackSelectionPreference &lhs, const TrackSelectionPreference &rhs)
{
    return lhs.mode == rhs.mode
        && lhs.streamIndex == rhs.streamIndex
        && lhs.preferredLanguage == rhs.preferredLanguage
        && lhs.forcedOnly == rhs.forcedOnly
        && lhs.hearingImpaired == rhs.hearingImpaired
        && lhs.strategy == rhs.strategy;
}

struct ScopedTrackPreferences
{
    TrackSelectionPreference audio;
    TrackSelectionPreference subtitle;

    [[nodiscard]] bool isEmpty() const
    {
        return !audio.isMeaningful() && !subtitle.isMeaningful();
    }
};

inline bool operator==(const ScopedTrackPreferences &lhs, const ScopedTrackPreferences &rhs)
{
    return lhs.audio == rhs.audio && lhs.subtitle == rhs.subtitle;
}

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

    ScopedTrackPreferences getSeasonPreferences(const QString &seasonId) const;
    void setSeasonPreferences(const QString &seasonId, const ScopedTrackPreferences &preferences);
    void clearSeasonPreferences(const QString &seasonId);

    ScopedTrackPreferences getMoviePreferences(const QString &movieId) const;
    void setMoviePreferences(const QString &movieId, const ScopedTrackPreferences &preferences);
    void clearMoviePreferences(const QString &movieId);

    void clearAllPreferences();
    
    /// Get the path to the preferences file
    static QString getPreferencesPath();

private:
    static constexpr int kCurrentSchemaVersion = 2;

    QHash<QString, ScopedTrackPreferences> m_episodePreferences;
    QHash<QString, ScopedTrackPreferences> m_moviePreferences;
    
    // Track if we have unsaved changes
    bool m_dirty = false;
    
    // Delayed save timer to batch multiple changes
    void scheduleSave();
};
