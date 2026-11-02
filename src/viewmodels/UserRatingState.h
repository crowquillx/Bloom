#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class LibraryService;

/**
 * @brief The viewer's own rating of one movie or series, for a page.
 *
 * The scale comes from the live provider (`LibraryService::userRatingScale`):
 * "stars" holds 1 to 5, "likes" holds 1 (like) or -1 (dislike), and 0 means
 * unrated in both. An empty scale hides the control. `rate` changes `value`
 * at once and puts it back if the server refuses (`updateFailed`); pressing
 * the current value clears the rating. Owners call `setItem` when they open
 * an item and `updateFromItem` with each canonical detail map.
 *
 * The item belongs to one connection (`connectionId`): only answers and
 * details from that connection apply, and `rate` writes only while it is
 * the live one (UserRatingControl routes there first in merged spaces).
 */
class UserRatingState : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString itemId READ itemId NOTIFY itemIdChanged)
    Q_PROPERTY(QString connectionId READ connectionId NOTIFY itemIdChanged)
    Q_PROPERTY(QString scale READ scale NOTIFY scaleChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY scaleChanged)
    Q_PROPERTY(int value READ value NOTIFY valueChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)

public:
    explicit UserRatingState(LibraryService *libraryService, QObject *parent = nullptr);

    QString itemId() const { return m_itemId; }
    QString connectionId() const { return m_connectionId; }
    QString scale() const;
    bool supported() const { return !m_itemId.isEmpty() && !scale().isEmpty(); }
    int value() const { return m_value; }
    bool pending() const { return m_pending; }

    /// Starts on another item (unrated until its details arrive) of
    /// @p connectionId, the live connection when empty.
    void setItem(const QString &itemId, const QString &connectionId = {});
    /// Takes the rating from a canonical item map, unless a change is in
    /// flight.
    void updateFromItem(const QVariantMap &item);
    void clear();

    /// Rates the item @p value in the current scale; the current value
    /// clears the rating. Ignored while a change is in flight or while
    /// another connection is live.
    Q_INVOKABLE void rate(int value);

    /// The rating in @p scale that a canonical item map carries.
    static int valueFromItem(const QVariantMap &item, const QString &scale);
    /// Whether @p value is a rating (or 0) in @p scale.
    static bool isValidValue(const QString &scale, int value);
    /// The canonical item key that holds the rating in @p scale.
    static QString itemKey(const QString &scale);

signals:
    void itemIdChanged();
    void scaleChanged();
    void valueChanged();
    void pendingChanged();
    /// The server accepted @p value for @p itemId.
    void confirmed(const QString &itemId, const QString &scale, int value);
    void updateFailed(const QString &message);

private:
    void setValue(int value);
    void setPending(bool pending);
    void onSupportChanged();
    void onRatingChanged(const QString &connectionId, const QString &itemId, int value);
    void onRatingFailed(const QString &connectionId, const QString &itemId, int value,
                        const QString &error);
    bool matches(const QString &connectionId, const QString &itemId) const;

    QPointer<LibraryService> m_libraryService;
    QString m_itemId;
    QString m_connectionId;
    // The last canonical map, read again when the scale becomes known.
    QVariantMap m_item;
    int m_value = 0;
    int m_valueBeforeChange = 0;
    bool m_pending = false;
};
