#include <QtTest/QtTest>

#include <QSignalSpy>

#include "../src/network/LibraryService.h"
#include "../src/viewmodels/PersonViewModel.h"

namespace {

// Records requests instead of sending them; tests answer through the
// service's own signals.
class ScriptedLibraryService : public LibraryService
{
    Q_OBJECT

public:
    struct PageRequest {
        QString personId;
        int startIndex = 0;
        QString cursor;
        int limit = 0;
        QString requestKey;
    };

    explicit ScriptedLibraryService(QObject *parent = nullptr)
        : LibraryService(nullptr, parent)
    {
    }

    QString getActiveConnectionId() const override { return connectionId; }
    void getPerson(const QString &personId) override { personRequests.append(personId); }
    void getPersonItems(const QString &personId, int startIndex, const QString &cursor,
                        int limit, const QString &requestKey) override
    {
        pageRequests.append({personId, startIndex, cursor, limit, requestKey});
    }

    void answerPerson(const QString &personId, const QVariantMap &person,
                      const QString &forConnection = QString())
    {
        emit canonicalPersonLoaded(forConnection.isEmpty() ? connectionId : forConnection,
                                   personId, person);
    }
    void failPerson(const QString &personId)
    {
        emit canonicalPersonFailed(connectionId, personId, QStringLiteral("Not Found"));
    }
    void answerPage(const QVariantList &items, bool hasMore, const QString &nextCursor = QString(),
                    const QString &forConnection = QString())
    {
        const PageRequest request = pageRequests.last();
        emit canonicalPersonItemsLoaded(forConnection.isEmpty() ? connectionId : forConnection,
                                        request.personId, request.requestKey, items,
                                        int(items.size()), hasMore, nextCursor);
    }
    void failPage()
    {
        const PageRequest request = pageRequests.last();
        emit canonicalPersonItemsFailed(connectionId, request.personId, request.requestKey,
                                        QStringLiteral("Server error"));
    }

    QString connectionId = QStringLiteral("connection-1");
    QStringList personRequests;
    QList<PageRequest> pageRequests;
};

QVariantMap title(const QString &itemId, const QString &mediaType)
{
    return {
        {QStringLiteral("connectionId"), QStringLiteral("connection-1")},
        {QStringLiteral("itemId"), itemId},
        {QStringLiteral("name"), itemId},
        {QStringLiteral("mediaType"), mediaType}
    };
}

// A cast credit as detail pages pass it.
QVariantMap credit(const QString &personId = QStringLiteral("person-1"))
{
    return {
        {QStringLiteral("connectionId"), QStringLiteral("connection-1")},
        {QStringLiteral("personId"), personId},
        {QStringLiteral("name"), QStringLiteral("Al Pacino")},
        {QStringLiteral("role"), QStringLiteral("Michael Corleone")},
        {QStringLiteral("kind"), QStringLiteral("Actor")},
        {QStringLiteral("artwork"), QVariantMap{{QStringLiteral("itemId"), personId}}}
    };
}

QStringList itemIds(const QVariantList &items)
{
    QStringList ids;
    for (const QVariant &item : items) {
        ids.append(item.toMap().value(QStringLiteral("itemId")).toString());
    }
    return ids;
}

} // namespace

class PersonViewModelTest : public QObject
{
    Q_OBJECT

private slots:
    void loadShowsTheSeedThenDetailsAndFirstPage();
    void filmographyKeepsOnlyMoviesAndSeriesOnce();
    void pagingContinuesFromServerCountAndCursor();
    void failedFirstPageIsAnErrorAndReloadRetries();
    void failedDetailsKeepTheSeedWithoutError();
    void failedLaterPageKeepsTitlesAndRetries();
    void staleAnswersAreIgnored();
    void personOfAnotherSourceIsRefused();
};

