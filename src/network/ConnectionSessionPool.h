#pragma once

#include "providers/ServerConnection.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <optional>

class ISecretStore;

/**
 * @brief Read-only credentials for one saved connection that is not the
 * active AuthenticationService session.
 */
struct ConnectionCredentials {
    QString accessToken;
    QString profileToken;

    [[nodiscard]] bool isValid() const { return !accessToken.isEmpty(); }
};

/**
 * @brief Holds credentials for the connections of a merged Bloom profile.
 *
 * AuthenticationService owns one live, refreshable session. Merged reads also
 * need the other members' tokens; this pool loads them from CredentialStore off
 * the UI thread and keeps them in memory only. It never writes or refreshes
 * credentials: a rejected token is dropped and reloaded once from the store,
 * then the member needs a full sign-in through the active session.
 */
class ConnectionSessionPool : public QObject
{
    Q_OBJECT

public:
    /// Runs on a worker thread. Tests replace it to avoid a platform keyring.
    using Loader = std::function<ConnectionCredentials(const ServerConnection &)>;

    explicit ConnectionSessionPool(std::shared_ptr<ISecretStore> secretStore,
                                   QObject *parent = nullptr);

    void setLoader(Loader loader);

    /// Tracks exactly these connections; loads new ones and forgets the rest.
    void setConnections(const QList<ServerConnection> &connections);
    void clear();

    /// Drops cached tokens for a connection and loads them again from storage.
    void reload(const QString &connectionId);
    /// Forgets a connection's tokens without reloading (sign-out, removal).
    void forget(const QString &connectionId);

    [[nodiscard]] bool isTracked(const QString &connectionId) const;
    [[nodiscard]] bool isLoading(const QString &connectionId) const;
    [[nodiscard]] std::optional<ServerConnection> connection(const QString &connectionId) const;
    [[nodiscard]] std::optional<ConnectionCredentials> credentials(
        const QString &connectionId) const;

signals:
    /// Emitted once per load; hasCredentials is false when storage had none.
    void connectionReady(const QString &connectionId, bool hasCredentials);

private:
    struct Entry {
        ServerConnection connection;
        ConnectionCredentials credentials;
        bool loading = false;
        quint64 loadGeneration = 0;
    };

    void startLoad(const QString &connectionId);

    std::shared_ptr<ISecretStore> m_secretStore;
    Loader m_loader;
    QHash<QString, Entry> m_entries;
    quint64 m_nextLoadGeneration = 0;
};
