#include "UpdateService.h"

#include "GitHubReleaseUpdateProvider.h"
#include "IUpdateApplier.h"
#include "config/version.h"
#include "player/PlayerController.h"
#include "utils/ConfigManager.h"
#include "updates/WindowsNsisUpdateApplier.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

namespace {

QVersionNumber safeVersion(const QString &version)
{
    QVersionNumber parsed = QVersionNumber::fromString(version.trimmed());
    if (parsed.isNull()) {
        return QVersionNumber(0, 0, 0);
    }
    return parsed;
}

bool playbackIsActive(const PlayerController *playerController)
{
    if (!playerController) {
        return false;
    }

    return playerController->isPlaybackActive();
}

} // namespace

UpdateService::UpdateService(ConfigManager *configManager,
                             PlayerController *playerController,
                             IUpdateProvider *provider,
                             IUpdateApplier *applier,
                             QObject *parent)
    : QObject(parent)
    , m_configManager(configManager)
    , m_playerController(playerController)
    , m_provider(provider ? provider : new GitHubReleaseUpdateProvider(this))
    , m_applier(applier ? applier : new WindowsNsisUpdateApplier(this))
{
    if (m_provider && !m_provider->parent()) {
        m_provider->setParent(this);
    }
    if (m_applier && !m_applier->parent()) {
        m_applier->setParent(this);
    }

    if (m_applier) {
        m_installEligibility = m_applier->detectEligibility();
        m_applySupported = m_installEligibility.support == UpdateApplySupport::Supported;

        connect(m_applier, &IUpdateApplier::downloadProgressChanged, this, [this](qint64 bytesReceived, qint64 bytesTotal) {
            if (bytesTotal > 0) {
                m_downloadProgress = static_cast<qreal>(bytesReceived) / static_cast<qreal>(bytesTotal);
            } else {
                m_downloadProgress = 0.0;
            }
            emit downloadProgressChanged();
            emitStateChanged();
        });

        connect(m_applier, &IUpdateApplier::installFinished, this, [this](bool success, const QString &message) {
            m_downloadInProgress = false;
            if (success) {
                setStatus(message);
                scheduleAutoQuitIfAllowed();
            } else {
                setError(message);
                emit updateError(message);
            }
            emitStateChanged();
        });
    }
}

QString UpdateService::currentVersion() const
{
    return QString::fromUtf8(BLOOM_VERSION);
}

QString UpdateService::currentChannel() const
{
    return normalizedChannel(m_configManager ? m_configManager->getUpdateChannel() : QString::fromUtf8(BLOOM_BUILD_CHANNEL));
}

QString UpdateService::downloadPageUrl() const
{
    if (!m_availableManifest.releaseTag.trimmed().isEmpty()) {
        return QStringLiteral("https://github.com/crowquillx/Bloom/releases/tag/%1").arg(m_availableManifest.releaseTag);
    }
    return QStringLiteral("https://github.com/crowquillx/Bloom/releases");
}

void UpdateService::performStartupCheck()
{
    if (m_startupCheckPerformed) {
        return;
    }
    m_startupCheckPerformed = true;

    if (!m_configManager || !m_configManager->getAutoUpdateCheckEnabled()) {
        return;
    }

    if (shouldThrottleStartupCheck()) {
        setStatus(tr("Update check skipped recently."));
        return;
    }

    startCheck(CheckOrigin::Startup);
}

void UpdateService::checkForUpdates(bool manual)
{
    startCheck(manual ? CheckOrigin::Manual : CheckOrigin::Startup);
}

void UpdateService::downloadAndInstallUpdate()
{
    if (!m_updateAvailable) {
        const QString message = tr("No update is currently available.");
        setError(message);
        emit updateError(message);
        return;
    }

    if (!m_applySupported || !m_applier) {
        openUpdateDownloadPage();
        return;
    }

    if (!m_availableManifest.installer.isValid()) {
        const QString message = tr("This update does not provide an installer download.");
        setError(message);
        emit updateError(message);
        return;
    }

    m_downloadInProgress = true;
    m_downloadProgress = 0.0;
    setError(QString());
    setStatus(tr("Downloading update installer..."));
    emit downloadProgressChanged();
    emitStateChanged();
    m_applier->downloadAndInstall(m_availableManifest, currentChannel());
}

