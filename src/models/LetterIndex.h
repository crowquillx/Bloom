#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>

/**
 * @brief The letter index behind the library's A-Z jump rail.
 *
 * An index is a QVariantMap with `total` (titles in the list), `letters`
 * ({letter, count, position} for each letter with titles, in list order) and
 * `runs` ({letter, position} for each block of consecutive titles under one
 * letter, covering the whole list). A letter's `position` is the zero-based
 * list position of its first title. Silo answers this shape itself
 * (queryCatalogLetters); providers without such a read build it from the
 * sort keys of the title-ordered list with `fromSortKeys`.
 *
 * Titles file under the first character of their sort key: A to Z, with
 * accented Latin initials folded to their base letter, and `#` for digits,
 * symbols and other scripts. Qt-Core only and free of I/O.
 */
namespace LetterIndex {

/// "A" to "Z", or "#".
inline QString letterFor(const QString &sortKey)
{
    const QString key = sortKey.trimmed();
    if (key.isEmpty()) {
        return QStringLiteral("#");
    }
    // Decompose so "É" leaves its base letter first.
    const QString decomposed = key.left(2).normalized(QString::NormalizationForm_D);
    const QChar initial = decomposed.isEmpty() ? QChar() : decomposed.at(0).toUpper();
    if (initial >= QLatin1Char('A') && initial <= QLatin1Char('Z')) {
        return QString(initial);
    }
    return QStringLiteral("#");
}

/// The index of a list whose titles have these sort keys, in list order.
inline QVariantMap fromSortKeys(const QStringList &sortKeys)
{
    QVariantList letters;
    QVariantList runs;
    QHash<QString, qsizetype> letterSlots;
    QString previous;
    for (qsizetype position = 0; position < sortKeys.size(); ++position) {
        const QString letter = letterFor(sortKeys.at(position));
        if (letter != previous) {
            runs.append(QVariantMap{{QStringLiteral("letter"), letter},
                                    {QStringLiteral("position"), int(position)}});
            previous = letter;
        }
        const auto slot = letterSlots.constFind(letter);
        if (slot == letterSlots.cend()) {
            letterSlots.insert(letter, letters.size());
            letters.append(QVariantMap{{QStringLiteral("letter"), letter},
                                       {QStringLiteral("count"), 1},
                                       {QStringLiteral("position"), int(position)}});
        } else {
            QVariantMap entry = letters.at(*slot).toMap();
            entry.insert(QStringLiteral("count"), entry.value(QStringLiteral("count")).toInt() + 1);
            letters[*slot] = entry;
        }
    }
    return {{QStringLiteral("total"), int(sortKeys.size())},
            {QStringLiteral("letters"), letters},
            {QStringLiteral("runs"), runs}};
}

/// The letter the title at @p position files under, from the index's runs;
/// empty when the runs do not cover it.
inline QString letterAt(const QVariantList &runs, int position)
{
    if (position < 0 || runs.isEmpty()) {
        return {};
    }
    // The last run starting at or before position.
    const auto after = std::upper_bound(
        runs.cbegin(), runs.cend(), position, [](int value, const QVariant &run) {
            return value < run.toMap().value(QStringLiteral("position")).toInt();
        });
    if (after == runs.cbegin()) {
        return {};
    }
    return std::prev(after)->toMap().value(QStringLiteral("letter")).toString();
}

/// The position of @p letter's first title, or -1 when it has none.
inline int positionOf(const QVariantList &letters, const QString &letter)
{
    for (const QVariant &value : letters) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("letter")).toString() == letter) {
            return entry.value(QStringLiteral("position")).toInt();
        }
    }
    return -1;
}

} // namespace LetterIndex
