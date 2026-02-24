#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

/**
 * @brief MPV Profile data structure
 * 
 * Represents a named collection of mpv command-line arguments.
 * Profiles can be assigned to libraries or individual series.
 */
struct MpvProfile {
    Q_GADGET
    Q_PROPERTY(QString name MEMBER name)
    Q_PROPERTY(QStringList args MEMBER args)
    Q_PROPERTY(bool hwdecEnabled MEMBER hwdecEnabled)
    Q_PROPERTY(QString hwdecMethod MEMBER hwdecMethod)
    Q_PROPERTY(bool deinterlace MEMBER deinterlace)
    Q_PROPERTY(QString deinterlaceMethod MEMBER deinterlaceMethod)
    Q_PROPERTY(QString videoOutput MEMBER videoOutput)
    Q_PROPERTY(bool interpolation MEMBER interpolation)
    Q_PROPERTY(QStringList extraArgs MEMBER extraArgs)
public:
    QString name;                    // Display name
    QStringList args;                // Full computed args (for backward compat / simple use)
    
    // Structured options (used by UI)
    bool hwdecEnabled = true;
    QString hwdecMethod = "auto";    // auto, vaapi, nvdec, videotoolbox, d3d11va, etc.
    bool deinterlace = false;
    QString deinterlaceMethod = ""; // yadif, bwdif, etc.
    QString videoOutput = "gpu-next"; // gpu, gpu-next, etc.
    bool interpolation = false;
    QStringList extraArgs;           // Additional raw args
    
    /// Build the final args list from structured options
    QStringList buildArgs() const;
    
    /// Convert to JSON for storage
    QJsonObject toJson() const;
    
    /// Create from JSON
    static MpvProfile fromJson(const QString &name, const QJsonObject &obj);
    
    /// Check if this is a valid profile
    bool isValid() const { return !name.isEmpty(); }
};

Q_DECLARE_METATYPE(MpvProfile)

class ConfigManager : public QObject
{
    Q_OBJECT
    
    // Playback settings exposed to QML
    Q_PROPERTY(int playbackCompletionThreshold READ getPlaybackCompletionThreshold WRITE setPlaybackCompletionThreshold NOTIFY playbackCompletionThresholdChanged)
    Q_PROPERTY(int skipButtonAutoHideSeconds READ getSkipButtonAutoHideSeconds WRITE setSkipButtonAutoHideSeconds NOTIFY skipButtonAutoHideSecondsChanged)
    Q_PROPERTY(int audioDelay READ getAudioDelay WRITE setAudioDelay NOTIFY audioDelayChanged)
    Q_PROPERTY(int playbackVolume READ getPlaybackVolume WRITE setPlaybackVolume NOTIFY playbackVolumeChanged)
    Q_PROPERTY(bool playbackMuted READ getPlaybackMuted WRITE setPlaybackMuted NOTIFY playbackMutedChanged)
    Q_PROPERTY(bool autoplayNextEpisode READ getAutoplayNextEpisode WRITE setAutoplayNextEpisode NOTIFY autoplayNextEpisodeChanged)
    Q_PROPERTY(int autoplayCountdownSeconds READ getAutoplayCountdownSeconds WRITE setAutoplayCountdownSeconds NOTIFY autoplayCountdownSecondsChanged)
    Q_PROPERTY(bool autoSkipIntro READ getAutoSkipIntro WRITE setAutoSkipIntro NOTIFY autoSkipIntroChanged)
    Q_PROPERTY(bool autoSkipOutro READ getAutoSkipOutro WRITE setAutoSkipOutro NOTIFY autoSkipOutroChanged)
    Q_PROPERTY(QString playerBackend READ getPlayerBackend WRITE setPlayerBackend NOTIFY playerBackendChanged)
    Q_PROPERTY(int backdropRotationInterval READ getBackdropRotationInterval WRITE setBackdropRotationInterval NOTIFY backdropRotationIntervalChanged)
    Q_PROPERTY(bool launchInFullscreen READ getLaunchInFullscreen WRITE setLaunchInFullscreen NOTIFY launchInFullscreenChanged)
    Q_PROPERTY(int themeSongVolume READ getThemeSongVolume WRITE setThemeSongVolume NOTIFY themeSongVolumeChanged)
    Q_PROPERTY(bool themeSongLoop READ getThemeSongLoop WRITE setThemeSongLoop NOTIFY themeSongLoopChanged)
    Q_PROPERTY(bool uiSoundsEnabled READ getUiSoundsEnabled WRITE setUiSoundsEnabled NOTIFY uiSoundsEnabledChanged)
    Q_PROPERTY(int uiSoundsVolume READ getUiSoundsVolume WRITE setUiSoundsVolume NOTIFY uiSoundsVolumeChanged)
    Q_PROPERTY(bool performanceModeEnabled READ getPerformanceModeEnabled WRITE setPerformanceModeEnabled NOTIFY performanceModeEnabledChanged)
    
