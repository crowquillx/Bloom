#pragma once

#include <QNetworkAccessManager>

#include "IUpdateProvider.h"

class GitHubReleaseUpdateProvider : public IUpdateProvider
{
    Q_OBJECT

public:
    explicit GitHubReleaseUpdateProvider(QObject *parent = nullptr);

    void fetchManifest(const QString &channel,
                       QObject *context,
                       std::function<void(std::optional<UpdateManifest>, const QString &)> completion) override;

    static std::optional<UpdateManifest> parseManifestBytes(const QByteArray &data, QString *errorMessage = nullptr);
    static QString manifestUrlForChannel(const QString &channel);

private:
    QNetworkAccessManager m_networkAccessManager;
};
