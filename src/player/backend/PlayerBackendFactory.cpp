#include "PlayerBackendFactory.h"
#include "IPlayerBackend.h"
#include "ExternalMpvBackend.h"

std::unique_ptr<IPlayerBackend> PlayerBackendFactory::create(QObject *parent)
{
    return std::make_unique<ExternalMpvBackend>(parent);
}