    // Video Settings
    Q_PROPERTY(bool enableFramerateMatching READ getEnableFramerateMatching WRITE setEnableFramerateMatching NOTIFY enableFramerateMatchingChanged)
    Q_PROPERTY(int framerateMatchDelay READ getFramerateMatchDelay WRITE setFramerateMatchDelay NOTIFY framerateMatchDelayChanged)
    Q_PROPERTY(bool enableHDR READ getEnableHDR WRITE setEnableHDR NOTIFY enableHDRChanged)
    Q_PROPERTY(QString linuxRefreshRateCommand READ getLinuxRefreshRateCommand WRITE setLinuxRefreshRateCommand NOTIFY linuxRefreshRateCommandChanged)
    Q_PROPERTY(QString linuxHDRCommand READ getLinuxHDRCommand WRITE setLinuxHDRCommand NOTIFY linuxHDRCommandChanged)
    Q_PROPERTY(QString windowsCustomHDRCommand READ getWindowsCustomHDRCommand WRITE setWindowsCustomHDRCommand NOTIFY windowsCustomHDRCommandChanged)
    
    // MPV Profiles
    Q_PROPERTY(QStringList mpvProfileNames READ getMpvProfileNames NOTIFY mpvProfilesChanged)
    Q_PROPERTY(QString defaultProfileName READ getDefaultProfileName WRITE setDefaultProfileName NOTIFY defaultProfileNameChanged)
    
    // Theme
    Q_PROPERTY(QString theme READ getTheme WRITE setTheme NOTIFY themeChanged)
    
    // Read-only session info for display
    Q_PROPERTY(QString serverUrl READ getServerUrl NOTIFY sessionChanged)
    Q_PROPERTY(QString username READ getUsername NOTIFY sessionChanged)
    Q_PROPERTY(QString userId READ getUserId NOTIFY sessionChanged)
    
    // Device identification (generated once, persisted)
    Q_PROPERTY(QString deviceId READ getDeviceId CONSTANT)

    // Cache Settings
    Q_PROPERTY(int imageCacheSizeMB READ getImageCacheSizeMB WRITE setImageCacheSizeMB NOTIFY imageCacheSizeChanged)
    Q_PROPERTY(QString roundedImageMode READ getRoundedImageMode WRITE setRoundedImageMode NOTIFY roundedImageModeChanged)
    Q_PROPERTY(bool roundedImagePreprocessEnabled READ getRoundedImagePreprocessEnabled WRITE setRoundedImagePreprocessEnabled NOTIFY roundedImagePreprocessEnabledChanged)
    
    // MDBList Integration
    Q_PROPERTY(QString mdbListApiKey READ getMdbListApiKey WRITE setMdbListApiKey NOTIFY mdbListApiKeyChanged)
    
    // Seerr Integration
    Q_PROPERTY(QString seerrBaseUrl READ getSeerrBaseUrl WRITE setSeerrBaseUrl NOTIFY seerrBaseUrlChanged)
    Q_PROPERTY(QString seerrApiKey READ getSeerrApiKey WRITE setSeerrApiKey NOTIFY seerrApiKeyChanged)
    
    // Manual DPI Scale Override
    Q_PROPERTY(qreal manualDpiScaleOverride READ getManualDpiScaleOverride WRITE setManualDpiScaleOverride NOTIFY manualDpiScaleOverrideChanged)
    
    // UI Animations
    Q_PROPERTY(bool uiAnimationsEnabled READ getUiAnimationsEnabled WRITE setUiAnimationsEnabled NOTIFY uiAnimationsEnabledChanged)
    
public:
    explicit ConfigManager(QObject *parent = nullptr);
    
    // Device ID - unique per installation, includes hostname
    // Generated on first launch and stored in settings
    QString getDeviceId() const;
    
    // Get device ID combined with user ID for per-user session uniqueness
    // This follows jellyfin-android's approach to prevent cross-user session conflicts
    QString getUserDeviceId(const QString &userId) const;

    void load();
    void save();
    
    // Application exit - saves config and quits
    Q_INVOKABLE void exitApplication();
    
    // Session Management
    void setJellyfinSession(const QString &serverUrl, const QString &userId, const QString &accessToken, const QString &username);
    void clearJellyfinSession();
    
