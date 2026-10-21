#pragma once

#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

/**
 * @brief Read operations a merged Bloom profile fans out across members.
 */
enum class MergedCatalogOperation {
    Views,
    NextUp,
    Search
};

/**
 * @brief Outcome of one membership's request inside a merged read.
 *
 * Items are canonical camelCase projections that already carry their owning
 * connectionId (MediaRef). memberId stays here and never enters item maps.
 */
struct MergedSourceResult {
    QString memberId;
    QString connectionId;
    QString label;
    int priority = 0;
    bool succeeded = false;
    QString error;
    QVariantList items;
};

/**
 * @brief Combines per-member results into one deterministic list.
 *
 * The interface is the extension point for later policies (deduplication,
 * source preference, failover, per-user visibility) without changing the
 * coordinator or the profile schema.
 */
class IMergedCatalogPolicy
{
public:
    virtual ~IMergedCatalogPolicy() = default;

    virtual QVariantList merge(MergedCatalogOperation operation,
                               const QList<MergedSourceResult> &sources) const = 0;
};

/**
 * @brief MVP merge semantics (docs/profiles.md).
 *
 * - No cross-server deduplication: the same title from two members appears twice.
 * - Failed sources contribute nothing; healthy sources are never hidden.
 * - Views: member priority, then provider order.
 * - Next Up: most recently played first, then member priority, then MediaRef.
 * - Search: relevance is not comparable across servers, so results interleave
 *   by per-source rank in member-priority order.
 * - Every item gains a display-only sourceLabel.
 */
class DefaultMergedCatalogPolicy final : public IMergedCatalogPolicy
{
public:
    QVariantList merge(MergedCatalogOperation operation,
                       const QList<MergedSourceResult> &sources) const override;

    static QVariantMap withSourceLabel(QVariantMap item, const QString &label);
};
