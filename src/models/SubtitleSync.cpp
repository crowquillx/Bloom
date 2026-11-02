#include "SubtitleSync.h"

#include <cmath>

namespace Bloom {

bool SubtitleTiming::isIdentity() const
{
    return offsetMs == 0 && std::abs(scale - 1.0) < 1e-9;
}

bool SubtitleTiming::operator==(const SubtitleTiming &other) const
{
    return offsetMs == other.offsetMs && std::abs(scale - other.scale) < 1e-9;
}

QVariantMap SubtitleTiming::toVariantMap() const
{
    return {
        {QStringLiteral("offsetMs"), offsetMs},
        {QStringLiteral("scale"), scale}
    };
}

QString subtitleSyncStatusName(SubtitleSyncStatus status)
{
    switch (status) {
    case SubtitleSyncStatus::None: return QStringLiteral("none");
    case SubtitleSyncStatus::Pending: return QStringLiteral("pending");
    case SubtitleSyncStatus::Running: return QStringLiteral("running");
    case SubtitleSyncStatus::Synced: return QStringLiteral("synced");
    case SubtitleSyncStatus::AlreadySynced: return QStringLiteral("alreadySynced");
    case SubtitleSyncStatus::NoMatch: return QStringLiteral("noMatch");
    case SubtitleSyncStatus::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("none");
}

bool SubtitleSyncJob::isActive() const
{
    return status == SubtitleSyncStatus::Pending || status == SubtitleSyncStatus::Running;
}

QVariantMap SubtitleSyncJob::toVariantMap() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("status"), subtitleSyncStatusName(status)},
        {QStringLiteral("phase"), phase},
        {QStringLiteral("progress"), progress},
        {QStringLiteral("failure"), failure}
    };
}

QVariantMap SubtitleSyncState::toVariantMap() const
{
    return {
        {QStringLiteral("key"), key},
        {QStringLiteral("mediaFileId"), mediaFileId},
        {QStringLiteral("timing"), timing.toVariantMap()},
        {QStringLiteral("job"), job.toVariantMap()}
    };
}

} // namespace Bloom