void UpdateService::dismissStartupPopup()
{
    setShouldShowStartupPopup(false);
    if (m_configManager && m_updateAvailable) {
        m_configManager->setSkippedUpdateVersion(availabilityMarker(m_availableManifest));
    }
}

void UpdateService::openUpdateDownloadPage()
{
    const QString url = downloadPageUrl();
    if (!url.isEmpty()) {
        QDesktopServices::openUrl(QUrl(url));
    }
}

void UpdateService::openInstallerAsset()
{
    if (!m_availableManifest.installer.url.trimmed().isEmpty()) {
        QDesktopServices::openUrl(QUrl(m_availableManifest.installer.url));
    } else {
        openUpdateDownloadPage();
    }
}

void UpdateService::openPortableAsset()
{
    if (!m_availableManifest.portable.url.trimmed().isEmpty()) {
        QDesktopServices::openUrl(QUrl(m_availableManifest.portable.url));
    } else {
        openUpdateDownloadPage();
    }
}

void UpdateService::setChannel(const QString &channel)
{
    if (!m_configManager) {
        return;
    }

    const QString normalized = normalizedChannel(channel);
    if (normalized == currentChannel()) {
        return;
    }

    if (m_checking) {
        m_pendingChannel = normalized;
        return;
    }

    m_configManager->setUpdateChannel(normalized);
    emit channelChanged();
    emitStateChanged();
    startCheck(CheckOrigin::Manual);
}

