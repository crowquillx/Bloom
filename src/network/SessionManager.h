#pragma once

#include <QObject>
#include <QDateTime>
#include <QString>
#include <QTimer>

class ConfigManager;
class ISecretStore;

/**
 * @brief Manages device-specific session state and device ID rotation.
 *
 * Responsibilities:
 * - Generate and persist device IDs with hostname
 * - Track session metadata (device name, type, last active)
 * - Handle device ID rotation with configurable intervals
 * - Coordinate with SecretStore for token migration during rotation
 *
 * @note Device IDs are rotated periodically for security. During rotation,
 *       the current access token is migrated to the new device ID.
 */
class SessionManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY deviceIdChanged)
    Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY deviceNameChanged)
    Q_PROPERTY(QString deviceType READ deviceType WRITE setDeviceType NOTIFY deviceTypeChanged)
    Q_PROPERTY(int rotationIntervalDays READ rotationIntervalDays WRITE setRotationIntervalDays NOTIFY rotationIntervalDaysChanged)
    Q_PROPERTY(QDateTime lastRotation READ lastRotation NOTIFY lastRotationChanged)
    Q_PROPERTY(bool autoRotationEnabled READ autoRotationEnabled WRITE setAutoRotationEnabled NOTIFY autoRotationEnabledChanged)

public:
    explicit SessionManager(ConfigManager *configManager, ISecretStore *secretStore, QObject *parent = nullptr);

    /**
     * @brief Initialize and check if rotation is needed
     */
    void initialize();

    // Device ID accessors
    QString deviceId() const;
    QString deviceName() const;
    QString deviceType() const;

    // Rotation settings
    int rotationIntervalDays() const;
    QDateTime lastRotation() const;
    bool autoRotationEnabled() const;

    // Setters
    void setDeviceName(const QString &name);
    void setDeviceType(const QString &type);
    void setRotationIntervalDays(int days);
    void setAutoRotationEnabled(bool enabled);

    /**
     * @brief Check if device ID rotation is due based on policy
     * @return true if rotation should occur
     */
    bool shouldRotate() const;

    /**
     * @brief Force immediate device ID rotation
     * Migrates existing token to new device ID.
     * @return true if rotation succeeded
     */
    Q_INVOKABLE bool rotateDeviceId();

    /**
     * @brief Get the effective device ID for a user
     * Includes user ID suffix to prevent cross-user conflicts.
     * @param userId The Jellyfin user ID
     * @return Device-specific identifier
     */
    QString getDeviceIdForUser(const QString &userId) const;

    /**
     * @brief Get the SecretStore account key for a session
     * @param serverUrl Jellyfin server URL
     * @param username Username
     * @param deviceId Device ID
     * @return Account key in format serverUrl|username|deviceId
     */
    static QString accountKey(const QString &serverUrl, const QString &username, const QString &deviceId);

    /**
     * @brief Parse an account key into components
     * @param accountKey The account key
     * @return Tuple of serverUrl, username, deviceId
     */
    static std::tuple<QString, QString, QString> parseAccountKey(const QString &accountKey);

    /**
     * @brief Update last activity timestamp for the current session
     */
    Q_INVOKABLE void updateLastActivity();

    /**
     * @brief Get the timestamp of last recorded activity
     */
    QDateTime lastActivity() const;

signals:
    void deviceIdChanged();
    void deviceNameChanged();
    void deviceTypeChanged();
    void rotationIntervalDaysChanged();
    void lastRotationChanged();
    void autoRotationEnabledChanged();
    void deviceIdRotated(QString oldDeviceId, QString newDeviceId);
    void rotationFailed(QString error);

private:
    ConfigManager *m_configManager;
    ISecretStore *m_secretStore;
    QTimer *m_rotationCheckTimer;

    // Cached values
    QString m_deviceId;
    QString m_deviceName;
    QString m_deviceType;
    int m_rotationIntervalDays;
    QDateTime m_lastRotation;
    bool m_autoRotationEnabled;

    void loadSettings();
    void saveSettings();
    QString generateDeviceId() const;
    void scheduleRotationCheck();
    void checkAndRotateIfNeeded();
    bool migrateToken(const QString &oldDeviceId, const QString &newDeviceId);
};
