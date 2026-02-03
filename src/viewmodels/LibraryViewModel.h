#pragma once

#include "BaseViewModel.h"
#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QVariantList>
#include <memory>
#include "../utils/LibraryCacheStore.h"

class LibraryService;
class ConfigManager;

/**
 * @brief Cache entry for library data with TTL support
 */
struct LibraryCacheEntry {
    QJsonArray items;
    int totalRecordCount = 0;
    qint64 timestamp = 0;  // ms since epoch
    
    bool isValid(qint64 ttlMs = 60000) const {  // Default 60 second TTL
        return timestamp > 0 && 
               (QDateTime::currentMSecsSinceEpoch() - timestamp) < ttlMs;
    }
    
    // Check if cache has any data (regardless of TTL - for SWR pattern)
    bool hasData() const {
        return timestamp > 0 && !items.isEmpty();
    }
};

/**
 * @brief ViewModel for library item display in LibraryScreen.
 *
 * This class provides a QAbstractListModel interface for efficient list rendering
 * of Jellyfin library items. It handles data fetching via LibraryService and
 * exposes loading/error states to QML.
 *
 * ## Usage in QML
 * ```qml
 * GridView {
 *     model: LibraryViewModel
 *     delegate: Item {
 *         required property string name
 *         required property string imageUrl
 *         required property string itemId
 *         required property string itemType
 *     }
 * }
 * ```
 *
 * ## Custom Roles
 * - NameRole: Display name of the item
 * - ImageUrlRole: URL for the item's primary image
 * - IdRole: Jellyfin item ID
 * - TypeRole: Item type (Series, Movie, Episode, Season, etc.)
 * - ModelDataRole: Full JSON object for advanced usage
 */
class LibraryViewModel : public BaseViewModel
{
    Q_OBJECT

    Q_PROPERTY(bool isLoadingMore READ isLoadingMore NOTIFY isLoadingMoreChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY canLoadMoreChanged)
    Q_PROPERTY(int totalRecordCount READ totalRecordCount NOTIFY totalRecordCountChanged)
    Q_PROPERTY(QString currentParentId READ currentParentId NOTIFY currentParentIdChanged)
    Q_PROPERTY(QVariantList views READ views NOTIFY viewsChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        ImageUrlRole,
        IdRole,
        TypeRole,
        ModelDataRole,
        ProductionYearRole,
        IndexNumberRole,
        ParentIndexNumberRole,
        OverviewRole
    };
    Q_ENUM(Roles)

    explicit LibraryViewModel(QObject *parent = nullptr);
    ~LibraryViewModel() override = default;

    // QAbstractListModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Property accessors
    bool isLoadingMore() const { return m_isLoadingMore; }
    bool canLoadMore() const { return m_items.size() < m_totalRecordCount && !isLoading() && !m_isLoadingMore; }
    int totalRecordCount() const { return m_totalRecordCount; }
    QString currentParentId() const { return m_currentParentId; }
    QVariantList views() const { return m_views; }

    /**
     * @brief Load items for a specific parent folder.
     * @param parentId The Jellyfin parent ID (library, series, season, etc.)
     * @param startIndex Starting index for pagination (default 0)
     * @param limit Maximum items to fetch (default 0 = no limit)
     */
    Q_INVOKABLE void loadLibrary(const QString &parentId, int startIndex = 0, int limit = 0);

    /**
     * @brief Load user's library views (top-level libraries).
     */
    Q_INVOKABLE void loadViews();

    /**
     * @brief Refresh the current view by reloading data.
     */
    Q_INVOKABLE void refresh();

    /**
     * @brief Reload hook for QML retry flows (aliases refresh()).
     */
    Q_INVOKABLE void reload() override;

    /**
     * @brief Clear all items and reset state.
     */
    Q_INVOKABLE void clear();

