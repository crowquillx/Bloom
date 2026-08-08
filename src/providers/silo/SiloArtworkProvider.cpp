#include "SiloArtworkProvider.h"

#include "network/AuthenticationService.h"
#include "network/HttpTransport.h"
#include "utils/ConfigManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <memory>
#include <utility>

namespace {

class RefreshCompletion final
{
public:
    explicit RefreshCompletion(IArtworkProvider::RefreshCallback callback)
        : m_callback(std::move(callback))
    {
    }

    void finish(std::optional<QNetworkRequest> request)
    {
        if (m_finished.exchange(true)) {
            return;
        }
        auto callback = std::move(m_callback);
        if (callback) {
            callback(std::move(request));
        }
    }

private:
    std::atomic_bool m_finished = false;
    IArtworkProvider::RefreshCallback m_callback;
};

void finishLater(const std::shared_ptr<RefreshCompletion> &completion)
{
    QTimer::singleShot(0, [completion]() {
        completion->finish(std::nullopt);
    });
}

bool hasMatchingSession(AuthenticationService *authService,
                        const Bloom::ArtworkRef &artwork)
{
    if (!authService || !artwork.isValid() || !authService->isAuthenticated()) {
        return false;
    }

    ConfigManager *config = authService->configManager();
    const auto connection = config ? config->getActiveConnection() : std::nullopt;
    if (!connection.has_value()
        || connection->connectionId != artwork.connectionId
        || connection->providerKind != ProviderKind::Silo
        || authService->activeProviderKind() != ProviderKind::Silo) {
        return false;
    }

    // The connection id is the cache identity, but it is not sufficient by
    // itself to authorize a request: a caller can retain an ArtworkRef while
    // the active connection is replaced in configuration.  Do not let the
    // current token be sent to a different server in that case.
    return ServerConnection::normalizeBaseUrl(authService->getServerUrl())
        == ServerConnection::normalizeBaseUrl(connection->baseUrl);
}

bool isHttpUrl(const QUrl &url)
{
    return url.isValid()
        && !url.host().isEmpty()
        && url.userName().isEmpty()
        && url.password().isEmpty()
        && (url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0
            || url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0);
}

std::optional<QUrl> resolveOpaqueUrl(const QString &baseUrl,
                                     const QString &sourceUrl)
{
    const QByteArray encodedSource = sourceUrl.toUtf8();
    if (encodedSource.isEmpty()) {
        return std::nullopt;
    }

    const QUrl direct = QUrl::fromEncoded(encodedSource, QUrl::StrictMode);
    if (!direct.isRelative() && !direct.scheme().isEmpty()) {
        return isHttpUrl(direct) ? std::optional<QUrl>(direct) : std::nullopt;
    }

    QUrl base(baseUrl.trimmed(), QUrl::StrictMode);
    if (!isHttpUrl(base)) {
        return std::nullopt;
    }
    base.setQuery(QString());
    base.setFragment(QString());

    QByteArray combined;
    if (encodedSource.startsWith("//")) {
        combined = base.scheme().toUtf8() + QByteArrayLiteral(":") + encodedSource;
    } else if (encodedSource.startsWith('/')) {
        QUrl origin = base;
        origin.setPath(QString());
        origin.setUserName(QString());
        origin.setPassword(QString());
        combined = origin.toEncoded(QUrl::FullyEncoded) + encodedSource;
    } else {
        QByteArray encodedBase = base.toEncoded(QUrl::FullyEncoded);
        if (encodedSource.startsWith('?') || encodedSource.startsWith('#')) {
            combined = encodedBase + encodedSource;
        } else if (encodedBase.endsWith('/')) {
            combined = encodedBase + encodedSource;
        } else {
            const qsizetype schemeEnd = encodedBase.indexOf(QByteArrayLiteral("://"));
            const qsizetype pathStart = schemeEnd < 0
                ? -1
                : encodedBase.indexOf('/', schemeEnd + 3);
            if (pathStart < 0) {
                combined = encodedBase + QByteArrayLiteral("/") + encodedSource;
            } else {
                combined = encodedBase.left(encodedBase.lastIndexOf('/') + 1)
                    + encodedSource;
            }
        }
    }

    const QUrl resolved = QUrl::fromEncoded(combined, QUrl::StrictMode);
    return isHttpUrl(resolved) ? std::optional<QUrl>(resolved) : std::nullopt;
}

int effectivePort(const QUrl &url)
{
    if (url.port() >= 0) {
        return url.port();
    }
    return url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
        ? 443 : 80;
}

bool isSameOrigin(const QUrl &base, const QUrl &resolved)
{
    return isHttpUrl(base)
        && base.scheme().compare(resolved.scheme(), Qt::CaseInsensitive) == 0
        && base.host().compare(resolved.host(), Qt::CaseInsensitive) == 0
        && effectivePort(base) == effectivePort(resolved);
}

std::optional<QNetworkRequest> requestForSource(
    AuthenticationService *authService,
    const Bloom::ArtworkRef &artwork)
{
    if (!hasMatchingSession(authService, artwork)) {
        return std::nullopt;
    }

    const auto url = resolveOpaqueUrl(authService->getServerUrl(), artwork.sourceUrl);
    if (!url.has_value()) {
        return std::nullopt;
    }

    const QUrl base(authService->getServerUrl().trimmed(), QUrl::StrictMode);
    QNetworkRequest request;
    if (isSameOrigin(base, *url)) {
        request = authService->createRequest(QStringLiteral("/api/v1/catalog"));
    }
    request.setUrl(*url);
    request.setRawHeader("Accept", "image/*");
    return request;
}


QString identityString(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(value.toVariant().toLongLong());
    }
    return {};
}