void PersonViewModelTest::loadShowsTheSeedThenDetailsAndFirstPage()
{
    ScriptedLibraryService service;
    PersonViewModel model(&service);
    QSignalSpy loadingChanged(&model, &BaseViewModel::isLoadingChanged);

    model.load(credit());
    QCOMPARE(service.personRequests, QStringList{QStringLiteral("person-1")});
    QCOMPARE(service.pageRequests.size(), 1);
    QCOMPARE(service.pageRequests.first().startIndex, 0);
    QVERIFY(service.pageRequests.first().cursor.isEmpty());
    QCOMPARE(service.pageRequests.first().limit, PersonViewModel::PageSize);
    QVERIFY(model.isLoading());
    QCOMPARE(model.personId(), QStringLiteral("person-1"));
    QCOMPARE(model.connectionId(), QStringLiteral("connection-1"));
    // The seed names the person at once; a credit's role is not the person's.
    QCOMPARE(model.person().value(QStringLiteral("name")).toString(), QStringLiteral("Al Pacino"));
    QVERIFY(!model.person().contains(QStringLiteral("role")));
    QVERIFY(!model.person().contains(QStringLiteral("kind")));

    service.answerPerson(QStringLiteral("person-1"), {
        {QStringLiteral("personId"), QStringLiteral("person-1")},
        {QStringLiteral("name"), QStringLiteral("Al Pacino")},
        {QStringLiteral("overview"), QStringLiteral("Actor.")},
        {QStringLiteral("birthDate"), QStringLiteral("1940-04-25")}
    });
    QVERIFY(model.isLoading());
    QCOMPARE(model.person().value(QStringLiteral("overview")).toString(), QStringLiteral("Actor."));
    // The read has no photo; the seed's stays.
    QVERIFY(model.person().contains(QStringLiteral("artwork")));

    service.answerPage({title(QStringLiteral("m1"), QStringLiteral("Movie"))}, false);
    QVERIFY(!model.isLoading());
    QVERIFY(!model.hasError());
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), PersonViewModel::ItemIdRole).toString(), QStringLiteral("m1"));
    QCOMPARE(model.roleNames().value(PersonViewModel::ItemDataRole), QByteArrayLiteral("itemData"));
    QVERIFY(!model.hasMore());
    QVERIFY(loadingChanged.count() >= 2);
}

void PersonViewModelTest::filmographyKeepsOnlyMoviesAndSeriesOnce()
{
    ScriptedLibraryService service;
    PersonViewModel model(&service);
    model.load(credit());
    service.answerPage({
        title(QStringLiteral("s1"), QStringLiteral("Series")),
        title(QStringLiteral("m1"), QStringLiteral("Movie")),
        title(QStringLiteral("e1"), QStringLiteral("Episode")),
        title(QStringLiteral("a1"), QStringLiteral("Audio")),
        title(QStringLiteral("b1"), QStringLiteral("BoxSet")),
        title(QStringLiteral("m1"), QStringLiteral("Movie")),
        title(QString(), QStringLiteral("Movie"))
    }, false);

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(itemIds(model.movies()), QStringList{QStringLiteral("m1")});
    QCOMPARE(itemIds(model.series()), QStringList{QStringLiteral("s1")});
}

void PersonViewModelTest::pagingContinuesFromServerCountAndCursor()
{
    ScriptedLibraryService service;
    PersonViewModel model(&service);
    QSignalSpy rowsInserted(&model, &QAbstractItemModel::rowsInserted);
    model.load(credit());
    service.answerPerson(QStringLiteral("person-1"), {{QStringLiteral("name"), QStringLiteral("Al Pacino")}});

    // loadMore waits for the first page.
    model.loadMore();
    QCOMPARE(service.pageRequests.size(), 1);

    service.answerPage({title(QStringLiteral("m1"), QStringLiteral("Movie")),
                        title(QStringLiteral("e1"), QStringLiteral("Episode")),
                        title(QStringLiteral("s1"), QStringLiteral("Series"))},
                       true, QStringLiteral("cursor-2"));
    QVERIFY(model.hasMore());
    QCOMPARE(model.rowCount(), 2);

    model.loadMore();
    QVERIFY(model.loadingMore());
    // One page at a time.
    model.loadMore();
    QCOMPARE(service.pageRequests.size(), 2);
    // The next page starts after everything the server sent, filtered or not.
    QCOMPARE(service.pageRequests.last().startIndex, 3);
    QCOMPARE(service.pageRequests.last().cursor, QStringLiteral("cursor-2"));
    QVERIFY(!model.isLoading());

    service.answerPage({title(QStringLiteral("s1"), QStringLiteral("Series")),
                        title(QStringLiteral("m2"), QStringLiteral("Movie"))},
                       true, QStringLiteral("cursor-3"));
    QVERIFY(!model.loadingMore());
    QCOMPARE(itemIds(model.movies()), (QStringList{QStringLiteral("m1"), QStringLiteral("m2")}));
    QCOMPARE(itemIds(model.series()), QStringList{QStringLiteral("s1")});
    // Appended, never reset.
    QCOMPARE(rowsInserted.count(), 2);

    // An empty page ends paging even if the server claims more.
    model.loadMore();
    QCOMPARE(service.pageRequests.last().startIndex, 5);
    service.answerPage({}, true);
    QVERIFY(!model.hasMore());
    model.loadMore();
    QCOMPARE(service.pageRequests.size(), 3);
}

