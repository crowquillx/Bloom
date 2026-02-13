#pragma once

#include "IPlayerBackend.h"
#include "../PlayerProcessManager.h"

class ExternalMpvBackend : public IPlayerBackend
{
    Q_OBJECT

public:
    explicit ExternalMpvBackend(QObject *parent = nullptr);
    ~ExternalMpvBackend() override = default;

    void startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl) override;
    void stopMpv() override;
    bool isRunning() const override;

    void sendCommand(const QStringList &command) override;
    void sendVariantCommand(const QVariantList &command) override;

private:
    PlayerProcessManager *m_processManager;
};
