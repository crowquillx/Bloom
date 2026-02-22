#include "ConfigManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>
#include <QCoreApplication>
#include <QUuid>
#include <QSysInfo>
#include <QRegularExpression>
#include <QByteArray>
#include <QMetaType>
#include <algorithm>

namespace {

QString preferredConfigDir()
{
#ifdef Q_OS_WIN
    // Keep Windows config rooted at %APPDATA%/Bloom (no nested org/app suffix).
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/Bloom";
#else
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/Bloom";
#endif
}

#ifdef Q_OS_WIN
QString legacyWindowsConfigDir()
{
    // Historical path logic used AppDataLocation and could resolve to .../Bloom/Bloom.
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(appData);
    dir.cdUp();
    return dir.absoluteFilePath("Bloom");
}

bool migrateLegacyWindowsConfigDirIfNeeded(const QString &targetDirPath)
{
    const QString legacyDirPath = legacyWindowsConfigDir();
    if (legacyDirPath == targetDirPath) {
        return true;
    }

    QDir legacyDir(legacyDirPath);
    if (!legacyDir.exists()) {
        return true;
    }

    QDir targetDir(targetDirPath);
    if (!targetDir.exists() && !targetDir.mkpath(".")) {
        qWarning() << "ConfigManager: Failed to create target config directory for migration:" << targetDirPath;
        return false;
    }

    const QFileInfoList entries = legacyDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const QFileInfo &entry : entries) {
        const QString srcPath = entry.absoluteFilePath();
        const QString dstPath = targetDir.absoluteFilePath(entry.fileName());

        if (QFileInfo::exists(dstPath)) {
            qWarning() << "ConfigManager: Skipping legacy config entry because target already exists:" << dstPath;
            continue;
        }

        bool migrated = false;
        if (entry.isDir()) {
            migrated = QDir().rename(srcPath, dstPath);
        } else {
            migrated = QFile::rename(srcPath, dstPath);
        }

        if (!migrated) {
            qWarning() << "ConfigManager: Failed to migrate legacy config entry:" << srcPath << "->" << dstPath;
        } else {
            qDebug() << "ConfigManager: Migrated legacy config entry:" << srcPath << "->" << dstPath;
        }
    }

    // Best-effort cleanup: remove legacy directory if it is now empty.
    if (legacyDir.exists() &&
        legacyDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) {
        QDir parent = legacyDir;
        parent.cdUp();
        parent.rmdir(legacyDir.dirName());
    }

    return true;
}
#endif

} // namespace

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{
}

QString ConfigManager::getConfigDir()
{
    return preferredConfigDir();
}

QString ConfigManager::getConfigPath()
{
    return getConfigDir() + "/app.json";
}

QString ConfigManager::getMpvConfigDir()
{
    return getConfigDir() + "/mpv";
}

QString ConfigManager::getMpvConfPath()
{
    QString path = getMpvConfigDir() + "/mpv.conf";
    if (QFile::exists(path)) {
        return path;
    }
    return QString();
}

QString ConfigManager::getMpvInputConfPath()
{
    QString path = getMpvConfigDir() + "/input.conf";
    if (QFile::exists(path)) {
        return path;
    }
    return QString();
}

QString ConfigManager::getMpvScriptsDir()
{
    QString path = getMpvConfigDir() + "/scripts";
    QDir dir(path);
    if (dir.exists()) {
        return path;
    }
    return QString();
}

QStringList ConfigManager::getMpvConfigArgs()
{
    QStringList args;
    
    // Disable mpv OSC; playback controls are handled by Bloom's native overlay.
    args << "--no-osc";
    
    QString mpvConfigDir = getMpvConfigDir();
    QDir dir(mpvConfigDir);
    
    // If the mpv config directory exists, tell mpv to use it as config-dir
    // This allows mpv to find watch_later, scripts subdir, etc.
    if (dir.exists()) {
        args << "--config-dir=" + mpvConfigDir;
        
        // Explicitly enable config loading from our directory
        args << "--config=yes";
        
    }
    
    // If we have a custom mpv.conf, use it
    QString mpvConf = getMpvConfPath();
    if (!mpvConf.isEmpty()) {
        args << "--include=" + mpvConf;
    }
    
    // If we have a custom input.conf, use it
    QString inputConf = getMpvInputConfPath();
    if (!inputConf.isEmpty()) {
        args << "--input-conf=" + inputConf;
    }
    
    // If we have a scripts directory with scripts, load them
    QString scriptsDir = getMpvScriptsDir();
    if (!scriptsDir.isEmpty()) {
        QDir scripts(scriptsDir);
        QStringList scriptFiles = scripts.entryList({"*.lua", "*.js", "*.so", "*.dll"}, QDir::Files);
        for (const QString &script : scriptFiles) {
            args << "--script=" + scripts.absoluteFilePath(script);
        }
    }
    
    return args;
}

bool ConfigManager::ensureConfigDirExists()
{
#ifdef Q_OS_WIN
    if (!migrateLegacyWindowsConfigDirIfNeeded(getConfigDir())) {
        return false;
    }
#endif

    QDir dir(getConfigDir());
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "ConfigManager: Failed to create config directory:" << getConfigDir();
            return false;
        }
        qDebug() << "ConfigManager: Created config directory:" << getConfigDir();
    }
    
    // Also create mpv subdirectory structure
    QString mpvDir = getMpvConfigDir();
    QDir mpv(mpvDir);
    if (!mpv.exists()) {
        if (!mpv.mkpath(".")) {
            qWarning() << "ConfigManager: Failed to create mpv config directory:" << mpvDir;
            return false;
        }
        qDebug() << "ConfigManager: Created mpv config directory:" << mpvDir;
    }
    
    return true;
}

void ConfigManager::load()
{
    // Ensure config directory exists
    ensureConfigDirExists();
    
    QString path = getConfigPath();
    QFile file(path);
    if (!file.exists()) {
        qWarning() << "Config file not found, creating default:" << path;
        m_config = defaultConfig();
        save();
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open config file for reading:" << path << ", using defaults";
        m_config = defaultConfig();
        return;
    }

    QByteArray data = file.readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Invalid config file (JSON parse error):" << parseError.errorString();
        // Backup the bad file
        QString backup = path + ".corrupt-" + QDateTime::currentDateTime().toString(Qt::ISODate);
        if (QFile::exists(backup)) {
            QFile::remove(backup);
        }
        if (!QFile::rename(path, backup)) {
            qWarning() << "Could not rename corrupt config file to backup:" << backup;
        } else {
            qWarning() << "Backed up corrupt config to" << backup;
        }
        m_config = defaultConfig();
        save();
        return;
    }

    m_config = doc.object();

    // Run migrations, ensure config is at the current version
    if (!migrateConfig()) {
        qWarning() << "Config migration failed -- resetting config to defaults";
        QString backup = path + ".migratefail-" + QDateTime::currentDateTime().toString(Qt::ISODate);
        if (QFile::exists(backup)) QFile::remove(backup);
        QFile::rename(path, backup);
        m_config = defaultConfig();
        save();
        return;
    }

    if (!validateConfig(m_config)) {
        qWarning() << "Config failed schema validation -- resetting to defaults";
        QString backup = path + ".badschema-" + QDateTime::currentDateTime().toString(Qt::ISODate);
        if (QFile::exists(backup)) QFile::remove(backup);
        QFile::rename(path, backup);
        m_config = defaultConfig();
        save();
        return;
    }

    qDebug() << "Loaded config from" << path;
}

