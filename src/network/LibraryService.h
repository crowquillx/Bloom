#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QNetworkReply>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QHash>
#include "Types.h"  // Shared data structs and error helpers

class AuthenticationService;

/**
 * @brief Handles library browsing, item fetching, and metadata retrieval.
 * 
 * This service manages:
 * - Library views and item listings
 * - Series/season/episode details
 * - Search functionality
 * - Image and stream URL generation
 * 
 * Part of the service decomposition formerly handled by the legacy client (Roadmap 1.1).
 */
class LibraryService : public QObject
{
    Q_OBJECT

public:
    explicit LibraryService(AuthenticationService *authService, QObject *parent = nullptr);
    virtual ~LibraryService() = default;
    
    // Library views
    Q_INVOKABLE virtual void getViews();
    
    // Items with pagination and filtering
    Q_INVOKABLE virtual void getItems(const QString &parentId, int startIndex = 0, int limit = 0,
                               const QStringList &genres = QStringList(),
                               const QStringList &networks = QStringList(),
                               const QString &sortBy = QString(),
                               const QString &sortOrder = QString(),
                               bool includeHeavyFields = true,
                               bool useCacheValidation = false);
    
    // Next up episodes
    Q_INVOKABLE virtual void getNextUp();
    
    // Latest media for a library
    Q_INVOKABLE virtual void getLatestMedia(const QString &parentId);
    
    // Generic Item Details
    Q_INVOKABLE virtual void getItem(const QString &itemId);

    // Series details and episodes
    Q_INVOKABLE virtual void getSeriesDetails(const QString &seriesId);
    Q_INVOKABLE virtual void getNextUnplayedEpisode(const QString &seriesId);
    Q_INVOKABLE virtual void markSeriesWatched(const QString &seriesId);
    Q_INVOKABLE virtual void markSeriesUnwatched(const QString &seriesId);
    Q_INVOKABLE virtual void markItemPlayed(const QString &itemId);
    Q_INVOKABLE virtual void markItemUnplayed(const QString &itemId);
    Q_INVOKABLE virtual void markItemFavorite(const QString &itemId);
    Q_INVOKABLE virtual void markItemUnfavorite(const QString &itemId);
    Q_INVOKABLE virtual void toggleFavorite(const QString &itemId, bool isFavorite);
    Q_INVOKABLE virtual void getThemeSongs(const QString &seriesId);
    
    // Search
    Q_INVOKABLE virtual void search(const QString &searchTerm, int limit = 50);
    Q_INVOKABLE virtual void getRandomItems(int limit = 20);
    
    // URL helpers
    Q_INVOKABLE virtual QString getStreamUrl(const QString &itemId);
    Q_INVOKABLE virtual QString getStreamUrlWithTracks(const QString &itemId, const QString &mediaSourceId = QString(),
                                                int audioStreamIndex = -1, int subtitleStreamIndex = -1);
    Q_INVOKABLE virtual QString getImageUrl(const QString &itemId, const QString &imageType);
    Q_INVOKABLE virtual QString getImageUrlWithWidth(const QString &itemId, const QString &imageType, int width);
    Q_INVOKABLE virtual QString getCachedImageUrl(const QString &itemId, const QString &imageType);
    Q_INVOKABLE virtual QString getCachedImageUrlWithWidth(const QString &itemId, const QString &imageType, int width);

signals:
    void viewsLoaded(const QJsonArray &views);
    void itemsLoaded(const QString &parentId, const QJsonArray &items);
    void itemsLoadedWithTotal(const QString &parentId, const QJsonArray &items, int totalRecordCount);
    void itemsNotModified(const QString &parentId);
    
    // Generic Item Signals
    void itemLoaded(const QString &itemId, const QJsonObject &data);
    void itemNotModified(const QString &itemId);
    void itemUserDataChanged(const QString &itemId, const QJsonObject &userData);

    void nextUpLoaded(const QJsonArray &items);
    void latestMediaLoaded(const QString &parentId, const QJsonArray &items);
    void seriesDetailsLoaded(const QString &seriesId, const QJsonObject &seriesData);
    void seriesDetailsNotModified(const QString &seriesId);
    void nextUnplayedEpisodeLoaded(const QString &seriesId, const QJsonObject &episodeData);
    void seriesWatchedStatusChanged(const QString &seriesId);
    void itemPlayedStatusChanged(const QString &itemId, bool isPlayed);
    void favoriteStatusChanged(const QString &itemId, bool isFavorite);
    void themeSongsLoaded(const QString &seriesId, const QStringList &urls);
    void searchResultsLoaded(const QString &searchTerm, const QJsonArray &movies, const QJsonArray &series);
    void randomItemsLoaded(const QJsonArray &items);
    
    // Error signals
    void errorOccurred(const QString &endpoint, const QString &error);
    void networkError(const NetworkError &error);
    
    // Progress signals for long-running operations
    void parsingStarted(const QString &operation);
    void parsingProgress(const QString &operation, int processed, int total);
    void parsingFinished(const QString &operation);

private:
    AuthenticationService *m_authService;
    RetryPolicy m_retryPolicy;
    
    // Retry mechanism types
    using ResponseHandler = std::function<void(QNetworkReply*)>;
    using RequestFactory = std::function<QNetworkReply*()>;
    
    void sendRequestWithRetry(const QString &endpoint,
                               RequestFactory requestFactory,
                               ResponseHandler responseHandler,
                               int attemptNumber = 0);
    
    void handleReplyWithRetry(QNetworkReply *reply,
                               const QString &endpoint,
                               RequestFactory requestFactory,
                               ResponseHandler responseHandler,
                               int attemptNumber);
    
    void emitError(const NetworkError &error);

    // Cache validation state (per endpoint/parent)
    QHash<QString, QString> m_etags;
    QHash<QString, QString> m_lastModified;
};
