#pragma once

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>

#include <functional>
#include <memory>

#include "IUpdateApplier.h"

class QNetworkReply;

struct UpdateDownloadOptions
{
    int totalDeadlineMs = 10 * 60 * 1000;
    int transferTimeoutMs = 30000;
    int maximumRedirects = 5;
    qint64 maximumBytes = 1024LL * 1024 * 1024;
    std::function<bool(const QUrl &, bool initialRequest)> urlValidator;
};

class WindowsNsisUpdateApplier : public IUpdateApplier
{
    Q_OBJECT

  public:
    explicit WindowsNsisUpdateApplier(QObject *parent = nullptr);
    WindowsNsisUpdateApplier(QNetworkAccessManager *networkAccessManager, UpdateDownloadOptions options,
                             QObject *parent = nullptr);
    ~WindowsNsisUpdateApplier() override;

    InstallEligibility detectEligibility() const override;
    void downloadAndInstall(const UpdateManifest &manifest, const QString &channel) override;

  private:
    bool isAllowedUrl(const QUrl &url, bool initialRequest) const;
    void beginRequest(const QUrl &url, bool initialRequest);
    void consumeReadyData();
    void onReplyFinished();
    void finalizeVerifiedDownload();
    void cleanupObsoleteDownloads(const QString &directoryPath, const QString &currentFilename);
    void resetDownloadState();
    void finishWithError(const QString &message);
    void discardPartialDownload();
    void removeCommittedDownload();
    static QString normalizedPath(const QString &path);

    QPointer<QNetworkAccessManager> m_networkAccessManager;
    UpdateDownloadOptions m_options;
    QPointer<QNetworkReply> m_reply;
    QSaveFile m_outputFile;
    QTimer m_deadline;
    std::unique_ptr<QCryptographicHash> m_hash;
    UpdateManifest m_pendingManifest;
    QString m_pendingChannel;
    QString m_pendingFilePath;
    QUrl m_currentUrl;
    qint64 m_bytesReceived = 0;
    qint64 m_expectedBytes = -1;
    int m_redirects = 0;
    bool m_committed = false;
    bool m_finishing = false;
};
