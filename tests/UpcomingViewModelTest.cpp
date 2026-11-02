#include <QtTest/QtTest>

#include <QSignalSpy>

#include "../src/network/LibraryService.h"
#include "../src/viewmodels/UpcomingViewModel.h"

namespace {

// Records reads instead of sending them; tests answer through the
// service's own signals.
class ScriptedLibraryService : public LibraryService
{
    Q_OBJECT

public:
    struct Read {
        QString filter;
        QString requestKey;
    };

    explicit ScriptedLibraryService(QObject *parent = nullptr)
        : LibraryService(nullptr, parent)
    {
    }

    QString getActiveConnectionId() const override { return connectionId; }
    bool supportsUpcomingAirings() const override { return true; }
    void getUpcomingAirings(const QString &filter, const QString &requestKey) override
    {
        reads.append({filter, requestKey});
    }

    void answer(const QVariantList &airings, bool filterable = true,
                const QString &forConnection = QString())
    {
        answerRead(reads.last(), airings, filterable, forConnection);
    }
    void answerRead(const Read &read, const QVariantList &airings, bool filterable = true,
                    const QString &forConnection = QString())
    {
        emit canonicalUpcomingAiringsLoaded(
            forConnection.isEmpty() ? connectionId : forConnection, read.requestKey, airings,
            filterable);
    }
    void fail(const QString &error = QStringLiteral("Server error"))
    {
        emit canonicalUpcomingAiringsFailed(connectionId, reads.last().requestKey, error);
    }

    QString connectionId = QStringLiteral("connection-1");
    QList<Read> reads;
};

QVariantMap airing(const QString &itemId, const QString &seriesName, int season, int episode,
                   const QString &airDate, const QString &airAt = QString(),
                   const QStringList &badges = {})
{
    QVariantMap map{
        {QStringLiteral("connectionId"), QStringLiteral("connection-1")},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("seriesId"), QStringLiteral("series:") + seriesName},
        {QStringLiteral("seriesName"), seriesName},
        {QStringLiteral("type"), QStringLiteral("episode")},
        {QStringLiteral("seasonNumber"), season},
        {QStringLiteral("episodeNumber"), episode},
        {QStringLiteral("airDate"), airDate},
        {QStringLiteral("badges"), badges}};
    if (!airAt.isEmpty()) {
        map.insert(QStringLiteral("airAt"), airAt);
    }
    return map;
}

QStringList itemIds(const QVariantMap &day)
{
    QStringList ids;
    for (const QVariant &value : day.value(QStringLiteral("airings")).toList()) {
        ids.append(value.toMap().value(QStringLiteral("itemId")).toString());
    }
    return ids;
}

QDateTime utc(const QString &iso)
{
    return QDateTime::fromString(iso, Qt::ISODate);
}

} // namespace

class UpcomingViewModelTest : public QObject
{
    Q_OBJECT

private slots:
    void groupsByLocalDayAcrossTheAutumnTransition();
    void groupsByLocalDayAcrossTheSpringTransition();
    void labelsEpisodesTimesAndBadges();
    void dayTitlesReadTodayTomorrowThenTheDate();
    void loadsThePresetAndIgnoresStaleAnswers();
    void filtersAndEmptyStatesFollowTheProvider();
    void failuresAndMissingConnectionAreErrors();
    void refreshesWhenStaleOrMovedAndRegroupsOnANewDay();
};

