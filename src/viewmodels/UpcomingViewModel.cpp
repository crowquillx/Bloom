#include "UpcomingViewModel.h"

#include "../core/ServiceLocator.h"
#include "../network/LibraryService.h"
#include "../utils/BloomLogging.h"

#include <QHash>

#include <algorithm>

namespace {

const QString kFollowing = QStringLiteral("following");
const QString kTrending = QStringLiteral("trending");
const QString kEverything = QStringLiteral("everything");

// Distinguishes the reads of every Upcoming page: LibraryService answers
// all of them on the same signals.
QString nextRequestKey()
{
    static quint64 counter = 0;
    return QStringLiteral("upcoming:%1").arg(++counter);
}

// The preset chosen on each connection this session, so the page opens on
// it again.
QHash<QString, QString> &rememberedFilters()
{
    static QHash<QString, QString> filters;
    return filters;
}

// The coarsest the day is checked: a timer does not run while the machine
// sleeps, so the next local midnight alone could pass unnoticed.
constexpr int kDayCheckMs = 60 * 1000;

QString badgeLabel(const QStringList &badges)
{
    QStringList labels;
    if (badges.contains(QStringLiteral("series_premiere"))) {
        labels.append(UpcomingViewModel::tr("Series premiere"));
    } else if (badges.contains(QStringLiteral("season_premiere"))) {
        labels.append(UpcomingViewModel::tr("Season premiere"));
    }
    if (badges.contains(QStringLiteral("finale"))) {
        labels.append(UpcomingViewModel::tr("Finale"));
    }
    return labels.join(QStringLiteral(", "));
}

QString episodeLabel(const QVariantMap &airing)
{
    const int season = airing.value(QStringLiteral("seasonNumber"), -1).toInt();
    const int episode = airing.value(QStringLiteral("episodeNumber"), -1).toInt();
    if (season >= 0 && episode >= 0) {
        return UpcomingViewModel::tr("S%1 E%2").arg(season).arg(episode);
    }
    if (season >= 0) {
        return UpcomingViewModel::tr("Season %1").arg(season);
    }
    return {};
}

struct DayAiring {
    QVariantMap airing;
    QDateTime instant;
    // The local time of the instant, else the network's wall clock
    // (`airTime`); invalid for a bare day.
    QTime time;
};

bool airsBefore(const DayAiring &left, const DayAiring &right)
{
    const bool leftTimed = left.time.isValid();
    const bool rightTimed = right.time.isValid();
    if (leftTimed != rightTimed) {
        return leftTimed;
    }
    if (left.instant.isValid() && right.instant.isValid()) {
        if (left.instant != right.instant) {
            return left.instant < right.instant;
        }
    } else if (leftTimed && left.time != right.time) {
        return left.time < right.time;
    }
    const int byName = QString::localeAwareCompare(
        left.airing.value(QStringLiteral("seriesName")).toString(),
        right.airing.value(QStringLiteral("seriesName")).toString());
    if (byName != 0) {
        return byName < 0;
    }
    for (const QString &key : {QStringLiteral("seasonNumber"), QStringLiteral("episodeNumber")}) {
        const int leftNumber = left.airing.value(key, -1).toInt();
        const int rightNumber = right.airing.value(key, -1).toInt();
        if (leftNumber != rightNumber) {
            return leftNumber < rightNumber;
        }
    }
    return left.airing.value(QStringLiteral("itemId")).toString()
        < right.airing.value(QStringLiteral("itemId")).toString();
}

} // namespace

UpcomingViewModel::UpcomingViewModel(QObject *parent)
    : UpcomingViewModel(ServiceLocator::tryGet<LibraryService>(), parent)
{
}

UpcomingViewModel::UpcomingViewModel(LibraryService *libraryService, QObject *parent)
    : BaseViewModel(parent)
    , m_libraryService(libraryService)
{
    m_dayCheck.setObjectName(QStringLiteral("upcomingDayCheck"));
    m_dayCheck.setSingleShot(true);
    connect(&m_dayCheck, &QTimer::timeout, this, &UpcomingViewModel::checkDay);
    connectService();
}