    struct SessionData {
        QString serverUrl;
        QString userId;
        QString accessToken;
        QString username;
        bool isValid() const { return !serverUrl.isEmpty() && !userId.isEmpty() && !username.isEmpty(); }
    };

    SessionData getJellyfinSession() const;

    // Playback Settings
    void setPlaybackCompletionThreshold(int percent);
    int getPlaybackCompletionThreshold() const;
    
    // Audio Delay (milliseconds)
    void setAudioDelay(int ms);
    int getAudioDelay() const;

    // Playback volume state for player overlay/session persistence.
    void setPlaybackVolume(int volume);
    int getPlaybackVolume() const;
    void setPlaybackMuted(bool muted);
    bool getPlaybackMuted() const;

    // Skip intro/outro pop-up auto-hide duration (seconds)
    // 0 disables the temporary pop-up while keeping the persistent overlay skip button.
    void setSkipButtonAutoHideSeconds(int seconds);
    int getSkipButtonAutoHideSeconds() const;
    
    void setAutoplayNextEpisode(bool enabled);
    bool getAutoplayNextEpisode() const;
    void setAutoplayCountdownSeconds(int seconds);
    int getAutoplayCountdownSeconds() const;
    void setAutoSkipIntro(bool enabled);
    bool getAutoSkipIntro() const;
    void setAutoSkipOutro(bool enabled);
    bool getAutoSkipOutro() const;

    void setPlayerBackend(const QString &backendName);
    QString getPlayerBackend() const;
    
    // Theme Song Settings
    void setThemeSongVolume(int level);
    int getThemeSongVolume() const;
    
    void setThemeSongLoop(bool enabled);
    bool getThemeSongLoop() const;

    // Performance mode for VRAM trimming aggressiveness
    void setPerformanceModeEnabled(bool enabled);
    bool getPerformanceModeEnabled() const;

    // UI Sounds
    void setUiSoundsEnabled(bool enabled);
    bool getUiSoundsEnabled() const;
    void setUiSoundsVolume(int level);
    int getUiSoundsVolume() const;
    
    // Backdrop Rotation Interval (milliseconds)
    void setBackdropRotationInterval(int ms);
    int getBackdropRotationInterval() const;

    // Launch in Fullscreen
    void setLaunchInFullscreen(bool enabled);
    bool getLaunchInFullscreen() const;

    // Video Settings
    void setEnableFramerateMatching(bool enabled);
    bool getEnableFramerateMatching() const;
    
    void setFramerateMatchDelay(int seconds);
    int getFramerateMatchDelay() const;
    
    void setEnableHDR(bool enabled);
    bool getEnableHDR() const;
    
    void setLinuxRefreshRateCommand(const QString &cmd);
    QString getLinuxRefreshRateCommand() const;
    
    void setLinuxHDRCommand(const QString &cmd);
    QString getLinuxHDRCommand() const;
    
    void setWindowsCustomHDRCommand(const QString &cmd);
    QString getWindowsCustomHDRCommand() const;
    
    // Theme
    void setTheme(const QString &theme);
    QString getTheme() const;
    
    // ========================================
    // MPV Profile Management
    // ========================================
    
    /// Get list of all profile names
    QStringList getMpvProfileNames() const;
    
    /// Get a specific profile by name
    Q_INVOKABLE QVariantMap getMpvProfile(const QString &name) const;
    
    /// Get a profile as MpvProfile struct (for C++ use)
    MpvProfile getMpvProfileStruct(const QString &name) const;
    
    /// Create or update a profile
    Q_INVOKABLE void setMpvProfile(const QString &name, const QVariantMap &profileData);
    
    /// Delete a profile (cannot delete "Default" or "High Quality")
    Q_INVOKABLE bool deleteMpvProfile(const QString &name);
    
    /// Get/set the default profile name (used when no library/series override)
    QString getDefaultProfileName() const;
    void setDefaultProfileName(const QString &name);
    
    /// Get profile assigned to a library (returns empty string if none)
    Q_INVOKABLE QString getLibraryProfile(const QString &libraryId) const;
    
    /// Set profile for a library (empty string to clear)
    Q_INVOKABLE void setLibraryProfile(const QString &libraryId, const QString &profileName);
    
    /// Get profile assigned to a series (returns empty string if none)
    Q_INVOKABLE QString getSeriesProfile(const QString &seriesId) const;
    
    /// Set profile for a series (empty string to clear)
    Q_INVOKABLE void setSeriesProfile(const QString &seriesId, const QString &profileName);
    