void UpcomingViewModelTest::groupsByLocalDayAcrossTheAutumnTransition()
{
    const QTimeZone zone(QByteArrayLiteral("America/New_York"));
    if (!zone.isValid()) {
        QSKIP("No time zone data for America/New_York here");
    }
    const QLocale locale(QLocale::English, QLocale::UnitedStates);
    // 1 November 2026, 10:00 EST: clocks went back from EDT at 06:00 UTC.
    const QDateTime now = utc(QStringLiteral("2026-11-01T15:00:00Z"));
    const QVariantList airings{
        // 23:30 EDT on 31 October: yesterday.
        airing(QStringLiteral("before"), QStringLiteral("A"), 1, 1, QStringLiteral("2026-10-31"),
               QStringLiteral("2026-11-01T03:30:00Z")),
        // 23:30 EST today; the summer offset would put it on 2 November.
        airing(QStringLiteral("late"), QStringLiteral("B"), 1, 2, QStringLiteral("2026-11-02"),
               QStringLiteral("2026-11-02T04:30:00Z")),
        airing(QStringLiteral("dayOnly"), QStringLiteral("A"), 1, 3, QStringLiteral("2026-11-01")),
        // 09:00 EST today, already past but still today's.
        airing(QStringLiteral("morning"), QStringLiteral("C"), 2, 1, QStringLiteral("2026-11-01"),
               QStringLiteral("2026-11-01T14:00:00Z")),
        airing(QStringLiteral("tomorrow"), QStringLiteral("D"), 1, 4, QStringLiteral("2026-11-02")),
        airing(QStringLiteral("thursday"), QStringLiteral("E"), 3, 1, QStringLiteral("2026-11-05")),
        // The window's last day, and the first day past it.
        airing(QStringLiteral("last"), QStringLiteral("F"), 1, 1, QStringLiteral("2026-12-01")),
        airing(QStringLiteral("past"), QStringLiteral("G"), 1, 1, QStringLiteral("2026-12-02")),
        airing(QStringLiteral("yesterday"), QStringLiteral("H"), 1, 1, QStringLiteral("2026-10-31")),
        airing(QStringLiteral("undated"), QStringLiteral("I"), 1, 1, QString())};

    const QVariantList days = UpcomingViewModel::groupByDay(airings, now, zone, locale);
    QCOMPARE(days.size(), 4);
    const QVariantMap today = days.at(0).toMap();
    QCOMPARE(today.value(QStringLiteral("date")).toString(), QStringLiteral("2026-11-01"));
    QCOMPARE(today.value(QStringLiteral("title")).toString(), QStringLiteral("Today"));
    // Timed first by time, then the day-only ones.
    QCOMPARE(itemIds(today), (QStringList{QStringLiteral("morning"), QStringLiteral("late"),
                                          QStringLiteral("dayOnly")}));
    const QVariantMap late = today.value(QStringLiteral("airings")).toList().at(1).toMap();
    QCOMPARE(late.value(QStringLiteral("localDate")).toString(), QStringLiteral("2026-11-01"));
    QCOMPARE(late.value(QStringLiteral("timeLabel")).toString(),
             locale.toString(QTime(23, 30), QLocale::ShortFormat));

    const QVariantMap tomorrow = days.at(1).toMap();
    QCOMPARE(tomorrow.value(QStringLiteral("title")).toString(), QStringLiteral("Tomorrow"));
    QCOMPARE(itemIds(tomorrow), QStringList{QStringLiteral("tomorrow")});
    const QVariantMap thursday = days.at(2).toMap();
    QCOMPARE(thursday.value(QStringLiteral("title")).toString(),
             QStringLiteral("Thursday, November 5"));
    QCOMPARE(days.at(3).toMap().value(QStringLiteral("date")).toString(),
             QStringLiteral("2026-12-01"));
}

void UpcomingViewModelTest::groupsByLocalDayAcrossTheSpringTransition()
{
    const QTimeZone zone(QByteArrayLiteral("America/New_York"));
    if (!zone.isValid()) {
        QSKIP("No time zone data for America/New_York here");
    }
    // 8 March 2026, 12:00 EDT: clocks went forward from EST at 07:00 UTC.
    const QDateTime now = utc(QStringLiteral("2026-03-08T16:00:00Z"));
    const QVariantList airings{
        // 23:30 EST on 7 March; the summer offset would put it on 8 March.
        airing(QStringLiteral("saturday"), QStringLiteral("A"), 1, 1, QStringLiteral("2026-03-07"),
               QStringLiteral("2026-03-08T04:30:00Z")),
        // 23:30 EDT on 8 March; the winter offset agrees, 22:30.
        airing(QStringLiteral("sunday"), QStringLiteral("B"), 1, 1, QStringLiteral("2026-03-08"),
               QStringLiteral("2026-03-09T03:30:00Z")),
        // 00:30 EDT on 9 March; the winter offset would keep it on 8 March.
        airing(QStringLiteral("monday"), QStringLiteral("C"), 1, 1, QStringLiteral("2026-03-09"),
               QStringLiteral("2026-03-09T04:30:00Z"))};
    const QVariantList days = UpcomingViewModel::groupByDay(airings, now, zone);
    QCOMPARE(days.size(), 2);
    QCOMPARE(days.at(0).toMap().value(QStringLiteral("date")).toString(),
             QStringLiteral("2026-03-08"));
    QCOMPARE(itemIds(days.at(0).toMap()), QStringList{QStringLiteral("sunday")});
    QCOMPARE(days.at(1).toMap().value(QStringLiteral("date")).toString(),
             QStringLiteral("2026-03-09"));
    QCOMPARE(itemIds(days.at(1).toMap()), QStringList{QStringLiteral("monday")});
}

