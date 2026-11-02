#include "network/discovery/ServerDiscoveryService.h"

#include "network/discovery/JellyfinServerBrowser.h"
#include "network/discovery/MdnsServerBrowser.h"
#include "network/discovery/ServerCandidateValidator.h"
#include "providers/silo/SiloLanDiscovery.h"
#include "utils/BloomLogging.h"

#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace {

QString providerName(ProviderKind kind)
{
    return kind == ProviderKind::Silo ? QStringLiteral("silo") : QStringLiteral("jellyfin");
}

} // namespace

ServerDiscoveryService::ServerDiscoveryService(const QList<LanServerBrowser *> &browsers,
                                               std::unique_ptr<ServerCandidateValidator> validator,
                                               QObject *parent)
    : QObject(parent)
    , m_browsers(browsers)
    , m_validator(std::move(validator))
{
    m_queryTimer.setSingleShot(true);
    m_windowTimer.setSingleShot(true);
    connect(&m_queryTimer, &QTimer::timeout, this, &ServerDiscoveryService::sendQueries);
    connect(&m_windowTimer, &QTimer::timeout, this, [this]() {
        // The window closes the sockets; validations already running finish.
        stopBrowsers();
        updateSearching();
    });
    for (LanServerBrowser *browser : std::as_const(m_browsers)) {
        browser->setParent(this);
        connect(browser, &LanServerBrowser::advertFound,
                this, &ServerDiscoveryService::handleAdvert);
    }
}

ServerDiscoveryService::~ServerDiscoveryService()
{
    endSearch();
}

std::unique_ptr<ServerDiscoveryService> ServerDiscoveryService::create(
    const QList<IProviderAdapter *> &adapters, QObject *parent)
{
    auto *silo = new MdnsServerBrowser(
        SiloLanDiscovery::serviceType(),
        [](const Bloom::Dns::ServiceInstance &service) {
            return SiloLanDiscovery::advert(service.instanceLabel, service.txt, service.port,
                                            service.addresses);
        });
    auto *jellyfin = new JellyfinServerBrowser();
    return std::make_unique<ServerDiscoveryService>(
        QList<LanServerBrowser *>{silo, jellyfin},
        std::make_unique<ProviderServerValidator>(adapters), parent);
}

void ServerDiscoveryService::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    if (active) {
        beginSearch();
    } else {
        endSearch();
    }
    emit activeChanged();
}

void ServerDiscoveryService::refresh()
{
    if (m_active) {
        endSearch();
        beginSearch();
    }
}

void ServerDiscoveryService::beginSearch()
{
    ++m_generation;
    for (LanServerBrowser *browser : std::as_const(m_browsers)) {
        if (browser->start()) {
            m_running.append(browser);
        }
    }
    setAvailable(!m_running.isEmpty() && m_validator);
    if (!m_available) {
        stopBrowsers();
        updateSearching();
        return;
    }
    qCDebug(lcDiscovery) << "Searching the local network with" << m_running.size()
                         << "browsers";
    m_queryIndex = 0;
    m_windowTimer.start(m_timing.searchWindowMs);
    if (m_timing.queryOffsetsMs.isEmpty() || m_timing.queryOffsetsMs.constFirst() <= 0) {
        sendQueries();
    } else {
        m_queryTimer.start(m_timing.queryOffsetsMs.constFirst());
    }
    updateSearching();
}

void ServerDiscoveryService::endSearch()
{
    ++m_generation;
    m_queryTimer.stop();
    m_windowTimer.stop();
    stopBrowsers();
    if (m_validator) {
        m_validator->cancelAll();
    }
    m_queue.clear();
    m_inFlight = 0;
    m_tried.clear();
    m_queuedPerSender.clear();
    m_listed.clear();
    if (!m_servers.isEmpty()) {
        m_servers.clear();
        emit serversChanged();
    }
    updateSearching();
}

void ServerDiscoveryService::stopBrowsers()
{
    m_queryTimer.stop();
    const auto running = std::exchange(m_running, {});
    for (const QPointer<LanServerBrowser> &browser : running) {
        if (browser) {
            browser->stop();
        }
    }
}

void ServerDiscoveryService::sendQueries()
{
    for (const QPointer<LanServerBrowser> &browser : std::as_const(m_running)) {
        if (browser) {
            browser->query();
        }
    }
    ++m_queryIndex;
    if (!m_running.isEmpty() && m_queryIndex < m_timing.queryOffsetsMs.size()) {
        const int delay = m_timing.queryOffsetsMs.at(m_queryIndex)
            - m_timing.queryOffsetsMs.at(m_queryIndex - 1);
        m_queryTimer.start(std::max(0, delay));
    }
}

