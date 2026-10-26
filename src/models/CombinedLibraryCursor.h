#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

/**
 * @brief Merges several libraries' sorted, paged item lists into one list.
 *
 * Providers page one library at a time (Jellyfin `ParentId`, Silo
 * `library_id`), so "All Movies" asks each library for its own sorted pages
 * and merges them here. The caller fetches the pages this cursor asks for
 * (`librariesNeedingPages`) and hands them back (`addPage`); `take` then
 * returns items in the combined order. An item is only released once every
 * library that still has items has at least one buffered, so the merged
 * order never changes as more pages arrive.
 *
 * Sorting mirrors the Bloom sort keys the providers translate (`title`,
 * `releaseDate`, `dateAdded`, `rating`, `year`); library order sorts by title
 * and `random` interleaves the libraries. Qt-Core only and free of I/O.
 */
class CombinedLibraryCursor
{
public:
    void reset(const QStringList &libraryIds, const QString &sortBy, const QString &sortOrder);

    /// Libraries whose next page is needed before more items can be taken.
    /// Each is reported once until its page arrives (or `pageFailed`).
    QStringList librariesNeedingPages();

    /// Offset of the next page to request for @p libraryId.
    int nextOffset(const QString &libraryId) const;

    /// Records a page for @p libraryId. An empty page or reaching @p total
    /// ends that library.
    void addPage(const QString &libraryId, const QVariantList &items, int total);

    /// Ends a library whose page failed so the others can still be merged.
    void pageFailed(const QString &libraryId);

    /// Up to @p count items in combined order. Returns fewer when a library
    /// needs another page first (see `librariesNeedingPages`).
    QVariantList take(int count);

    /// Sum of every library's total once its first page arrived.
    int totalCount() const;
    /// Every library has delivered and drained all of its items.
    bool finished() const;
    QStringList libraryIds() const { return m_order; }

    /// Whether @p a sorts before @p b for a Bloom sort key.
    static bool lessThan(const QVariantMap &a, const QVariantMap &b,
                         const QString &sortBy, bool descending);

private:
    struct Source {
        QList<QVariantMap> buffer;
        int nextOffset = 0;
        int total = -1;
        bool exhausted = false;
        bool requested = false;
    };

    QStringList m_order;
    QHash<QString, Source> m_sources;
    QString m_sortBy;
    bool m_descending = false;
    int m_randomCursor = 0;
};