    /**
     * @brief Load more items (incremental loading / infinite scroll).
     * Appends items to the existing model instead of replacing.
     * @param limit Maximum items to fetch (default 50)
     */
    Q_INVOKABLE void loadMore(int limit = 50);

    /**
     * @brief Get item at specified index.
     * @param index The model index
     * @return QVariantMap with item data, or empty map if invalid
     */
    Q_INVOKABLE QVariantMap getItem(int index) const;

    /**
     * @brief Build image URL for an item based on its type and available images.
     * @param item The item JSON object
     * @return The best available image URL
     */
    Q_INVOKABLE QString buildImageUrl(const QVariantMap &item) const;

signals:
    void isLoadingMoreChanged();
    void canLoadMoreChanged();
    void totalRecordCountChanged();
    void currentParentIdChanged();
    void loadComplete();
    void loadMoreComplete();
    void loadError(const QString &error);
    void viewsChanged();

private slots:
    void onViewsLoaded(const QJsonArray &views);
    void onItemsLoaded(const QString &parentId, const QJsonArray &items);
    void onItemsLoadedWithTotal(const QString &parentId, const QJsonArray &items, int totalRecordCount);
    void onErrorOccurred(const QString &endpoint, const QString &error);

private:
    void setIsLoadingMore(bool loading);
    void setTotalRecordCount(int count);
    void setItems(const QJsonArray &items);
    void appendItems(const QJsonArray &items);
    QString getImageUrl(const QJsonObject &item) const;
    bool isEmptyFolder(const QJsonObject &item) const;

    LibraryService *m_libraryService = nullptr;
    ConfigManager *m_configManager = nullptr;
    QVector<QJsonObject> m_items;
    QString m_currentParentId;
    int m_lastStartIndex = 0;
    int m_lastLimit = 0;
    bool m_lastIncludeHeavyFields = true;
    bool m_isLoadingMore = false;
    bool m_isBackgroundRefresh = false;  // SWR: true when revalidating in background
    int m_totalRecordCount = 0;
    bool m_loadingViews = false;
    QVariantList m_views;  // Stores user's library views for Settings access
    std::unique_ptr<class LibraryCacheStore> m_cacheStore;
    
    // In-memory cache for library data to speed up back navigation
    // Key: parentId, Value: cached items and metadata
    static QHash<QString, LibraryCacheEntry> s_libraryCache;
    static constexpr qint64 kCacheTtlMs = 120000;  // 2 minute TTL for library cache
    static constexpr qint64 kDiskCacheTtlMs = 600000; // 10 minute TTL for disk cache
    
    // Check if we have valid cached data for a parent ID (respects TTL)
    bool hasCachedData(const QString &parentId) const;
    // Check if we have any cached data (ignores TTL - for SWR)
    bool hasAnyCachedData(const QString &parentId) const;
    // Get cached data (returns empty entry if not cached)
    LibraryCacheEntry getCachedData(const QString &parentId) const;
    // Update cache with new data
    void updateCache(const QString &parentId, const QJsonArray &items, int totalRecordCount);
    // Clear specific cache entry
    void clearCacheEntry(const QString &parentId);
    // Cache helpers
    QString cacheDir() const;
    QString cacheDbPath() const;
    // SWR: Check if fresh data differs from cached data
    bool hasDataChanged(const QJsonArray &newItems, int newTotal, const LibraryCacheEntry &cached) const;
    // SWR: Update model with new items, using efficient diff when possible
    void updateItemsFromBackground(const QJsonArray &items);

    // Timing
    mutable QElapsedTimer m_loadTimer;
    mutable QElapsedTimer m_loadMoreTimer;
    
public:
    /**
     * @brief Clear all cached library data.
     * Call this when user logs out or server changes.
     */
    Q_INVOKABLE static void clearAllCache();
    
    /**
     * @brief Invalidate cache for a specific parent ID.
     * Call this when data is known to have changed (e.g., after marking watched).
     */
    Q_INVOKABLE void invalidateCache(const QString &parentId);
};
