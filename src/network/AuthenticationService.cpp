#include "AuthenticationService.h"
#include "../security/ISecretStore.h"
#include "../utils/ConfigManager.h"
#include "config/version.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QPointer>
#include <QThreadPool>
#include "../utils/BloomLogging.h"

AuthenticationService::AuthenticationService(ISecretStore *secretStore, QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_secretStore(secretStore)
{
}

void AuthenticationService::initialize(ConfigManager *configManager)
{
    if (!configManager) {
        qCWarning(lcAuth) << "AuthenticationService::initialize called with null ConfigManager";
        return;
    }

    m_configManager = configManager;
    m_isRestoringSession = true;
    emit isRestoringSessionChanged();

    const ConfigManager::SessionData session = configManager->getJellyfinSession();
    const ConfigManager::SessionData legacySession =
        configManager->getPendingLegacyJellyfinSession();
    const std::optional<ServerConnection> connection = configManager->getActiveConnection();
    const bool pendingLegacyMigration = configManager->hasPendingLegacyJellyfinMigration();
    const bool legacyMatchesConnection = connection.has_value()
        && ServerConnection::normalizeBaseUrl(legacySession.serverUrl) == connection->baseUrl
        && legacySession.userId == connection->accountId;
    ISecretStore *store = m_secretStore;
    const QString deviceId = configManager->getDeviceId();

    QFuture<RestorationResult> future = QtConcurrent::run(
        [session, legacySession, connection, pendingLegacyMigration,
         legacyMatchesConnection, store, deviceId]() {
            RestorationResult result{};
            result.serverUrl = session.serverUrl;
            result.userId = session.userId;
            result.username = session.username;
            result.connection = connection.value_or(ServerConnection{});

            if (!store || !connection.has_value() || !connection->isValid()
                || connection->providerKind != ProviderKind::Jellyfin || !session.isValid()) {
                return result;
            }

            CredentialStore credentials(store);
            const CredentialReadResult credentialResult = credentials.readAccessToken(
                *connection,
                deviceId,
                legacyMatchesConnection ? legacySession.serverUrl : QString(),
                legacyMatchesConnection ? legacySession.username : QString(),
                session.accessToken);
            result.accessToken = credentialResult.secret;
            result.success = !result.accessToken.isEmpty();
            result.error = credentialResult.error;
            result.cleanupError = credentialResult.cleanupError;
            result.legacyMigrationComplete = pendingLegacyMigration
                && legacyMatchesConnection && result.success && result.error.isEmpty()
                && result.cleanupError.isEmpty();
            return result;
        });

    m_restorationWatcher.disconnect(this);
    connect(&m_restorationWatcher, &QFutureWatcher<RestorationResult>::finished,
            this, [this, configManager]() {
        const RestorationResult result = m_restorationWatcher.result();
        const auto currentConnection = configManager->getActiveConnection();
        const bool connectionChanged = result.connection.isValid()
            && (!currentConnection.has_value()
                || currentConnection->connectionId != result.connection.connectionId);

        if (connectionChanged) {
            qCInfo(lcAuth) << "Ignoring stale session restoration result after connection switch";
        } else {
            if (result.legacyMigrationComplete) {
                configManager->finalizeLegacyJellyfinMigration();
            }

            if (result.success) {
                m_activeConnection = result.connection;
                restoreSession(result.serverUrl,
                               result.userId,
                               result.accessToken,
                               result.username);
            } else if (!result.error.isEmpty()) {
                qCWarning(lcAuth) << "Session restoration failed:" << result.error;
            }
        }
        if (!result.cleanupError.isEmpty()) {
            qCWarning(lcAuth) << "Legacy credential cleanup failed:"
                              << result.cleanupError;
        }

        m_isRestoringSession = false;
        emit isRestoringSessionChanged();
    });

    m_restorationWatcher.setFuture(future);
}

QString AuthenticationService::normalizeUrl(const QString &url)
{
    QString normalized = url.trimmed();
    while (normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized;
}

QNetworkRequest AuthenticationService::createRequest(const QString &endpoint) const
{
    QUrl url(m_serverUrl + endpoint);
    QNetworkRequest request(url);
    
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    // Build authorization header with unique device ID
    QString deviceId = m_configManager ? m_configManager->getDeviceId() : "bloom-desktop-fallback";
    QString authHeader = QString("MediaBrowser Client=\"Bloom\", Device=\"Desktop\", DeviceId=\"%1\", Version=\"%2\"").arg(deviceId, QString::fromUtf8(BLOOM_VERSION));
    if (!m_accessToken.isEmpty()) {
        authHeader += QString(", Token=\"%1\"").arg(m_accessToken);
    }
    request.setRawHeader("Authorization", authHeader.toUtf8());
    
    return request;
}

void AuthenticationService::authenticate(const QString &serverUrl, const QString &username, const QString &password)
{
    m_serverUrl = normalizeUrl(serverUrl);
    
    QJsonObject body;
    body["Username"] = username;
    body["Pw"] = password;
    
    QNetworkRequest request = createRequest("/Users/AuthenticateByName");
    
    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onAuthenticateFinished(reply);
    });
}

