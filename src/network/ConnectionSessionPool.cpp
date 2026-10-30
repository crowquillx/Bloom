#include "ConnectionSessionPool.h"

#include "security/CredentialStore.h"

#include <QFutureWatcher>
#include <QPointer>
#include <QSet>
#include <QtConcurrent>

ConnectionSessionPool::ConnectionSessionPool(std::shared_ptr<ISecretStore> secretStore,
                                             QObject *parent)
    : QObject(parent)
    , m_secretStore(std::move(secretStore))
{
    // Shared copy: a load still running at shutdown keeps the store alive.
    m_loader = [store = m_secretStore](const ServerConnection &connection) {
        ConnectionCredentials credentials;
        if (!store) {
            return credentials;
        }
        CredentialStore credentialStore(store.get());
        credentials.accessToken =
            credentialStore.read(connection, CredentialKind::AccessToken);
        credentials.refreshToken =
            credentialStore.read(connection, CredentialKind::RefreshToken);
        if (!connection.profileId.isEmpty()) {
            credentials.profileToken =
                credentialStore.read(connection, CredentialKind::ProfileToken);
        }
        return credentials;
    };
}

void ConnectionSessionPool::setConnections(const QList<ServerConnection> &connections)
{
    QSet<QString> wanted;
    for (const ServerConnection &connection : connections) {
        if (!connection.isValid()) {
            continue;
        }
        wanted.insert(connection.connectionId);
        auto it = m_entries.find(connection.connectionId);
        if (it == m_entries.end()) {
            Entry entry;
            entry.connection = connection;
            m_entries.insert(connection.connectionId, entry);
            startLoad(connection.connectionId);
            continue;
        }
        // Account or provider-profile identity changed: the cached tokens
        // belong to the old identity.
        const bool identityChanged = it->connection.baseUrl != connection.baseUrl
            || it->connection.accountId != connection.accountId
            || it->connection.profileId != connection.profileId
            || it->connection.credentialReference != connection.credentialReference;
        it->connection = connection;
        if (identityChanged) {
            it->credentials = {};
            it->renewed = false;
            it->rejected = false;
            startLoad(connection.connectionId);
        }
    }

    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (!wanted.contains(it.key())) {
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
}

void ConnectionSessionPool::clear()
{
    m_entries.clear();
}

void ConnectionSessionPool::reload(const QString &connectionId)
{
    auto it = m_entries.find(connectionId);
    if (it == m_entries.end()) {
        return;
    }
    it->credentials = {};
    it->renewed = false;
    it->rejected = false;
    startLoad(connectionId);
}

void ConnectionSessionPool::forget(const QString &connectionId)
{
    m_entries.remove(connectionId);
}

void ConnectionSessionPool::updateCredentials(const QString &connectionId,
                                              const ConnectionCredentials &credentials)
{
    auto it = m_entries.find(connectionId);
    if (it == m_entries.end() || !credentials.isValid()) {
        return;
    }
    const bool wasLoading = it->loading;
    // A load still running read older tokens from storage.
    it->loadGeneration = ++m_nextLoadGeneration;
    it->loading = false;
    it->credentials = credentials;
    it->renewed = true;
    it->rejected = false;
    if (wasLoading) {
        emit connectionReady(connectionId, true);
    }
}

void ConnectionSessionPool::markRejected(const QString &connectionId)
{
    auto it = m_entries.find(connectionId);
    if (it == m_entries.end()) {
        return;
    }
    const bool wasLoading = it->loading;
    it->loadGeneration = ++m_nextLoadGeneration;
    it->loading = false;
    it->credentials = {};
    it->renewed = false;
    it->rejected = true;
    if (wasLoading) {
        emit connectionReady(connectionId, false);
    }
}

bool ConnectionSessionPool::isRejected(const QString &connectionId) const
{
    const auto it = m_entries.constFind(connectionId);
    return it != m_entries.cend() && it->rejected;
}

std::optional<ConnectionCredentials> ConnectionSessionPool::renewedCredentials(
    const QString &connectionId) const
{
    const auto it = m_entries.constFind(connectionId);
    if (it == m_entries.cend() || !it->renewed || !it->credentials.isValid()) {
        return std::nullopt;
    }
    return it->credentials;
}

bool ConnectionSessionPool::isTracked(const QString &connectionId) const
{
    return m_entries.contains(connectionId);
}

bool ConnectionSessionPool::isLoading(const QString &connectionId) const
{
    const auto it = m_entries.constFind(connectionId);
    return it != m_entries.cend() && it->loading;
}

std::optional<ServerConnection> ConnectionSessionPool::connection(
    const QString &connectionId) const
{
    const auto it = m_entries.constFind(connectionId);
    if (it == m_entries.cend()) {
        return std::nullopt;
    }
    return it->connection;
}

std::optional<ConnectionCredentials> ConnectionSessionPool::credentials(
    const QString &connectionId) const
{
    const auto it = m_entries.constFind(connectionId);
    if (it == m_entries.cend() || it->loading || !it->credentials.isValid()) {
        return std::nullopt;
    }
    return it->credentials;
}

void ConnectionSessionPool::startLoad(const QString &connectionId)
{
    auto it = m_entries.find(connectionId);
    if (it == m_entries.end()) {
        return;
    }
    const quint64 generation = ++m_nextLoadGeneration;
    it->loading = true;
    it->loadGeneration = generation;

    auto *watcher = new QFutureWatcher<ConnectionCredentials>(this);
    connect(watcher, &QFutureWatcher<ConnectionCredentials>::finished,
            this, [this, watcher, connectionId, generation]() {
        const ConnectionCredentials loaded = watcher->result();
        watcher->deleteLater();
        auto entry = m_entries.find(connectionId);
        // Forgotten or superseded by a newer load.
        if (entry == m_entries.end() || entry->loadGeneration != generation) {
            return;
        }
        entry->loading = false;
        entry->credentials = loaded;
        emit connectionReady(connectionId, loaded.isValid());
    });
    watcher->setFuture(QtConcurrent::run(
        [loader = m_loader, connection = it->connection]() {
            return loader(connection);
        }));
}
