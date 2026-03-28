#include "WindowsNsisUpdateApplier.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace {

QString bloomExeDirectory()
{
    return QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
}

QString extractUninstallLocation(const QString &registryPath)
{
    QSettings settings(registryPath, QSettings::NativeFormat);
    const QString displayName = settings.value(QStringLiteral("DisplayName")).toString().trimmed();
    if (displayName.compare(QStringLiteral("Bloom"), Qt::CaseInsensitive) != 0) {
        return QString();
    }

    const QString installLocation = settings.value(QStringLiteral("InstallLocation")).toString().trimmed();
    const QString uninstallString = settings.value(QStringLiteral("UninstallString")).toString().trimmed();
    if (installLocation.isEmpty() || uninstallString.isEmpty()) {
        return QString();
    }

    return installLocation;
}

QString registryInstallLocation()
{
#ifdef Q_OS_WIN
    const QStringList uninstallRoots{
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Bloom"),
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Bloom")
    };

    for (const QString &root : uninstallRoots) {
        const QString location = extractUninstallLocation(root);
        if (!location.isEmpty()) {
            return location;
        }
    }
#endif
    return QString();
}

QString sanitizedInstallerFilename(const QString &filename)
{
    const QString trimmed = filename.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('/')) || trimmed.contains(QLatin1Char('\\'))
        || trimmed.contains(QStringLiteral(".."))) {
        return QString();
    }

    const QString sanitized = QFileInfo(trimmed).fileName();
    if (sanitized.isEmpty() || sanitized != trimmed) {
        return QString();
    }

    return sanitized;
}

} // namespace

WindowsNsisUpdateApplier::WindowsNsisUpdateApplier(QObject *parent)
    : IUpdateApplier(parent)
{
}

WindowsNsisUpdateApplier::~WindowsNsisUpdateApplier()
{
    resetDownloadState();
}

InstallEligibility WindowsNsisUpdateApplier::detectEligibility() const
{
#ifdef Q_OS_WIN
    const QString installLocation = registryInstallLocation();
    if (installLocation.isEmpty()) {
        return {UpdateApplySupport::NotifyOnly,
                tr("Automatic install is unavailable for this build.")};
    }

    const QString currentDir = normalizedPath(bloomExeDirectory());
    const QString registeredDir = normalizedPath(installLocation);
    const QString uninstallerPath = QDir(registeredDir).filePath(QStringLiteral("Uninstall.exe"));
    if (currentDir == registeredDir && QFileInfo::exists(uninstallerPath)) {
        return {UpdateApplySupport::Supported, QString()};
    }

    return {UpdateApplySupport::NotifyOnly,
            tr("Bloom is not running from its registered installer location.")};
#else
    return {UpdateApplySupport::NotifyOnly,
            tr("Automatic install is only supported for Windows installer builds.")};
#endif
}

