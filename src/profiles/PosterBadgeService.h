#pragma once

#include "models/PosterBadges.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class AuthenticationService;
class BloomProfileRepository;
class ConfigManager;
class MergedCatalogService;

/**
 * @brief Poster badges drawn from each connection's server-side preferences.
 *
 * Silo profiles choose which badges their poster cards show and where. This
 * service reads those preferences for every Silo connection of the active
 * space, keeps one `Bloom::PosterBadgeLayout` per connection, and resolves an
 * item's badges against the layout of the connection that owns it. QML only
 * draws the result. The `serverPosterBadgesEnabled` setting turns the feature
 * off without forgetting layouts.
 */
class PosterBadgeService : public QObject
{
    Q_OBJECT
    /// Bumped whenever any item's badges may have changed; bindings that call
    /// badgesFor() read it to re-evaluate.
    Q_PROPERTY(int revision READ revision NOTIFY badgesChanged)
    /// Whether any saved connection has server-side badge preferences, which
    /// is when Settings offers the toggle.
    Q_PROPERTY(bool hasBadgeConnections READ hasBadgeConnections NOTIFY hasBadgeConnectionsChanged)

public:
    /// Preferences older than this are read again on refresh().
    static constexpr qint64 kRefreshAfterMs = 60 * 1000;

    PosterBadgeService(AuthenticationService *authService,
                       BloomProfileRepository *repository,
                       ConfigManager *configManager,
                       MergedCatalogService *mergedCatalog,
                       QObject *parent = nullptr);

    int revision() const { return m_revision; }
    bool hasBadgeConnections() const { return m_hasBadgeConnections; }

    /// The badges a canonical media item shows: maps with `corner`, `kind`,
    /// `text`, `icon`, and for ratings `ratingSource` and `score`.
    Q_INVOKABLE QVariantList badgesFor(const QVariantMap &item) const;
    /// Reads preferences again for connections whose copy is stale.
    Q_INVOKABLE void refresh();

    /// The layout held for a connection; disabled when none is held.
    Bloom::PosterBadgeLayout layoutFor(const QString &connectionId) const;

signals:
    void badgesChanged();
    void hasBadgeConnectionsChanged();

private:
    struct Fetch {
        quint64 generation = 0;
        // The merged-read epoch it started in; a later epoch dropped it.
        quint64 epoch = 0;
        int pending = 0;
        bool settingsFailed = false;
        QJsonObject settings;
        QJsonObject defaults;
    };

    void reconcile();
    bool inFlight(const QString &connectionId) const;
    void fetch(const QString &connectionId);
    void finishFetch(const QString &connectionId, quint64 generation);
    QStringList badgeConnections() const;
    void updateHasBadgeConnections();
    bool featureEnabled() const;
    void bumpRevision();

    AuthenticationService *m_authService = nullptr;
    BloomProfileRepository *m_repository = nullptr;
    ConfigManager *m_configManager = nullptr;
    MergedCatalogService *m_mergedCatalog = nullptr;
    QHash<QString, Bloom::PosterBadgeLayout> m_layouts;
    QHash<QString, qint64> m_attemptedAtMs;
    QHash<QString, Fetch> m_fetches;
    quint64 m_generation = 0;
    int m_revision = 0;
    bool m_hasBadgeConnections = false;
};