    /// Resolve the effective profile for an item given its context
    /// Priority: series > library > default
    /// @param libraryId The library containing the item
    /// @param seriesId The series ID (empty for movies)
    /// @return The resolved profile name
    Q_INVOKABLE QString resolveProfileForItem(const QString &libraryId, const QString &seriesId) const;
    
    /// Get the final mpv args for playback, applying HDR overrides only for HDR content
    /// @param profileName The profile to use
    /// @param isHdrContent Whether the current item is HDR content
    /// @return List of mpv command-line arguments
    QStringList getMpvArgsForProfile(const QString &profileName, bool isHdrContent = false) const;
    
    /// Create the two default profiles (used by migration)
    static QJsonObject defaultMpvProfiles();
    
    // Path Accessors
    
    /// Returns the base config directory for Bloom (e.g., ~/.config/Bloom on Linux)
    static QString getConfigDir();
    
    /// Returns the path to the app config file (app.json)
    static QString getConfigPath();
    
    /// Returns the path to Bloom's mpv config directory (contains mpv.conf, input.conf, and optional user scripts/)
    static QString getMpvConfigDir();
    
    /// Returns the path to Bloom's mpv.conf if it exists, empty string otherwise
    static QString getMpvConfPath();
    
    /// Returns the path to Bloom's input.conf if it exists, empty string otherwise  
    static QString getMpvInputConfPath();
    
    /// Returns the path to Bloom's mpv scripts directory if it exists, empty string otherwise
    static QString getMpvScriptsDir();
    
    /// Returns mpv command-line arguments for config files (--config-dir, --config, --input-conf, --script)
    /// Only includes arguments for files/directories that actually exist
    static QStringList getMpvConfigArgs();
    
    /// Ensures the config directory structure exists
    static bool ensureConfigDirExists();
    
    // Migration and validation (internal)
    bool migrateConfig();
    bool validateConfig(const QJsonObject &cfg);
    
    // Session info getters for QML (read-only)
    QString getServerUrl() const;
    QString getUsername() const;
    QString getUserId() const;

    // Cache Settings
    void setImageCacheSizeMB(int mb);
    int getImageCacheSizeMB() const;
    void setRoundedImageMode(const QString &mode);
    QString getRoundedImageMode() const;
    void setRoundedImagePreprocessEnabled(bool enabled);
    bool getRoundedImagePreprocessEnabled() const;

    // MDBList API Key
    void setMdbListApiKey(const QString &key);
    QString getMdbListApiKey() const;
    
    // Seerr
    void setSeerrBaseUrl(const QString &url);
    QString getSeerrBaseUrl() const;
    void setSeerrApiKey(const QString &key);
    QString getSeerrApiKey() const;
    
    // Manual DPI Scale Override
    void setManualDpiScaleOverride(qreal scale);
    qreal getManualDpiScaleOverride() const;
    
    // UI Animations
    void setUiAnimationsEnabled(bool enabled);
    bool getUiAnimationsEnabled() const;

signals:
    void backdropRotationIntervalChanged();
    void launchInFullscreenChanged();
    void enableFramerateMatchingChanged();
    void framerateMatchDelayChanged();
    void enableHDRChanged();
    void linuxRefreshRateCommandChanged();
    void linuxHDRCommandChanged();
    void windowsCustomHDRCommandChanged();
    void playbackCompletionThresholdChanged();
    void skipButtonAutoHideSecondsChanged();
    void audioDelayChanged();
    void playbackVolumeChanged();
    void playbackMutedChanged();
    void autoplayNextEpisodeChanged();
    void autoplayCountdownSecondsChanged();
    void autoSkipIntroChanged();
    void autoSkipOutroChanged();
    void playerBackendChanged();
    void themeSongVolumeChanged();
    void themeSongLoopChanged();
    void uiSoundsEnabledChanged();
    void uiSoundsVolumeChanged();
    void sessionChanged();
    void mpvProfilesChanged();
    void defaultProfileNameChanged();
    void libraryProfilesChanged();
    void seriesProfilesChanged();
    void themeChanged();
    void imageCacheSizeChanged();
    void roundedImageModeChanged();
    void roundedImagePreprocessEnabledChanged();

    void performanceModeEnabledChanged();
    void mdbListApiKeyChanged();
    void seerrBaseUrlChanged();
    void seerrApiKeyChanged();
    void manualDpiScaleOverrideChanged();
    void uiAnimationsEnabledChanged();

private:
    QString normalizePlayerBackendName(const QString &backendName) const;
    QString normalizeRoundedMode(const QString &raw) const;
    bool envOverridesRoundedPreprocess(bool current) const;

    QJsonObject m_config;

    static constexpr int kCurrentConfigVersion = 14;
    QJsonObject defaultConfig() const;
};
