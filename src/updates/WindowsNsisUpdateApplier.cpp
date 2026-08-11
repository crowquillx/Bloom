#include "WindowsNsisUpdateApplier.h"

#include "UpdateNetworkPolicy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <shellapi.h>
#include <softpub.h>
#include <windows.h>
#include <wintrust.h>
#endif

#include <algorithm>
#include <utility>

namespace
{

QString bloomExeDirectory()
{
    return QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
}

QString extractUninstallLocation(const QString &registryPath)
{
    QSettings settings(registryPath, QSettings::NativeFormat);
    const QString displayName = settings.value(QStringLiteral("DisplayName")).toString().trimmed();
    if (displayName.compare(QStringLiteral("Bloom"), Qt::CaseInsensitive) != 0)
    {
        return {};
    }

    const QString installLocation = settings.value(QStringLiteral("InstallLocation")).toString().trimmed();
    const QString uninstallString = settings.value(QStringLiteral("UninstallString")).toString().trimmed();
    if (installLocation.isEmpty() || uninstallString.isEmpty())
    {
        return {};
    }
    return installLocation;
}

QString registryInstallLocation()
{
#ifdef Q_OS_WIN
    const QStringList uninstallRoots{
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\"
                       "CurrentVersion\\Uninstall\\Bloom"),
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\"
                       "CurrentVersion\\Uninstall\\Bloom"),
    };
    for (const QString &root : uninstallRoots)
    {
        const QString location = extractUninstallLocation(root);
        if (!location.isEmpty())
        {
            return location;
        }
    }
#endif
    return {};
}

QString sanitizedInstallerFilename(const QString &filename)
{
    const QString trimmed = filename.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('/')) || trimmed.contains(QLatin1Char('\\')) ||
        trimmed.contains(QStringLiteral("..")))
    {
        return {};
    }
    const QString sanitized = QFileInfo(trimmed).fileName();
    return sanitized.isEmpty() || sanitized != trimmed ? QString() : sanitized;
}

#ifdef Q_OS_WIN
QString windowsErrorMessage(DWORD errorCode)
{
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString message;
    if (length > 0 && buffer)
    {
        message = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
    }
    if (buffer)
    {
        LocalFree(buffer);
    }
    return message.isEmpty() ? QStringLiteral("Windows error %1").arg(errorCode)
                             : QStringLiteral("%1 (Windows error %2)").arg(message).arg(errorCode);
}

enum class AuthenticodeResult
{
    Unsigned,
    Valid,
    Invalid,
};

struct AuthenticodeVerification
{
    AuthenticodeResult result = AuthenticodeResult::Invalid;
    QString publisher;
};

QString authenticodePublisher(const std::wstring &nativePath)
{
    HCERTSTORE certificateStore = nullptr;
    HCRYPTMSG message = nullptr;
    DWORD encoding = 0;
    DWORD contentType = 0;
    DWORD formatType = 0;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, nativePath.c_str(), CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0, &encoding, &contentType, &formatType, &certificateStore,
                          &message, nullptr))
    {
        return {};
    }

    DWORD signerSize = 0;
    QString publisher;
    if (CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &signerSize) && signerSize > 0)
    {
        QByteArray signerBuffer(static_cast<qsizetype>(signerSize), '\0');
        if (CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, signerBuffer.data(), &signerSize))
        {
            const auto *signer = reinterpret_cast<const CMSG_SIGNER_INFO *>(signerBuffer.constData());
            CERT_INFO certificateInfo{};
            certificateInfo.Issuer = signer->Issuer;
            certificateInfo.SerialNumber = signer->SerialNumber;
            PCCERT_CONTEXT certificate = CertFindCertificateInStore(certificateStore, encoding, 0,
                                                                    CERT_FIND_SUBJECT_CERT, &certificateInfo, nullptr);
            if (certificate)
            {
                const DWORD characters =
                    CertGetNameStringW(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nullptr, 0);
                if (characters > 1)
                {
                    std::wstring name(characters, L'\0');
                    CertGetNameStringW(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name.data(), characters);
                    publisher = QString::fromWCharArray(name.c_str(), static_cast<qsizetype>(characters - 1));
                }
                CertFreeCertificateContext(certificate);
            }
        }
    }
    if (message)
    {
        CryptMsgClose(message);
    }
    if (certificateStore)
    {
        CertCloseStore(certificateStore, 0);
    }
    return publisher;
}

