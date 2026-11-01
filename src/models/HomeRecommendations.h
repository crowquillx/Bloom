#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <optional>

/**
 * @brief Home's personalised recommendation rows (Jellyfin).
 *
 * Jellyfin recommends movies per library in categories: a reason
 * (`RecommendationType`) and the title or person it starts from
 * (`BaselineItemName`). Home reads each visible movie library and shows the
 * categories as rows with the same {sectionId, title, items} shape as
 * Silo's curated rows. Items are canonical maps.
 */
namespace HomeRecommendations {

/// Rows a Home read keeps, at most.
inline constexpr int MaxRows = 8;
/// Cards per row, at most (also what Home asks each category for).
inline constexpr int MaxItems = 15;
/// Categories Home asks each library for.
inline constexpr int CategoriesPerLibrary = 4;
/// Item IDs per library membership read (keeps the request line short).
inline constexpr int MembershipBatchSize = CategoriesPerLibrary * MaxItems;
/// How long a read is reused before Home reads the server again. The
/// server picks its seed titles at random, so every read reshuffles rows.
inline constexpr qint64 CacheLifetimeMs = 45 * 60 * 1000;

/// Section IDs of these rows start with this prefix.
inline const QString SectionIdPrefix = QStringLiteral("bloom:recommendations:");

struct Category {
    QString recommendationType;
    QString baselineItemName;
    QVariantList items;
};

/// What rows() keeps.
struct Filter {
    /// Whether the server's "similar to" categories compare with their
    /// baseline (similarRowsWork()). When false they are dropped.
    bool similarRowsWork = true;
    /// Items in the libraries Home shows. When set, every other item is
    /// dropped: the server finds similar titles and people's films in
    /// every library the user may open, not only the one it was asked for.
    std::optional<QSet<QString>> allowedItemIds;
};

/// Whether recommendationType is SimilarToRecentlyPlayed or
/// SimilarToLikedItem.
bool isSimilarType(const QString &recommendationType);

/// Whether a Jellyfin server of this version (PublicSystemInfo.Version)
/// fills "similar to" categories with titles like the baseline. Jellyfin
/// 10.11.0 to 10.11.11 query them without the baseline item, so every such
/// category lists the same arbitrary movies; 10.10 and older and 12.0 and
/// newer compare with it. An unreadable version answers false.
bool similarRowsWork(const QString &serverVersion);

/// Whether a read fetched at fetchedAtMs may still be shown at nowMs
/// (CacheLifetimeMs). fetchedAtMs 0 means the read must be repeated.
bool cacheFresh(qint64 fetchedAtMs, qint64 nowMs);

/// The item ID of every item in the categories, once, in order.
QStringList itemIds(const QList<QList<Category>> &categoriesByLibrary);

/// The localized row title: "Because you watched %1", "More like %1",
/// "Directed by %1" or "Starring %1". Empty for an unknown type or a blank
/// baseline name.
QString title(const QString &recommendationType, const QString &baselineItemName);

/// The rows for the categories of each library, in library order and then
/// the server's order. "Similar to" categories are dropped when the filter
/// says they do not work, and also when their items are exactly those of
/// a category with another title (what a server that ignores the baseline
/// answers). Only movie cards are kept, and only allowed ones when the
/// filter has a list, each item once per row and at most MaxItems of them.
/// A category with the type and baseline of an earlier one (the same title
/// read from another library) adds its new cards to that row. Rows without
/// a title or cards are dropped, and at most MaxRows are kept.
QVariantList rows(const QList<QList<Category>> &categoriesByLibrary, const Filter &filter = {});

} // namespace HomeRecommendations
