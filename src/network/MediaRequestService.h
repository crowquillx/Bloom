#pragma once

#include <QHash>
#include <QPair>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

#include "providers/ICatalogProvider.h"

class AuthenticationService;
class LibraryService;
class SeerrService;

/**
 * @brief QML-facing media requests, routed to Seerr or the active Silo server.
 *
 * A configured Seerr URL and API key take priority: while one is set, every
 * call goes to SeerrService and Silo's request surface stays hidden.
 * Otherwise an authenticated Silo session whose request capability document
 * allows the viewer serves search, similar titles, requests, and the
 * Discover page through LibraryService. Both backends serve Discover rows,
 * genre browsing, and the request list; Silo adds studio and network
 * browsing, following a requested title, and watchlisting titles the
 * library lacks.
 *
 * Results use the discovery map shape both backends share (docs/seerr.md).
 * A Silo response for a connection, account, or profile that is no longer
 * active is dropped; its caller is told the work was abandoned (an empty
 * search, a failure signal) so no loading state stays stuck.
 */
class MediaRequestService : public QObject
{
    Q_OBJECT
    /// Requests can be made through some backend.
    Q_PROPERTY(bool available READ isAvailable NOTIFY availabilityChanged)
    /// "seerr", "silo", or empty.
    Q_PROPERTY(QString backend READ backend NOTIFY availabilityChanged)
    /// Display name for the backend: "Seerr", or the Silo server's name.
    Q_PROPERTY(QString backendName READ backendName NOTIFY availabilityChanged)
    /// The Discover page has a backend.
    Q_PROPERTY(bool discoverAvailable READ isDiscoverAvailable NOTIFY availabilityChanged)
    /// Silo only: titles with someone else's request can be followed.
    Q_PROPERTY(bool followSupported READ followSupported NOTIFY availabilityChanged)
    /// Silo only: titles the library lacks can go on the watchlist.
    Q_PROPERTY(bool watchlistTitlesSupported READ watchlistTitlesSupported NOTIFY availabilityChanged)
    /// Silo only: adding a missing title to the watchlist also requests it.
    Q_PROPERTY(bool watchlistRequests READ watchlistRequests NOTIFY availabilityChanged)

public:
    MediaRequestService(AuthenticationService *authService,
                        LibraryService *libraryService,
                        SeerrService *seerrService,
                        QObject *parent = nullptr);

    bool isAvailable() const { return !backend().isEmpty(); }
    QString backend() const;
    QString backendName() const;
    bool isDiscoverAvailable() const { return isAvailable(); }
    bool followSupported() const;
    bool watchlistTitlesSupported() const;
    bool watchlistRequests() const;

    /// Card label for a discovery item's `requestStatus` token; empty when
    /// nobody has requested the title.
    Q_INVOKABLE QString statusLabel(const QString &status) const;

    /// Re-reads the Silo viewer's request capability.
    Q_INVOKABLE void refreshAvailability();

    /// Titles to request matching @p searchTerm. Silo leaves out titles
    /// already in the library, which the library shelves show.
    Q_INVOKABLE void search(const QString &searchTerm);
    /// Titles similar to a movie or series ("movie" or "tv"), without titles
    /// already in the library.
    Q_INVOKABLE void getSimilar(const QString &mediaType, int tmdbId);
    /// Loads what the request dialog needs. @p data carries `backend`; Seerr
    /// adds servers, profiles, and root folders, Silo the title's seasons and
    /// whether the viewer may request it.
    Q_INVOKABLE void prepareRequest(const QString &mediaType, int tmdbId, const QString &title);
    /// Submits a request. Seerr uses the server/profile/root folder choices;
    /// Silo routes on the server and ignores them. With @p requestAllSeasons
    /// Silo requests every aired season the library lacks.
    Q_INVOKABLE void createRequest(const QString &mediaType,
                                   int tmdbId,
                                   bool requestAllSeasons,
                                   const QVariantList &seasonNumbers,
                                   int serverId,
                                   int profileId,
                                   const QString &rootFolderPath);
    /// The discovery rows with their first page: Silo's own rows, or
    /// Seerr's trending, popular, and upcoming movies and series.
    Q_INVOKABLE void loadDiscover();
    /// One more page of a discovery row.
    Q_INVOKABLE void loadDiscoverRow(const QString &key, int page);