QString sourceForKind(const QJsonObject &object, Bloom::ArtworkKind kind)
{
    switch (kind) {
    case Bloom::ArtworkKind::Primary: {
        const QString poster = object.value(QStringLiteral("poster_url")).toString();
        return poster.isEmpty()
            ? object.value(QStringLiteral("still_url")).toString()
            : poster;
    }
    case Bloom::ArtworkKind::Thumb: {
        const QString thumb = object.value(QStringLiteral("thumb_url")).toString();
        return thumb.isEmpty()
            ? object.value(QStringLiteral("thumbnail_url")).toString()
            : thumb;
    }
    case Bloom::ArtworkKind::Backdrop:
        return object.value(QStringLiteral("backdrop_url")).toString();
    case Bloom::ArtworkKind::Logo:
        return object.value(QStringLiteral("logo_url")).toString();
    case Bloom::ArtworkKind::Chapter:
        return object.value(QStringLiteral("thumbnail_url")).toString();
    case Bloom::ArtworkKind::Person:
        return object.value(QStringLiteral("photo_url")).toString();
    case Bloom::ArtworkKind::Unknown:
        return {};
    }
    return {};
}

QString chapterSource(const QJsonArray &chapters, int requestedIndex)
{
    for (qsizetype position = 0; position < chapters.size(); ++position) {
        const QJsonObject chapter = chapters.at(position).toObject();
        if (chapter.isEmpty()) {
            continue;
        }
        const int index = chapter.value(QStringLiteral("index")).isDouble()
            ? chapter.value(QStringLiteral("index")).toInt()
            : static_cast<int>(position);
        if (index == requestedIndex) {
            return sourceForKind(chapter, Bloom::ArtworkKind::Chapter);
        }
    }
    return {};
}

QString chapterSource(const QJsonObject &item,
                      const Bloom::ArtworkRef &artwork)
{
    QString source = chapterSource(
        item.value(QStringLiteral("chapters")).toArray(), artwork.index);
    if (!source.isEmpty()) {
        return source;
    }

    const QJsonArray versions = item.value(QStringLiteral("versions")).toArray();
    for (const QJsonValue &value : versions) {
        const QJsonObject version = value.toObject();
        if (!artwork.tag.isEmpty()
            && identityString(version.value(QStringLiteral("file_id"))) != artwork.tag) {
            continue;
        }
        source = chapterSource(
            version.value(QStringLiteral("chapters")).toArray(), artwork.index);
        if (!source.isEmpty()) {
            return source;
        }
    }
    return {};
}

QJsonObject unwrappedObject(const QJsonObject &object, const QString &property)
{
    const QJsonObject nested = object.value(property).toObject();
    return nested.isEmpty() ? object : nested;
}

QString refreshedSource(const QByteArray &payload,
                        const Bloom::ArtworkRef &artwork)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return {};
    }

    switch (artwork.ownerKind) {
    case Bloom::ArtworkOwnerKind::Library: {
        QJsonArray libraries;
        if (document.isArray()) {
            libraries = document.array();
        } else if (document.isObject()) {
            const QJsonObject object = document.object();
            libraries = object.value(QStringLiteral("libraries")).toArray();
            if (libraries.isEmpty()) {
                libraries = object.value(QStringLiteral("items")).toArray();
            }
        }
        for (const QJsonValue &value : libraries) {
            const QJsonObject library = value.toObject();
            if (identityString(library.value(QStringLiteral("id"))) == artwork.itemId) {
                return sourceForKind(library, artwork.kind);
            }
        }
        return {};
    }
    case Bloom::ArtworkOwnerKind::Person:
        if (!document.isObject()) {
            return {};
        }
        return sourceForKind(
            unwrappedObject(document.object(), QStringLiteral("person")), artwork.kind);
    case Bloom::ArtworkOwnerKind::Chapter:
        if (!document.isObject() || artwork.kind != Bloom::ArtworkKind::Chapter) {
            return {};
        }
        return chapterSource(
            unwrappedObject(document.object(), QStringLiteral("item")), artwork);
    case Bloom::ArtworkOwnerKind::MediaItem:
        if (!document.isObject()) {
            return {};
        }
        return sourceForKind(
            unwrappedObject(document.object(), QStringLiteral("item")), artwork.kind);
    }
    return {};
}

