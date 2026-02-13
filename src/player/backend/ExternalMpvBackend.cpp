#include "ExternalMpvBackend.h"

ExternalMpvBackend::ExternalMpvBackend(QObject *parent)
    : IPlayerBackend(parent)
    , m_processManager(new PlayerProcessManager(this))
{
    connect(m_processManager, &PlayerProcessManager::stateChanged,
            this, &ExternalMpvBackend::stateChanged);
    connect(m_processManager, &PlayerProcessManager::errorOccurred,
            this, &ExternalMpvBackend::errorOccurred);
    connect(m_processManager, &PlayerProcessManager::positionChanged,
            this, &ExternalMpvBackend::positionChanged);
    connect(m_processManager, &PlayerProcessManager::durationChanged,
            this, &ExternalMpvBackend::durationChanged);
    connect(m_processManager, &PlayerProcessManager::pauseChanged,
            this, &ExternalMpvBackend::pauseChanged);
    connect(m_processManager, &PlayerProcessManager::pausedForCacheChanged,
            this, &ExternalMpvBackend::pausedForCacheChanged);
    connect(m_processManager, &PlayerProcessManager::playbackEnded,
            this, &ExternalMpvBackend::playbackEnded);
    connect(m_processManager, &PlayerProcessManager::audioTrackChanged,
            this, &ExternalMpvBackend::audioTrackChanged);
    connect(m_processManager, &PlayerProcessManager::subtitleTrackChanged,
            this, &ExternalMpvBackend::subtitleTrackChanged);
    connect(m_processManager, &PlayerProcessManager::scriptMessage,
            this, &ExternalMpvBackend::scriptMessage);
}

void ExternalMpvBackend::startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl)
{
    m_processManager->startMpv(mpvBin, args, mediaUrl);
}

void ExternalMpvBackend::stopMpv()
{
    m_processManager->stopMpv();
}

bool ExternalMpvBackend::isRunning() const
{
    return m_processManager->isRunning();
}

void ExternalMpvBackend::sendCommand(const QStringList &command)
{
    m_processManager->sendCommand(command);
}

void ExternalMpvBackend::sendVariantCommand(const QVariantList &command)
{
    m_processManager->sendVariantCommand(command);
}