void ConfigManager::save()
{
    QString path = getConfigPath();
    QFile file(path);
    
    // Ensure directory exists
    QFileInfo info(path);
    QDir dir = info.absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Could not open config file for writing:" << path;
        return;
    }
    // Ensure current version and settings exist before saving
    if (!m_config.contains("version")) {
        m_config["version"] = kCurrentConfigVersion;
    }
    if (!m_config.contains("settings") || !m_config["settings"].isObject()) {
        // If the config is in legacy format (top-level keys), migrate/move to settings
        migrateConfig();
        if (!m_config.contains("settings") || !m_config["settings"].isObject()) {
            m_config["settings"] = QJsonObject();
        }
    }

    QJsonDocument doc(m_config);
    file.write(doc.toJson(QJsonDocument::Indented));
    qDebug() << "Saved config to" << path;
}

void ConfigManager::exitApplication()
{
    // Save configuration before exiting
    save();
    
    // Exit the application
    QCoreApplication::quit();
}

void ConfigManager::setJellyfinSession(const QString &serverUrl, const QString &userId, const QString &accessToken, const QString &username)
{
    QJsonObject jellyfin;
    jellyfin["server_url"] = serverUrl;
    jellyfin["user_id"] = userId;
    jellyfin["access_token"] = accessToken;
    jellyfin["username"] = username;

    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    settings["jellyfin"] = jellyfin;
    m_config["settings"] = settings;
    save();
    emit sessionChanged();
}

ConfigManager::SessionData ConfigManager::getJellyfinSession() const
{
    SessionData data;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("jellyfin")) {
            QJsonObject jellyfin = settings["jellyfin"].toObject();
            data.serverUrl = jellyfin["server_url"].toString();
            data.userId = jellyfin["user_id"].toString();
            data.accessToken = jellyfin["access_token"].toString();
            data.username = jellyfin["username"].toString();
        }
    }
    
    return data;
}

QString ConfigManager::getServerUrl() const
{
    return getJellyfinSession().serverUrl;
}

QString ConfigManager::getUsername() const
{
    return getJellyfinSession().username;
}

QString ConfigManager::getUserId() const
{
    return getJellyfinSession().userId;
}

void ConfigManager::setImageCacheSizeMB(int mb)
{
    // Enforce a minimum of 50MB; no upper bound
    int clamped = std::max(50, mb);
    if (clamped == getImageCacheSizeMB()) {
        return;
    }

    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject cache;
    if (settings.contains("cache") && settings["cache"].isObject()) {
        cache = settings["cache"].toObject();
    }
    cache["image_cache_size_mb"] = clamped;
    settings["cache"] = cache;
    m_config["settings"] = settings;
    save();
    emit imageCacheSizeChanged();
}

int ConfigManager::getImageCacheSizeMB() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("cache") && settings["cache"].isObject()) {
            QJsonObject cache = settings["cache"].toObject();
            if (cache.contains("image_cache_size_mb")) {
                return cache["image_cache_size_mb"].toInt();
            }
        }
    }
    return 500; // Default 500MB
}

void ConfigManager::setRoundedImageMode(const QString &mode)
{
    const QString normalized = normalizeRoundedMode(mode);

    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject cache;
    if (settings.contains("cache") && settings["cache"].isObject()) {
        cache = settings["cache"].toObject();
    }

    const QString current = normalizeRoundedMode(cache.value("rounded_image_mode").toString("auto"));
    if (current == normalized) {
        return;
    }

    cache["rounded_image_mode"] = normalized;
    settings["cache"] = cache;
    m_config["settings"] = settings;
    save();
    emit roundedImageModeChanged();
}

QString ConfigManager::getRoundedImageMode() const
{
    QString mode = "auto";
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("cache") && settings["cache"].isObject()) {
            QJsonObject cache = settings["cache"].toObject();
            if (cache.contains("rounded_image_mode")) {
                mode = cache["rounded_image_mode"].toString();
            }
        }
    }

    const QString envMode = qEnvironmentVariable("BLOOM_ROUNDED_IMAGE_MODE");
    if (!envMode.isEmpty()) {
        mode = envMode;
    }

    return normalizeRoundedMode(mode);
}

void ConfigManager::setRoundedImagePreprocessEnabled(bool enabled)
{
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject cache;
    if (settings.contains("cache") && settings["cache"].isObject()) {
        cache = settings["cache"].toObject();
    }

    if (cache.contains("rounded_preprocess_enabled") &&
        cache["rounded_preprocess_enabled"].toBool(true) == enabled) {
        return;
    }

    cache["rounded_preprocess_enabled"] = enabled;
    settings["cache"] = cache;
    m_config["settings"] = settings;
    save();
    emit roundedImagePreprocessEnabledChanged();
}

bool ConfigManager::getRoundedImagePreprocessEnabled() const
{
    bool enabled = true;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("cache") && settings["cache"].isObject()) {
            QJsonObject cache = settings["cache"].toObject();
            if (cache.contains("rounded_preprocess_enabled")) {
                enabled = cache["rounded_preprocess_enabled"].toBool(true);
            }
        }
    }

    return envOverridesRoundedPreprocess(enabled);
}

QString ConfigManager::normalizePlayerBackendName(const QString &backendName) const
{
    const QString normalized = backendName.trimmed().toLower();
    if (normalized.isEmpty() || normalized == "auto") {
        return QString();
    }
    if (normalized == "external-mpv-ipc"
        || normalized == "linux-libmpv-opengl"
        || normalized == "win-libmpv") {
        return normalized;
    }

    qWarning() << "ConfigManager: Ignoring unknown player backend preference:" << backendName;
    return QString();
}

QString ConfigManager::normalizeRoundedMode(const QString &raw) const
{
    const QString lowered = raw.trimmed().toLower();
    if (lowered == "shader" || lowered == "prerender" || lowered == "auto") {
        return lowered;
    }
    return "auto";
}

bool ConfigManager::envOverridesRoundedPreprocess(bool current) const
{
    auto parseBool = [](const QByteArray &value, bool fallback) {
        if (value.isEmpty()) return fallback;
        const QString trimmed = QString::fromLatin1(value).trimmed().toLower();
        if (trimmed == "1" || trimmed == "true" || trimmed == "on" || trimmed == "yes") return true;
        if (trimmed == "0" || trimmed == "false" || trimmed == "off" || trimmed == "no") return false;
        return fallback;
    };

    QByteArray env = qgetenv("BLOOM_ROUNDED_PREPROCESS");
    if (env.isEmpty()) {
        env = qgetenv("BLOOM_ROUNDED_IMAGE_PREPROCESS");
    }
    return parseBool(env, current);
}

QString ConfigManager::getDeviceId() const
{
    // Check if we already have a device ID stored
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("device_id") && !settings["device_id"].toString().isEmpty()) {
            return settings["device_id"].toString();
        }
    }
    
    // Generate a new device ID: hostname + UUID for uniqueness
    // Format: "Bloom-<hostname>-<uuid>"
    QString hostname = QSysInfo::machineHostName();
    if (hostname.isEmpty()) {
        hostname = "unknown";
    }
    // Sanitize hostname (remove special chars that might cause issues)
    hostname = hostname.replace(QRegularExpression("[^a-zA-Z0-9-]"), "-");
    
    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString deviceId = QString("Bloom-%1-%2").arg(hostname, uuid);
    
    // Store it for future use (need to cast away const for this one-time generation)
    ConfigManager *mutableThis = const_cast<ConfigManager*>(this);
    QJsonObject settings;
    if (mutableThis->m_config.contains("settings") && mutableThis->m_config["settings"].isObject()) {
        settings = mutableThis->m_config["settings"].toObject();
    }
    settings["device_id"] = deviceId;
    mutableThis->m_config["settings"] = settings;
    mutableThis->save();
    
    qDebug() << "ConfigManager: Generated new device ID:" << deviceId;
    return deviceId;
}

QString ConfigManager::getUserDeviceId(const QString &userId) const
{
    // Following jellyfin-android's approach: append userId to deviceId
    // This ensures each user on the same device has a unique session identity
    QString baseDeviceId = getDeviceId();
    if (userId.isEmpty()) {
        return baseDeviceId;
    }
    return baseDeviceId + "-" + userId;
}