void UpcomingViewModelTest::labelsEpisodesTimesAndBadges()
{
    const QTimeZone zone(QTimeZone::UTC);
    const QLocale locale(QLocale::English, QLocale::UnitedStates);
    const QDateTime now = utc(QStringLiteral("2026-11-01T08:00:00Z"));
    QVariantMap seasonPremiere = airing(QStringLiteral("season"), QStringLiteral("B"), 3, -1,
                                        QStringLiteral("2026-11-01"), QString(),
                                        {QStringLiteral("season_premiere")});
    seasonPremiere.insert(QStringLiteral("type"), QStringLiteral("season_premiere"));
    const QVariantList airings{
        airing(QStringLiteral("finale"), QStringLiteral("A"), 2, 10, QStringLiteral("2026-11-01"),
               QStringLiteral("2026-11-01T21:00:00Z"), {QStringLiteral("finale")}),
        seasonPremiere,
        airing(QStringLiteral("pilot"), QStringLiteral("C"), 1, 1, QStringLiteral("2026-11-01"),
               QString(), {QStringLiteral("series_premiere"), QStringLiteral("finale"),
                           QStringLiteral("unknown")}),
        airing(QStringLiteral("plain"), QStringLiteral("D"), 4, 2, QStringLiteral("2026-11-01"))};
    const QVariantList days = UpcomingViewModel::groupByDay(airings, now, zone, locale);
    QCOMPARE(days.size(), 1);
    const QVariantList today = days.first().toMap().value(QStringLiteral("airings")).toList();
    QCOMPARE(today.size(), 4);
    const QString nine = locale.toString(QTime(21, 0), QLocale::ShortFormat);

    const QVariantMap finale = today.at(0).toMap();
    QCOMPARE(finale.value(QStringLiteral("episodeLabel")).toString(), QStringLiteral("S2 E10"));
    QCOMPARE(finale.value(QStringLiteral("badgeLabel")).toString(), QStringLiteral("Finale"));
    QCOMPARE(finale.value(QStringLiteral("subtitle")).toString(),
             QStringLiteral("S2 E10  ·  %1  ·  Finale").arg(nine));
    // Untimed airings follow by series name.
    const QVariantMap season = today.at(1).toMap();
    QCOMPARE(season.value(QStringLiteral("itemId")).toString(), QStringLiteral("season"));
    QCOMPARE(season.value(QStringLiteral("episodeLabel")).toString(), QStringLiteral("Season 3"));
    QCOMPARE(season.value(QStringLiteral("timeLabel")).toString(), QString());
    QCOMPARE(season.value(QStringLiteral("subtitle")).toString(),
             QStringLiteral("Season 3  ·  Season premiere"));
    const QVariantMap pilot = today.at(2).toMap();
    QCOMPARE(pilot.value(QStringLiteral("badgeLabel")).toString(),
             QStringLiteral("Series premiere, Finale"));
    QCOMPARE(today.at(3).toMap().value(QStringLiteral("subtitle")).toString(),
             QStringLiteral("S4 E2"));
}

void UpcomingViewModelTest::dayTitlesReadTodayTomorrowThenTheDate()
{
    const QLocale locale(QLocale::English, QLocale::UnitedStates);
    const QDate today(2026, 11, 1);
    QCOMPARE(UpcomingViewModel::dayTitle(today, today, locale), QStringLiteral("Today"));
    QCOMPARE(UpcomingViewModel::dayTitle(today.addDays(1), today, locale), QStringLiteral("Tomorrow"));
    QCOMPARE(UpcomingViewModel::dayTitle(today.addDays(2), today, locale),
             QStringLiteral("Tuesday, November 3"));
    QCOMPARE(UpcomingViewModel::dayTitle(today.addDays(30), today, locale),
             QStringLiteral("Tuesday, December 1"));
}