AuthenticodeVerification verifyAuthenticode(const QString &path)
{
    const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = nativePath.c_str();

    WINTRUST_DATA trustData{};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(nullptr, &policy, &trustData);
    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &trustData);

    if (result == ERROR_SUCCESS)
    {
        return {AuthenticodeResult::Valid, authenticodePublisher(nativePath)};
    }
    if (result == TRUST_E_NOSIGNATURE || result == TRUST_E_SUBJECT_FORM_UNKNOWN || result == TRUST_E_PROVIDER_UNKNOWN)
    {
        return {AuthenticodeResult::Unsigned, {}};
    }
    return {AuthenticodeResult::Invalid, {}};
}
#endif

} // namespace

WindowsNsisUpdateApplier::WindowsNsisUpdateApplier(QObject *parent)
    : IUpdateApplier(parent), m_networkAccessManager(new QNetworkAccessManager(this))
{
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this]() { finishWithError(tr("Update download timed out.")); });
}

WindowsNsisUpdateApplier::WindowsNsisUpdateApplier(QNetworkAccessManager *networkAccessManager,
                                                   UpdateDownloadOptions options, QObject *parent)
    : IUpdateApplier(parent), m_networkAccessManager(networkAccessManager), m_options(std::move(options))
{
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this]() { finishWithError(tr("Update download timed out.")); });
}

WindowsNsisUpdateApplier::~WindowsNsisUpdateApplier()
{
    discardPartialDownload();
}

InstallEligibility WindowsNsisUpdateApplier::detectEligibility() const
{
#ifdef Q_OS_WIN
    const QString installLocation = registryInstallLocation();
    if (installLocation.isEmpty())
    {
        return {UpdateApplySupport::NotifyOnly, tr("Automatic install is unavailable for this build.")};
    }

    const QString currentDir = normalizedPath(bloomExeDirectory());
    const QString registeredDir = normalizedPath(installLocation);
    const QString uninstallerPath = QDir(registeredDir).filePath(QStringLiteral("Uninstall.exe"));
    if (currentDir == registeredDir && QFileInfo::exists(uninstallerPath))
    {
        return {UpdateApplySupport::Supported, {}};
    }
    return {UpdateApplySupport::NotifyOnly, tr("Bloom is not running from its registered installer location.")};
#else
    return {UpdateApplySupport::NotifyOnly, tr("Automatic install is only supported for Windows installer builds.")};
#endif
}

void WindowsNsisUpdateApplier::downloadAndInstall(const UpdateManifest &manifest, const QString &channel)
{
    discardPartialDownload();
    resetDownloadState();

    if (!manifest.installer.isValid())
    {
        emit installFinished(false, tr("No installer asset is available for this update."));
        return;
    }

    const QUrl installerUrl(manifest.installer.url);
    if (!isAllowedUrl(installerUrl, true))
    {
        emit installFinished(false, tr("Update installer URL is not an allowed HTTPS origin."));
        return;
    }

    const QString installerFilename = sanitizedInstallerFilename(manifest.installer.filename);
    if (installerFilename.isEmpty())
    {
        emit installFinished(false, tr("Update manifest specified an invalid installer filename."));
        return;
    }

    const QString normalizedChannel = channel.trimmed().compare(QStringLiteral("dev"), Qt::CaseInsensitive) == 0
                                          ? QStringLiteral("dev")
                                          : QStringLiteral("stable");
    QDir updatesDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    const QString relativeDirectory = QStringLiteral("updates/%1").arg(normalizedChannel);
    if (!updatesDir.mkpath(relativeDirectory))
    {
        emit installFinished(false, tr("Failed to create the updater download directory."));
        return;
    }

    const QString downloadDirectory = updatesDir.filePath(relativeDirectory);
    cleanupObsoleteDownloads(downloadDirectory, installerFilename);

    m_pendingManifest = manifest;
    m_pendingChannel = normalizedChannel;
    m_pendingFilePath = QDir(downloadDirectory).filePath(installerFilename);
    m_outputFile.setFileName(m_pendingFilePath);
    m_outputFile.setDirectWriteFallback(false);
    if (!m_outputFile.open(QIODevice::WriteOnly))
    {
        emit installFinished(false, tr("Failed to open the updater download target."));
        return;
    }

    m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_options.maximumBytes = std::max<qint64>(1, m_options.maximumBytes);
    m_bytesReceived = 0;
    m_expectedBytes = -1;
    m_redirects = 0;
    m_committed = false;
    m_finishing = false;
    m_deadline.start(std::max(1, m_options.totalDeadlineMs));
    beginRequest(installerUrl, true);
}

