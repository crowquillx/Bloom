#pragma once

#include <QNetworkAccessManager>

#include <functional>

#include "IUpdateProvider.h"
#include "UpdateManifestVerifier.h"

struct GitHubReleaseUpdateProviderOptions
{
    int deadlineMs = 15000;
    int transferTimeoutMs = 10000;
    int maximumRedirects = 3;
    qint64 maximumResponseBytes = UpdateManifestVerifier::MaximumEnvelopeBytes;
    QString manifestBaseUrl;
    std::function<bool(const QUrl &)> urlValidator;
    QList<TrustedUpdateManifestKey> trustedKeys;
};

class GitHubReleaseUpdateProvider : public IUpdateProvider
{
    Q_OBJECT

  public:
    explicit GitHubReleaseUpdateProvider(QObject *parent = nullptr);
    GitHubReleaseUpdateProvider(QNetworkAccessManager *networkAccessManager, GitHubReleaseUpdateProviderOptions options,
                                QObject *parent = nullptr);

    void fetchManifest(const QString &channel, QObject *context, FetchManifestCallback completion) override;

    static std::optional<UpdateManifest> parseManifestBytes(const QByteArray &data, QString *errorMessage = nullptr);
    static std::optional<UpdateManifest> parseManifestBytes(const QByteArray &data,
                                                            const QList<TrustedUpdateManifestKey> &trustedKeys,
                                                            QString *errorMessage = nullptr);
    static QString manifestUrlForChannel(const QString &channel);

  private:
    QString configuredManifestUrl(const QString &channel) const;

    QNetworkAccessManager *m_networkAccessManager = nullptr;
    GitHubReleaseUpdateProviderOptions m_options;
};
