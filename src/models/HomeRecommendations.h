#pragma once

#include <QList>
#include <QString>
#include <QVariantList>

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

/// Section IDs of these rows start with this prefix.
inline const QString SectionIdPrefix = QStringLiteral("bloom:recommendations:");

struct Category {
    QString recommendationType;
    QString baselineItemName;
    QVariantList items;
};

/// The localized row title: "Because you watched %1", "More like %1",
/// "Directed by %1" or "Starring %1". Empty for an unknown type or a blank
/// baseline name.
QString title(const QString &recommendationType, const QString &baselineItemName);

/// The rows for the categories of each library, in library order and then
/// the server's order. Only movie cards are kept, each item once per row
/// and at most MaxItems of them. A category with the type and baseline of
/// an earlier one (the same title read from another library) adds its new
/// cards to that row. Rows without a title or cards are dropped, and at
/// most MaxRows are kept.
QVariantList rows(const QList<QList<Category>> &categoriesByLibrary);

} // namespace HomeRecommendations