void PersonViewModelTest::failedFirstPageIsAnErrorAndReloadRetries()
{
    ScriptedLibraryService service;
    PersonViewModel model(&service);
    model.load(credit());
    service.answerPerson(QStringLiteral("person-1"), {{QStringLiteral("name"), QStringLiteral("Al Pacino")}});
    service.failPage();
    QVERIFY(!model.isLoading());
    QVERIFY(model.hasError());
    QVERIFY(!model.errorMessage().isEmpty());

    model.reload();
    QVERIFY(!model.hasError());
    QVERIFY(model.isLoading());
    QCOMPARE(service.personRequests.size(), 2);
    QCOMPARE(service.pageRequests.size(), 2);
    QCOMPARE(service.pageRequests.last().startIndex, 0);
    service.answerPerson(QStringLiteral("person-1"), {{QStringLiteral("name"), QStringLiteral("Al Pacino")}});
    service.answerPage({title(QStringLiteral("m1"), QStringLiteral("Movie"))}, false);
    QVERIFY(!model.isLoading());
    QCOMPARE(model.rowCount(), 1);
}

void PersonViewModelTest::failedDetailsKeepTheSeedWithoutError()
{
    ScriptedLibraryService service;
    PersonViewModel model(&service);
    model.load(credit());
    service.failPerson(QStringLiteral("person-1"));
    service.answerPage({title(QStringLiteral("m1"), QStringLiteral("Movie"))}, false);
    QVERIFY(!model.isLoading());
    QVERIFY(!model.hasError());
    QCOMPARE(model.person().value(QStringLiteral("name")).toString(), QStringLiteral("Al Pacino"));
    QCOMPARE(model.rowCount(), 1);
}

void PersonViewModelTest::failedLaterPageKeepsTitlesAndRetries()
{
    ScriptedLibraryService service;
    PersonViewModel model(&service);
    model.load(credit());
    service.answerPerson(QStringLiteral("person-1"), {{QStringLiteral("name"), QStringLiteral("Al Pacino")}});
    service.answerPage({title(QStringLiteral("m1"), QStringLiteral("Movie"))}, true, QStringLiteral("c2"));

    model.loadMore();
    service.failPage();
    QVERIFY(!model.hasError());
    QVERIFY(!model.loadingMore());
    QVERIFY(model.hasMore());
    QCOMPARE(model.rowCount(), 1);

    model.loadMore();
    QCOMPARE(service.pageRequests.size(), 3);
    QCOMPARE(service.pageRequests.last().cursor, QStringLiteral("c2"));
    QCOMPARE(service.pageRequests.last().startIndex, 1);
}

void PersonViewModelTest::staleAnswersAreIgnored()
{
    ScriptedLibraryService service;
    PersonViewModel model(&service);
    model.load(credit());
    const ScriptedLibraryService::PageRequest firstLoad = service.pageRequests.last();

    // Another connection's answer for the same person.
    service.answerPage({title(QStringLiteral("x1"), QStringLiteral("Movie"))}, false, QString(),
                       QStringLiteral("connection-2"));
    service.answerPerson(QStringLiteral("person-1"),
                         {{QStringLiteral("name"), QStringLiteral("Someone Else")}},
                         QStringLiteral("connection-2"));
    QVERIFY(model.isLoading());
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.person().value(QStringLiteral("name")).toString(), QStringLiteral("Al Pacino"));

    // Another person.
    model.load(credit(QStringLiteral("person-2")));
    emit service.canonicalPersonItemsLoaded(QStringLiteral("connection-1"), QStringLiteral("person-1"),
                                            firstLoad.requestKey,
                                            {title(QStringLiteral("old"), QStringLiteral("Movie"))},
                                            1, false, QString());
    QCOMPARE(model.rowCount(), 0);

    // An older load of the same person.
    model.reload();
    emit service.canonicalPersonItemsLoaded(QStringLiteral("connection-1"), QStringLiteral("person-2"),
                                            service.pageRequests.at(1).requestKey,
                                            {title(QStringLiteral("old"), QStringLiteral("Movie"))},
                                            1, false, QString());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.isLoading());

    service.answerPage({title(QStringLiteral("m2"), QStringLiteral("Movie"))}, false);
    QCOMPARE(itemIds(model.movies()), QStringList{QStringLiteral("m2")});
}

void PersonViewModelTest::personOfAnotherSourceIsRefused()
{
    ScriptedLibraryService service;
    PersonViewModel model(&service);
    QVariantMap person = credit();
    person.insert(QStringLiteral("connectionId"), QStringLiteral("connection-2"));
    model.load(person);
    QVERIFY(service.personRequests.isEmpty());
    QVERIFY(service.pageRequests.isEmpty());
    QVERIFY(!model.isLoading());
    QVERIFY(model.hasError());

    model.load({});
    QVERIFY(service.pageRequests.isEmpty());
    QVERIFY(model.hasError());
}

QTEST_MAIN(PersonViewModelTest)
#include "PersonViewModelTest.moc"