void ConfigManager::clearJellyfinSession()
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        settings.remove("jellyfin");
        m_config["settings"] = settings;
        save();
        emit sessionChanged();
        qDebug() << "ConfigManager: Cleared Jellyfin session data";
    }
}

void ConfigManager::setPlaybackCompletionThreshold(int percent)
{
    if (percent == getPlaybackCompletionThreshold()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }
    playback["completion_threshold"] = percent;
    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit playbackCompletionThresholdChanged();
}

int ConfigManager::getPlaybackCompletionThreshold() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("completion_threshold")) {
                return playback["completion_threshold"].toInt();
            }
        }
    }
    return 90; // Default to 90%
}

void ConfigManager::setSkipButtonAutoHideSeconds(int seconds)
{
    const int clampedSeconds = std::max(0, std::min(seconds, 15));
    if (clampedSeconds == getSkipButtonAutoHideSeconds()) return;

    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }
    playback["skip_button_auto_hide_seconds"] = clampedSeconds;
    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit skipButtonAutoHideSecondsChanged();
}

int ConfigManager::getSkipButtonAutoHideSeconds() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        const QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            const QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("skip_button_auto_hide_seconds")) {
                const int value = playback["skip_button_auto_hide_seconds"].toInt();
                return std::max(0, std::min(value, 15));
            }
        }
    }
    return 6; // Default to 6 seconds
}

void ConfigManager::setAudioDelay(int ms)
{
    if (ms == getAudioDelay()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }
    playback["audio_delay"] = ms;
    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit audioDelayChanged();
}

int ConfigManager::getAudioDelay() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("audio_delay")) {
                return playback["audio_delay"].toInt();
            }
        }
    }
    return 0; // Default to 0ms
}

void ConfigManager::setPlaybackVolume(int volume)
{
    const int clamped = std::max(0, std::min(volume, 200));
    if (clamped == getPlaybackVolume()) return;

    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }
    playback["playback_volume"] = clamped;
    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit playbackVolumeChanged();
}

int ConfigManager::getPlaybackVolume() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("playback_volume")) {
                return std::max(0, std::min(playback["playback_volume"].toInt(), 200));
            }
        }
    }
    return 100;
}

void ConfigManager::setPlaybackMuted(bool muted)
{
    if (muted == getPlaybackMuted()) return;

    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }
    playback["playback_muted"] = muted;
    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit playbackMutedChanged();
}

bool ConfigManager::getPlaybackMuted() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("playback_muted")) {
                return playback["playback_muted"].toBool();
            }
        }
    }
    return false;
}

void ConfigManager::setAutoplayNextEpisode(bool enabled)
{
    if (enabled == getAutoplayNextEpisode()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }
    playback["autoplay_next_episode"] = enabled;
    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit autoplayNextEpisodeChanged();
}

bool ConfigManager::getAutoplayNextEpisode() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("autoplay_next_episode")) {
                return playback["autoplay_next_episode"].toBool();
            }
        }
    }
    return true; // Default to enabled
}

void ConfigManager::setAutoplayCountdownSeconds(int seconds)
{
    // Clamp to valid range: 5, 10, 15, 20, 25, 30
    int clamped = std::max(5, std::min(seconds, 30));
    // Round to nearest multiple of 5
    clamped = ((clamped + 2) / 5) * 5;
    if (clamped == getAutoplayCountdownSeconds()) return;

    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }
    playback["autoplay_countdown_seconds"] = clamped;
    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit autoplayCountdownSecondsChanged();
}

int ConfigManager::getAutoplayCountdownSeconds() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("autoplay_countdown_seconds")) {
                int val = playback["autoplay_countdown_seconds"].toInt();
                return std::max(5, std::min(val, 30));
            }
        }
    }
    return 10; // Default to 10 seconds
}

void ConfigManager::setAutoSkipIntro(bool enabled)
{
    if (enabled == getAutoSkipIntro()) return;

    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }
    playback["auto_skip_intro"] = enabled;
    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit autoSkipIntroChanged();
}

bool ConfigManager::getAutoSkipIntro() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("auto_skip_intro")) {
                return playback["auto_skip_intro"].toBool();
            }
        }
    }
    return false; // Default off
}

void ConfigManager::setAutoSkipOutro(bool enabled)
{
    if (enabled == getAutoSkipOutro()) return;

    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }
    playback["auto_skip_outro"] = enabled;
    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit autoSkipOutroChanged();
}

bool ConfigManager::getAutoSkipOutro() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("auto_skip_outro")) {
                return playback["auto_skip_outro"].toBool();
            }
        }
    }
    return false; // Default off
}

void ConfigManager::setPlayerBackend(const QString &backendName)
{
    const QString normalizedBackend = normalizePlayerBackendName(backendName);
    if (normalizedBackend == getPlayerBackend()) {
        return;
    }

    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }

    if (normalizedBackend.isEmpty()) {
        playback.remove("player_backend");
    } else {
        playback["player_backend"] = normalizedBackend;
    }

    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit playerBackendChanged();
}

QString ConfigManager::getPlayerBackend() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("player_backend")) {
                return normalizePlayerBackendName(playback["player_backend"].toString());
            }
        }
    }

    return QString();
}

void ConfigManager::setThemeSongVolume(int level)
{
    // Clamp to expected range [0,4]
    int clamped = std::max(0, std::min(level, 4));
    if (clamped == getThemeSongVolume()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }
    playback["theme_song_volume"] = clamped;
    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit themeSongVolumeChanged();
}

int ConfigManager::getThemeSongVolume() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("theme_song_volume")) {
                return playback["theme_song_volume"].toInt();
            }
        }
    }
    return 0; // Default Off
}

void ConfigManager::setThemeSongLoop(bool enabled)
{
    if (enabled == getThemeSongLoop()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }
    playback["theme_song_loop"] = enabled;
    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit themeSongLoopChanged();
}

bool ConfigManager::getThemeSongLoop() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("theme_song_loop")) {
                return playback["theme_song_loop"].toBool();
            }
        }
    }
    return false; // Default to play once
}

void ConfigManager::setPerformanceModeEnabled(bool enabled)
{
    if (enabled == getPerformanceModeEnabled()) return;

    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }
    playback["performance_mode_enabled"] = enabled;
    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit performanceModeEnabledChanged();
}

bool ConfigManager::getPerformanceModeEnabled() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("performance_mode_enabled")) {
                return playback["performance_mode_enabled"].toBool();
            }
        }
    }
    return false; // Default to disabled
}

void ConfigManager::setMdbListApiKey(const QString &key)
{
    if (key == getMdbListApiKey()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject mdblist;
    if (settings.contains("mdblist") && settings["mdblist"].isObject()) {
        mdblist = settings["mdblist"].toObject();
    }
    mdblist["api_key"] = key;
    settings["mdblist"] = mdblist;
    m_config["settings"] = settings;
    save();
    emit mdbListApiKeyChanged();
}

QString ConfigManager::getMdbListApiKey() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("mdblist") && settings["mdblist"].isObject()) {
            QJsonObject mdblist = settings["mdblist"].toObject();
            if (mdblist.contains("api_key")) {
                return mdblist["api_key"].toString();
            }
        }
    }
    return QString();
}

void ConfigManager::setManualDpiScaleOverride(qreal scale)
{
    // Clamp to reasonable range (0.5 to 2.0)
    qreal clamped = qBound(0.5, scale, 2.0);
    
    qreal current = getManualDpiScaleOverride();
    qDebug() << "ConfigManager::setManualDpiScaleOverride called with:" << scale 
             << "clamped to:" << clamped << "current value:" << current;
    
    if (qFuzzyCompare(clamped, current)) {
        qDebug() << "ConfigManager: Value unchanged, skipping";
        return;
    }
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    settings["manualDpiScaleOverride"] = clamped;
    m_config["settings"] = settings;
    save();
    qDebug() << "ConfigManager: Emitting manualDpiScaleOverrideChanged() signal";
    emit manualDpiScaleOverrideChanged();
}

