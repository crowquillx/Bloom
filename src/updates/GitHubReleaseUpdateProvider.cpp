#include "GitHubReleaseUpdateProvider.h"

#include "UpdateNetworkPolicy.h"
#include "config/version.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace
{

QString normalizedChannel(const QString &channel)
{
    return channel.trimmed().compare(QStringLiteral("dev"), Qt::CaseInsensitive) == 0 ? QStringLiteral("dev")
                                                                                      : QStringLiteral("stable");
}

UpdateAsset parseAsset(const QJsonObject &object)
{
    UpdateAsset asset;
    asset.url = object.value(QStringLiteral("url")).toString().trimmed();
    asset.filename = object.value(QStringLiteral("filename")).toString().trimmed();
    asset.sha256 = object.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    return asset;
}

bool hasValidSha256(const UpdateAsset &asset)
{
    const QByteArray checksum = asset.sha256.toLatin1();
    return checksum.size() == 64 && std::all_of(checksum.cbegin(), checksum.cend(), [](char character) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
    });
}

class ManifestFetchJob final : public QObject
{
  public:
    using Completion = std::function<void(QByteArray, QString)>;

    ManifestFetchJob(QNetworkAccessManager *networkAccessManager, GitHubReleaseUpdateProviderOptions options,
                     QUrl initialUrl, QObject *context, Completion completion, QObject *parent)
        : QObject(parent), m_networkAccessManager(networkAccessManager), m_options(std::move(options)),
          m_url(std::move(initialUrl)), m_context(context), m_completion(std::move(completion))
    {
        m_deadline.setSingleShot(true);
        connect(&m_deadline, &QTimer::timeout, this, [this]() { fail(QObject::tr("Update check timed out.")); });
        if (context)
        {
            connect(context, &QObject::destroyed, this, [this]() { cancel(); });
        }
    }

    ~ManifestFetchJob() override
    {
        if (m_reply)
        {
            disconnect(m_reply, nullptr, this, nullptr);
            m_reply->abort();
            m_reply->deleteLater();
        }
    }

    void start()
    {
        m_options.maximumResponseBytes = std::max<qint64>(1, m_options.maximumResponseBytes);
        m_deadline.start(std::max(1, m_options.deadlineMs));
        issueRequest();
    }

  private:
    bool urlAllowed(const QUrl &url) const
    {
        return m_options.urlValidator ? m_options.urlValidator(url) : UpdateNetworkPolicy::isAllowedManifestUrl(url);
    }

    void issueRequest()
    {
        if (!m_networkAccessManager || !urlAllowed(m_url))
        {
            fail(QObject::tr("Update manifest URL is not an allowed HTTPS origin."));
            return;
        }

        QNetworkRequest request(m_url);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("Bloom/%1").arg(QString::fromUtf8(BLOOM_VERSION)));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(std::max(1, m_options.transferTimeoutMs));

