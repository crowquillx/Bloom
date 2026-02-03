#include "BaseViewModel.h"

#include <QDebug>
#include <QtConcurrent/QtConcurrent>

BaseViewModel::BaseViewModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int BaseViewModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return 0;
}

QVariant BaseViewModel::data(const QModelIndex &index, int role) const
{
    Q_UNUSED(index)
    Q_UNUSED(role)
    return QVariant();
}

QHash<int, QByteArray> BaseViewModel::roleNames() const
{
    return {};
}

void BaseViewModel::reload()
{
    // No-op by default; derived classes should override.
}

void BaseViewModel::setLoading(bool loading)
{
    if (m_isLoading == loading)
        return;
    m_isLoading = loading;
    emit isLoadingChanged();
}

void BaseViewModel::setError(const QString &message)
{
    const bool errorChanged = m_errorMessage != message;
    const bool hadError = m_hasError;

    m_errorMessage = message;
    m_hasError = !message.isEmpty();

    if (errorChanged)
        emit errorMessageChanged();
    if (hadError != m_hasError)
        emit hasErrorChanged();
}

void BaseViewModel::clearError()
{
    setError(QString());
}

void BaseViewModel::emitModelReset(const std::function<void()> &mutator)
{
    beginResetModel();
    mutator();
    endResetModel();
}

QString BaseViewModel::mapNetworkError(const QString &endpoint, const QString &error) const
{
    if (!error.isEmpty())
        return error;

    if (endpoint.contains("auth", Qt::CaseInsensitive)) {
        return QStringLiteral("Authentication failed. Please try again.");
    }
    if (endpoint.contains("items", Qt::CaseInsensitive)) {
        return QStringLiteral("Unable to load items. Check your connection.");
    }
    if (endpoint.contains("playback", Qt::CaseInsensitive)) {
        return QStringLiteral("Playback request failed. Please retry.");
    }
    return QStringLiteral("An unexpected error occurred.");
}

void BaseViewModel::setBusyWhile(QFutureWatcherBase &watcher)
{
    setLoading(true);
    connect(&watcher, &QFutureWatcherBase::finished, this, [this]() {
        setLoading(false);
    }, Qt::UniqueConnection);
    connect(&watcher, &QFutureWatcherBase::canceled, this, [this]() {
        setLoading(false);
    }, Qt::UniqueConnection);
}

void BaseViewModel::setBusyWhile(QFutureInterfaceBase &future)
{
    setLoading(true);
    if (future.isFinished() || future.isCanceled()) {
        setLoading(false);
    }
}