void ServerDiscoveryService::handleAdvert(const LanServerAdvert &advert)
{
    if (!m_active || m_running.isEmpty() || advert.serverId.isEmpty()) {
        return;
    }
    const QString serverKey = providerName(advert.providerKind) + QLatin1Char(':')
        + advert.serverId;
    const QString entryKey = serverKey + QLatin1Char('\n') + advert.origin;
    if (m_listed.contains(entryKey)) {
        return;
    }
    // One sender cannot fill the whole candidate budget.
    int &queuedBySender = m_queuedPerSender[advert.sender.toString()];
    for (const QUrl &url : advert.urls) {
        const QString triedKey = serverKey + QLatin1Char('\n') + url.toString();
        if (m_tried.contains(triedKey) || m_tried.size() >= MaxCandidates
            || queuedBySender >= MaxCandidatesPerSender) {
            continue;
        }
        m_tried.insert(triedKey);
        ++queuedBySender;
        m_queue.append(Candidate{serverKey, entryKey, advert, url});
    }
    pump();
    updateSearching();
}

void ServerDiscoveryService::pump()
{
    if (m_listed.size() >= MaxServers) {
        m_queue.clear();
    }
    while (m_inFlight < MaxValidationsInFlight && !m_queue.isEmpty()) {
        const Candidate candidate = m_queue.takeFirst();
        if (m_listed.contains(candidate.entryKey)) {
            continue;
        }
        ++m_inFlight;
        const quint64 generation = m_generation;
        m_validator->validate(
            candidate.advert.providerKind, candidate.url, candidate.advert.serverId,
            [this, guard = QPointer<ServerDiscoveryService>(this), generation, candidate](
                std::optional<ProviderServerIdentity> identity) {
            if (!guard || generation != m_generation) {
                return;
            }
            --m_inFlight;
            finishValidation(candidate, identity);
            pump();
            updateSearching();
        });
    }
}

void ServerDiscoveryService::finishValidation(
    const Candidate &candidate, const std::optional<ProviderServerIdentity> &identity)
{
    if (!identity) {
        qCDebug(lcDiscovery) << "Dropped a candidate that did not confirm its identity:"
                             << candidate.url.toString();
        return;
    }
    if (m_listed.contains(candidate.entryKey) || m_listed.size() >= MaxServers) {
        return;
    }
    m_listed.insert(candidate.entryKey);
    m_queue.removeIf([&candidate](const Candidate &queued) {
        return queued.entryKey == candidate.entryKey;
    });
    const QString name = !identity->serverName.isEmpty() ? identity->serverName
        : !candidate.advert.name.isEmpty()               ? candidate.advert.name
                                                         : candidate.url.host();
    QVariantMap entry;
    entry.insert(QStringLiteral("key"), candidate.entryKey);
    entry.insert(QStringLiteral("name"), name);
    entry.insert(QStringLiteral("url"), candidate.url.toString(QUrl::StripTrailingSlash));
    entry.insert(QStringLiteral("address"), candidate.url.authority());
    entry.insert(QStringLiteral("provider"), providerName(candidate.advert.providerKind));
    entry.insert(QStringLiteral("saved"), isSaved(candidate));
    m_servers.append(entry);
    qCInfo(lcDiscovery) << "Found" << providerName(candidate.advert.providerKind)
                        << "server at" << candidate.url.toString();
    emit serversChanged();
}

bool ServerDiscoveryService::isSaved(const Candidate &candidate) const
{
    if (!m_savedConnections) {
        return false;
    }
    // The id alone is the advertiser's claim, so the saved server's host must
    // match too: another host answering the same id is not marked saved.
    const QList<ServerConnection> connections = m_savedConnections();
    return std::any_of(connections.cbegin(), connections.cend(),
                       [&candidate](const ServerConnection &connection) {
        return connection.providerKind == candidate.advert.providerKind
            && connection.serverId == candidate.advert.serverId
            && QUrl(connection.baseUrl).host().compare(candidate.url.host(),
                                                       Qt::CaseInsensitive) == 0;
    });
}

void ServerDiscoveryService::updateSearching()
{
    const bool searching = m_active && m_available
        && (m_windowTimer.isActive() || m_inFlight > 0 || !m_queue.isEmpty());
    if (searching != m_searching) {
        m_searching = searching;
        emit searchingChanged();
    }
}

void ServerDiscoveryService::setAvailable(bool available)
{
    if (available != m_available) {
        m_available = available;
        emit availableChanged();
    }
}
