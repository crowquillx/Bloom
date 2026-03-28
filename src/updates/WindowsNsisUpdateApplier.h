#pragma once

#include <QFile>
#include <QNetworkAccessManager>
#include <QPointer>

#include "IUpdateApplier.h"

class QNetworkReply;

class WindowsNsisUpdateApplier : public IUpdateApplier
{
    Q_OBJECT

public:
    explicit WindowsNsisUpdateApplier(QObject *parent = nullptr);
    ~WindowsNsisUpdateApplier() override;

    InstallEligibility detectEligibility() const override;
    void downloadAndInstall(const UpdateManifest &manifest, const QString &channel) override;

private:
    void resetDownloadState();
    void finishWithError(const QString &message);
    void discardPartialDownload();
    static QString normalizedPath(const QString &path);

    QNetworkAccessManager m_networkAccessManager;
    QPointer<QNetworkReply> m_reply;
    QFile m_outputFile;
    UpdateManifest m_pendingManifest;
    QString m_pendingChannel;
    QString m_pendingFilePath;
};