void UpcomingViewModel::connectService()
{
    if (!m_libraryService) {
        qCWarning(lcViewModels) << "UpcomingViewModel: LibraryService not available";
        return;
    }
    connect(m_libraryService, &LibraryService::canonicalUpcomingAiringsLoaded,
            this, &UpcomingViewModel::onAiringsLoaded);
    connect(m_libraryService, &LibraryService::canonicalUpcomingAiringsFailed,
            this, &UpcomingViewModel::onAiringsFailed);
}

int UpcomingViewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant UpcomingViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_days.size()) {
        return {};
    }
    const QVariantMap day = m_days.at(index.row()).toMap();
    switch (role) {
    case DateRole:
        return day.value(QStringLiteral("date"));
    case TitleRole:
        return day.value(QStringLiteral("title"));
    case AiringsRole:
        return day.value(QStringLiteral("airings"));
    default:
        return {};
    }
}

QHash<int, QByteArray> UpcomingViewModel::roleNames() const
{
    return {
        {DateRole, "date"},
        {TitleRole, "title"},
        {AiringsRole, "airings"}
    };
}

void UpcomingViewModel::setFilter(const QString &filter)
{
    if (filter == m_filter
        || !(filter == kFollowing || filter == kTrending || filter == kEverything)) {
        return;
    }
    m_filter = filter;
    if (!m_connectionId.isEmpty()) {
        rememberedFilters().insert(m_connectionId, filter);
    }
    emit filterChanged();
    emit emptyStateChanged();
    reload();
}

QVariantList UpcomingViewModel::filters() const
{
    const auto entry = [](const QString &id, const QString &text) {
        return QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("text"), text}};
    };
    return {entry(kFollowing, tr("Following")), entry(kTrending, tr("Trending")),
            entry(kEverything, tr("All"))};
}

int UpcomingViewModel::airingCount() const
{
    int total = 0;
    for (const QVariant &day : m_days) {
        total += static_cast<int>(day.toMap().value(QStringLiteral("airings")).toList().size());
    }
    return total;
}

QString UpcomingViewModel::emptyTitle() const
{
    if (m_filterable && m_filter == kFollowing) {
        return tr("Nothing coming up from your shows");
    }
    if (m_filterable && m_filter == kTrending) {
        return tr("Nothing trending coming up");
    }
    return tr("Nothing scheduled");
}

QString UpcomingViewModel::emptyMessage() const
{
    if (m_filterable && m_filter == kFollowing) {
        return tr("Shows you follow (your favorites, your watchlist and shows you've watched) "
                  "have no episodes airing in the next %1 days.").arg(WindowDays);
    }
    if (m_filterable && m_filter == kTrending) {
        return tr("No trending show has an episode airing in the next %1 days.").arg(WindowDays);
    }
    if (m_filterable) {
        return tr("No episodes air in the next %1 days.").arg(WindowDays);
    }
    // Jellyfin lists only the virtual episodes a metadata plugin created;
    // its bundled TMDb plugin does when this option is on and the library
    // is opted in under its Libraries.
    return tr("No episodes air in the next %1 days. Jellyfin lists episodes that are not "
              "in the library yet only when a metadata plugin creates them, such as the "
              "TMDb plugin's \"Create unaired (upcoming) episodes\" option for this "
              "library.").arg(WindowDays);
}

QString UpcomingViewModel::emptyActionFilter() const
{
    return m_filterable && m_filter != kEverything ? kEverything : QString();
}

void UpcomingViewModel::load()
{
    reload();
}

