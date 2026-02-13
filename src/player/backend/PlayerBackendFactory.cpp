#include "PlayerBackendFactory.h"
#include "IPlayerBackend.h"
#include "ExternalMpvBackend.h"

#include <QByteArray>
#include <QLoggingCategory>
#include <QtGlobal>

Q_LOGGING_CATEGORY(lcPlayerBackendFactory, "bloom.playback.backend.factory")

static constexpr auto kDefaultBackendName = "external-mpv-ipc";

std::unique_ptr<IPlayerBackend> PlayerBackendFactory::create(QObject *parent)
{
    const QByteArray configuredBackend = qgetenv("BLOOM_PLAYER_BACKEND");
    if (configuredBackend.isEmpty()) {
        return createByName(QString::fromLatin1(kDefaultBackendName), parent);
    }

    return createByName(QString::fromUtf8(configuredBackend), parent);
}

std::unique_ptr<IPlayerBackend> PlayerBackendFactory::createByName(const QString &backendName, QObject *parent)
{
    if (backendName.compare(QString::fromLatin1(kDefaultBackendName), Qt::CaseInsensitive) == 0) {
        return std::make_unique<ExternalMpvBackend>(parent);
    }

    qCWarning(lcPlayerBackendFactory)
        << "Unknown backend requested:" << backendName
        << "- falling back to" << kDefaultBackendName;
    return std::make_unique<ExternalMpvBackend>(parent);
}