void AuthenticationService::onAuthenticateFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    
    if (reply->error() != QNetworkReply::NoError) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errorMessage;
        
        if (statusCode == 401) {
            errorMessage = tr("Invalid username or password");
        } else if (statusCode == 0) {
            errorMessage = tr("Could not connect to server. Please check the URL and your network connection.");
        } else {
            errorMessage = tr("Authentication failed: %1").arg(reply->errorString());
        }
        
        emit loginError(errorMessage);
        return;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject obj = doc.object();
    
    m_accessToken = obj["AccessToken"].toString();
    m_userId = obj["User"].toObject()["Id"].toString();
    m_username = obj["User"].toObject()["Name"].toString();
    
    qCDebug(lcAuth) << "Authentication successful. User ID:" << m_userId << "Username:" << m_username;

    if (m_configManager) {
        m_configManager->setJellyfinSession(m_serverUrl, m_userId, QString(), m_username);
        m_activeConnection = m_configManager->getActiveConnection().value_or(ServerConnection{});
    }

    if (m_secretStore && m_activeConnection.isValid()) {
        const QString token = m_accessToken;
        const ServerConnection connection = m_activeConnection;
        const QString deviceId = m_configManager ? m_configManager->getDeviceId() : QString();
        const ConfigManager::SessionData legacySession = m_configManager
            ? m_configManager->getPendingLegacyJellyfinSession()
            : ConfigManager::SessionData{};
        const bool legacyMatchesConnection =
            ServerConnection::normalizeBaseUrl(legacySession.serverUrl) == connection.baseUrl
            && legacySession.userId == connection.accountId;
        ISecretStore *store = m_secretStore;
        QPointer<ConfigManager> configManager = m_configManager;

        QThreadPool::globalInstance()->start(
            [store, connection, token, deviceId, legacySession,
             legacyMatchesConnection, configManager]() {
                CredentialStore credentials(store);
                if (!credentials.write(connection, CredentialKind::AccessToken, token)) {
                    qCWarning(lcAuth) << "Failed to store token in keychain:" << store->lastError();
                    return;
                }
                if (credentials.read(connection, CredentialKind::AccessToken) != token) {
                    credentials.remove(connection, CredentialKind::AccessToken);
                    qCWarning(lcAuth) << "Stored token failed keychain verification";
                    return;
                }

                if (legacyMatchesConnection) {
                    const CredentialReadResult cleanup = credentials.readAccessToken(
                        connection,
                        deviceId,
                        legacySession.serverUrl,
                        legacySession.username);
                    if (!cleanup.error.isEmpty() || !cleanup.cleanupError.isEmpty()
                        || cleanup.secret != token) {
                        qCWarning(lcAuth) << "Legacy credential cleanup failed:"
                                          << (cleanup.error.isEmpty()
                                                  ? cleanup.cleanupError
                                                  : cleanup.error);
                        return;
                    }
                }

                qCDebug(lcAuth) << "Token stored in provider-neutral keychain entry";
                if (legacyMatchesConnection && configManager) {
                    QMetaObject::invokeMethod(configManager, [configManager]() {
                        if (configManager) {
                            configManager->finalizeLegacyJellyfinMigration();
                        }
                    }, Qt::QueuedConnection);
                }
            });
    }
    
    emit serverUrlChanged();
    emit userIdChanged();
    emit authenticatedChanged();
    qCCritical(lcAuth) << "=== AuthenticationService: EMITTING loginSuccess signal ===" << m_userId << m_username;
    emit loginSuccess(m_userId, m_accessToken, m_username);
}