qreal ConfigManager::getManualDpiScaleOverride() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("manualDpiScaleOverride")) {
            return qBound(0.5, settings["manualDpiScaleOverride"].toDouble(), 2.0);
        }
    }
    return 1.0; // Default: no override (1.0 = automatic)
}

void ConfigManager::setUiAnimationsEnabled(bool enabled)
{
    if (enabled == getUiAnimationsEnabled()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject ui;
    if (settings.contains("ui") && settings["ui"].isObject()) {
        ui = settings["ui"].toObject();
    }
    ui["ui_animations_enabled"] = enabled;
    settings["ui"] = ui;
    m_config["settings"] = settings;
    save();
    emit uiAnimationsEnabledChanged();
}

bool ConfigManager::getUiAnimationsEnabled() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("ui") && settings["ui"].isObject()) {
            QJsonObject ui = settings["ui"].toObject();
            if (ui.contains("ui_animations_enabled")) {
                return ui["ui_animations_enabled"].toBool();
            }
        }
    }
    return true; // Default: animations enabled
}


void ConfigManager::setUiSoundsEnabled(bool enabled)
{
    if (enabled == getUiSoundsEnabled()) return;

    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }
    playback["ui_sounds_enabled"] = enabled;
    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit uiSoundsEnabledChanged();
}

bool ConfigManager::getUiSoundsEnabled() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("ui_sounds_enabled")) {
                return playback["ui_sounds_enabled"].toBool();
            }
        }
    }
    return true; // Default enabled
}

void ConfigManager::setUiSoundsVolume(int level)
{
    int clamped = std::max(0, std::min(level, 4));
    if (clamped == getUiSoundsVolume()) return;

    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject playback;
    if (settings.contains("playback") && settings["playback"].isObject()) {
        playback = settings["playback"].toObject();
    }
    playback["ui_sounds_volume"] = clamped;
    settings["playback"] = playback;
    m_config["settings"] = settings;
    save();
    emit uiSoundsVolumeChanged();
}

int ConfigManager::getUiSoundsVolume() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("playback") && settings["playback"].isObject()) {
            QJsonObject playback = settings["playback"].toObject();
            if (playback.contains("ui_sounds_volume")) {
                return playback["ui_sounds_volume"].toInt();
            }
        }
    }
    return 3; // Default medium
}

void ConfigManager::setBackdropRotationInterval(int ms)
{
    if (ms == getBackdropRotationInterval()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject ui;
    if (settings.contains("ui") && settings["ui"].isObject()) {
        ui = settings["ui"].toObject();
    }
    ui["backdrop_rotation_interval"] = ms;
    settings["ui"] = ui;
    m_config["settings"] = settings;
    save();
    emit backdropRotationIntervalChanged();
}

int ConfigManager::getBackdropRotationInterval() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("ui") && settings["ui"].isObject()) {
            QJsonObject ui = settings["ui"].toObject();
            if (ui.contains("backdrop_rotation_interval")) {
                return ui["backdrop_rotation_interval"].toInt();
            }
        }
    }
    return 30000; // Default 30 seconds
}

void ConfigManager::setLaunchInFullscreen(bool enabled)
{
    if (enabled == getLaunchInFullscreen()) return;

    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject ui;
    if (settings.contains("ui") && settings["ui"].isObject()) {
        ui = settings["ui"].toObject();
    }
    ui["launch_in_fullscreen"] = enabled;
    settings["ui"] = ui;
    m_config["settings"] = settings;
    save();
    emit launchInFullscreenChanged();
}

bool ConfigManager::getLaunchInFullscreen() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("ui") && settings["ui"].isObject()) {
            QJsonObject ui = settings["ui"].toObject();
            if (ui.contains("launch_in_fullscreen")) {
                return ui["launch_in_fullscreen"].toBool();
            }
        }
    }
    return false; // Default to windowed mode
}

void ConfigManager::setEnableFramerateMatching(bool enabled)
{
    if (enabled == getEnableFramerateMatching()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject video;
    if (settings.contains("video") && settings["video"].isObject()) {
        video = settings["video"].toObject();
    }
    video["enable_framerate_matching"] = enabled;
    settings["video"] = video;
    m_config["settings"] = settings;
    save();
    emit enableFramerateMatchingChanged();
}

bool ConfigManager::getEnableFramerateMatching() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("video") && settings["video"].isObject()) {
            QJsonObject video = settings["video"].toObject();
            if (video.contains("enable_framerate_matching")) {
                return video["enable_framerate_matching"].toBool();
            }
        }
    }
    return false;
}

void ConfigManager::setFramerateMatchDelay(int seconds)
{
    // Clamp to valid range 0-5 seconds
    int clamped = std::max(0, std::min(seconds, 5));
    if (clamped == getFramerateMatchDelay()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject video;
    if (settings.contains("video") && settings["video"].isObject()) {
        video = settings["video"].toObject();
    }
    video["framerate_match_delay"] = clamped;
    settings["video"] = video;
    m_config["settings"] = settings;
    save();
    emit framerateMatchDelayChanged();
}

int ConfigManager::getFramerateMatchDelay() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("video") && settings["video"].isObject()) {
            QJsonObject video = settings["video"].toObject();
            if (video.contains("framerate_match_delay")) {
                return video["framerate_match_delay"].toInt();
            }
        }
    }
    return 1; // Default to 1 second
}

void ConfigManager::setEnableHDR(bool enabled)
{
    if (enabled == getEnableHDR()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject video;
    if (settings.contains("video") && settings["video"].isObject()) {
        video = settings["video"].toObject();
    }
    video["enable_hdr"] = enabled;
    settings["video"] = video;
    m_config["settings"] = settings;
    save();
    emit enableHDRChanged();
}

bool ConfigManager::getEnableHDR() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("video") && settings["video"].isObject()) {
            QJsonObject video = settings["video"].toObject();
            if (video.contains("enable_hdr")) {
                return video["enable_hdr"].toBool();
            }
        }
    }
    return false;
}

void ConfigManager::setLinuxRefreshRateCommand(const QString &cmd)
{
    if (cmd == getLinuxRefreshRateCommand()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject video;
    if (settings.contains("video") && settings["video"].isObject()) {
        video = settings["video"].toObject();
    }
    video["linux_refresh_rate_command"] = cmd;
    settings["video"] = video;
    m_config["settings"] = settings;
    save();
    emit linuxRefreshRateCommandChanged();
}

QString ConfigManager::getLinuxRefreshRateCommand() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("video") && settings["video"].isObject()) {
            QJsonObject video = settings["video"].toObject();
            if (video.contains("linux_refresh_rate_command")) {
                return video["linux_refresh_rate_command"].toString();
            }
        }
    }
    return QString();
}

void ConfigManager::setLinuxHDRCommand(const QString &cmd)
{
    if (cmd == getLinuxHDRCommand()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject video;
    if (settings.contains("video") && settings["video"].isObject()) {
        video = settings["video"].toObject();
    }
    video["linux_hdr_command"] = cmd;
    settings["video"] = video;
    m_config["settings"] = settings;
    save();
    emit linuxHDRCommandChanged();
}

QString ConfigManager::getLinuxHDRCommand() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("video") && settings["video"].isObject()) {
            QJsonObject video = settings["video"].toObject();
            if (video.contains("linux_hdr_command")) {
                return video["linux_hdr_command"].toString();
            }
        }
    }
    return QString();
}