        m_reply = m_networkAccessManager->get(request);
        m_reply->setReadBufferSize(std::min<qint64>(m_options.maximumResponseBytes, 64 * 1024));
        connect(m_reply, &QNetworkReply::readyRead, this, [this]() { consumeBody(); });
        connect(m_reply, &QNetworkReply::finished, this, [this]() { onFinished(); });
    }

    void consumeBody()
    {
        if (!m_reply || m_finished)
        {
            return;
        }
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 300 && status < 400)
        {
            m_reply->readAll();
            return;
        }
        const qint64 contentLength = m_reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (contentLength > m_options.maximumResponseBytes)
        {
            fail(QObject::tr("Update manifest exceeds the allowed size."));
            return;
        }
        m_body.append(m_reply->readAll());
        if (m_body.size() > m_options.maximumResponseBytes)
        {
            fail(QObject::tr("Update manifest exceeds the allowed size."));
        }
    }

    void onFinished()
    {
        if (!m_reply || m_finished)
        {
            return;
        }
        consumeBody();
        if (m_finished)
        {
            return;
        }

        QNetworkReply *finishedReply = m_reply;
        m_reply = nullptr;
        const int status = finishedReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl redirect = UpdateNetworkPolicy::resolvedRedirect(
            finishedReply->url(), finishedReply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
        const auto networkError = finishedReply->error();
        const QString networkErrorText = finishedReply->errorString();
        const QVariant contentLengthHeader = finishedReply->header(QNetworkRequest::ContentLengthHeader);
        finishedReply->deleteLater();

        if (status >= 300 && status < 400)
        {
            if (!redirect.isValid() || !urlAllowed(redirect))
            {
                fail(QObject::tr("Update manifest redirect was rejected."));
                return;
            }
            if (++m_redirects > std::max(0, m_options.maximumRedirects))
            {
                fail(QObject::tr("Update manifest used too many redirects."));
                return;
            }
            m_url = redirect;
            m_body.clear();
            issueRequest();
            return;
        }

        if (status == 200 && contentLengthHeader.isValid() && contentLengthHeader.toLongLong() >= 0 &&
            contentLengthHeader.toLongLong() != m_body.size())
        {
            fail(QObject::tr("Update manifest download was truncated."));
            return;
        }
        if (networkError != QNetworkReply::NoError)
        {
            fail(QObject::tr("Update check failed: %1").arg(networkErrorText));
            return;
        }
        if (status != 200)
        {
            fail(QObject::tr("Update check failed with HTTP %1").arg(status));
            return;
        }
        succeed();
    }

    void succeed()
    {
        finish(std::move(m_body), QString());
    }

    void fail(const QString &message)
    {
        if (m_reply)
        {
            disconnect(m_reply, nullptr, this, nullptr);
            m_reply->abort();
            m_reply->deleteLater();
            m_reply = nullptr;
        }
        finish({}, message);
    }

    void cancel()
    {
        m_completion = {};
        fail(QString());
    }

    void finish(QByteArray body, const QString &error)
    {
        if (m_finished)
        {
            return;
        }
        m_finished = true;
        m_deadline.stop();
        if (m_context && m_completion)
        {
            m_completion(std::move(body), error);
        }
        deleteLater();
    }

  private:
    QNetworkAccessManager *m_networkAccessManager = nullptr;
    GitHubReleaseUpdateProviderOptions m_options;
    QUrl m_url;
    QPointer<QObject> m_context;
    Completion m_completion;
    QPointer<QNetworkReply> m_reply;
    QTimer m_deadline;
    QByteArray m_body;
    int m_redirects = 0;
    bool m_finished = false;
};

} // namespace

GitHubReleaseUpdateProvider::GitHubReleaseUpdateProvider(QObject *parent)
    : IUpdateProvider(parent), m_networkAccessManager(new QNetworkAccessManager(this))
{
}

GitHubReleaseUpdateProvider::GitHubReleaseUpdateProvider(QNetworkAccessManager *networkAccessManager,
                                                         GitHubReleaseUpdateProviderOptions options, QObject *parent)
    : IUpdateProvider(parent), m_networkAccessManager(networkAccessManager), m_options(std::move(options))
{
}

void GitHubReleaseUpdateProvider::fetchManifest(const QString &channel, QObject *context,
                                                FetchManifestCallback completion)
{
    const QString expectedChannel = normalizedChannel(channel);
    const QUrl url(configuredManifestUrl(channel));
    auto byteCompletion = [expectedChannel, trustedKeys = m_options.trustedKeys,
                           completion = std::move(completion)](QByteArray body, const QString &error) mutable
    {
        if (!error.isEmpty())
        {
            completion(std::nullopt, error);
            return;
        }
        QString parseError;
        auto manifest = trustedKeys.isEmpty()
                            ? GitHubReleaseUpdateProvider::parseManifestBytes(body, &parseError)
                            : GitHubReleaseUpdateProvider::parseManifestBytes(body, trustedKeys, &parseError);
        if (!manifest.has_value())
        {
            completion(std::nullopt, parseError);
            return;
        }
        if (manifest->channel != expectedChannel)
        {
            completion(std::nullopt, QObject::tr("Update manifest channel does not match the request."));
            return;
        }
        completion(std::move(manifest), QString());
    };
    auto *job = new ManifestFetchJob(m_networkAccessManager, m_options, url, context, std::move(byteCompletion), this);
    job->start();
}

