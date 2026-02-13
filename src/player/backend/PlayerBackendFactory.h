#pragma once

#include <memory>

class QObject;
class IPlayerBackend;

class PlayerBackendFactory
{
public:
    static std::unique_ptr<IPlayerBackend> create(QObject *parent = nullptr);
};
