#pragma once

#include <QObject>
#include <functional>
#include <optional>

#include "UpdateTypes.h"

class IUpdateProvider : public QObject
{
    Q_OBJECT

public:
    using FetchManifestCallback = std::function<void(std::optional<UpdateManifest>, const QString &)>;

    explicit IUpdateProvider(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~IUpdateProvider() override = default;

    virtual void fetchManifest(const QString &channel,
                               QObject *context,
                               FetchManifestCallback completion) = 0;
};
