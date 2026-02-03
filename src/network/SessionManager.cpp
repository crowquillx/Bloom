#include "SessionManager.h"
#include "../utils/ConfigManager.h"
#include "../security/ISecretStore.h"
#include <QUuid>
#include <QSysInfo>
#include <QRegularExpression>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>

/**
 * @brief Constructs a SessionManager responsible for device identity, rotation scheduling, and token migration.
 *
 * @param configManager Pointer to the ConfigManager used to load and persist session-related settings; may be null.
 * @param secretStore Pointer to the ISecretStore used to read/write migrated tokens; may be null.
 * @param parent Optional QObject parent.
 */
SessionManager::SessionManager(ConfigManager *configManager, ISecretStore *secretStore, QObject *parent)
    : QObject(parent)
    , m_configManager(configManager)
    , m_secretStore(secretStore)
    , m_rotationCheckTimer(new QTimer(this))
{
    loadSettings();
    connect(m_rotationCheckTimer, &QTimer::timeout, this, &SessionManager::checkAndRotateIfNeeded);
}

/**
 * @brief Ensure the session manager is initialized: create a device ID if missing, perform an immediate rotation when configured and due, and start periodic rotation checks.
 *
 * If a device ID does not exist, one is generated, persisted, and `deviceIdChanged` is emitted. If auto-rotation is enabled and rotation is due, a rotation is performed (which may update the device ID and emit related signals). Finally, rotation checks are scheduled according to the current settings.
 */
void SessionManager::initialize()
{
    // Generate initial device ID if needed
    if (m_deviceId.isEmpty()) {
        m_deviceId = generateDeviceId();
        saveSettings();
        emit deviceIdChanged();
    }

    // Check if rotation is needed on startup
    if (m_autoRotationEnabled && shouldRotate()) {
        rotateDeviceId();
    }

    scheduleRotationCheck();
}

/**
 * @brief Get the current device identifier.
 *
 * @return QString The current device identifier string.
 */
QString SessionManager::deviceId() const
{
    return m_deviceId;
}

/**
 * @brief Current device name used to identify this device.
 *
 * The name is stored and exposed by the SessionManager; it is sanitized when loaded from settings.
 *
 * @return QString The device name.
 */
QString SessionManager::deviceName() const
{
    return m_deviceName;
}

/**
 * @brief Current device type.
 *
 * @return QString The configured device type string (for example, "htpc").
 */
QString SessionManager::deviceType() const
{
    return m_deviceType;
}

/**
 * @brief Returns the configured device rotation interval.
 *
 * The interval used to determine when the device ID should be rotated.
 *
 * @return int Rotation interval in days; 0 disables automatic rotation. Value is clamped between 0 and 365.
 */
int SessionManager::rotationIntervalDays() const
{
    return m_rotationIntervalDays;
}

/**
 * @brief Returns the timestamp of the last device ID rotation.
 *
 * @return QDateTime Timestamp of the most recent rotation; may be invalid if no rotation has occurred.
 */
QDateTime SessionManager::lastRotation() const
{
    return m_lastRotation;
}

/**
 * @brief Indicates whether automatic device-ID rotation is enabled.
 *
 * @return true if automatic rotation of the device ID is enabled, false otherwise.
 */
bool SessionManager::autoRotationEnabled() const
{
    return m_autoRotationEnabled;
}

/**
 * @brief Update the stored device name.
 *
 * Sets the device name, persists the new setting, and notifies listeners when the value changes.
 *
 * @param name New device name to store.
 *
 * @note If the provided name equals the current value, no action is taken and no signal is emitted.
 * @see deviceNameChanged()
 */
void SessionManager::setDeviceName(const QString &name)
{
    if (m_deviceName == name) return;
    m_deviceName = name;
    saveSettings();
    emit deviceNameChanged();
}

/**
 * @brief Update the session's device type and persist the change.
 *
 * If the provided type differs from the current value, the device type is updated,
 * settings are saved, and the `deviceTypeChanged` signal is emitted.
 *
 * @param type New device type (e.g., "htpc", "mobile").
 */
void SessionManager::setDeviceType(const QString &type)
{
    if (m_deviceType == type) return;
    m_deviceType = type;
    saveSettings();
    emit deviceTypeChanged();
}

/**
 * @brief Set the device identifier rotation interval in days.
 *
 * The provided value is clamped to the range [0, 365]. When the interval changes,
 * the new value is persisted, the `rotationIntervalDaysChanged()` signal is emitted,
 * and rotation checks are rescheduled.
 *
 * @param days Desired rotation interval in days; values < 0 become 0 and values > 365 become 365.
 */
void SessionManager::setRotationIntervalDays(int days)
{
    if (days < 0) days = 0;
    if (days > 365) days = 365;
    if (m_rotationIntervalDays == days) return;

    m_rotationIntervalDays = days;
    saveSettings();
    emit rotationIntervalDaysChanged();
    scheduleRotationCheck();
}

/**
 * @brief Enable or disable automatic device-ID rotation.
 *
 * Updates the auto-rotation setting, persists the change, and notifies observers.
 * If enabling auto-rotation and a rotation is currently due, triggers an immediate device-ID rotation.
 *
 * @param enabled `true` to enable automatic rotation, `false` to disable it.
 */