QString encodedIdentity(const QString &identity)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(identity));
}

QString refreshEndpoint(const Bloom::ArtworkRef &artwork)
{
    switch (artwork.ownerKind) {
    case Bloom::ArtworkOwnerKind::Library:
        return artwork.kind == Bloom::ArtworkKind::Primary
            ? QStringLiteral("/api/v1/user/libraries") : QString();
    case Bloom::ArtworkOwnerKind::Person:
        return artwork.kind == Bloom::ArtworkKind::Person
            ? QStringLiteral("/api/v1/catalog/people/%1").arg(encodedIdentity(artwork.itemId))
            : QString();
    case Bloom::ArtworkOwnerKind::Chapter:
        return artwork.kind == Bloom::ArtworkKind::Chapter
            ? QStringLiteral("/api/v1/catalog/items/%1").arg(encodedIdentity(artwork.itemId))
            : QString();
    case Bloom::ArtworkOwnerKind::MediaItem:
        switch (artwork.kind) {
        case Bloom::ArtworkKind::Primary:
        case Bloom::ArtworkKind::Thumb:
        case Bloom::ArtworkKind::Backdrop:
        case Bloom::ArtworkKind::Logo:
            return QStringLiteral("/api/v1/catalog/items/%1")
                .arg(encodedIdentity(artwork.itemId));
        case Bloom::ArtworkKind::Chapter:
        case Bloom::ArtworkKind::Person:
        case Bloom::ArtworkKind::Unknown:
            return {};
        }
    }
    return {};
}

} // namespace

SiloArtworkProvider::SiloArtworkProvider(AuthenticationService *authService)
    : m_authService(authService)
{
}

std::optional<QNetworkRequest> SiloArtworkProvider::resolveArtwork(
    const Bloom::ArtworkRef &artwork) const
{
    return requestForSource(m_authService, artwork);
}

void SiloArtworkProvider::refreshArtwork(
    const Bloom::ArtworkRef &artwork,
    RefreshCallback callback) const
{
    if (!callback) {
        return;
    }

    auto completion = std::make_shared<RefreshCompletion>(std::move(callback));
    const QString endpoint = refreshEndpoint(artwork);
    if (!hasMatchingSession(m_authService, artwork) || endpoint.isEmpty()) {
        finishLater(completion);
        return;
    }

    QPointer<AuthenticationService> authService(m_authService);
    QPointer<HttpTransport> transport(m_authService->transport());
    QPointer<QNetworkAccessManager> networkManager(m_authService->networkManager());
    if (!transport || !networkManager) {
        finishLater(completion);
        return;
    }

    HttpRequestOptions options;
    options.retryEnabled = false;
    options.unauthorizedPolicy = UnauthorizedPolicy::ExpireSession;

    HttpRequestHandle *handle = transport->sendWithRetry(
        transport.data(),
        endpoint,
        [authService, artwork, networkManager, endpoint]() -> QNetworkReply * {
            if (!authService || !networkManager
                || !hasMatchingSession(authService.data(), artwork)) {
                return nullptr;
            }
            const QNetworkRequest request = authService->createRequest(endpoint);
            if (!request.url().isValid() || request.url().isEmpty()) {
                return nullptr;
            }
            return networkManager->get(request);
        },
        [authService, artwork, completion](QNetworkReply *reply) {
            if (!authService || !reply) {
                completion->finish(std::nullopt);
                return;
            }
            const QString source = refreshedSource(reply->readAll(), artwork);
            if (source.isEmpty()) {
                completion->finish(std::nullopt);
                return;
            }
            Bloom::ArtworkRef refreshed = artwork;
            refreshed.sourceUrl = source;
            completion->finish(requestForSource(authService.data(), refreshed));
        },
        [completion](const NetworkError &) {
            completion->finish(std::nullopt);
        },
        options);

    if (!handle) {
        finishLater(completion);
        return;
    }
    QObject::connect(handle, &QObject::destroyed, [completion]() {
        completion->finish(std::nullopt);
    });
}