void UpcomingViewModel::reload()
{
    const QString connectionId =
        m_libraryService ? m_libraryService->getActiveConnectionId() : QString();
    // Days of another source or preset never show under this one; a
    // refresh of the same view keeps its days until the answer.
    const bool sameView = connectionId == m_connectionId
        && m_loadedFilter == m_filter && m_loadedAt.isValid();
    if (!sameView && !m_days.isEmpty()) {
        emitModelReset([this]() {
            m_days.clear();
            m_airings.clear();
        });
        emit countChanged();
    }
    if (connectionId != m_connectionId) {
        m_connectionId = connectionId;
        emit connectionChanged();
        // Each connection opens on the preset last chosen on it.
        const QString filter = rememberedFilters().value(connectionId, kFollowing);
        if (filter != m_filter) {
            m_filter = filter;
            emit filterChanged();
            emit emptyStateChanged();
        }
    }
    clearError();
    m_requestKey = nextRequestKey();
    if (!m_libraryService || m_connectionId.isEmpty()) {
        m_requestKey.clear();
        setLoading(false);
        setError(tr("Sign in to see what's coming up."));
        return;
    }
    setLoading(true);
    // May answer synchronously.
    m_libraryService->getUpcomingAirings(m_filter, m_requestKey);
}

void UpcomingViewModel::refreshIfStale()
{
    if (isLoading()) {
        return;
    }
    const QString connectionId =
        m_libraryService ? m_libraryService->getActiveConnectionId() : QString();
    if (connectionId != m_connectionId || !m_loadedAt.isValid()
        || m_loadedAt.msecsTo(now()) > StaleAfterMs) {
        reload();
        return;
    }
    // The day may have changed while the page was hidden.
    regroup();
}

void UpcomingViewModel::onAiringsLoaded(const QString &connectionId,
                                        const QString &requestKey,
                                        const QVariantList &airings,
                                        bool filterable)
{
    if (requestKey.isEmpty() || requestKey != m_requestKey || connectionId != m_connectionId) {
        return;
    }
    m_requestKey.clear();
    m_airings = airings;
    m_loadedAt = now().toUTC();
    m_loadedFilter = m_filter;
    if (filterable != m_filterable) {
        m_filterable = filterable;
        emit filterableChanged();
        emit emptyStateChanged();
    }
    regroup();
    setLoading(false);
}

void UpcomingViewModel::onAiringsFailed(const QString &connectionId,
                                        const QString &requestKey,
                                        const QString &error)
{
    if (requestKey.isEmpty() || requestKey != m_requestKey || connectionId != m_connectionId) {
        return;
    }
    m_requestKey.clear();
    qCWarning(lcViewModels) << "UpcomingViewModel: calendar read failed:" << error;
    setError(error.isEmpty() ? tr("The server didn't answer.") : error);
    setLoading(false);
    // The days of the last read stay; the page says the refresh failed.
    if (!m_days.isEmpty()) {
        emit refreshFailed(errorMessage());
    }
}

QVariantMap UpcomingViewModel::get(int row) const
{
    return row >= 0 && row < m_days.size() ? m_days.at(row).toMap() : QVariantMap{};
}

QVariantMap UpcomingViewModel::airingAt(int row, int index) const
{
    const QVariantList airings = get(row).value(QStringLiteral("airings")).toList();
    return index >= 0 && index < airings.size() ? airings.at(index).toMap() : QVariantMap{};
}