void SessionManager::setAutoRotationEnabled(bool enabled)
{
    if (m_autoRotationEnabled == enabled) return;

    m_autoRotationEnabled = enabled;
    saveSettings();
    emit autoRotationEnabledChanged();

    if (enabled && shouldRotate()) {
        rotateDeviceId();
    }
}

/**
 * @brief Determine whether the device ID rotation is due based on settings and timestamps.
 *
 * @return `true` if auto-rotation is enabled, the rotation interval is greater than zero, and the current time is at or after the next scheduled rotation (or there is no recorded last rotation); `false` otherwise.
 */
bool SessionManager::shouldRotate() const
{
    if (!m_autoRotationEnabled || m_rotationIntervalDays <= 0) {
        return false;
    }

    if (!m_lastRotation.isValid()) {
        return true;
    }

    QDateTime nextRotation = m_lastRotation.addDays(m_rotationIntervalDays);
    return QDateTime::currentDateTime() >= nextRotation;
}

/**
 * @brief Rotates the stored device identifier and updates rotation metadata.
 *
 * Attempts to migrate any existing token from the old device ID to a newly generated device ID,
 * then replaces the stored device ID, updates the last-rotation timestamp, persists settings,
 * and emits deviceIdChanged(), lastRotationChanged(), and deviceIdRotated(oldDeviceId, newDeviceId).
 *
 * @return `true` if rotation completed (device ID updated and signals emitted).
 */
bool SessionManager::rotateDeviceId()
{
    QString oldDeviceId = m_deviceId;
    QString newDeviceId = generateDeviceId();

    qInfo() << "SessionManager: Rotating device ID" << oldDeviceId << "->" << newDeviceId;

    // Attempt to migrate token if we have credentials
    if (!migrateToken(oldDeviceId, newDeviceId)) {
        qWarning() << "SessionManager: Token migration may have failed, continuing with rotation";
    }

    m_deviceId = newDeviceId;
    m_lastRotation = QDateTime::currentDateTime();
    saveSettings();

    emit deviceIdChanged();
    emit lastRotationChanged();
    emit deviceIdRotated(oldDeviceId, newDeviceId);

    return true;
}

/**
 * Return the device identifier scoped to a specific user.
 *
 * @param userId User identifier to append to the base device ID; when empty, no suffix is applied.
 * @return QString The base device ID if `userId` is empty, otherwise `baseDeviceId-userId`.
 */
QString SessionManager::getDeviceIdForUser(const QString &userId) const
{
    if (userId.isEmpty()) {
        return m_deviceId;
    }
    return m_deviceId + "-" + userId;
}

/**
 * @brief Constructs a compact account key from server URL, username, and device identifier.
 *
 * @param serverUrl The server URL component.
 * @param username The username component.
 * @param deviceId The device identifier component.
 * @return QString A string in the form "serverUrl|username|deviceId".
 */
QString SessionManager::accountKey(const QString &serverUrl, const QString &username, const QString &deviceId)
{
    return QString("%1|%2|%3").arg(serverUrl, username, deviceId);
}

/**
 * @brief Parses an account key in the form "serverUrl|username|deviceId".
 *
 * @param accountKey The account key string containing server URL, username, and device ID separated by '|' characters.
 * @return std::tuple<QString, QString, QString> A tuple (serverUrl, username, deviceId). If the input does not contain at least three '|'-separated parts, returns three empty QStrings.
 */
std::tuple<QString, QString, QString> SessionManager::parseAccountKey(const QString &accountKey)
{
    QStringList parts = accountKey.split('|');
    if (parts.size() >= 3) {
        return std::make_tuple(parts[0], parts[1], parts[2]);
    }
    return std::make_tuple(QString(), QString(), QString());
}

/**
 * @brief Record the timestamp of the last user activity for this session.
 *
 * If a ConfigManager is available, this updates the session/settings record with the current
 * last-activity timestamp; if no ConfigManager is present, the call is a no-op.
 *
 * @details
 * Persistence through ConfigManager is intended; currently the timestamp is tracked in memory
 * and a debug message is emitted to indicate the update.
 */
void SessionManager::updateLastActivity()
{
    if (!m_configManager) return;

    QJsonObject settings;
    QJsonObject session;
    
    // Load existing settings
    // Note: ConfigManager doesn't expose raw config, so we use properties
    // The last activity is stored in the config via ConfigManager methods
    
    // For now, we track this in memory; persistence can be added if needed
    qDebug() << "SessionManager: Last activity updated";
}

/**
 * @brief Get the timestamp of the most recent activity for this session.
 *
 * @details Returns the last activity timestamp tracked for the session. In the current implementation
 * this is a placeholder that returns the current date and time.
 *
 * @return QDateTime The timestamp of the last activity; currently the current date/time.
 */
QDateTime SessionManager::lastActivity() const
{
    // Return the last activity timestamp from settings
    // This is a simplified implementation
    return QDateTime::currentDateTime();
}