void ConfigManager::setWindowsCustomHDRCommand(const QString &cmd)
{
    if (cmd == getWindowsCustomHDRCommand()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject video;
    if (settings.contains("video") && settings["video"].isObject()) {
        video = settings["video"].toObject();
    }
    video["windows_custom_hdr_command"] = cmd;
    settings["video"] = video;
    m_config["settings"] = settings;
    save();
    emit windowsCustomHDRCommandChanged();
}

QString ConfigManager::getWindowsCustomHDRCommand() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("video") && settings["video"].isObject()) {
            QJsonObject video = settings["video"].toObject();
            if (video.contains("windows_custom_hdr_command")) {
                return video["windows_custom_hdr_command"].toString();
            }
        }
    }
    return QString();
}

// Local helper for migration steps, defined in this compilation unit
namespace {
class ConfigMigrator
{
public:
    static QJsonObject migrateV0ToV1(const QJsonObject &oldConfig)
    {
        QJsonObject newConfig;
        newConfig["version"] = 1;

        QJsonObject settings;

        // If the old config already had a settings object and additional top-level keys,
        // merge both into the new settings object.
        for (auto it = oldConfig.begin(); it != oldConfig.end(); ++it) {
            const QString &key = it.key();
            if (key == "version") continue;
            if (key == "settings" && it.value().isObject()) {
                QJsonObject oldSettings = it.value().toObject();
                for (auto sIt = oldSettings.begin(); sIt != oldSettings.end(); ++sIt) {
                    settings.insert(sIt.key(), sIt.value());
                }
            } else {
                settings.insert(key, it.value());
            }
        }

        // Ensure playback has a sensible default
        if (!settings.contains("playback") || !settings["playback"].isObject()) {
            QJsonObject playback;
            playback["completion_threshold"] = 90;
            playback["autoplay_next_episode"] = true;
            settings.insert("playback", playback);
        } else {
            QJsonObject playback = settings["playback"].toObject();
            if (!playback.contains("completion_threshold")) {
                playback.insert("completion_threshold", 90);
            }
            if (!playback.contains("autoplay_next_episode")) {
                playback.insert("autoplay_next_episode", true);
            }
            if (!playback.contains("audio_delay")) {
                playback.insert("audio_delay", 0);
            }
            settings.insert("playback", playback);
        }

        newConfig.insert("settings", settings);
        return newConfig;
    }
    
    static QJsonObject migrateV1ToV2(const QJsonObject &oldConfig)
    {
        QJsonObject newConfig = oldConfig;
        newConfig["version"] = 2;
        
        QJsonObject settings = newConfig["settings"].toObject();
        
        // Add mpv_profiles if not present
        if (!settings.contains("mpv_profiles")) {
            settings["mpv_profiles"] = ConfigManager::defaultMpvProfiles();
        }
        
        // Add default_profile if not present
        if (!settings.contains("default_profile")) {
            settings["default_profile"] = "Default";
        }
        
        // Add library_profiles if not present
        if (!settings.contains("library_profiles")) {
            settings["library_profiles"] = QJsonObject();
        }
        
        // Add series_profiles if not present
        if (!settings.contains("series_profiles")) {
            settings["series_profiles"] = QJsonObject();
        }
        
        newConfig["settings"] = settings;
        return newConfig;
    }
    
    static QJsonObject migrateV2ToV3(const QJsonObject &oldConfig)
    {
        QJsonObject newConfig = oldConfig;
        newConfig["version"] = 3;
        
        QJsonObject settings = newConfig["settings"].toObject();
        QJsonObject playback = settings.value("playback").toObject();
        
        if (!playback.contains("theme_song_volume")) {
            playback["theme_song_volume"] = 0;
        }
        if (!playback.contains("theme_song_loop")) {
            playback["theme_song_loop"] = false;
        }
        if (!playback.contains("audio_delay")) {
            playback["audio_delay"] = 0;
        }
        
        settings["playback"] = playback;
        newConfig["settings"] = settings;
        return newConfig;
    }

    static QJsonObject migrateV3ToV4(const QJsonObject &oldConfig)
    {
        QJsonObject newConfig = oldConfig;
        newConfig["version"] = 4;

        QJsonObject settings = newConfig["settings"].toObject();
        QJsonObject cache = settings.value("cache").toObject();
        if (!cache.contains("image_cache_size_mb")) {
            cache["image_cache_size_mb"] = 500;
        }
        settings["cache"] = cache;
        newConfig["settings"] = settings;
        return newConfig;
    }

    static QJsonObject migrateV4ToV5(const QJsonObject &oldConfig)
    {
        QJsonObject newConfig = oldConfig;
        newConfig["version"] = 5;

        QJsonObject settings = newConfig["settings"].toObject();
        QJsonObject cache = settings.value("cache").toObject();

        if (!cache.contains("rounded_image_mode")) {
            cache["rounded_image_mode"] = "auto";
        }
        if (!cache.contains("rounded_preprocess_enabled")) {
            cache["rounded_preprocess_enabled"] = true;
        }

        settings["cache"] = cache;
        newConfig["settings"] = settings;
        return newConfig;
    }

    static QJsonObject migrateV5ToV6(const QJsonObject &oldConfig)
    {
        QJsonObject newConfig = oldConfig;
        newConfig["version"] = 6;

        QJsonObject settings = newConfig["settings"].toObject();
        QJsonObject playback = settings.value("playback").toObject();

        if (!playback.contains("ui_sounds_enabled")) {
            playback["ui_sounds_enabled"] = true;
        }
        if (!playback.contains("ui_sounds_volume")) {
            playback["ui_sounds_volume"] = 3;
        }

        settings["playback"] = playback;
        newConfig["settings"] = settings;
        return newConfig;
    }

    static QJsonObject migrateV6ToV7(const QJsonObject &oldConfig)
    {
        QJsonObject newConfig = oldConfig;
        newConfig["version"] = 7;

        QJsonObject settings = newConfig["settings"].toObject();
        QJsonObject playback = settings.value("playback").toObject();

        if (!playback.contains("performance_mode_enabled")) {
            playback["performance_mode_enabled"] = false;
        }

        settings["playback"] = playback;
        newConfig["settings"] = settings;
        return newConfig;
    }

    static QJsonObject migrateV7ToV8(const QJsonObject &oldConfig)
    {
        QJsonObject newConfig = oldConfig;
        newConfig["version"] = 8;

        QJsonObject settings = newConfig["settings"].toObject();
        QJsonObject ui = settings.value("ui").toObject();

        if (!ui.contains("launch_in_fullscreen")) {
            ui["launch_in_fullscreen"] = false;
        }

        settings["ui"] = ui;
        newConfig["settings"] = settings;
        return newConfig;
    }

    static QJsonObject migrateV8ToV9(const QJsonObject &oldConfig)
    {
        QJsonObject newConfig = oldConfig;
        newConfig["version"] = 9;

        QJsonObject settings = newConfig["settings"].toObject();
        if (!settings.contains("manualDpiScaleOverride")) {
            settings["manualDpiScaleOverride"] = 1.0;
        }

        newConfig["settings"] = settings;
        return newConfig;
    }

    static QJsonObject migrateV9ToV10(const QJsonObject &oldConfig)
    {
        QJsonObject newConfig = oldConfig;
        newConfig["version"] = 10;

        QJsonObject settings = newConfig["settings"].toObject();
        QJsonObject ui = settings.value("ui").toObject();

        if (!ui.contains("ui_animations_enabled")) {
            ui["ui_animations_enabled"] = true;
        }

        settings["ui"] = ui;
        newConfig["settings"] = settings;
        return newConfig;
    }