int UpcomingViewModel::indexOfDay(const QString &date) const
{
    for (qsizetype row = 0; row < m_days.size(); ++row) {
        if (m_days.at(row).toMap().value(QStringLiteral("date")).toString() == date) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

int UpcomingViewModel::indexOfAiring(int row, const QString &itemId) const
{
    const QVariantList airings = get(row).value(QStringLiteral("airings")).toList();
    for (qsizetype index = 0; index < airings.size(); ++index) {
        if (airings.at(index).toMap().value(QStringLiteral("itemId")).toString() == itemId) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

QString UpcomingViewModel::dayTitle(const QDate &day, const QDate &today, const QLocale &locale)
{
    if (day == today) {
        return tr("Today");
    }
    if (day == today.addDays(1)) {
        return tr("Tomorrow");
    }
    return locale.toString(day, QStringLiteral("dddd, MMMM d"));
}

QVariantList UpcomingViewModel::groupByDay(const QVariantList &airings,
                                           const QDateTime &now,
                                           const QTimeZone &zone,
                                           const QLocale &locale,
                                           int windowDays)
{
    const QDate today = now.toTimeZone(zone).date();
    const QDate lastDay = today.addDays(qMax(0, windowDays));
    QMap<QDate, QList<DayAiring>> byDay;
    for (const QVariant &value : airings) {
        QVariantMap airing = value.toMap();
        const QDateTime instant = QDateTime::fromString(
            airing.value(QStringLiteral("airAt")).toString(), Qt::ISODate);
        // An instant falls on the viewer's day in zone; a bare day is
        // already the day it airs.
        const QDate day = instant.isValid()
            ? instant.toTimeZone(zone).date()
            : QDate::fromString(airing.value(QStringLiteral("airDate")).toString(), Qt::ISODate);
        if (!day.isValid() || day < today || day > lastDay) {
            continue;
        }
        // An instant reads on the viewer's clock; a network wall clock
        // with no zone (Silo) is shown as the network's time.
        const QTime clock = instant.isValid()
            ? instant.toTimeZone(zone).time()
            : QTime::fromString(airing.value(QStringLiteral("airTime")).toString(),
                                QStringLiteral("HH:mm"));
        const QString time =
            clock.isValid() ? locale.toString(clock, QLocale::ShortFormat) : QString();
        const QString episode = episodeLabel(airing);
        const QString badge = badgeLabel(airing.value(QStringLiteral("badges")).toStringList());
        QStringList parts;
        for (const QString &part : {episode, time, badge}) {
            if (!part.isEmpty()) {
                parts.append(part);
            }
        }
        airing.insert(QStringLiteral("localDate"), day.toString(Qt::ISODate));
        airing.insert(QStringLiteral("timeLabel"), time);
        airing.insert(QStringLiteral("episodeLabel"), episode);
        airing.insert(QStringLiteral("badgeLabel"), badge);
        airing.insert(QStringLiteral("subtitle"), parts.join(QStringLiteral("  ·  ")));
        byDay[day].append({airing, instant, clock});
    }

    QVariantList days;
    for (auto it = byDay.begin(); it != byDay.end(); ++it) {
        std::stable_sort(it->begin(), it->end(), airsBefore);
        QVariantList dayAirings;
        dayAirings.reserve(it->size());
        for (const DayAiring &entry : std::as_const(*it)) {
            dayAirings.append(entry.airing);
        }
        days.append(QVariantMap{
            {QStringLiteral("date"), it.key().toString(Qt::ISODate)},
            {QStringLiteral("title"), dayTitle(it.key(), today, locale)},
            {QStringLiteral("airings"), dayAirings}});
    }
    return days;
}

void UpcomingViewModel::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

void UpcomingViewModel::setTimeZone(const QTimeZone &zone)
{
    m_zone = zone;
}

void UpcomingViewModel::forgetFilters()
{
    rememberedFilters().clear();
}

QDateTime UpcomingViewModel::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

void UpcomingViewModel::regroup()
{
    const QDateTime current = now();
    m_groupedDay = current.toTimeZone(m_zone).date();
    const QVariantList days = groupByDay(m_airings, current, m_zone);
    if (days != m_days) {
        emitModelReset([this, &days]() { m_days = days; });
        emit countChanged();
    }
    scheduleDayCheck();
}

void UpcomingViewModel::checkDay()
{
    if (now().toTimeZone(m_zone).date() != m_groupedDay) {
        regroup();
        // After a long sleep the calendar may be stale too.
        refreshIfStale();
    } else {
        scheduleDayCheck();
    }
}

void UpcomingViewModel::scheduleDayCheck()
{
    // Just past the next local midnight, so "Today" moves on, and at least
    // every kDayCheckMs: after a suspend across midnight the timer, which
    // stood still, is late by the whole sleep.
    const QDateTime current = now().toTimeZone(m_zone);
    const QDateTime midnight = current.date().addDays(1).startOfDay(m_zone);
    m_dayCheck.start(static_cast<int>(
        std::clamp(current.msecsTo(midnight) + 1000, qint64{1000}, qint64{kDayCheckMs})));
}