    /// What Discover can browse: groups of {kind, title, items}, where each
    /// item is {kind, id, name, logoUrl, movieId, tvId} and an empty
    /// movieId or tvId means it has no list of that type. Silo offers
    /// genres, studios, and networks; Seerr offers genres.
    Q_INVOKABLE void loadBrowseCategories();
    /// Identifies a browse list in browseLoaded() and browseFailed().
    Q_INVOKABLE QString browseKey(const QString &kind, const QString &id,
                                  const QString &mediaType, const QString &sort) const;
    /// One page of a genre, studio, or network. @p id is the category's
    /// movieId or tvId for @p mediaType ("movie" or "tv"); @p sort is
    /// "popularity", "vote_average", or "release_date".
    Q_INVOKABLE void browse(const QString &kind, const QString &id, const QString &mediaType,
                            const QString &sort, int page);

    /// Silo: the profile's requests, newest first. Seerr: the server's most
    /// recent requests. Items are discovery maps plus `requestId`,
    /// `requestState`, `requestedSeasons`, `createdAt`, and `canCancel`.
    Q_INVOKABLE void loadMyRequests();
    /// Cancels a request. @p mediaType and @p tmdbId identify its title in
    /// requestCancelled().
    Q_INVOKABLE void cancelRequest(const QString &requestId, const QString &mediaType, int tmdbId);
    /// Silo: get notified when a title someone else requested arrives.
    Q_INVOKABLE void setFollowing(const QString &mediaType, int tmdbId, bool following);
    /// Silo: keep a title the library lacks on the profile's watchlist.
    Q_INVOKABLE void setWatchlisted(const QString &mediaType, int tmdbId, bool inWatchlist);

signals:
    void availabilityChanged();
    void searchResultsLoaded(const QString &searchTerm, const QVariantList &results);
    void similarResultsLoaded(const QString &mediaType, int tmdbId, const QVariantList &results);
    void similarResultsFailed(const QString &mediaType, int tmdbId, const QString &error);
    void requestPreparationLoaded(const QString &mediaType, int tmdbId, const QVariantMap &data);
    /// @p result carries `backend` and `requestState` (pending, approved, ...).
    void requestCreated(const QString &mediaType, int tmdbId, const QVariantMap &result);
    /// @p operation is "search", "prepareRequest", "request", "cancel",
    /// "follow", or "watchlist".
    void errorOccurred(const QString &operation, const QString &error);
    /// Rows of {key, title, items, page, nextPage, hasMore}.
    void discoverLoaded(const QVariantList &rows);
    void discoverFailed(const QString &error);
    void discoverRowLoaded(const QString &key, int page, const QVariantMap &row);
    void discoverRowFailed(const QString &key, int page, const QString &error);
    void browseCategoriesLoaded(const QVariantList &groups);
    void browseCategoriesFailed(const QString &error);
    void browseLoaded(const QString &browseKey, int page, const QVariantMap &row);
    void browseFailed(const QString &browseKey, int page, const QString &error);
    void myRequestsLoaded(const QVariantList &requests);
    void myRequestsFailed(const QString &error);
    void requestCancelled(const QString &requestId, const QString &mediaType, int tmdbId);
    void followingChanged(const QString &mediaType, int tmdbId, bool following);
    void watchlistChanged(const QString &mediaType, int tmdbId, bool inWatchlist);

private:
    using Abandon = std::function<void()>;

    QString viewerKey() const;
    void syncViewer();
    void setSiloAvailable(bool available, const QVariantMap &features);
    bool siloFeature(const char *name) const;
    void loadSiloBrowseCategories();
    void setSiloTitleState(ProviderCatalogOperation operation, const QString &errorOperation,
                           const QString &mediaType, int tmdbId, bool on);
    void fetchSiloStatus();
    bool useSilo() const;
    /// Tracks an in-flight Silo call; @p onAbandoned runs if the viewer
    /// changes first. finish() is false once the call was abandoned.
    quint64 track(Abandon onAbandoned);
    bool finish(quint64 token);
    void abandonPending();

    AuthenticationService *m_authService = nullptr;
    LibraryService *m_libraryService = nullptr;
    SeerrService *m_seerrService = nullptr;

    QString m_viewerKey;
    QString m_lastBackend;
    bool m_siloAvailable = false;
    /// The Silo viewer's request capability (SiloMediaRequestMapper::features).
    QVariantMap m_siloFeatures;
    /// Seerr Discover rows still loading their first page, by key.
    QStringList m_seerrDiscoverPending;
    QHash<QString, QVariantMap> m_seerrDiscoverRows;
    /// Seerr cancellations in flight: the title (media type, TMDB id) by
    /// request id, for requestCancelled().
    QHash<int, QPair<QString, int>> m_seerrCancels;
    quint64 m_statusToken = 0;
    quint64 m_nextToken = 0;
    QHash<quint64, Abandon> m_pending;
    /// Prepared Silo titles by "mediaType:tmdbId": the request body needs
    /// their title, year, overview, and artwork paths.
    QHash<QString, QVariantMap> m_preparedSiloMedia;
};
