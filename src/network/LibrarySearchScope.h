#pragma once

#include <QList>
#include <QStringList>
#include <QVariantList>

/**
 * @brief Search limited to some of a connection's libraries.
 *
 * Providers search one library at a time (Jellyfin `ParentId`, Silo
 * `library_id`), so a source that leaves libraries out of search runs one
 * search per remaining library and combines the ranked lists here.
 */
namespace LibrarySearchScope {

/// Library IDs from canonical views, in view order, minus the excluded ones.
QStringList searchableLibraryIds(const QVariantList &views, const QStringList &excludedIds);

/// Interleaves per-library ranked results by rank, drops repeated item IDs,
/// and stops at @p limit (no cap when limit <= 0).
QVariantList interleave(const QList<QVariantList> &perLibraryResults, int limit);

} // namespace LibrarySearchScope
