#include "HeroBackdrops.h"

#include <QSet>
#include <QString>
#include <algorithm>

namespace Bloom::HeroBackdrops {

namespace {

QString identity(const QVariantMap &ref)
{
    return ref.value(QStringLiteral("index")).toString() + QLatin1Char('\n')
        + ref.value(QStringLiteral("tag")).toString();
}

} // namespace

QVariantList refs(const QVariantMap &item, int limit)
{
    const QVariantMap main = item.value(QStringLiteral("backdropArtwork")).toMap();
    const QString connectionId = main.value(QStringLiteral("connectionId")).toString();
    const QString itemId = main.value(QStringLiteral("itemId")).toString();
    if (limit <= 0 || connectionId.isEmpty() || itemId.isEmpty()) {
        return {};
    }
    const QString ownerKind = main.value(QStringLiteral("ownerKind")).toString();

    QList<QVariantMap> others;
    for (const QVariant &value : item.value(QStringLiteral("artwork")).toList()) {
        const QVariantMap ref = value.toMap();
        if (ref.value(QStringLiteral("kind")).toString() == QStringLiteral("backdrop")
            && ref.value(QStringLiteral("connectionId")).toString() == connectionId
            && ref.value(QStringLiteral("itemId")).toString() == itemId
            && ref.value(QStringLiteral("ownerKind")).toString() == ownerKind) {
            others.append(ref);
        }
    }
    std::stable_sort(others.begin(), others.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("index")).toInt() < b.value(QStringLiteral("index")).toInt();
    });

    QVariantList result{main};
    QSet<QString> seen{identity(main)};
    // Two positions may hold the same image; it is shown once.
    QSet<QString> seenTags;
    if (!main.value(QStringLiteral("tag")).toString().isEmpty()) {
        seenTags.insert(main.value(QStringLiteral("tag")).toString());
    }
    for (const QVariantMap &ref : std::as_const(others)) {
        if (result.size() >= limit) {
            break;
        }
        const QString tag = ref.value(QStringLiteral("tag")).toString();
        if (seen.contains(identity(ref)) || (!tag.isEmpty() && seenTags.contains(tag))) {
            continue;
        }
        seen.insert(identity(ref));
        if (!tag.isEmpty()) {
            seenTags.insert(tag);
        }
        result.append(ref);
    }
    return result;
}

QVariantMap withArtworkOf(QVariantMap detail, const QVariantMap &remapped)
{
    const auto isArtwork = [](const QString &key) {
        return key == QStringLiteral("artwork") || key == QStringLiteral("artworkThumbhashes")
            || key.endsWith(QStringLiteral("Artwork")) || key.endsWith(QStringLiteral("ArtworkUrl"));
    };
    for (auto it = remapped.cbegin(); it != remapped.cend(); ++it) {
        if (isArtwork(it.key())) {
            detail.insert(it.key(), it.value());
        }
    }
    return detail;
}

} // namespace Bloom::HeroBackdrops