bool WindowsNsisUpdateApplier::isAllowedUrl(const QUrl &url, bool initialRequest) const
{
    return m_options.urlValidator ? m_options.urlValidator(url, initialRequest)
                                  : UpdateNetworkPolicy::isAllowedAssetUrl(url, initialRequest);
}

void WindowsNsisUpdateApplier::beginRequest(const QUrl &url, bool initialRequest)
{
    if (!m_networkAccessManager || !isAllowedUrl(url, initialRequest))
    {
        finishWithError(tr("Update installer redirect was rejected."));
        return;
    }

    m_currentUrl = url;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Bloom-Updater"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(std::max(1, m_options.transferTimeoutMs));
    m_reply = m_networkAccessManager->get(request);
    m_reply->setReadBufferSize(std::min<qint64>(m_options.maximumBytes, 1024 * 1024));
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() { consumeReadyData(); });
    connect(m_reply, &QNetworkReply::finished, this, [this]() { onReplyFinished(); });
}

void WindowsNsisUpdateApplier::consumeReadyData()
{
    if (!m_reply || m_finishing)
    {
        return;
    }
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400)
    {
        m_reply->readAll();
        return;
    }
    if (status != 200)
    {
        return;
    }

    const QVariant lengthHeader = m_reply->header(QNetworkRequest::ContentLengthHeader);
    if (lengthHeader.isValid())
    {
        m_expectedBytes = lengthHeader.toLongLong();
        if (m_expectedBytes < 0 || m_expectedBytes > m_options.maximumBytes)
        {
            finishWithError(tr("Update installer exceeds the allowed size."));
            return;
        }
    }

    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty())
    {
        return;
    }
    if (m_bytesReceived > m_options.maximumBytes - chunk.size())
    {
        finishWithError(tr("Update installer exceeds the allowed size."));
        return;
    }
    if (m_outputFile.write(chunk) != chunk.size())
    {
        finishWithError(tr("Failed to write the downloaded update installer to disk."));
        return;
    }
    m_hash->addData(chunk);
    m_bytesReceived += chunk.size();
    emit downloadProgressChanged(m_bytesReceived, m_expectedBytes);
}

