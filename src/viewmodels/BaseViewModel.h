#pragma once

#include <QAbstractListModel>
#include <QFutureInterfaceBase>
#include <QFutureWatcherBase>
#include <functional>

/**
 * @brief Shared ViewModel base that standardizes loading/error state.
 *
 * Provides QML-friendly properties (`isLoading`, `hasError`, `errorMessage`)
 * and helpers for derived list models to manage lifecycle and network errors.
 * Data/role implementations remain the responsibility of derived classes.
 */
class BaseViewModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY hasErrorChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit BaseViewModel(QObject *parent = nullptr);
    ~BaseViewModel() override = default;

    /**
     * @brief Standard reload hook for QML retry flows.
     *
     * Default is a no-op; derived classes should override to call their
     * existing refresh/load entry point.
     */
    Q_INVOKABLE virtual void reload();

    // QAbstractListModel defaults (0 rows, no data) — override in derived classes.
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool isLoading() const { return m_isLoading; }
    bool hasError() const { return m_hasError; }
    QString errorMessage() const { return m_errorMessage; }

signals:
    void isLoadingChanged();
    void hasErrorChanged();
    void errorMessageChanged();

protected:
    // State helpers
    void setLoading(bool loading);
    void setError(const QString &message);
    void clearError();

    // Utility to wrap reset operations for list models.
    void emitModelReset(const std::function<void()> &mutator);

    // Maps service-specific errors to user-friendly strings.
    QString mapNetworkError(const QString &endpoint, const QString &error) const;

    // Optional helper to track async work (Qt Concurrent).
    void setBusyWhile(QFutureWatcherBase &watcher);
    void setBusyWhile(QFutureInterfaceBase &future);

private:
    bool m_isLoading = false;
    bool m_hasError = false;
    QString m_errorMessage;
};