QString UpdateService::normalizedChannel(const QString &channel) const
{
    return channel.trimmed().compare(QStringLiteral("dev"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("dev")
        : QStringLiteral("stable");
}

QString UpdateService::currentBuildChannel() const
{
    return normalizedChannel(QString::fromUtf8(BLOOM_BUILD_CHANNEL));
}

QString UpdateService::currentBuildId() const
{
    return QString::fromUtf8(BLOOM_BUILD_ID).trimmed();
}

void UpdateService::startCheck(CheckOrigin origin)
{
    if (!m_provider || m_checking) {
        return;
    }

    if (!m_applier) {
        m_installEligibility = {UpdateApplySupport::NotifyOnly,
                                tr("Automatic install is unavailable for this build.")};
        m_applySupported = false;
    } else {
        m_installEligibility = m_applier->detectEligibility();
        m_applySupported = m_installEligibility.support == UpdateApplySupport::Supported;
    }

    m_checking = true;
    m_downloadProgress = 0.0;
    setShouldShowStartupPopup(false);
    setError(QString());
    setStatus(tr("Checking for updates..."));
    emit downloadProgressChanged();
    emitStateChanged();

    const QString channel = currentChannel();
    QPointer<UpdateService> guard(this);
    m_provider->fetchManifest(channel, this, [guard, origin](std::optional<UpdateManifest> manifest, const QString &errorMessage) {
        if (!guard) {
            return;
        }
        guard->finishCheck(std::move(manifest), errorMessage, origin);
    });
}

void UpdateService::finishCheck(std::optional<UpdateManifest> manifest,
                                const QString &errorMessage,
                                CheckOrigin origin)
{
    m_checking = false;
    if (m_configManager) {
        m_configManager->setLastUpdateCheckAt(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    }

    if (!errorMessage.trimmed().isEmpty() || !manifest.has_value()) {
        setUpdateAvailableState(false);
        m_availableManifest = UpdateManifest();
        setError(errorMessage.trimmed().isEmpty() ? tr("Update information was unavailable.") : errorMessage);
        if (origin == CheckOrigin::Manual) {
            emit updateError(m_errorMessage);
        }
        emitStateChanged();
        applyPendingChannelIfNeeded();
        return;
    }

    m_availableManifest = manifest.value();

    const bool available = manifestRepresentsNewerVersion(m_availableManifest);
    setUpdateAvailableState(available);

    if (!available) {
        setStatus(tr("Bloom is up to date."));
        emitStateChanged();
        applyPendingChannelIfNeeded();
        return;
    }

    if (m_applySupported) {
        setStatus(tr("Update %1 is available.").arg(m_availableManifest.version));
    } else {
        const QString reason = m_installEligibility.reason.trimmed().isEmpty()
            ? tr("Automatic install is unavailable for this build.")
            : m_installEligibility.reason;
        setStatus(reason);
    }

    const bool popupAllowed = origin == CheckOrigin::Startup
        && !playbackIsActive(m_playerController)
        && availabilityMarker(m_availableManifest) != (m_configManager ? m_configManager->getSkippedUpdateVersion() : QString());
    setShouldShowStartupPopup(popupAllowed);
    if (popupAllowed) {
        emit startupPopupRequested();
    }

    emitStateChanged();
    applyPendingChannelIfNeeded();
}

bool UpdateService::shouldThrottleStartupCheck() const
{
    if (!m_configManager) {
        return false;
    }

    const QString isoTimestamp = m_configManager->getLastUpdateCheckAt().trimmed();
    if (isoTimestamp.isEmpty()) {
        return false;
    }

    const QDateTime lastCheck = QDateTime::fromString(isoTimestamp, Qt::ISODate);
    if (!lastCheck.isValid()) {
        return false;
    }

    return lastCheck.secsTo(QDateTime::currentDateTimeUtc()) < (12 * 60 * 60);
}

bool UpdateService::manifestRepresentsNewerVersion(const UpdateManifest &manifest) const
{
    if (!manifest.rolloutEnabled) {
        return false;
    }

    const QString selectedChannel = currentChannel();
    const QString buildChannel = currentBuildChannel();
    const QString availableMarker = availabilityMarker(manifest);
    const QString currentMarker = buildChannel + ":" + currentVersion() + ":" + currentBuildId();
    if (selectedChannel != buildChannel) {
        return availableMarker != currentMarker;
    }

    if (selectedChannel == QLatin1String("dev")) {
        const QString currentId = currentBuildId();
        const QString remoteId = manifest.buildId.trimmed().isEmpty() ? manifest.publishedAt.trimmed() : manifest.buildId.trimmed();
        // Dev build IDs are compared lexicographically, so they must be ISO-8601 timestamps
        // or another fixed-width format where remoteId > currentId preserves build order.
        return remoteId > currentId;
    }

    return QVersionNumber::compare(safeVersion(manifest.version), safeVersion(currentVersion())) > 0;
}

QString UpdateService::availabilityMarker(const UpdateManifest &manifest) const
{
    return manifest.availabilityMarker();
}

bool UpdateService::confirmAutoQuit() const
{
    return !playbackIsActive(m_playerController);
}

void UpdateService::scheduleAutoQuitIfAllowed()
{
    if (!confirmAutoQuit()) {
        setStatus(tr("Bloom launched the installer. Close playback to continue the update."));
        return;
    }

    emit requestAutoQuit();
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void UpdateService::setUpdateAvailableState(bool available)
{
    if (m_updateAvailable == available) {
        return;
    }

    m_updateAvailable = available;
    emit updateAvailableChanged();
}

void UpdateService::setShouldShowStartupPopup(bool show)
{
    if (m_shouldShowStartupPopup == show) {
        return;
    }

    m_shouldShowStartupPopup = show;
    emitStateChanged();
}

void UpdateService::setStatus(const QString &status)
{
    m_statusMessage = status;
}

void UpdateService::setError(const QString &error)
{
    m_errorMessage = error;
    if (!error.trimmed().isEmpty()) {
        m_statusMessage = error;
    }
}

void UpdateService::applyPendingChannelIfNeeded()
{
    if (!m_configManager || m_pendingChannel.trimmed().isEmpty()) {
        return;
    }

    const QString pendingChannel = m_pendingChannel;
    m_pendingChannel.clear();
    if (pendingChannel == currentChannel()) {
        return;
    }

    m_configManager->setUpdateChannel(pendingChannel);
    emit channelChanged();
    emitStateChanged();
    startCheck(CheckOrigin::Manual);
}

void UpdateService::emitStateChanged()
{
    emit updateStateChanged();
}