    static QJsonObject migrateV10ToV11(const QJsonObject &oldConfig)
    {
        QJsonObject newConfig = oldConfig;
        newConfig["version"] = 11;

        QJsonObject settings = newConfig["settings"].toObject();
        QJsonObject playback = settings.value("playback").toObject();
        if (playback.contains("player_backend")) {
            const QString normalized = playback.value("player_backend").toString().trimmed().toLower();
            if (normalized.isEmpty() || normalized == "auto") {
                playback.remove("player_backend");
            } else if (normalized == "external-mpv-ipc"
                       || normalized == "linux-libmpv-opengl"
                       || normalized == "win-libmpv") {
                playback["player_backend"] = normalized;
            } else {
                playback.remove("player_backend");
            }
        }

        settings["playback"] = playback;
        newConfig["settings"] = settings;
        return newConfig;
    }

    static QJsonObject migrateV11ToV12(const QJsonObject &oldConfig)
    {
        QJsonObject newConfig = oldConfig;
        newConfig["version"] = 12;

        QJsonObject settings = newConfig["settings"].toObject();
        QJsonObject playback = settings.value("playback").toObject();
        if (!playback.contains("skip_button_auto_hide_seconds")) {
            playback["skip_button_auto_hide_seconds"] = 6;
        }

        settings["playback"] = playback;
        newConfig["settings"] = settings;
        return newConfig;
    }

    static QJsonObject migrateV12ToV13(const QJsonObject &oldConfig)
    {
        QJsonObject newConfig = oldConfig;
        newConfig["version"] = 13;

        QJsonObject settings = newConfig["settings"].toObject();
        QJsonObject playback = settings.value("playback").toObject();
        if (!playback.contains("auto_skip_intro")) {
            playback["auto_skip_intro"] = false;
        }
        if (!playback.contains("auto_skip_outro")) {
            playback["auto_skip_outro"] = false;
        }

        settings["playback"] = playback;
        newConfig["settings"] = settings;
        return newConfig;
    }
};
}

bool ConfigManager::migrateConfig()
{
    int version = 0;
    if (m_config.contains("version") && m_config["version"].isDouble()) {
        version = m_config["version"].toInt();
    }

    while (version < kCurrentConfigVersion) {
        if (version == 0) {
            m_config = ConfigMigrator::migrateV0ToV1(m_config);
            if (m_config.contains("version") && m_config["version"].isDouble()) {
                version = m_config["version"].toInt();
            } else {
                qWarning() << "Migration produced invalid config (no version)";
                return false;
            }
        } else if (version == 1) {
            m_config = ConfigMigrator::migrateV1ToV2(m_config);
            if (m_config.contains("version") && m_config["version"].isDouble()) {
                version = m_config["version"].toInt();
            } else {
                qWarning() << "Migration produced invalid config (no version)";
                return false;
            }
        } else if (version == 2) {
            m_config = ConfigMigrator::migrateV2ToV3(m_config);
            if (m_config.contains("version") && m_config["version"].isDouble()) {
                version = m_config["version"].toInt();
            } else {
                qWarning() << "Migration produced invalid config (no version)";
                return false;
            }
        } else if (version == 3) {
            m_config = ConfigMigrator::migrateV3ToV4(m_config);
            if (m_config.contains("version") && m_config["version"].isDouble()) {
                version = m_config["version"].toInt();
            } else {
                qWarning() << "Migration produced invalid config (no version)";
                return false;
            }
        } else if (version == 4) {
            m_config = ConfigMigrator::migrateV4ToV5(m_config);
            if (m_config.contains("version") && m_config["version"].isDouble()) {
                version = m_config["version"].toInt();
            } else {
                qWarning() << "Migration produced invalid config (no version)";
                return false;
            }
        } else if (version == 5) {
            m_config = ConfigMigrator::migrateV5ToV6(m_config);
            if (m_config.contains("version") && m_config["version"].isDouble()) {
                version = m_config["version"].toInt();
            } else {
                qWarning() << "Migration produced invalid config (no version)";
                return false;
            }
        } else if (version == 6) {
            m_config = ConfigMigrator::migrateV6ToV7(m_config);
            if (m_config.contains("version") && m_config["version"].isDouble()) {
                version = m_config["version"].toInt();
            } else {
                qWarning() << "Migration produced invalid config (no version)";
                return false;
            }
        } else if (version == 7) {
            m_config = ConfigMigrator::migrateV7ToV8(m_config);
            if (m_config.contains("version") && m_config["version"].isDouble()) {
                version = m_config["version"].toInt();
            } else {
                qWarning() << "Migration produced invalid config (no version)";
                return false;
            }
        } else if (version == 8) {
            m_config = ConfigMigrator::migrateV8ToV9(m_config);
            if (m_config.contains("version") && m_config["version"].isDouble()) {
                version = m_config["version"].toInt();
            } else {
                qWarning() << "Migration produced invalid config (no version)";
                return false;
            }
        } else if (version == 9) {
            m_config = ConfigMigrator::migrateV9ToV10(m_config);
            if (m_config.contains("version") && m_config["version"].isDouble()) {
                version = m_config["version"].toInt();
            } else {
                qWarning() << "Migration produced invalid config (no version)";
                return false;
            }
        } else if (version == 10) {
            m_config = ConfigMigrator::migrateV10ToV11(m_config);
            if (m_config.contains("version") && m_config["version"].isDouble()) {
                version = m_config["version"].toInt();
            } else {
                qWarning() << "Migration produced invalid config (no version)";
                return false;
            }
        } else if (version == 11) {
            m_config = ConfigMigrator::migrateV11ToV12(m_config);
            if (m_config.contains("version") && m_config["version"].isDouble()) {
                version = m_config["version"].toInt();
            } else {
                qWarning() << "Migration produced invalid config (no version)";
                return false;
            }
        } else if (version == 12) {
            m_config = ConfigMigrator::migrateV12ToV13(m_config);
            if (m_config.contains("version") && m_config["version"].isDouble()) {
                version = m_config["version"].toInt();
            } else {
                qWarning() << "Migration produced invalid config (no version)";
                return false;
            }
        } else {
            qWarning() << "Unknown config version during migration:" << version;
            return false;
        }
    }
    // Ensure we end at current version
    return version == kCurrentConfigVersion;
}

bool ConfigManager::validateConfig(const QJsonObject &cfg)
{
    if (!cfg.contains("version") || !cfg["version"].isDouble()) return false;
    int version = cfg["version"].toInt();
    if (version < 1 || version > kCurrentConfigVersion) return false;
    if (!cfg.contains("settings") || !cfg["settings"].isObject()) return false;
    // Basic validation of expected subfields
    QJsonObject settings = cfg["settings"].toObject();
    if (!settings.contains("playback") || !settings["playback"].isObject()) return false;
    QJsonObject playback = settings["playback"].toObject();
    if (!playback.contains("completion_threshold")) return false;
    if (playback.contains("player_backend") && !playback["player_backend"].isString()) return false;
    return true;
}

QJsonObject ConfigManager::defaultConfig() const
{
    QJsonObject cfg;
    cfg["version"] = kCurrentConfigVersion;

    QJsonObject settings;
    QJsonObject playback;
    playback["completion_threshold"] = 90;
    playback["autoplay_next_episode"] = true;
    playback["auto_skip_intro"] = false;
    playback["auto_skip_outro"] = false;
    playback["audio_delay"] = 0;
    playback["playback_volume"] = 100;
    playback["playback_muted"] = false;
    playback["skip_button_auto_hide_seconds"] = 6;
    playback["theme_song_volume"] = 0;
    playback["theme_song_loop"] = false;
    playback["ui_sounds_enabled"] = true;
    playback["ui_sounds_volume"] = 3;
    playback["performance_mode_enabled"] = false;
    settings["playback"] = playback;
    
    QJsonObject video;
    video["enable_framerate_matching"] = false;
    video["framerate_match_delay"] = 1;  // Default 1 second delay after refresh rate switch
    video["enable_hdr"] = false;
    settings["video"] = video;
    
    QJsonObject cache;
    cache["image_cache_size_mb"] = 500;
    cache["rounded_image_mode"] = "auto";
    cache["rounded_preprocess_enabled"] = true;
    settings["cache"] = cache;

    // UI Settings
    QJsonObject ui;
    ui["backdrop_rotation_interval"] = 30000;
    ui["launch_in_fullscreen"] = false;
    ui["ui_animations_enabled"] = true;
    settings["ui"] = ui;

    // Manual DPI Scale Override
    settings["manualDpiScaleOverride"] = 1.0;

    // MPV Profiles
    settings["mpv_profiles"] = defaultMpvProfiles();
    settings["default_profile"] = "Default";
    settings["library_profiles"] = QJsonObject();
    settings["series_profiles"] = QJsonObject();

    cfg["settings"] = settings;
    return cfg;
}

