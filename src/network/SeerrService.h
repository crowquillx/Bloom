#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

class AuthenticationService;
class ConfigManager;

class SeerrService : public QObject
{
    Q_OBJECT

public:
    explicit SeerrService(AuthenticationService *authService, ConfigManager *configManager, QObject *parent = nullptr);

    Q_INVOKABLE bool isConfigured() const;
    Q_INVOKABLE void validateConnection();
    Q_INVOKABLE void search(const QString &searchTerm, int page = 1);
    Q_INVOKABLE void getSimilar(const QString &mediaType, int tmdbId, int page = 1);
    Q_INVOKABLE void prepareRequest(const QString &mediaType, int tmdbId, const QString &title = QString());
    Q_INVOKABLE void createRequest(const QString &mediaType,
                                   int tmdbId,
                                   bool requestAllSeasons,
                                   const QVariantList &seasonNumbers,
                                   int serverId,
                                   int profileId,
                                   const QString &rootFolderPath);

signals:
    void connectionValidated(bool ok, const QString &message);
    void searchResultsLoaded(const QString &searchTerm, const QJsonArray &results);
    void similarResultsLoaded(const QString &mediaType, int tmdbId, const QJsonArray &results);
    void requestPreparationLoaded(const QString &mediaType, int tmdbId, const QJsonObject &data);
    void requestCreated(const QString &mediaType, int tmdbId, const QJsonObject &requestData);
    void errorOccurred(const QString &endpoint, const QString &error);

private:
    AuthenticationService *m_authService;
    ConfigManager *m_configManager;

    QString normalizedBaseUrl() const;
    QNetworkRequest createRequest(const QString &endpoint, const QUrlQuery &query = QUrlQuery()) const;
    bool ensureConfigured(const QString &endpoint);

    QJsonObject mapSearchResultItem(const QJsonObject &item) const;
    QJsonObject pickDefaultServer(const QJsonArray &servers) const;
};
