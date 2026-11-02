#pragma once

#include "providers/LanServerAdvert.h"
#include "providers/ServerConnection.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <memory>

class IProviderAdapter;
class LanServerBrowser;
class ServerCandidateValidator;

/**
 * @brief Lists the media servers on the local network for the add-server
 * screen (docs/server-discovery.md).
 *
 * QML sets `active` while the screen that shows the list is visible. Each
 * activation is one bounded search: the browsers ask at 0, 1, 3, 7 and 15
 * seconds and close their sockets after 20; refresh() starts another. Every
 * advert is a candidate until its provider's public identity read at one of
 * its addresses answers the advertised server id. Entries are keyed by
 * provider, server id and advert origin (the DNS-SD host name or the
 * Jellyfin reply's sender): one origin's addresses are one entry with the
 * address that validated first, while the same id confirmed from another
 * origin is listed as its own entry, so an advert from another host cannot
 * take over a server's card. Candidates that fail are dropped. Nothing
 * blocks the UI thread, and deactivating closes the sockets and cancels
 * pending reads at once.
 *
 * `servers` entries are camelCase maps: `key`, `name`, `url` (the base URL to
 * sign in to), `address` (host and port for display), `provider` (`silo` or
 * `jellyfin`) and `saved` (a saved connection has this provider, server id
 * and host).
 */
class ServerDiscoveryService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool available READ isAvailable NOTIFY availableChanged)
    Q_PROPERTY(bool searching READ isSearching NOTIFY searchingChanged)
    Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged)

public:
    struct Timing {
        int searchWindowMs = 20000;
        QList<int> queryOffsetsMs{0, 1000, 3000, 7000, 15000};
    };

    static constexpr int MaxServers = 16;
    static constexpr int MaxCandidates = 64;
    static constexpr int MaxCandidatesPerSender = 8;
    static constexpr int MaxValidationsInFlight = 4;

    /// Takes ownership of @p browsers and @p validator. With no browser the
    /// service is inert (test mode): never searching, never listing.
    ServerDiscoveryService(const QList<LanServerBrowser *> &browsers,
                           std::unique_ptr<ServerCandidateValidator> validator,
                           QObject *parent = nullptr);
    ~ServerDiscoveryService() override;

    /// The production service: Silo over mDNS and Jellyfin's UDP broadcast,
    /// validated through @p adapters.
    static std::unique_ptr<ServerDiscoveryService> create(
        const QList<IProviderAdapter *> &adapters, QObject *parent = nullptr);

    void setTiming(const Timing &timing) { m_timing = timing; }

    using SavedConnections = std::function<QList<ServerConnection>()>;
    /// Where `saved` looks up the connections already saved.
    void setSavedConnections(SavedConnections savedConnections)
    {
        m_savedConnections = std::move(savedConnections);
    }

    bool isActive() const { return m_active; }
    void setActive(bool active);
    bool isAvailable() const { return m_available; }
    bool isSearching() const { return m_searching; }
    QVariantList servers() const { return m_servers; }

    /// Starts a fresh search while active; the list is rebuilt.
    Q_INVOKABLE void refresh();

signals:
    void activeChanged();
    void availableChanged();
    void searchingChanged();
    void serversChanged();

private:
    struct Candidate {
        QString serverKey; // provider and server id
        QString entryKey;  // serverKey and advert origin
        LanServerAdvert advert;
        QUrl url;
    };

    QList<LanServerBrowser *> m_browsers;
    QList<QPointer<LanServerBrowser>> m_running;
    std::unique_ptr<ServerCandidateValidator> m_validator;
    Timing m_timing;
    QTimer m_queryTimer;
    QTimer m_windowTimer;
    bool m_active = false;
    bool m_available = false;
    bool m_searching = false;
    quint64 m_generation = 0;
    int m_queryIndex = 0;
    int m_inFlight = 0;
    QList<Candidate> m_queue;
    // Server key + URL already queued this search: an address is checked
    // once per server, whichever origin advertised it.
    QSet<QString> m_tried;
    QHash<QString, int> m_queuedPerSender;
    QSet<QString> m_listed; // entry keys already listed
    QVariantList m_servers;
    SavedConnections m_savedConnections;

    void beginSearch();
    void endSearch();
    void stopBrowsers();
    void sendQueries();
    void handleAdvert(const LanServerAdvert &advert);
    void pump();
    void finishValidation(const Candidate &candidate,
                          const std::optional<ProviderServerIdentity> &identity);
    bool isSaved(const Candidate &candidate) const;
    void updateSearching();
    void setAvailable(bool available);
};
