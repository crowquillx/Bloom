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
    m_pendingFilePath = updatesDir.filePath(QStringLiteral("updates/%1/%2")
                                                .arg(m_pendingChannel,
                                                     manifest.installer.filename));

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
            m_outputFile.write(m_reply->readAll());
        }
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, &WindowsNsisUpdateApplier::downloadProgressChanged);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply) {
            finishWithError(tr("The update download was interrupted."));
            return;
        }

        if (m_outputFile.isOpen()) {
            m_outputFile.write(m_reply->readAll());
            m_outputFile.flush();
            m_outputFile.close();
        }

        const int statusCode = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString networkError = m_reply->errorString();
        const QNetworkReply::NetworkError errorCode = m_reply->error();
        m_reply->deleteLater();
        m_reply = nullptr;

        if (errorCode != QNetworkReply::NoError) {
            finishWithError(tr("Failed to download the update installer: %1").arg(networkError));
            return;
        }

        if (statusCode != 200) {
            finishWithError(tr("Failed to download the update installer (HTTP %1).").arg(statusCode));
            return;
        }

        if (!m_pendingManifest.installer.sha256.trimmed().isEmpty()) {
            QFile file(m_pendingFilePath);
            if (!file.open(QIODevice::ReadOnly)) {
                finishWithError(tr("Downloaded installer could not be verified."));
                return;
            }

            const QByteArray digest = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex().toLower();
            const QByteArray expected = m_pendingManifest.installer.sha256.toUtf8().trimmed().toLower();
            if (digest != expected) {
                finishWithError(tr("Downloaded installer failed checksum verification."));
                return;
            }
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
