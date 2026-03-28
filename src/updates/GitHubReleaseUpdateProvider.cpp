#include "GitHubReleaseUpdateProvider.h"

#include "config/version.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QUrl>

namespace {

QString normalizedChannel(const QString &channel)
{
    return channel.trimmed().compare(QStringLiteral("dev"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("dev")
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

} // namespace

GitHubReleaseUpdateProvider::GitHubReleaseUpdateProvider(QObject *parent)
    : IUpdateProvider(parent)
{
}

void GitHubReleaseUpdateProvider::fetchManifest(const QString &channel,
                                                QObject *context,
                                                FetchManifestCallback completion)
{
    const QString manifestUrl = manifestUrlForChannel(channel);
    QNetworkRequest request{QUrl(manifestUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Bloom/%1").arg(QString::fromUtf8(BLOOM_VERSION)));

    QPointer<QObject> guardedContext(context);
    QNetworkReply *reply = m_networkAccessManager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, this, [reply, completion = std::move(completion), guardedContext]() mutable {
        const auto finish = [&](std::optional<UpdateManifest> manifest, const QString &error) {
            if (guardedContext) {
                completion(std::move(manifest), error);
            }
        };

        const QByteArray body = reply->readAll();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString errorString = reply->errorString();
        reply->deleteLater();

        if (networkError != QNetworkReply::NoError) {
            finish(std::nullopt, QObject::tr("Update check failed: %1").arg(errorString));
            return;
        }

        if (statusCode != 200) {
            finish(std::nullopt, QObject::tr("Update check failed with HTTP %1").arg(statusCode));
            return;
        }

        QString parseError;
        std::optional<UpdateManifest> manifest = parseManifestBytes(body, &parseError);
        if (!manifest.has_value()) {
            finish(std::nullopt, parseError);
            return;
        }

        finish(std::move(manifest), QString());
    });
}

std::optional<UpdateManifest> GitHubReleaseUpdateProvider::parseManifestBytes(const QByteArray &data, QString *errorMessage)
{
    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (document.isNull() || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Update manifest is not valid JSON.");
        }
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    UpdateManifest manifest;
    manifest.channel = normalizedChannel(root.value(QStringLiteral("channel")).toString());
    manifest.version = root.value(QStringLiteral("version")).toString().trimmed();
    manifest.buildId = root.value(QStringLiteral("build_id")).toString().trimmed();
    manifest.releaseTag = root.value(QStringLiteral("release_tag")).toString().trimmed();
    manifest.publishedAt = root.value(QStringLiteral("published_at")).toString().trimmed();
    manifest.notes = root.value(QStringLiteral("notes")).toString().trimmed();
    manifest.installer = parseAsset(root.value(QStringLiteral("installer")).toObject());
    manifest.portable = parseAsset(root.value(QStringLiteral("portable")).toObject());

    const QJsonObject rollout = root.value(QStringLiteral("rollout")).toObject();
    if (!rollout.isEmpty()) {
        manifest.rolloutEnabled = rollout.value(QStringLiteral("enabled")).toBool(true);
        manifest.minimumSupportedVersion = rollout.value(QStringLiteral("minimum_supported_version")).toString().trimmed();
    }

    if (!manifest.isValid()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Update manifest is missing required fields.");
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