QJsonObject ConfigManager::defaultMpvProfiles()
{
    QJsonObject profiles;
    
    // Default profile - uses mpv defaults with basic settings
    QJsonObject defaultProfile;
    defaultProfile["hwdec_enabled"] = true;
    defaultProfile["hwdec_method"] = "auto";
    defaultProfile["deinterlace"] = false;
    defaultProfile["deinterlace_method"] = "";
    defaultProfile["video_output"] = "gpu-next";
    defaultProfile["interpolation"] = false;
    defaultProfile["extra_args"] = QJsonArray({"--fullscreen"});
    profiles["Default"] = defaultProfile;
    
    // High Quality profile - uses mpv's built-in high-quality profile
    QJsonObject highQuality;
    highQuality["hwdec_enabled"] = true;
    highQuality["hwdec_method"] = "auto";
    highQuality["deinterlace"] = false;
    highQuality["deinterlace_method"] = "";
    highQuality["video_output"] = "gpu-next";
    highQuality["interpolation"] = false;
    highQuality["extra_args"] = QJsonArray({"--fullscreen", "--profile=high-quality"});
    profiles["High Quality"] = highQuality;
    
    return profiles;
}

// ========================================
// MpvProfile Implementation
// ========================================

QStringList MpvProfile::buildArgs() const
{
    QStringList result;
    
    // Video output
    if (!videoOutput.isEmpty()) {
        result << "--vo=" + videoOutput;
    }
    
    // Hardware decoding
    if (hwdecEnabled) {
        result << "--hwdec=" + hwdecMethod;
    } else {
        result << "--hwdec=no";
    }
    
    // Deinterlacing
    if (deinterlace && !deinterlaceMethod.isEmpty()) {
        result << "--deinterlace=yes";
        result << "--vf=lavfi=[" + deinterlaceMethod + "]";
    }
    
    // Interpolation
    if (interpolation) {
        result << "--interpolation=yes";
        result << "--video-sync=display-resample";
    }
    
    // Extra args
    result << extraArgs;
    
    return result;
}

QJsonObject MpvProfile::toJson() const
{
    QJsonObject obj;
    obj["hwdec_enabled"] = hwdecEnabled;
    obj["hwdec_method"] = hwdecMethod;
    obj["deinterlace"] = deinterlace;
    obj["deinterlace_method"] = deinterlaceMethod;
    obj["video_output"] = videoOutput;
    obj["interpolation"] = interpolation;
    
    QJsonArray extraArgsArray;
    for (const QString &arg : extraArgs) {
        extraArgsArray.append(arg);
    }
    obj["extra_args"] = extraArgsArray;
    
    return obj;
}

MpvProfile MpvProfile::fromJson(const QString &name, const QJsonObject &obj)
{
    MpvProfile profile;
    profile.name = name;
    profile.hwdecEnabled = obj["hwdec_enabled"].toBool(true);
    profile.hwdecMethod = obj["hwdec_method"].toString("auto");
    profile.deinterlace = obj["deinterlace"].toBool(false);
    profile.deinterlaceMethod = obj["deinterlace_method"].toString("");
    profile.videoOutput = obj["video_output"].toString("gpu-next");
    profile.interpolation = obj["interpolation"].toBool(false);
    
    // Migration-safe parsing:
    // - Preferred: array of strings (current format)
    // - Legacy/edge: single string (newline-separated)
    const QJsonValue extraArgsVal = obj.value("extra_args");
    if (extraArgsVal.isArray()) {
        const QJsonArray extraArgsArray = extraArgsVal.toArray();
        for (const QJsonValue &val : extraArgsArray) {
            const QString arg = val.toString().trimmed();
            if (!arg.isEmpty()) {
                profile.extraArgs << arg;
            }
        }
    } else if (extraArgsVal.isString()) {
        const QStringList lines = extraArgsVal.toString().split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QString arg = line.trimmed();
            if (!arg.isEmpty()) {
                profile.extraArgs << arg;
            }
        }
    }
    
    profile.args = profile.buildArgs();
    return profile;
}

// ========================================
// MPV Profile Management
// ========================================

QStringList ConfigManager::getMpvProfileNames() const
{
    QStringList names;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("mpv_profiles") && settings["mpv_profiles"].isObject()) {
            QJsonObject profiles = settings["mpv_profiles"].toObject();
            names = profiles.keys();
        }
    }
    // Ensure Default profiles are always present in the list
    if (!names.contains("Default")) names.prepend("Default");
    if (!names.contains("High Quality") && !names.contains("High Quality")) {
        names.insert(1, "High Quality");
    }
    return names;
}

QVariantMap ConfigManager::getMpvProfile(const QString &name) const
{
    MpvProfile profile = getMpvProfileStruct(name);
    QVariantMap result;
    result["name"] = profile.name;
    result["hwdecEnabled"] = profile.hwdecEnabled;
    result["hwdecMethod"] = profile.hwdecMethod;
    result["deinterlace"] = profile.deinterlace;
    result["deinterlaceMethod"] = profile.deinterlaceMethod;
    result["videoOutput"] = profile.videoOutput;
    result["interpolation"] = profile.interpolation;
    result["extraArgs"] = QVariant::fromValue(profile.extraArgs);
    result["args"] = QVariant::fromValue(profile.args);
    return result;
}

MpvProfile ConfigManager::getMpvProfileStruct(const QString &name) const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("mpv_profiles") && settings["mpv_profiles"].isObject()) {
            QJsonObject profiles = settings["mpv_profiles"].toObject();
            if (profiles.contains(name)) {
                return MpvProfile::fromJson(name, profiles[name].toObject());
            }
        }
    }
    
    // Return default profile if not found
    QJsonObject defaultProfiles = defaultMpvProfiles();
    if (defaultProfiles.contains(name)) {
        return MpvProfile::fromJson(name, defaultProfiles[name].toObject());
    }
    
    // Fallback to Default profile
    return MpvProfile::fromJson("Default", defaultProfiles["Default"].toObject());
}

void ConfigManager::setMpvProfile(const QString &name, const QVariantMap &profileData)
{
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    
    QJsonObject profiles;
    if (settings.contains("mpv_profiles") && settings["mpv_profiles"].isObject()) {
        profiles = settings["mpv_profiles"].toObject();
    } else {
        profiles = defaultMpvProfiles();
    }
    
    // Build profile JSON from variant map
    QJsonObject profileJson;
    profileJson["hwdec_enabled"] = profileData["hwdecEnabled"].toBool();
    profileJson["hwdec_method"] = profileData["hwdecMethod"].toString();
    profileJson["deinterlace"] = profileData["deinterlace"].toBool();
    profileJson["deinterlace_method"] = profileData["deinterlaceMethod"].toString();
    profileJson["video_output"] = profileData["videoOutput"].toString();
    profileJson["interpolation"] = profileData["interpolation"].toBool();
    
    QJsonArray extraArgsArray;
    const QVariant extraArgsVariant = profileData.value("extraArgs");
    QStringList extraArgs;
    if (extraArgsVariant.metaType().id() == QMetaType::QStringList) {
        extraArgs = extraArgsVariant.toStringList();
    } else if (extraArgsVariant.metaType().id() == QMetaType::QVariantList) {
        const QVariantList rawList = extraArgsVariant.toList();
        for (const QVariant &entry : rawList) {
            const QString arg = entry.toString().trimmed();
            if (!arg.isEmpty()) {
                extraArgs << arg;
            }
        }
    } else if (extraArgsVariant.metaType().id() == QMetaType::QString) {
        extraArgs = extraArgsVariant.toString().split('\n', Qt::SkipEmptyParts);
    }

    for (const QString &argRaw : extraArgs) {
        const QString arg = argRaw.trimmed();
        if (!arg.isEmpty()) {
            extraArgsArray.append(arg);
        }
    }
    profileJson["extra_args"] = extraArgsArray;
    
    profiles[name] = profileJson;
    settings["mpv_profiles"] = profiles;
    m_config["settings"] = settings;
    save();
    emit mpvProfilesChanged();
}

