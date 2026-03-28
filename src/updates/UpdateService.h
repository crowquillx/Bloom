#pragma once

#include <QDateTime>
#include <QObject>

#include "UpdateTypes.h"

class ConfigManager;
class PlayerController;
class IUpdateProvider;
class IUpdateApplier;

class UpdateService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool checking READ checking NOTIFY updateStateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateAvailableChanged)
    Q_PROPERTY(bool downloadInProgress READ downloadInProgress NOTIFY updateStateChanged)
    Q_PROPERTY(bool applySupported READ applySupported NOTIFY updateStateChanged)
    Q_PROPERTY(bool shouldShowStartupPopup READ shouldShowStartupPopup NOTIFY updateStateChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString currentChannel READ currentChannel NOTIFY channelChanged)
    Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY updateStateChanged)
    Q_PROPERTY(QString availableChannel READ availableChannel NOTIFY updateStateChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY updateStateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY updateStateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY updateStateChanged)
    Q_PROPERTY(QString downloadPageUrl READ downloadPageUrl NOTIFY updateStateChanged)
    Q_PROPERTY(QString installerUrl READ installerUrl NOTIFY updateStateChanged)
    Q_PROPERTY(QString portableUrl READ portableUrl NOTIFY updateStateChanged)
    Q_PROPERTY(qreal downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)

public:
    explicit UpdateService(ConfigManager *configManager,
                           PlayerController *playerController,
                           IUpdateProvider *provider = nullptr,
                           IUpdateApplier *applier = nullptr,
                           QObject *parent = nullptr);
    ~UpdateService() override = default;

    bool checking() const { return m_checking; }
    bool updateAvailable() const { return m_updateAvailable; }
    bool downloadInProgress() const { return m_downloadInProgress; }
    bool applySupported() const { return m_applySupported; }
    bool shouldShowStartupPopup() const { return m_shouldShowStartupPopup; }
    QString currentVersion() const;
    QString currentChannel() const;
    QString availableVersion() const { return m_availableManifest.version; }
    QString availableChannel() const { return m_availableManifest.channel; }
    QString releaseNotes() const { return m_availableManifest.notes; }
    QString statusMessage() const { return m_statusMessage; }
    QString errorMessage() const { return m_errorMessage; }
    QString downloadPageUrl() const;
    QString installerUrl() const { return m_availableManifest.installer.url; }
    QString portableUrl() const { return m_availableManifest.portable.url; }
    qreal downloadProgress() const { return m_downloadProgress; }

    Q_INVOKABLE void performStartupCheck();
    Q_INVOKABLE void checkForUpdates(bool manual = false);
    Q_INVOKABLE void downloadAndInstallUpdate();
    Q_INVOKABLE void dismissStartupPopup();
    Q_INVOKABLE void openUpdateDownloadPage();
    Q_INVOKABLE void openInstallerAsset();
    Q_INVOKABLE void openPortableAsset();
    Q_INVOKABLE void setChannel(const QString &channel);

signals:
    void updateStateChanged();
    void updateAvailableChanged();
    void downloadProgressChanged();
    void startupPopupRequested();
    void updateError(const QString &message);
    void channelChanged();

private:
    enum class CheckOrigin {
        Startup,
        Manual
    };

    QString normalizedChannel(const QString &channel) const;
    QString currentBuildChannel() const;
    QString currentBuildId() const;
    void startCheck(CheckOrigin origin);
    void finishCheck(std::optional<UpdateManifest> manifest,
                     const QString &errorMessage,
                     CheckOrigin origin);
    bool shouldThrottleStartupCheck() const;
    bool manifestRepresentsNewerVersion(const UpdateManifest &manifest) const;
    QString availabilityMarker(const UpdateManifest &manifest) const;
    void setUpdateAvailableState(bool available);
    void setShouldShowStartupPopup(bool show);
    void setStatus(const QString &status);
    void setError(const QString &error);
    void applyPendingChannelIfNeeded();
    void emitStateChanged();

    ConfigManager *m_configManager = nullptr;
    PlayerController *m_playerController = nullptr;
    IUpdateProvider *m_provider = nullptr;
    IUpdateApplier *m_applier = nullptr;

    bool m_checking = false;
    bool m_updateAvailable = false;
    bool m_downloadInProgress = false;
    bool m_applySupported = false;
    bool m_shouldShowStartupPopup = false;
    qreal m_downloadProgress = 0.0;
    bool m_startupCheckPerformed = false;
    InstallEligibility m_installEligibility;
    UpdateManifest m_availableManifest;
    QString m_statusMessage;
    QString m_errorMessage;
    QString m_pendingChannel;
};
