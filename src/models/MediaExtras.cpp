#include "MediaExtras.h"

namespace Bloom::MediaExtras {

QStringList kinds()
{
    return {kTrailer, kTeaser, kFeaturette, kClip, kBehindTheScenes, kBloopers,
            kDeletedScene, kInterview, kScene, kShort, kOther};
}

QString normalizedKind(const QString &kind)
{
    return kinds().contains(kind) ? kind : kOther;
}

QVariantMap preferredTrailer(const QVariantList &extras)
{
    for (const QString &kind : {kTrailer, kTeaser}) {
        for (const QVariant &value : extras) {
            const QVariantMap extra = value.toMap();
            if (extra.value(QStringLiteral("extraKind")).toString() == kind
                && !extra.value(QStringLiteral("itemId")).toString().isEmpty()) {
                return extra;
            }
        }
    }
    return {};
}

} // namespace Bloom::MediaExtras
