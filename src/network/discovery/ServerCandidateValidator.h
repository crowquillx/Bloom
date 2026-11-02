#pragma once

#include "providers/LanServerAdvert.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>

class HttpTransport;
class IProviderAdapter;
class QNetworkAccessManager;

/**
 * @brief Confirms that a found address serves the advertised server.
 *
 * validate() completes asynchronously with the identity the address reports
 * when it matches @p expectedServerId, or nullopt. cancelAll() drops every
 * running check without completing it.
 */
class ServerCandidateValidator
{
public:
    using Completion = std::function<void(std::optional<ProviderServerIdentity>)>;

    virtual ~ServerCandidateValidator() = default;
    virtual void validate(ProviderKind kind, const QUrl &baseUrl,
                          const QString &expectedServerId, Completion done) = 0;
    virtual void cancelAll() = 0;
};

/**
 * @brief Validation through the provider adapters' public reads: the
 * ServerIdentity route (Silo getServerIdentity, Jellyfin
 * GetPublicSystemInfo), then the optional ServerBranding route for the live
 * name (Silo getBranding).
 *
 * Requests carry no credentials, Authorization header or device identity,
 * follow no redirect (the
 * candidate itself must answer), make one attempt under a short deadline,
 * and buffer at most a small response: a larger body stalls the read until
 * the deadline drops it.
 */
class ProviderServerValidator final : public QObject, public ServerCandidateValidator
{
    Q_OBJECT

public:
    static constexpr int AttemptTimeoutMs = 3000;
    static constexpr qint64 MaxResponseBytes = 64 * 1024;

    explicit ProviderServerValidator(const QList<IProviderAdapter *> &adapters,
                                     QObject *parent = nullptr);
    ~ProviderServerValidator() override;

    void validate(ProviderKind kind, const QUrl &baseUrl, const QString &expectedServerId,
                  Completion done) override;
    void cancelAll() override;

private:
    QList<IProviderAdapter *> m_adapters;
    QNetworkAccessManager *m_networkManager = nullptr;
    std::unique_ptr<HttpTransport> m_transport;

    IProviderAdapter *adapterFor(ProviderKind kind) const;
    void get(IProviderAdapter *adapter, const QUrl &baseUrl, const QString &endpoint,
             std::function<void(std::optional<QJsonObject>)> done);
};
