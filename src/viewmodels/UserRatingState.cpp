#include "UserRatingState.h"

#include "network/LibraryService.h"
#include "utils/BloomLogging.h"

UserRatingState::UserRatingState(LibraryService *libraryService, QObject *parent)
    : QObject(parent)
    , m_libraryService(libraryService)
{
    if (!m_libraryService) {
        return;
    }
    connect(m_libraryService, &LibraryService::userRatingSupportChanged,
            this, &UserRatingState::onSupportChanged);
    connect(m_libraryService, &LibraryService::userRatingChanged,
            this, &UserRatingState::onRatingChanged);
    connect(m_libraryService, &LibraryService::userRatingFailed,
            this, &UserRatingState::onRatingFailed);
}

QString UserRatingState::scale() const
{
    return m_libraryService ? m_libraryService->userRatingScale() : QString();
}

void UserRatingState::setItem(const QString &itemId)
{
    if (itemId != m_itemId) {
        m_itemId = itemId;
        m_item.clear();
        setPending(false);
        setValue(0);
        emit itemIdChanged();
    }
    // The scale may differ on the connection this item belongs to.
    emit scaleChanged();
    if (m_libraryService && !itemId.isEmpty()) {
        m_libraryService->loadUserRatingCapability();
    }
}

void UserRatingState::updateFromItem(const QVariantMap &item)
{
    if (m_itemId.isEmpty()) {
        return;
    }
    m_item = item;
    if (!m_pending) {
        setValue(valueFromItem(item, scale()));
    }
}

void UserRatingState::clear()
{
    const bool hadItem = !m_itemId.isEmpty();
    m_itemId.clear();
    m_item.clear();
    setPending(false);
    setValue(0);
    if (hadItem) {
        emit itemIdChanged();
        emit scaleChanged();
    }
}

void UserRatingState::rate(int value)
{
    const QString currentScale = scale();
    if (!m_libraryService || m_itemId.isEmpty() || m_pending || currentScale.isEmpty()
        || value == 0 || !isValidValue(currentScale, value)) {
        return;
    }
    // Optimistic: the control follows the press; a refusal restores it.
    m_valueBeforeChange = m_value;
    setValue(value == m_value ? 0 : value);
    setPending(true);
    m_libraryService->setUserRating(m_itemId, m_value);
}

int UserRatingState::valueFromItem(const QVariantMap &item, const QString &scale)
{
    const QString key = itemKey(scale);
    if (key.isEmpty()) {
        return 0;
    }
    const QVariant direct = item.value(key);
    const int value = direct.isValid()
        ? direct.toInt()
        : item.value(QStringLiteral("userState")).toMap().value(key).toInt();
    return isValidValue(scale, value) ? value : 0;
}

bool UserRatingState::isValidValue(const QString &scale, int value)
{
    if (scale == QStringLiteral("stars")) {
        return value >= 0 && value <= 5;
    }
    if (scale == QStringLiteral("likes")) {
        return value >= -1 && value <= 1;
    }
    return false;
}

QString UserRatingState::itemKey(const QString &scale)
{
    if (scale == QStringLiteral("stars")) {
        return QStringLiteral("userRating");
    }
    if (scale == QStringLiteral("likes")) {
        return QStringLiteral("userLikes");
    }
    return {};
}

void UserRatingState::onSupportChanged()
{
    // Details that arrived before the capability carry the rating too.
    if (!m_pending && !m_itemId.isEmpty()) {
        setValue(valueFromItem(m_item, scale()));
    }
    emit scaleChanged();
}

void UserRatingState::setValue(int value)
{
    if (m_value == value) {
        return;
    }
    m_value = value;
    emit valueChanged();
}

void UserRatingState::setPending(bool pending)
{
    if (m_pending == pending) {
        return;
    }
    m_pending = pending;
    emit pendingChanged();
}

void UserRatingState::onRatingChanged(const QString &itemId, int value)
{
    if (itemId.isEmpty() || itemId != m_itemId) {
        return;
    }
    // Also a rating set for the same item on another page.
    const QString key = itemKey(scale());
    if (!key.isEmpty()) {
        m_item.insert(key, value);
    }
    setPending(false);
    setValue(value);
    emit confirmed(itemId, scale(), value);
}

void UserRatingState::onRatingFailed(const QString &itemId, int value, const QString &error)
{
    if (itemId != m_itemId || !m_pending || value != m_value) {
        return;
    }
    qCWarning(lcViewModels) << "UserRatingState: rating change failed:" << error;
    setPending(false);
    setValue(m_valueBeforeChange);
    emit updateFailed(value == 0 ? tr("Couldn't clear your rating.")
                                 : tr("Couldn't save your rating."));
}