void WindowsNsisUpdateApplier::downloadAndInstall(const UpdateManifest &manifest, const QString &channel)
{
    resetDownloadState();

    if (!manifest.installer.isValid()) {
        emit installFinished(false, tr("No installer asset is available for this update."));
        return;
    }

    const QString appLocalData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir updatesDir(appLocalData);
    if (!updatesDir.mkpath(QStringLiteral("updates/%1").arg(channel))) {
        emit installFinished(false, tr("Failed to create the updater download directory."));
        return;
    }

    m_pendingManifest = manifest;
    m_pendingChannel = channel.trimmed().isEmpty() ? QStringLiteral("stable") : channel.trimmed();
    const QString installerFilename = sanitizedInstallerFilename(manifest.installer.filename);
    if (installerFilename.isEmpty()) {
        emit installFinished(false, tr("Update manifest specified an invalid installer filename."));
        return;
    }
    m_pendingFilePath = updatesDir.filePath(QStringLiteral("updates/%1/%2")
                                                .arg(m_pendingChannel,
                                                     installerFilename));

    if (QFileInfo::exists(m_pendingFilePath)) {
        QFile::remove(m_pendingFilePath);
    }

    m_outputFile.setFileName(m_pendingFilePath);
    if (!m_outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit installFinished(false, tr("Failed to open the updater download target."));
        return;
    }

    QNetworkRequest request(QUrl(manifest.installer.url));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Bloom-Updater"));
    m_reply = m_networkAccessManager.get(request);

    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_reply && m_outputFile.isOpen()) {
            const QByteArray chunk = m_reply->readAll();
            const qint64 bytesWritten = m_outputFile.write(chunk);
            if (bytesWritten != chunk.size()) {
                discardPartialDownload();
                finishWithError(tr("Failed to write the downloaded update installer to disk."));
            }
        }
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, &WindowsNsisUpdateApplier::downloadProgressChanged);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply) {
            finishWithError(tr("The update download was interrupted."));
            return;
        }

        if (m_outputFile.isOpen()) {
            const QByteArray remainingBytes = m_reply->readAll();
            const qint64 bytesWritten = m_outputFile.write(remainingBytes);
            if (bytesWritten != remainingBytes.size()) {
                discardPartialDownload();
                finishWithError(tr("Failed to write the downloaded update installer to disk."));
                return;
            }
            if (!m_outputFile.flush()) {
                discardPartialDownload();
                finishWithError(tr("Failed to finalize the downloaded update installer on disk."));
                return;
            }
            m_outputFile.close();
        }

        const int statusCode = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString networkError = m_reply->errorString();
        const QNetworkReply::NetworkError errorCode = m_reply->error();

        if (errorCode != QNetworkReply::NoError) {
            discardPartialDownload();
            finishWithError(tr("Failed to download the update installer: %1").arg(networkError));
            return;
        }

        if (statusCode != 200) {
            discardPartialDownload();
            finishWithError(tr("Failed to download the update installer (HTTP %1).").arg(statusCode));
            return;
        }

        if (m_pendingManifest.installer.sha256.trimmed().isEmpty()) {
            discardPartialDownload();
            finishWithError(tr("Update manifest is missing an installer checksum; cannot verify download."));
            return;
        }

        QFile file(m_pendingFilePath);
        if (!file.open(QIODevice::ReadOnly)) {
            discardPartialDownload();
            finishWithError(tr("Downloaded installer could not be verified."));
            return;
        }

        QCryptographicHash hash(QCryptographicHash::Sha256);
        constexpr qint64 chunkSize = 64 * 1024;
        while (!file.atEnd()) {
            const QByteArray chunk = file.read(chunkSize);
            if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
                discardPartialDownload();
                finishWithError(tr("Downloaded installer could not be verified."));
                return;
            }
            hash.addData(chunk);
        }

        const QByteArray digest = hash.result().toHex().toLower();
        const QByteArray expected = m_pendingManifest.installer.sha256.toUtf8().trimmed().toLower();
        if (digest != expected) {
            discardPartialDownload();
            finishWithError(tr("Downloaded installer failed checksum verification."));
            return;
        }

        if (m_reply) {
            m_reply->deleteLater();
            m_reply = nullptr;
        }

#ifdef Q_OS_WIN
        const bool launched = QProcess::startDetached(m_pendingFilePath, {QStringLiteral("/S")});
        if (!launched) {
            finishWithError(tr("Bloom downloaded the update but could not launch the installer."));
            return;
        }

        emit installFinished(true, tr("Launching Bloom installer update."));
#else
        finishWithError(tr("Automatic install is only supported for Windows installer builds."));
#endif
    });
}

void WindowsNsisUpdateApplier::resetDownloadState()
{
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    if (m_outputFile.isOpen()) {
        m_outputFile.close();
    }
}

void WindowsNsisUpdateApplier::discardPartialDownload()
{
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    if (m_outputFile.isOpen()) {
        m_outputFile.close();
    }

    if (!m_pendingFilePath.trimmed().isEmpty()) {
        QFile::remove(m_pendingFilePath);
    }
}

void WindowsNsisUpdateApplier::finishWithError(const QString &message)
{
    resetDownloadState();
    emit installFinished(false, message);
}

QString WindowsNsisUpdateApplier::normalizedPath(const QString &path)
{
    QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(path.trimmed()));
#ifdef Q_OS_WIN
    normalized = normalized.toLower();
#endif
    return normalized;
}
