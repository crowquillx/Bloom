#include "AuthenticationService.h"
#include "../security/ISecretStore.h"
#include "../utils/ConfigManager.h"
#include "config/version.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
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

    // Prepare data for the background thread
    // ConfigManager is not thread-safe, so we read the values properties here on the main thread
    auto session = configManager->getJellyfinSession();
    bool hasSecretStore = (m_secretStore != nullptr);
    ISecretStore* store = m_secretStore;
    QString deviceId = configManager->getDeviceId();

    // Use a lambda for the background task
    QFuture<RestorationResult> future = QtConcurrent::run([session, hasSecretStore, store, deviceId]() -> RestorationResult {
        RestorationResult result;
        result.success = false;
        result.migrated = false;
        result.serverUrl = session.serverUrl;
        result.userId = session.userId;
        result.username = session.username;

        if (!session.accessToken.isEmpty()) {
            // Legacy token found in config -> migrate to SecretStore
            qCInfo(lcAuth) << "Migrating legacy token to secure storage...";
            
            if (hasSecretStore && !session.username.isEmpty()) {
                // Use device-specific account key: serverUrl|username|deviceId
                QString account = QString("%1|%2|%3").arg(session.serverUrl, session.username, deviceId);
                qCDebug(lcAuth) << "Migrating token with account key:" << account;
                // Synchronous call on background thread
                if (store->setSecret("Bloom/Jellyfin", account, session.accessToken)) {
                    qCInfo(lcAuth) << "Token migrated successfully";
                    result.migrated = true;
                    result.accessToken = session.accessToken;
                    result.success = true;
                } else {
                    result.error = store->lastError();
                    qCWarning(lcAuth) << "Failed to migrate token:" << result.error;
                }
            } else {
                qCWarning(lcAuth) << "Cannot migrate token: missing username or SecretStore unavailable";
            }
        } else if (session.isValid()) {
            // No token in config, but we have userId/serverUrl/username -> try SecretStore
            if (hasSecretStore && !session.username.isEmpty()) {
                // Use device-specific account key: serverUrl|username|deviceId
                QString account = QString("%1|%2|%3").arg(session.serverUrl, session.username, deviceId);
                qCDebug(lcAuth) << "Attempting to restore session with account key:" << account;
                // Synchronous call on background thread
                QString token = store->getSecret("Bloom/Jellyfin", account);
                if (!token.isEmpty()) {
                    qCInfo(lcAuth) << "Restored session from secure storage";
                    result.accessToken = token;
                    result.success = true;
                } else {
                    qCDebug(lcAuth) << "No token found in secure storage for account:" << account;
                }
            }
        }
        
        return result;
    });

    // Use a lambda connected to the watcher to handle completion with specific context
    // We disconnect previous connections to start fresh (in case initialize is called multiple times? Should be once.)
    m_restorationWatcher.disconnect(this); // specific slots
    
    // Re-connect the "isRestoringSession = false" logic
    connect(&m_restorationWatcher, &QFutureWatcher<RestorationResult>::finished, this, [this]() {
         m_isRestoringSession = false;
         emit isRestoringSessionChanged();
    });

    // Connect the completion handler
    connect(&m_restorationWatcher, &QFutureWatcher<RestorationResult>::finished, this, [this, configManager]() {
        RestorationResult result = m_restorationWatcher.result();
        
        if (result.migrated) {
            // Clear token from config (write happens on main thread, safe)
            configManager->setJellyfinSession(result.serverUrl, result.userId, "", result.username);
        }
        
        if (result.success) {
            restoreSession(result.serverUrl, result.userId, result.accessToken);
        } else {
             if (!result.error.isEmpty()) {
                 qCWarning(lcAuth) << "Session restoration failed:" << result.error;
             }
             // If failed, we remain logged out (default state)
        }
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
    
    // Store token in SecretStore asynchronously
    if (m_secretStore && m_configManager) {
        QString deviceId = m_configManager->getDeviceId();
        QString account = QString("%1|%2|%3").arg(m_serverUrl, m_username, deviceId);
        QString token = m_accessToken;
        ISecretStore* store = m_secretStore;
        
        QtConcurrent::run([store, account, token]() {
            if (!store->setSecret("Bloom/Jellyfin", account, token)) {
                qCWarning(lcAuth) << "Failed to store token in keychain:" << store->lastError();
            } else {
                qCDebug(lcAuth) << "Token stored in keychain (async)";
            }
        });
    }
    
    emit serverUrlChanged();
    emit userIdChanged();
    emit authenticatedChanged();
    qCCritical(lcAuth) << "=== AuthenticationService: EMITTING loginSuccess signal ===" << m_userId << m_username;
    emit loginSuccess(m_userId, m_accessToken, m_username);
}

void AuthenticationService::restoreSession(const QString &serverUrl, const QString &userId, const QString &accessToken)
{
    m_serverUrl = normalizeUrl(serverUrl);
    m_userId = userId;
    m_accessToken = accessToken;
    m_username = "";  // Will be fetched if needed
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

void AuthenticationService::logout()
{
    qCDebug(lcAuth) << "Logging out user:" << m_userId;
    
    // Delete token from SecretStore asynchronously BEFORE clearing member vars (so we have username/url)
    if (m_secretStore && !m_username.isEmpty() && m_configManager) {
        QString deviceId = m_configManager->getDeviceId();
        QString account = QString("%1|%2|%3").arg(m_serverUrl, m_username, deviceId);
        ISecretStore* store = m_secretStore;
        
        QtConcurrent::run([store, account]() {
            store->deleteSecret("Bloom/Jellyfin", account);
            qCDebug(lcAuth) << "Token deleted from keychain (async)";
        });
    }

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