void WindowsNsisUpdateApplier::onReplyFinished()
{
    if (!m_reply || m_finishing)
    {
        return;
    }
    consumeReadyData();
    if (!m_reply || m_finishing)
    {
        return;
    }

    QNetworkReply *finishedReply = m_reply;
    m_reply = nullptr;
    const int status = finishedReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QUrl redirect = UpdateNetworkPolicy::resolvedRedirect(
        finishedReply->url(), finishedReply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
    const auto errorCode = finishedReply->error();
    const QString errorText = finishedReply->errorString();
    const QVariant lengthHeader = finishedReply->header(QNetworkRequest::ContentLengthHeader);
    finishedReply->deleteLater();

    if (status >= 300 && status < 400)
    {
        if (!redirect.isValid() || !isAllowedUrl(redirect, false))
        {
            finishWithError(tr("Update installer redirect was rejected."));
            return;
        }
        if (++m_redirects > std::max(0, m_options.maximumRedirects))
        {
            finishWithError(tr("Update installer used too many redirects."));
            return;
        }
        beginRequest(redirect, false);
        return;
    }
    if (status == 200 && lengthHeader.isValid() &&
        (lengthHeader.toLongLong() > m_options.maximumBytes ||
         (lengthHeader.toLongLong() >= 0 && lengthHeader.toLongLong() != m_bytesReceived)))
    {
        finishWithError(lengthHeader.toLongLong() > m_options.maximumBytes
                            ? tr("Update installer exceeds the allowed size.")
                            : tr("Update installer download was truncated."));
        return;
    }
    if (errorCode != QNetworkReply::NoError)
    {
        finishWithError(tr("Failed to download the update installer: %1").arg(errorText));
        return;
    }
    if (status != 200)
    {
        finishWithError(tr("Failed to download the update installer (HTTP %1).").arg(status));
        return;
    }
    finalizeVerifiedDownload();
}

void WindowsNsisUpdateApplier::finalizeVerifiedDownload()
{
    if (!m_hash || m_pendingManifest.installer.sha256.trimmed().isEmpty())
    {
        finishWithError(tr("Update manifest is missing an installer checksum; "
                           "cannot verify download."));
        return;
    }
    const QByteArray digest = m_hash->result().toHex().toLower();
    const QByteArray expected = m_pendingManifest.installer.sha256.toUtf8().trimmed().toLower();
    if (digest != expected)
    {
        finishWithError(tr("Downloaded installer failed checksum verification."));
        return;
    }
    if (!m_outputFile.commit())
    {
        finishWithError(tr("Failed to atomically finalize the downloaded update installer."));
        return;
    }
    m_committed = true;
    m_deadline.stop();

#ifdef Q_OS_WIN
    const AuthenticodeVerification signature = verifyAuthenticode(m_pendingFilePath);
    const QString expectedPublisher = QString::fromUtf8(BLOOM_UPDATE_AUTHENTICODE_PUBLISHER).trimmed();
    if (signature.result == AuthenticodeResult::Invalid ||
        (!expectedPublisher.isEmpty() && (signature.result != AuthenticodeResult::Valid ||
                                          signature.publisher.compare(expectedPublisher, Qt::CaseInsensitive) != 0)))
    {
        removeCommittedDownload();
        finishWithError(expectedPublisher.isEmpty() ? tr("Downloaded installer has an invalid Authenticode signature.")
                                                    : tr("Downloaded installer is not signed by the expected "
                                                         "publisher."));
        return;
    }

    const QString installLocation = registryInstallLocation();
    const QString currentDir = normalizedPath(bloomExeDirectory());
    const QString registeredDir = normalizedPath(installLocation);
    const QString uninstallerPath = QDir(registeredDir).filePath(QStringLiteral("Uninstall.exe"));
    if (installLocation.isEmpty() || currentDir != registeredDir || !QFileInfo::exists(uninstallerPath))
    {
        finishWithError(tr("Bloom downloaded the update, but this build is no "
                           "longer eligible for automatic install."));
        return;
    }

    const QString parameters = QStringLiteral("/S /D=%1").arg(QDir::toNativeSeparators(installLocation));
    const std::wstring installerPath = QDir::toNativeSeparators(m_pendingFilePath).toStdWString();
    const std::wstring parameterString = parameters.toStdWString();

    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.lpVerb = L"runas";
    executeInfo.lpFile = installerPath.c_str();
    executeInfo.lpParameters = parameterString.c_str();
    executeInfo.nShow = SW_SHOWNORMAL;

    SetLastError(ERROR_SUCCESS);
    const bool launched = ShellExecuteExW(&executeInfo) != FALSE;
    const DWORD launchError = launched ? ERROR_SUCCESS : GetLastError();
    if (executeInfo.hProcess)
    {
        CloseHandle(executeInfo.hProcess);
    }
    if (!launched)
    {
        finishWithError(tr("Bloom downloaded the update but could not launch the "
                           "elevated installer: %1")
                            .arg(windowsErrorMessage(launchError)));
        return;
    }
    resetDownloadState();
    emit installFinished(true, tr("Launching Bloom installer update."));
#else
    finishWithError(tr("Automatic install is only supported for Windows installer builds."));
#endif
}

void WindowsNsisUpdateApplier::cleanupObsoleteDownloads(const QString &directoryPath, const QString &currentFilename)
{
    QDir directory(directoryPath);
    const QFileInfoList entries = directory.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &entry : entries)
    {
        if (entry.fileName() != currentFilename)
        {
            QFile::remove(entry.absoluteFilePath());
        }
    }
}

void WindowsNsisUpdateApplier::resetDownloadState()
{
    m_deadline.stop();
    if (m_reply)
    {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_hash.reset();
    m_finishing = false;
}

void WindowsNsisUpdateApplier::discardPartialDownload()
{
    m_finishing = true;
    m_deadline.stop();
    if (m_reply)
    {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_outputFile.isOpen())
    {
        m_outputFile.cancelWriting();
    }
    m_hash.reset();
}

void WindowsNsisUpdateApplier::removeCommittedDownload()
{
    if (m_committed && !m_pendingFilePath.isEmpty())
    {
        QFile::remove(m_pendingFilePath);
        m_committed = false;
    }
}

void WindowsNsisUpdateApplier::finishWithError(const QString &message)
{
    if (m_finishing)
    {
        return;
    }
    discardPartialDownload();
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
