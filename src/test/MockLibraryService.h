#pragma once

#include "network/LibraryService.h"

/**
 * @brief Mock implementation of LibraryService for visual regression testing.
 * 
 * This service returns deterministic data from a test fixture JSON file
 * instead of making network requests to a Jellyfin server.
 * 
 * All methods emit their signals immediately with fixture data, enabling
 * consistent and reproducible UI states for screenshot comparison.
 */
class MockLibraryService : public LibraryService
{
    Q_OBJECT

public:
    explicit MockLibraryService(QObject *parent = nullptr);
    
    /**
     * @brief Load fixture data from the given JSON object.
     * @param fixture The parsed fixture JSON containing test data.
     */
    void loadFixture(const QJsonObject &fixture);
    
    // Library views - returns data from fixture["libraries"]
    Q_INVOKABLE void getViews() override;
    
    // Items with pagination and filtering - returns data from fixture["movies"] or fixture["series"]
    Q_INVOKABLE void getItems(const QString &parentId, int startIndex = 0, int limit = 0,
                               const QStringList &genres = QStringList(),
                               const QStringList &networks = QStringList(),
                               const QString &sortBy = QString(),
                               const QString &sortOrder = QString(),
                               bool includeHeavyFields = true,
                               bool useCacheValidation = false) override;
    
    // Next up episodes - returns data from fixture["nextUp"]
    Q_INVOKABLE void getNextUp() override;
    
    // Latest media for a library - returns data from fixture["latestItems"]
    Q_INVOKABLE void getLatestMedia(const QString &parentId) override;
    
    // Generic Item Details - searches all fixture items
    Q_INVOKABLE void getItem(const QString &itemId) override;

    // Series details and episodes
    Q_INVOKABLE void getSeriesDetails(const QString &seriesId) override;
    Q_INVOKABLE void getNextUnplayedEpisode(const QString &seriesId) override;
    Q_INVOKABLE void markSeriesWatched(const QString &seriesId) override;
    Q_INVOKABLE void markSeriesUnwatched(const QString &seriesId) override;
    Q_INVOKABLE void markItemPlayed(const QString &itemId) override;
    Q_INVOKABLE void markItemUnplayed(const QString &itemId) override;
    Q_INVOKABLE void markItemFavorite(const QString &itemId) override;
    Q_INVOKABLE void markItemUnfavorite(const QString &itemId) override;
    Q_INVOKABLE void toggleFavorite(const QString &itemId, bool isFavorite) override;
    Q_INVOKABLE void getThemeSongs(const QString &seriesId) override;
    
    // Search - searches across movies and series
    Q_INVOKABLE void search(const QString &searchTerm, int limit = 50) override;
    Q_INVOKABLE void getRandomItems(int limit = 20) override;
    
    // URL helpers - returns placeholder URLs for test images
    Q_INVOKABLE QString getStreamUrl(const QString &itemId) override;
    Q_INVOKABLE QString getStreamUrlWithTracks(const QString &itemId, const QString &mediaSourceId = QString(),
                                                int audioStreamIndex = -1, int subtitleStreamIndex = -1) override;
    Q_INVOKABLE QString getImageUrl(const QString &itemId, const QString &imageType) override;
    Q_INVOKABLE QString getImageUrlWithWidth(const QString &itemId, const QString &imageType, int width) override;
    Q_INVOKABLE QString getCachedImageUrl(const QString &itemId, const QString &imageType) override;
    Q_INVOKABLE QString getCachedImageUrlWithWidth(const QString &itemId, const QString &imageType, int width) override;

private:
    QJsonObject m_fixture;
    QJsonObject m_movies;
    QJsonObject m_series;
    QJsonObject m_seasons;
    QJsonObject m_episodes;
    QJsonObject m_nextUp;
    QJsonObject m_latestItems;
    QJsonObject m_libraries;
    
    QJsonObject findItemById(const QString &itemId) const;
    QJsonArray findEpisodesBySeriesId(const QString &seriesId) const;
    QJsonArray findSeasonsBySeriesId(const QString &seriesId) const;
};
