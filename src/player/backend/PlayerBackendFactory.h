#pragma once

#include <memory>

#include <QString>

class QObject;
class IPlayerBackend;

class PlayerBackendFactory
{
public:
    static std::unique_ptr<IPlayerBackend> create(QObject *parent = nullptr);
    static std::unique_ptr<IPlayerBackend> create(const QString &configuredBackendName, QObject *parent = nullptr);
    static std::unique_ptr<IPlayerBackend> createByName(const QString &backendName, QObject *parent = nullptr);
};