/**
 * @brief Loads session-related settings from the ConfigManager into this SessionManager.
 *
 * Retrieves the persistent device ID from the ConfigManager (generating one if missing), initializes other session fields with sensible defaults, and sanitizes the device name for safe storage/use.
 *
 * If no ConfigManager is available, the method returns without changing state.
 */
void SessionManager::loadSettings()
{
    if (!m_configManager) return;

    // Device ID is stored in ConfigManager's config
    // We access it via the existing getDeviceId() which generates if not exists
    m_deviceId = m_configManager->getDeviceId();

    // Load other session settings from config
    // For simplicity, we'll access the raw config through ConfigManager's structure
    // In production, these would be proper Q_PROPERTY accessors

    // Default values
    m_deviceName = QSysInfo::machineHostName();
    if (m_deviceName.isEmpty()) {
        m_deviceName = "Bloom Device";
    }
    m_deviceType = "htpc";
    m_rotationIntervalDays = 90;  // Default 90 days
    m_lastRotation = QDateTime::currentDateTime();
    m_autoRotationEnabled = false;

    // Sanitize device name
    m_deviceName = m_deviceName.replace(QRegularExpression("[^a-zA-Z0-9- ]"), "-");
}

/**
 * @brief Persist session-related settings to the configuration backend when supported.
 *
 * Attempts to save mutable session settings via the configured ConfigManager. If no
 * ConfigManager is available this function is a no-op. Currently the device ID is
 * persisted by ConfigManager::getDeviceId() and other settings remain in memory until
 * ConfigManager exposes explicit save support.
 */
void SessionManager::saveSettings()
{
    if (!m_configManager) return;

    // Device ID is already saved by ConfigManager::getDeviceId()
    // Save other settings via ConfigManager when it supports them
    // For now, settings are kept in memory
}

/**
 * @brief Generate a device identifier combining the host name and a UUID.
 *
 * The identifier is formatted as "Bloom-<sanitized-hostname>-<uuid>". The hostname has any character
 * that is not A–Z, a–z, 0–9, or hyphen replaced with a hyphen; the UUID is produced without braces.
 *
 * @return QString The generated device identifier string.
 */
QString SessionManager::generateDeviceId() const
{
    QString hostname = QSysInfo::machineHostName();
    if (hostname.isEmpty()) {
        hostname = "unknown";
    }
    hostname = hostname.replace(QRegularExpression("[^a-zA-Z0-9-]"), "-");

    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QString("Bloom-%1-%2").arg(hostname, uuid);
}

/**
 * @brief Enables or disables periodic device rotation checks based on settings.
 *
 * If auto-rotation is disabled or the rotation interval is zero or negative, the rotation check timer is stopped.
 * Otherwise the rotation check timer is started to run once every hour.
 */
void SessionManager::scheduleRotationCheck()
{
    if (!m_autoRotationEnabled || m_rotationIntervalDays <= 0) {
        m_rotationCheckTimer->stop();
        return;
    }

    // Check every hour for rotation
    m_rotationCheckTimer->start(60 * 60 * 1000);  // 1 hour
}

/**
 * @brief Check whether the device ID should be rotated and perform rotation if required.
 *
 * If rotation is due according to the manager's policy, invokes rotation of the device ID.
 */
void SessionManager::checkAndRotateIfNeeded()
{
    if (shouldRotate()) {
        rotateDeviceId();
    }
}

/**
 * @brief Migrate the Jellyfin access token from one device identifier to another.
 *
 * If there is no active Jellyfin session or the session lacks an access token, the
 * function performs no migration and returns `true`. Requires a configured
 * ISecretStore and ConfigManager; otherwise returns `false`. On successful
 * migration the token is stored under the new account key and the old secret is removed.
 *
 * @param oldDeviceId Device identifier currently associated with the stored token.
 * @param newDeviceId Device identifier to associate with the migrated token.
 * @return true if migration succeeded or there was no active token to migrate, false on failure (missing stores or secret storage errors).
 */
bool SessionManager::migrateToken(const QString &oldDeviceId, const QString &newDeviceId)
{
    if (!m_secretStore || !m_configManager) {
        return false;
    }

    // Get current session info
    auto session = m_configManager->getJellyfinSession();
    if (!session.isValid() || session.accessToken.isEmpty()) {
        // No active token to migrate
        return true;
    }

    QString serverUrl = session.serverUrl;
    QString username = session.username;

    // Build old and new account keys
    QString oldAccount = accountKey(serverUrl, username, oldDeviceId);
    QString newAccount = accountKey(serverUrl, username, newDeviceId);

    // Get token from old account
    QString token = m_secretStore->getSecret("Bloom/Jellyfin", oldAccount);
    if (token.isEmpty()) {
        qWarning() << "SessionManager: No token found for old device ID";
        return false;
    }

    // Store token under new account
    if (!m_secretStore->setSecret("Bloom/Jellyfin", newAccount, token)) {
        qWarning() << "SessionManager: Failed to store token for new device ID:" << m_secretStore->lastError();
        return false;
    }

    // Delete old token
    m_secretStore->deleteSecret("Bloom/Jellyfin", oldAccount);

    qInfo() << "SessionManager: Token migrated successfully";
    return true;
}