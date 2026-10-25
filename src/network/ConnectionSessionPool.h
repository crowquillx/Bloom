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
 * @brief Tokens for one saved connection held by ConnectionSessionPool.
 */
struct ConnectionCredentials {
    QString accessToken;
    QString refreshToken;
    QString profileToken;

    [[nodiscard]] bool isValid() const { return !accessToken.isEmpty(); }
};

/**
 * @brief Holds credentials for the connections of a merged Bloom profile.
 *
 * AuthenticationService owns the one live session. Merged reads also need the
 * other members' tokens; this pool loads them from CredentialStore off the UI
 * thread and keeps them in memory. AuthenticationService renews a member's
 * expired access token and hands the result back through updateCredentials();
 * a rejected renewal marks the member so reads report that it needs a sign-in.
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
    /// Replaces a tracked connection's tokens with newer ones (a renewal, or
    /// the live session's current tokens). Supersedes a load in flight.
    void updateCredentials(const QString &connectionId,
                           const ConnectionCredentials &credentials);
    /// The server rejected renewal: drop the in-memory tokens (storage is
    /// untouched) until a sign-in or reload supplies new ones.
    void markRejected(const QString &connectionId);

    [[nodiscard]] bool isTracked(const QString &connectionId) const;
    [[nodiscard]] bool isLoading(const QString &connectionId) const;
    [[nodiscard]] bool isRejected(const QString &connectionId) const;
    /// Tokens renewed in memory since they were loaded; newer than storage
    /// until the background write lands.
    [[nodiscard]] std::optional<ConnectionCredentials> renewedCredentials(
        const QString &connectionId) const;
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
        bool renewed = false;
        bool rejected = false;
        quint64 loadGeneration = 0;
    };

    void startLoad(const QString &connectionId);

    std::shared_ptr<ISecretStore> m_secretStore;
    Loader m_loader;
    QHash<QString, Entry> m_entries;
    quint64 m_nextLoadGeneration = 0;
};