void UpcomingViewModelTest::loadsThePresetAndIgnoresStaleAnswers()
{
    ScriptedLibraryService service;
    UpcomingViewModel vm(&service);
    vm.setTimeZone(QTimeZone(QTimeZone::UTC));
    vm.setClock([]() { return utc(QStringLiteral("2026-11-01T08:00:00Z")); });
    QSignalSpy countChanged(&vm, &UpcomingViewModel::countChanged);

    // Following is the default preset.
    QCOMPARE(vm.filter(), QStringLiteral("following"));
    vm.load();
    QVERIFY(vm.isLoading());
    QCOMPARE(vm.connectionId(), QStringLiteral("connection-1"));
    QCOMPARE(service.reads.size(), 1);
    QCOMPARE(service.reads.last().filter, QStringLiteral("following"));
    const ScriptedLibraryService::Read first = service.reads.last();

    // Another connection's answer is not this page's.
    service.answer({airing(QStringLiteral("x"), QStringLiteral("X"), 1, 1,
                           QStringLiteral("2026-11-01"))},
                   true, QStringLiteral("connection-2"));
    QVERIFY(vm.isLoading());
    QCOMPARE(vm.count(), 0);

    service.answer({airing(QStringLiteral("a"), QStringLiteral("A"), 1, 1, QStringLiteral("2026-11-01")),
                    airing(QStringLiteral("b"), QStringLiteral("B"), 1, 2, QStringLiteral("2026-11-03")),
                    airing(QStringLiteral("c"), QStringLiteral("C"), 1, 3, QStringLiteral("2026-11-03"))});
    QVERIFY(!vm.isLoading());
    QVERIFY(!vm.hasError());
    QVERIFY(vm.filterable());
    QCOMPARE(vm.count(), 2);
    QCOMPARE(vm.airingCount(), 3);
    QVERIFY(countChanged.count() > 0);
    QCOMPARE(vm.rowCount(), 2);
    const QModelIndex row = vm.index(1, 0);
    QCOMPARE(vm.data(row, UpcomingViewModel::DateRole).toString(), QStringLiteral("2026-11-03"));
    QCOMPARE(vm.data(row, UpcomingViewModel::TitleRole).toString(),
             UpcomingViewModel::dayTitle(QDate(2026, 11, 3), QDate(2026, 11, 1)));
    QCOMPARE(vm.data(row, UpcomingViewModel::AiringsRole).toList().size(), 2);
    QCOMPARE(vm.indexOfDay(QStringLiteral("2026-11-03")), 1);
    QCOMPARE(vm.indexOfDay(QStringLiteral("2026-11-02")), -1);
    QCOMPARE(vm.indexOfAiring(1, QStringLiteral("c")), 1);
    QCOMPARE(vm.airingAt(1, 1).value(QStringLiteral("itemId")).toString(), QStringLiteral("c"));
    QVERIFY(vm.airingAt(1, 2).isEmpty());
    QVERIFY(vm.get(5).isEmpty());

    // A new preset reads again; the old preset's days leave at once and a
    // late answer to the old read is ignored.
    vm.setFilter(QStringLiteral("trending"));
    QCOMPARE(vm.filter(), QStringLiteral("trending"));
    QCOMPARE(service.reads.size(), 2);
    QCOMPARE(service.reads.last().filter, QStringLiteral("trending"));
    QCOMPARE(vm.count(), 0);
    QVERIFY(vm.isLoading());
    service.answerRead(first, {airing(QStringLiteral("old"), QStringLiteral("O"), 1, 1,
                                      QStringLiteral("2026-11-01"))});
    QCOMPARE(vm.count(), 0);
    service.answer({airing(QStringLiteral("t"), QStringLiteral("T"), 1, 1, QStringLiteral("2026-11-02"))});
    QCOMPARE(vm.count(), 1);
    QCOMPARE(vm.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Tomorrow"));

    // Unknown presets and the current one change nothing.
    vm.setFilter(QStringLiteral("mine"));
    vm.setFilter(QStringLiteral("trending"));
    QCOMPARE(service.reads.size(), 2);

    // The same preset read again keeps its days until the answer.
    vm.reload();
    QCOMPARE(vm.count(), 1);
    QVERIFY(vm.isLoading());
}

void UpcomingViewModelTest::filtersAndEmptyStatesFollowTheProvider()
{
    ScriptedLibraryService service;
    UpcomingViewModel vm(&service);
    vm.setTimeZone(QTimeZone(QTimeZone::UTC));
    vm.setClock([]() { return utc(QStringLiteral("2026-11-01T08:00:00Z")); });

    const QVariantList filters = vm.filters();
    QCOMPARE(filters.size(), 3);
    QCOMPARE(filters.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("following"));
    QCOMPARE(filters.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("trending"));
    QCOMPARE(filters.at(2).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("everything"));

    // Silo: an empty Following calendar offers everything.
    QSignalSpy filterable(&vm, &UpcomingViewModel::filterableChanged);
    vm.load();
    service.answer({}, true);
    QVERIFY(!vm.hasError());
    QCOMPARE(vm.count(), 0);
    QVERIFY(vm.filterable());
    QCOMPARE(filterable.count(), 1);
    QCOMPARE(vm.emptyTitle(), QStringLiteral("Nothing coming up from your shows"));
    QCOMPARE(vm.emptyActionFilter(), QStringLiteral("everything"));
    vm.setFilter(vm.emptyActionFilter());
    service.answer({}, true);
    QCOMPARE(vm.emptyTitle(), QStringLiteral("Nothing scheduled"));
    QVERIFY(vm.emptyActionFilter().isEmpty());

    // Jellyfin honors no preset: an empty answer is "nothing scheduled",
    // with no preset to offer, and airings outside the window count as none.
    ScriptedLibraryService jellyfin;
    UpcomingViewModel plain(&jellyfin);
    plain.setTimeZone(QTimeZone(QTimeZone::UTC));
    plain.setClock([]() { return utc(QStringLiteral("2026-11-01T08:00:00Z")); });
    plain.load();
    jellyfin.answer({airing(QStringLiteral("old"), QStringLiteral("A"), 1, 1,
                            QStringLiteral("2026-10-31"))},
                    false);
    QVERIFY(!plain.hasError());
    QVERIFY(!plain.filterable());
    QCOMPARE(plain.count(), 0);
    QCOMPARE(plain.emptyTitle(), QStringLiteral("Nothing scheduled"));
    QVERIFY(plain.emptyMessage().contains(QStringLiteral("not in the library yet")));
    QVERIFY(plain.emptyActionFilter().isEmpty());
}

void UpcomingViewModelTest::failuresAndMissingConnectionAreErrors()
{
    ScriptedLibraryService service;
    UpcomingViewModel vm(&service);
    vm.load();
    service.fail(QStringLiteral("Server error"));
    QVERIFY(!vm.isLoading());
    QVERIFY(vm.hasError());
    QCOMPARE(vm.errorMessage(), QStringLiteral("Server error"));
    QCOMPARE(vm.count(), 0);

    // Try again clears the error while it reads.
    vm.reload();
    QVERIFY(!vm.hasError());
    QVERIFY(vm.isLoading());
    service.answer({});
    QVERIFY(!vm.hasError());

    // Without a live connection nothing is sent.
    service.connectionId.clear();
    const qsizetype reads = service.reads.size();
    vm.reload();
    QCOMPARE(service.reads.size(), reads);
    QVERIFY(vm.hasError());
    QVERIFY(!vm.isLoading());

    // Without a service the page says so instead of waiting.
    UpcomingViewModel orphan(static_cast<LibraryService *>(nullptr));
    orphan.load();
    QVERIFY(orphan.hasError());
    QVERIFY(!orphan.isLoading());
}

void UpcomingViewModelTest::refreshesWhenStaleOrMovedAndRegroupsOnANewDay()
{
    ScriptedLibraryService service;
    UpcomingViewModel vm(&service);
    vm.setTimeZone(QTimeZone(QTimeZone::UTC));
    QDateTime now = utc(QStringLiteral("2026-11-01T08:00:00Z"));
    vm.setClock([&now]() { return now; });
    vm.load();
    service.answer({airing(QStringLiteral("a"), QStringLiteral("A"), 1, 1, QStringLiteral("2026-11-01")),
                    airing(QStringLiteral("b"), QStringLiteral("B"), 1, 2, QStringLiteral("2026-11-02"))});
    QCOMPARE(vm.count(), 2);
    QCOMPARE(vm.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Today"));

    // A fresh calendar is regrouped, not read: the next day drops
    // yesterday and moves "Today" on.
    now = utc(QStringLiteral("2026-11-02T08:00:00Z"));
    vm.refreshIfStale();
    QCOMPARE(service.reads.size(), 1);
    QCOMPARE(vm.count(), 1);
    QCOMPARE(vm.get(0).value(QStringLiteral("date")).toString(), QStringLiteral("2026-11-02"));
    QCOMPARE(vm.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Today"));

    // Another live connection (a merged space routed elsewhere) reads again,
    // and drops the other source's days.
    service.connectionId = QStringLiteral("connection-2");
    QSignalSpy connectionChanged(&vm, &UpcomingViewModel::connectionChanged);
    vm.refreshIfStale();
    QCOMPARE(service.reads.size(), 2);
    QCOMPARE(connectionChanged.count(), 1);
    QCOMPARE(vm.connectionId(), QStringLiteral("connection-2"));
    QCOMPARE(vm.count(), 0);
    // While it reads, a refresh does not start another.
    vm.refreshIfStale();
    QCOMPARE(service.reads.size(), 2);
}

QTEST_GUILESS_MAIN(UpcomingViewModelTest)
#include "UpcomingViewModelTest.moc"
