#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

class AuthenticationService;
class ConfigManager;

/**
 * @brief Service for interacting with a Jellyseerr/Overseerr instance.
 *
 * SeerrService provides QML-invokable methods to search for media, fetch
 * similar titles, and submit download requests through a configured Seerr
 * server.  All operations are asynchronous; results are delivered via signals.
 *
 * Requires a base URL and API key stored in ConfigManager.  Call
 * isConfigured() to check readiness before invoking network methods.
 */
class SeerrService : public QObject
{
    Q_OBJECT

public:
    /** @brief Constructs the service with the given auth and config dependencies. */
    explicit SeerrService(AuthenticationService *authService, ConfigManager *configManager, QObject *parent = nullptr);

    /**
     * @brief Returns true if both a base URL and API key are set in ConfigManager.
     *
     * Does not perform a network check; use validateConnection() for that.
     */
    Q_INVOKABLE bool isConfigured() const;

    /**
     * @brief Performs a live connectivity check against the Seerr /auth/me endpoint.
     *
     * Emits connectionValidated(true, ...) on success or connectionValidated(false, ...)
     * on failure.
     */
    Q_INVOKABLE void validateConnection();

    /**
     * @brief Searches Seerr for movies and TV shows matching @p searchTerm.
     *
     * Results are normalised to the same map structure used by LibraryService so
     * that search-result delegates can treat both sources uniformly.
     * Emits searchResultsLoaded() on completion or errorOccurred() on failure.
     *
     * @param searchTerm The query string; empty strings are short-circuited.
     * @param page       1-based page number (default 1).
     */
    Q_INVOKABLE void search(const QString &searchTerm, int page = 1);

    /**
     * @brief Fetches titles similar to the given media item from Seerr.
     *
     * Uses the Seerr /movie/{id}/similar or /tv/{id}/similar endpoint.
     * When individual result objects lack a mediaType field the request's
     * @p mediaType is used as a fallback so no results are silently dropped.
     * Emits similarResultsLoaded() on completion or errorOccurred() on failure.
     *
     * @param mediaType  "movie" or "tv" (case-insensitive).
     * @param tmdbId     The TMDB identifier for the reference title.
     * @param page       1-based page number (default 1).
     */
    Q_INVOKABLE void getSimilar(const QString &mediaType, int tmdbId, int page = 1);

    /**
     * @brief Loads the server/profile/root-folder options needed to build a request dialog.
     *
     * Fetches service configuration from Radarr or Sonarr via Seerr, then for TV
     * titles also fetches season count.  Emits requestPreparationLoaded() with a
     * payload containing servers, profiles, rootFolders, season count, and pre-selected
     * defaults, or errorOccurred() on failure.
     *
     * @param mediaType  "movie" or "tv".
     * @param tmdbId     The TMDB identifier.
     * @param title      Optional display title included in the payload for UI use.
     */
    Q_INVOKABLE void prepareRequest(const QString &mediaType, int tmdbId, const QString &title = QString());

    /**
     * @brief Submits a media download request to Seerr.
     *
     * Builds the request payload and POSTs it to the Seerr /request endpoint.
     * Emits requestCreated() on success or errorOccurred() on failure.
     *
     * @param mediaType        "movie" or "tv".
     * @param tmdbId           The TMDB identifier.
     * @param requestAllSeasons If true, requests all seasons regardless of @p seasonNumbers.
     * @param seasonNumbers    List of individual season numbers to request (used when
     *                         @p requestAllSeasons is false and mediaType is "tv").
     * @param serverId         Radarr/Sonarr server ID (-1 to omit).
     * @param profileId        Quality profile ID (-1 to omit).
     * @param rootFolderPath   Root folder path (empty string to omit).
     */
    Q_INVOKABLE void createRequest(const QString &mediaType,
                                   int tmdbId,
                                   bool requestAllSeasons,
                                   const QVariantList &seasonNumbers,
                                   int serverId,
                                   int profileId,
                                   const QString &rootFolderPath);

signals:
    /** @brief Emitted after validateConnection() completes. @p ok is false on any error. */
    void connectionValidated(bool ok, const QString &message);

    /** @brief Emitted when a search() call completes with normalised result items. */
    void searchResultsLoaded(const QString &searchTerm, const QJsonArray &results);

    /** @brief Emitted when a getSimilar() call completes with normalised result items. */
    void similarResultsLoaded(const QString &mediaType, int tmdbId, const QJsonArray &results);

    /** @brief Emitted when prepareRequest() completes; @p data contains servers/profiles/rootFolders. */
    void requestPreparationLoaded(const QString &mediaType, int tmdbId, const QJsonObject &data);

    /** @brief Emitted when createRequest() succeeds; @p requestData is the Seerr response object. */
    void requestCreated(const QString &mediaType, int tmdbId, const QJsonObject &requestData);

    /** @brief Emitted on any network or parsing error; @p endpoint identifies the failing call. */
    void errorOccurred(const QString &endpoint, const QString &error);

private:
    AuthenticationService *m_authService;
    ConfigManager *m_configManager;

    /** @brief Returns the configured base URL with any trailing slashes stripped. */
    QString normalizedBaseUrl() const;

    /** @brief Builds a QNetworkRequest for the given API @p endpoint with optional query params. */
    QNetworkRequest createRequest(const QString &endpoint, const QUrlQuery &query = QUrlQuery()) const;

    /**
     * @brief Checks that both the network manager and Seerr credentials are available.
     *
     * Emits errorOccurred() and returns false if either check fails.
     */
    bool ensureConfigured(const QString &endpoint);

    /**
     * @brief Normalises a raw Seerr search/similar result object into the shared item map format.
     *
     * Sets Source="Seerr", maps TMDB poster paths to full image URLs, synthesises
     * a stable Id of the form "seerr:{mediaType}:{tmdbId}", and copies SeerrMediaInfo
     * when present.
     */
    QJsonObject mapSearchResultItem(const QJsonObject &item) const;

    /**
     * @brief Returns the default server from @p servers, falling back to the first entry.
     *
     * The default server is identified by isDefault==true.  Returns an empty object
     * if @p servers is empty.
     */
    QJsonObject pickDefaultServer(const QJsonArray &servers) const;
};
