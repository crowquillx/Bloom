#pragma once

#include <QObject>

#include "UpdateTypes.h"

class IUpdateApplier : public QObject
{
    Q_OBJECT

public:
    explicit IUpdateApplier(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~IUpdateApplier() override = default;

    virtual InstallEligibility detectEligibility() const = 0;
    virtual void downloadAndInstall(const UpdateManifest &manifest, const QString &channel) = 0;

signals:
    void downloadProgressChanged(qint64 bytesReceived, qint64 bytesTotal);
    void installFinished(bool success, const QString &message);
};