std::optional<UpdateManifest> GitHubReleaseUpdateProvider::parseManifestBytes(const QByteArray &data,
                                                                              QString *errorMessage)
{
    return parseManifestBytes(data, UpdateManifestVerifier::embeddedKeys(), errorMessage);
}

std::optional<UpdateManifest> GitHubReleaseUpdateProvider::parseManifestBytes(
    const QByteArray &data, const QList<TrustedUpdateManifestKey> &trustedKeys, QString *errorMessage)
{
    QByteArray payload;
    if (!UpdateManifestVerifier::verify(data, trustedKeys, &payload, errorMessage))
    {
        return std::nullopt;
    }

    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (document.isNull() || !document.isObject())
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("Signed update manifest payload is not valid JSON.");
        }
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    UpdateManifest manifest;
    const QString payloadChannel = root.value(QStringLiteral("channel")).toString().trimmed().toLower();
    if (payloadChannel != QStringLiteral("stable") && payloadChannel != QStringLiteral("dev"))
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("Update manifest channel is invalid.");
        }
        return std::nullopt;
    }
    manifest.channel = payloadChannel;
    manifest.version = root.value(QStringLiteral("version")).toString().trimmed();
    manifest.buildId = root.value(QStringLiteral("build_id")).toString().trimmed();
    manifest.releaseTag = root.value(QStringLiteral("release_tag")).toString().trimmed();
    manifest.publishedAt = root.value(QStringLiteral("published_at")).toString().trimmed();
    manifest.notes = root.value(QStringLiteral("notes")).toString().trimmed();
    manifest.installer = parseAsset(root.value(QStringLiteral("installer")).toObject());
    manifest.portable = parseAsset(root.value(QStringLiteral("portable")).toObject());

    const QJsonObject rollout = root.value(QStringLiteral("rollout")).toObject();
    if (!rollout.isEmpty())
    {
        manifest.rolloutEnabled = rollout.value(QStringLiteral("enabled")).toBool(true);
        manifest.minimumSupportedVersion =
            rollout.value(QStringLiteral("minimum_supported_version")).toString().trimmed();
    }

    if (!manifest.isValid())
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("Update manifest is missing required fields.");
        }
        return std::nullopt;
    }
    if (!hasValidSha256(manifest.installer) || !hasValidSha256(manifest.portable))
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("Update manifest contains an invalid checksum.");
        }
        return std::nullopt;
    }
    if (!UpdateNetworkPolicy::isAllowedAssetUrl(QUrl(manifest.installer.url), true) ||
        !UpdateNetworkPolicy::isAllowedAssetUrl(QUrl(manifest.portable.url), true))
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("Update manifest contains an untrusted asset URL.");
        }
        return std::nullopt;
    }

    return manifest;
}

QString GitHubReleaseUpdateProvider::manifestUrlForChannel(const QString &channel)
{
    const QString normalized = normalizedChannel(channel);
    const QString baseUrl = QString::fromUtf8(BLOOM_UPDATE_MANIFEST_BASE_URL).trimmed();
    return baseUrl + QLatin1Char('/') + normalized + QStringLiteral(".json");
}

QString GitHubReleaseUpdateProvider::configuredManifestUrl(const QString &channel) const
{
    if (m_options.manifestBaseUrl.trimmed().isEmpty())
    {
        return manifestUrlForChannel(channel);
    }
    return m_options.manifestBaseUrl.trimmed() + QLatin1Char('/') + normalizedChannel(channel) +
           QStringLiteral(".json");
}
