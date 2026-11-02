#include "network/discovery/ServerCandidateValidator.h"

#include "network/HttpTransport.h"
#include "providers/IProviderAdapter.h"
#include "providers/IProviderRequestFactory.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>

ProviderServerValidator::ProviderServerValidator(const QList<IProviderAdapter *> &adapters,
                                                 QObject *parent)
    : QObject(parent)
    , m_adapters(adapters)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_transport(std::make_unique<HttpTransport>(m_networkManager))
{
}

ProviderServerValidator::~ProviderServerValidator()
{
    m_transport->cancelAll();
}

IProviderAdapter *ProviderServerValidator::adapterFor(ProviderKind kind) const
{
    for (IProviderAdapter *adapter : m_adapters) {
        if (adapter && adapter->providerKind() == kind && adapter->requestFactory()) {
            return adapter;
        }
    }
    return nullptr;
}

void ProviderServerValidator::get(IProviderAdapter *adapter, const QUrl &baseUrl,
                                  const QString &endpoint,
                                  std::function<void(std::optional<QJsonObject>)> done)
{
    ProviderRequestContext context;
    context.baseUrl = baseUrl.toString(QUrl::StripTrailingSlash);
    const IProviderRequestFactory *factory = adapter->requestFactory();
    HttpRequestOptions options;
    options.retrySafety = RetrySafety::Never;
    options.retryPolicy.maxAttempts = 1;
    options.attemptTimeoutMs = AttemptTimeoutMs;
    options.unauthorizedPolicy = UnauthorizedPolicy::Ignore;
    m_transport->sendWithRetry(
        this,
        endpoint,
        [this, factory, context, endpoint]() {
            QNetworkRequest request = factory->createRequest(context, endpoint);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::ManualRedirectPolicy);
            QNetworkReply *reply = m_networkManager->get(request);
            reply->setReadBufferSize(MaxResponseBytes);
            return reply;
        },
        [done](QNetworkReply *reply) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QJsonParseError error;
            const QJsonDocument document =
                QJsonDocument::fromJson(reply->read(MaxResponseBytes), &error);
            if (status < 200 || status >= 300 || error.error != QJsonParseError::NoError
                || !document.isObject()) {
                done(std::nullopt);
                return;
            }
            done(document.object());
        },
        [done](const NetworkError &) { done(std::nullopt); },
        options);
}

void ProviderServerValidator::validate(ProviderKind kind, const QUrl &baseUrl,
                                       const QString &expectedServerId, Completion done)
{
    IProviderAdapter *adapter = adapterFor(kind);
    const auto identityEndpoint =
        adapter ? adapter->endpointFor(ProviderRoute::ServerIdentity) : std::nullopt;
    if (!identityEndpoint || !baseUrl.isValid() || expectedServerId.isEmpty()) {
        QMetaObject::invokeMethod(this, [done]() { done(std::nullopt); }, Qt::QueuedConnection);
        return;
    }
    get(adapter, baseUrl, *identityEndpoint,
        [this, adapter, baseUrl, expectedServerId, done](std::optional<QJsonObject> wire) {
        const auto identity = wire ? adapter->mapServerIdentity(*wire) : std::nullopt;
        if (!identity || identity->serverId != expectedServerId) {
            done(std::nullopt);
            return;
        }
        const auto brandingEndpoint = adapter->endpointFor(ProviderRoute::ServerBranding);
        if (!brandingEndpoint || !identity->serverName.isEmpty()) {
            done(identity);
            return;
        }
        // The name is a label only: a failed read keeps the advertised one.
        get(adapter, baseUrl, *brandingEndpoint,
            [adapter, identity, done](std::optional<QJsonObject> branding) {
            ProviderServerIdentity named = *identity;
            if (branding) {
                named.serverName = adapter->mapServerBrandingName(*branding);
            }
            done(named);
        });
    });
}

void ProviderServerValidator::cancelAll()
{
    m_transport->cancelAll();
}
