#pragma once

#include "BaseViewModel.h"

#include <QDateTime>
#include <QLocale>
#include <QString>
#include <QTimeZone>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class LibraryService;

/**
 * @brief The Upcoming page: the episodes airing on the live connection from
 * today through the next WindowDays days, one row per local day.
 *
 * Each Upcoming page owns one instance (QML type `UpcomingViewModel`).
 * Rows are days in order, each `{date, title, airings}`: `date` is the
 * local day (`YYYY-MM-DD`), `title` reads "Today", "Tomorrow" or the
 * weekday and date, and `airings` are canonical airing maps
 * (docs/canonical-models.md) with the page's labels added: `localDate`,
 * `timeLabel` (the air time on the viewer's clock, empty when the server
 * knows only the day), `episodeLabel` ("S2 E5", "Season 3"),
 * `badgeLabel` ("Season premiere", "Finale", ...) and `subtitle` (the
 * three joined, for the card). Roles: `date`, `title`, `airings`.
 *
 * - `load()` reads the calendar of the live connection with the current
 *   `filter` (LibraryService::getUpcomingAirings). The connection is the
 *   live one even in a merged space; the calendar is not merged across
 *   sources.
 * - An airing's day is the local day of its instant (`airAt`) in the
 *   system zone, else its `airDate`. Days before today and after the
 *   window are left out. Within a day, airings with a time come first by
 *   time, then the rest by series, season and episode.
 * - `filters` are the presets the page offers when the provider honors
 *   them (`filterable`, Silo): Following (the default: the profile's
 *   favorites, watchlist and every title it has watched), Trending and
 *   All. Setting `filter` reads again.
 * - `isLoading` covers a read; a failed read sets `hasError` and keeps no
 *   days. An empty calendar is not an error: `emptyTitle` and
 *   `emptyMessage` say why, and `emptyActionFilter` names the preset that
 *   shows more ("everything"), if any.
 * - `refreshIfStale()` reads again when the last read is older than
 *   StaleAfterMs (Silo's signed poster URLs expire) or the live connection
 *   changed. At local midnight the days are regrouped, so "Today" moves on.
 * - Answers for another connection or an older read are ignored.
 */
class UpcomingViewModel : public BaseViewModel
{
    Q_OBJECT
    Q_PROPERTY(QString connectionId READ connectionId NOTIFY connectionChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QVariantList filters READ filters CONSTANT)
    Q_PROPERTY(bool filterable READ filterable NOTIFY filterableChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int airingCount READ airingCount NOTIFY countChanged)
    Q_PROPERTY(QString emptyTitle READ emptyTitle NOTIFY emptyStateChanged)
    Q_PROPERTY(QString emptyMessage READ emptyMessage NOTIFY emptyStateChanged)
    Q_PROPERTY(QString emptyActionFilter READ emptyActionFilter NOTIFY emptyStateChanged)

public:
    enum Roles {
        DateRole = Qt::UserRole + 1,
        TitleRole,
        AiringsRole
    };
    Q_ENUM(Roles)

    /// Days after today the page covers (Silo reads at most 31 days).
    static constexpr int WindowDays = 30;
    /// A calendar older than this is read again when the page is shown.
    static constexpr qint64 StaleAfterMs = 10 * 60 * 1000;

    explicit UpcomingViewModel(QObject *parent = nullptr);
    explicit UpcomingViewModel(LibraryService *libraryService, QObject *parent = nullptr);
    ~UpcomingViewModel() override = default;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString connectionId() const { return m_connectionId; }
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    QVariantList filters() const;
    bool filterable() const { return m_filterable; }
    int count() const { return static_cast<int>(m_days.size()); }
    int airingCount() const;
    QString emptyTitle() const;
    QString emptyMessage() const;
    QString emptyActionFilter() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void reload() override;
    Q_INVOKABLE void refreshIfStale();
    /// The day at `row`, or an empty map.
    Q_INVOKABLE QVariantMap get(int row) const;
    /// The airing at `index` of the day at `row`, or an empty map.
    Q_INVOKABLE QVariantMap airingAt(int row, int index) const;
    /// The row of the day `date` (`YYYY-MM-DD`), or -1.
    Q_INVOKABLE int indexOfDay(const QString &date) const;
    /// The index of itemId among the airings of the day at `row`, or -1.
    Q_INVOKABLE int indexOfAiring(int row, const QString &itemId) const;

    /// Groups canonical airings into the days of the window that starts on
    /// now's day in zone, as the rows describe.
    static QVariantList groupByDay(const QVariantList &airings,
                                   const QDateTime &now,
                                   const QTimeZone &zone,
                                   const QLocale &locale = QLocale(),
                                   int windowDays = WindowDays);
    /// "Today", "Tomorrow", else the weekday and date.
    static QString dayTitle(const QDate &day, const QDate &today,
                            const QLocale &locale = QLocale());

    /// Tests: the clock and zone the days are reckoned in.
    void setClock(std::function<QDateTime()> clock);
    void setTimeZone(const QTimeZone &zone);

signals:
    void connectionChanged();
    void filterChanged();
    void filterableChanged();
    void countChanged();
    void emptyStateChanged();

private slots:
    void onAiringsLoaded(const QString &connectionId, const QString &requestKey,
                         const QVariantList &airings, bool filterable);
    void onAiringsFailed(const QString &connectionId, const QString &requestKey,
                         const QString &error);

private:
    void connectService();
    QDateTime now() const;
    void regroup();
    void scheduleMidnight();

    LibraryService *m_libraryService = nullptr;
    QVariantList m_airings;
    QVariantList m_days;
    QString m_connectionId;
    QString m_filter = QStringLiteral("following");
    bool m_filterable = false;
    QString m_requestKey;
    // When the shown days were read, and with which preset.
    QDateTime m_loadedAt;
    QString m_loadedFilter;
    std::function<QDateTime()> m_clock;
    QTimeZone m_zone = QTimeZone::systemTimeZone();
    QTimer m_midnight;
};