bool ConfigManager::deleteMpvProfile(const QString &name)
{
    // Cannot delete built-in profiles
    if (name == "Default" || name == "High Quality") {
        qWarning() << "ConfigManager: Cannot delete built-in profile:" << name;
        return false;
    }
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    
    QJsonObject profiles;
    if (settings.contains("mpv_profiles") && settings["mpv_profiles"].isObject()) {
        profiles = settings["mpv_profiles"].toObject();
    }
    
    if (!profiles.contains(name)) {
        return false;
    }
    
    profiles.remove(name);
    settings["mpv_profiles"] = profiles;
    
    // If this was the default profile, reset to Default
    if (settings["default_profile"].toString() == name) {
        settings["default_profile"] = "Default";
        emit defaultProfileNameChanged();
    }
    
    // Remove from any library/series assignments
    QJsonObject libraryProfiles = settings["library_profiles"].toObject();
    QStringList libraryKeys = libraryProfiles.keys();
    for (const QString &key : libraryKeys) {
        if (libraryProfiles[key].toString() == name) {
            libraryProfiles.remove(key);
        }
    }
    settings["library_profiles"] = libraryProfiles;
    
    QJsonObject seriesProfiles = settings["series_profiles"].toObject();
    QStringList seriesKeys = seriesProfiles.keys();
    for (const QString &key : seriesKeys) {
        if (seriesProfiles[key].toString() == name) {
            seriesProfiles.remove(key);
        }
    }
    settings["series_profiles"] = seriesProfiles;
    
    m_config["settings"] = settings;
    save();
    emit mpvProfilesChanged();
    emit libraryProfilesChanged();
    emit seriesProfilesChanged();
    return true;
}

QString ConfigManager::getDefaultProfileName() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("default_profile")) {
            return settings["default_profile"].toString();
        }
    }
    return "Default";
}

void ConfigManager::setDefaultProfileName(const QString &name)
{
    if (name == getDefaultProfileName()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    
    settings["default_profile"] = name;
    m_config["settings"] = settings;
    save();
    emit defaultProfileNameChanged();
}

QString ConfigManager::getLibraryProfile(const QString &libraryId) const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("library_profiles") && settings["library_profiles"].isObject()) {
            QJsonObject libraryProfiles = settings["library_profiles"].toObject();
            if (libraryProfiles.contains(libraryId)) {
                return libraryProfiles[libraryId].toString();
            }
        }
    }
    return QString();
}

void ConfigManager::setLibraryProfile(const QString &libraryId, const QString &profileName)
{
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    
    QJsonObject libraryProfiles;
    if (settings.contains("library_profiles") && settings["library_profiles"].isObject()) {
        libraryProfiles = settings["library_profiles"].toObject();
    }
    
    if (profileName.isEmpty()) {
        libraryProfiles.remove(libraryId);
    } else {
        libraryProfiles[libraryId] = profileName;
    }
    
    settings["library_profiles"] = libraryProfiles;
    m_config["settings"] = settings;
    save();
    emit libraryProfilesChanged();
}

QString ConfigManager::getSeriesProfile(const QString &seriesId) const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("series_profiles") && settings["series_profiles"].isObject()) {
            QJsonObject seriesProfiles = settings["series_profiles"].toObject();
            if (seriesProfiles.contains(seriesId)) {
                return seriesProfiles[seriesId].toString();
            }
        }
    }
    return QString();
}

void ConfigManager::setSeriesProfile(const QString &seriesId, const QString &profileName)
{
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    
    QJsonObject seriesProfiles;
    if (settings.contains("series_profiles") && settings["series_profiles"].isObject()) {
        seriesProfiles = settings["series_profiles"].toObject();
    }
    
    if (profileName.isEmpty()) {
        seriesProfiles.remove(seriesId);
    } else {
        seriesProfiles[seriesId] = profileName;
    }
    
    settings["series_profiles"] = seriesProfiles;
    m_config["settings"] = settings;
    save();
    emit seriesProfilesChanged();
}

QString ConfigManager::resolveProfileForItem(const QString &libraryId, const QString &seriesId) const
{
    // Priority: series > library > default
    
    // Check series-specific profile
    if (!seriesId.isEmpty()) {
        QString seriesProfile = getSeriesProfile(seriesId);
        if (!seriesProfile.isEmpty()) {
            return seriesProfile;
        }
    }
    
    // Check library-specific profile
    if (!libraryId.isEmpty()) {
        QString libraryProfile = getLibraryProfile(libraryId);
        if (!libraryProfile.isEmpty()) {
            return libraryProfile;
        }
    }
    
    // Fall back to default
    return getDefaultProfileName();
}

QStringList ConfigManager::getMpvArgsForProfile(const QString &profileName, bool isHdrContent) const
{
    MpvProfile profile = getMpvProfileStruct(profileName);
    QStringList args = profile.buildArgs();
    
    // Only apply HDR-specific renderer hints for HDR items.
    // Applying these globally can trigger HDR behavior for SDR playback on some stacks.
    if (getEnableHDR() && isHdrContent) {
        // Ensure gpu-next for HDR
        bool hasGpuNext = false;
        for (int i = 0; i < args.size(); ++i) {
            if (args[i].startsWith("--vo=")) {
                if (args[i] != "--vo=gpu-next") {
                    args[i] = "--vo=gpu-next";
                }
                hasGpuNext = true;
                break;
            }
        }
        if (!hasGpuNext) {
            args.prepend("--vo=gpu-next");
        }
        
        // Normalize colorspace hint for HDR to avoid conflicting values from profiles.
        bool hasTargetColorspaceHint = false;
        for (int i = 0; i < args.size(); ++i) {
            if (args[i].startsWith("--target-colorspace-hint=")) {
                args[i] = "--target-colorspace-hint=auto";
                hasTargetColorspaceHint = true;
            }
        }
        if (!hasTargetColorspaceHint) {
            args << "--target-colorspace-hint=auto";
        }
    }
    
    return args;
}

void ConfigManager::setTheme(const QString &theme)
{
    if (theme == getTheme()) return;
    
    QJsonObject settings;
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        settings = m_config["settings"].toObject();
    }
    QJsonObject ui;
    if (settings.contains("ui") && settings["ui"].isObject()) {
        ui = settings["ui"].toObject();
    }
    ui["theme"] = theme;
    settings["ui"] = ui;
    m_config["settings"] = settings;
    save();
    emit themeChanged();
}

QString ConfigManager::getTheme() const
{
    if (m_config.contains("settings") && m_config["settings"].isObject()) {
        QJsonObject settings = m_config["settings"].toObject();
        if (settings.contains("ui") && settings["ui"].isObject()) {
            QJsonObject ui = settings["ui"].toObject();
            if (ui.contains("theme")) {
                return ui["theme"].toString();
            }
        }
    }
    return "Jellyfin"; // Default theme
}