void AuthenticationService::restoreSession(const QString &serverUrl,
                                           const QString &userId,
                                           const QString &accessToken,
                                           const QString &username)
{
    m_serverUrl = normalizeUrl(serverUrl);
    m_userId = userId;
    m_accessToken = accessToken;
    m_username = username;
    m_sessionExpiredPending = false;
    m_sessionExpiredEmitted = false;
    
    qCDebug(lcAuth) << "Restoring session for user:" << userId << "on server:" << serverUrl;
    
    // Validate the restored session
    validateAccessToken([this](bool valid) {
        if (valid) {
            qCDebug(lcAuth) << "Session restored successfully";
            emit serverUrlChanged();
            emit userIdChanged();
            emit authenticatedChanged();
            qCCritical(lcAuth) << "=== AuthenticationService: EMITTING loginSuccess from restoreSession ===" << m_userId;
            emit loginSuccess(m_userId, m_accessToken, m_username);
        } else {
            qCWarning(lcAuth) << "Stored session is invalid or expired";
            logout();
        }
    });
}

void AuthenticationService::seedSession(const QString &serverUrl,
                                        const QString &userId,
                                        const QString &accessToken,
                                        const QString &username)
{
    m_serverUrl = normalizeUrl(serverUrl);
    m_userId = userId;
    m_accessToken = accessToken;
    m_username = username;
    m_sessionExpiredPending = false;
    m_sessionExpiredEmitted = false;

    emit serverUrlChanged();
    emit userIdChanged();
    emit authenticatedChanged();
}

void AuthenticationService::logout()
{
    qCDebug(lcAuth) << "Logging out user:" << m_userId;
    
    ServerConnection connection = m_activeConnection;
    if (!connection.isValid() && m_configManager) {
        connection = m_configManager->getActiveConnection().value_or(ServerConnection{});
    }
    if (m_secretStore && connection.isValid() && m_configManager) {
        const QString deviceId = m_configManager->getDeviceId();
        ConfigManager::SessionData legacySession =
            m_configManager->getPendingLegacyJellyfinSession();
        const bool legacyMatchesConnection =
            ServerConnection::normalizeBaseUrl(legacySession.serverUrl) == connection.baseUrl
            && legacySession.userId == connection.accountId;
        if (!legacyMatchesConnection) {
            legacySession = {};
        }
        ISecretStore *store = m_secretStore;
        QThreadPool::globalInstance()->start(
            [store, connection, deviceId, legacySession]() {
                CredentialStore credentials(store);
                if (!credentials.removeAll(connection,
                                           deviceId,
                                           legacySession.serverUrl,
                                           legacySession.username)) {
                    qCWarning(lcAuth) << "Failed to remove one or more session credentials:"
                                      << store->lastError();
                } else {
                    qCDebug(lcAuth) << "Session credentials deleted from keychain";
                }
            });
    }

    m_activeConnection = {};
    m_accessToken.clear();
    m_userId.clear();
    m_username.clear();
    m_sessionExpiredPending = false;
    m_sessionExpiredEmitted = false;
    
    emit serverUrlChanged();
    emit userIdChanged();
    emit authenticatedChanged();
    emit loggedOut();
}

void AuthenticationService::checkPendingSessionExpiry()
{
    if (m_sessionExpiredPending && !m_sessionExpiredEmitted) {
        m_sessionExpiredPending = false;
        m_sessionExpiredEmitted = true;
        emit sessionExpiredAfterPlayback();
    }
}

bool AuthenticationService::checkForSessionExpiry(QNetworkReply *reply, bool deferLogout)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    
    if (statusCode == 401) {
        qCWarning(lcAuth) << "Received 401 Unauthorized - session expired";
        
        if (deferLogout) {
            // During playback, defer the logout until playback ends
            m_sessionExpiredPending = true;
        } else if (!m_sessionExpiredEmitted) {
            m_sessionExpiredEmitted = true;
            emit sessionExpired();
        }
        return true;
    }
    return false;
}

void AuthenticationService::validateAccessToken(std::function<void(bool)> callback)
{
    if (m_accessToken.isEmpty() || m_userId.isEmpty()) {
        callback(false);
        return;
    }
    
    // Make a lightweight API call to validate the token
    QNetworkRequest request = createRequest(QString("/Users/%1").arg(m_userId));
    QNetworkReply *reply = m_nam->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();
        
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool valid = (reply->error() == QNetworkReply::NoError && statusCode == 200);
        
        if (!valid) {
            qCDebug(lcAuth) << "Token validation failed. Status:" << statusCode 
                     << "Error:" << reply->errorString();
        }
        
        callback(valid);
    });
}
