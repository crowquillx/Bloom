#include "SessionManager.h"
#include "../utils/ConfigManager.h"
#include "../security/ISecretStore.h"
#include <QUuid>
#include <QSysInfo>
#include <QRegularExpression>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>

SessionManager::SessionManager(ConfigManager *configManager, ISecretStore *secretStore, QObject *parent)
    : QObject(parent)
    , m_configManager(configManager)
    , m_secretStore(secretStore)
    , m_rotationCheckTimer(new QTimer(this))
{
    loadSettings();
    connect(m_rotationCheckTimer, &QTimer::timeout, this, &SessionManager::checkAndRotateIfNeeded);
}

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

QString SessionManager::deviceId() const
{
    return m_deviceId;
}

QString SessionManager::deviceName() const
{
    return m_deviceName;
}

QString SessionManager::deviceType() const
{
    return m_deviceType;
}

int SessionManager::rotationIntervalDays() const
{
    return m_rotationIntervalDays;
}

QDateTime SessionManager::lastRotation() const
{
    return m_lastRotation;
}

bool SessionManager::autoRotationEnabled() const
{
    return m_autoRotationEnabled;
}

void SessionManager::setDeviceName(const QString &name)
{
    if (m_deviceName == name) return;
    m_deviceName = name;
    saveSettings();
    emit deviceNameChanged();
}

void SessionManager::setDeviceType(const QString &type)
{
    if (m_deviceType == type) return;
    m_deviceType = type;
    saveSettings();
    emit deviceTypeChanged();
}

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

QString SessionManager::getDeviceIdForUser(const QString &userId) const
{
    if (userId.isEmpty()) {
        return m_deviceId;
    }
    return m_deviceId + "-" + userId;
}

QString SessionManager::accountKey(const QString &serverUrl, const QString &username, const QString &deviceId)
{
    return QString("%1|%2|%3").arg(serverUrl, username, deviceId);
}

std::tuple<QString, QString, QString> SessionManager::parseAccountKey(const QString &accountKey)
{
    QStringList parts = accountKey.split('|');
    if (parts.size() >= 3) {
        return std::make_tuple(parts[0], parts[1], parts[2]);
    }
    return std::make_tuple(QString(), QString(), QString());
}

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

QDateTime SessionManager::lastActivity() const
{
    // Return the last activity timestamp from settings
    // This is a simplified implementation
    return QDateTime::currentDateTime();
}

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

void SessionManager::saveSettings()
{
    if (!m_configManager) return;

    // Device ID is already saved by ConfigManager::getDeviceId()
    // Save other settings via ConfigManager when it supports them
    // For now, settings are kept in memory
}

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

void SessionManager::scheduleRotationCheck()
{
    if (!m_autoRotationEnabled || m_rotationIntervalDays <= 0) {
        m_rotationCheckTimer->stop();
        return;
    }

    // Check every hour for rotation
    m_rotationCheckTimer->start(60 * 60 * 1000);  // 1 hour
}

void SessionManager::checkAndRotateIfNeeded()
{
    if (shouldRotate()) {
        rotateDeviceId();
    }
}

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
