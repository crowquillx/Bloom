#include <QtTest/QtTest>

#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>

#include <memory>

// Paging in DiscoveryListModel.qml: the rest of a trimmed row, pages of
// repeated titles, and the bounds that keep paging from looping.
class DiscoveryListModelTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void trimmedRowRereadsItsFirstPage();
    void trimmedRowContinuesMidList();
    void repeatedPagesReadOnAFewAtMost();
    void newTitlesResetTheRepeatCount();
    void samePageIsNeverReadAgain();
    void onlyThePageBeingReadIsTaken();
    void failedPageStopsPaging();

private:
    // Titles first..last, keyed by itemId.
    static QVariantList titles(int first, int last);
    static QVariantMap row(const QVariantList &items, int nextPage, bool hasMore);
    void seed(const QVariantList &items, int nextPage, bool hasMore);
    int beginNextPage();
    int acceptPage(int page, const QVariantMap &row);
    bool failPage(int page);
    int count() const { return m_model->property("count").toInt(); }
    int intProperty(const char *name) const { return m_model->property(name).toInt(); }

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QObject> m_model;
};

void DiscoveryListModelTest::init()
{
    m_engine = std::make_unique<QQmlEngine>();
    QQmlComponent component(m_engine.get(),
                            QUrl::fromLocalFile(QDir(QStringLiteral(BLOOM_UI_SOURCE_DIR))
                                                    .filePath(QStringLiteral("DiscoveryListModel.qml"))));
    m_model.reset(component.create());
    QVERIFY2(m_model, qPrintable(component.errorString()));
}

void DiscoveryListModelTest::cleanup()
{
    m_model.reset();
    m_engine.reset();
}

QVariantList DiscoveryListModelTest::titles(int first, int last)
{
    QVariantList items;
    for (int id = first; id <= last; ++id) {
        items.append(QVariantMap{{QStringLiteral("itemId"), QString::number(id)},
                                 {QStringLiteral("name"), QStringLiteral("Title %1").arg(id)}});
    }
    return items;
}

QVariantMap DiscoveryListModelTest::row(const QVariantList &items, int nextPage, bool hasMore)
{
    return {{QStringLiteral("items"), items},
            {QStringLiteral("nextPage"), nextPage},
            {QStringLiteral("hasMore"), hasMore}};
}

// As DiscoverScreen fills a row from Discover's first page.
void DiscoveryListModelTest::seed(const QVariantList &items, int nextPage, bool hasMore)
{
    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(m_model.get(), "appendItems", Q_RETURN_ARG(QVariant, added),
                                      Q_ARG(QVariant, QVariant(items))));
    QCOMPARE(added.toInt(), items.size());
    m_model->setProperty("nextPage", nextPage);
    m_model->setProperty("hasMore", hasMore);
}

int DiscoveryListModelTest::beginNextPage()
{
    QVariant page;
    QMetaObject::invokeMethod(m_model.get(), "beginNextPage", Q_RETURN_ARG(QVariant, page));
    return page.toInt();
}

int DiscoveryListModelTest::acceptPage(int page, const QVariantMap &row)
{
    QVariant next;
    QMetaObject::invokeMethod(m_model.get(), "acceptPage", Q_RETURN_ARG(QVariant, next),
                              Q_ARG(QVariant, page), Q_ARG(QVariant, QVariant(row)));
    return next.toInt();
}

bool DiscoveryListModelTest::failPage(int page)
{
    QVariant failed;
    QMetaObject::invokeMethod(m_model.get(), "failPage", Q_RETURN_ARG(QVariant, failed),
                              Q_ARG(QVariant, page));
    return failed.toBool();
}

void DiscoveryListModelTest::trimmedRowRereadsItsFirstPage()
{
    // Discover showed 10 of an 18-title list and said to read page 1.
    seed(titles(1, 10), 1, true);
    QCOMPARE(beginNextPage(), 1);
    // A read is under way: no second one starts.
    QCOMPARE(beginNextPage(), 0);

    // Page 1 comes back whole: its first 10 titles are skipped.
    QCOMPARE(acceptPage(1, row(titles(1, 18), 2, false)), 0);
    QCOMPARE(count(), 18);
    QCOMPARE(intProperty("loadingPage"), 0);
    QVERIFY(!m_model->property("hasMore").toBool());
    QCOMPARE(beginNextPage(), 0);
}

void DiscoveryListModelTest::trimmedRowContinuesMidList()
{
    // 90 titles shown: pages 1-4 and half of page 5.
    seed(titles(1, 90), 5, true);
    QCOMPARE(beginNextPage(), 5);
    QCOMPARE(acceptPage(5, row(titles(81, 100), 6, true)), 0);
    QCOMPARE(count(), 100);
    QCOMPARE(intProperty("nextPage"), 6);
    QCOMPARE(beginNextPage(), 6);
}

void DiscoveryListModelTest::repeatedPagesReadOnAFewAtMost()
{
    seed(titles(1, 20), 2, true);
    QCOMPARE(beginNextPage(), 2);
    // Pages of titles already shown read on at once...
    QCOMPARE(acceptPage(2, row(titles(1, 20), 3, true)), 3);
    QCOMPARE(intProperty("loadingPage"), 3);
    QCOMPARE(acceptPage(3, row(titles(5, 15), 4, true)), 4);
    // ...three at most: then the row stops.
    QCOMPARE(acceptPage(4, row(titles(1, 20), 5, true)), 0);
    QCOMPARE(intProperty("repeatedPages"), 3);
    QVERIFY(!m_model->property("hasMore").toBool());
    QCOMPARE(intProperty("loadingPage"), 0);
    QCOMPARE(beginNextPage(), 0);
    QCOMPARE(count(), 20);
}

void DiscoveryListModelTest::newTitlesResetTheRepeatCount()
{
    seed(titles(1, 20), 2, true);
    QCOMPARE(beginNextPage(), 2);
    QCOMPARE(acceptPage(2, row(titles(1, 20), 3, true)), 3);
    QCOMPARE(acceptPage(3, row(titles(1, 20), 4, true)), 4);
    // One new title is progress: the next page waits for the viewer.
    QCOMPARE(acceptPage(4, row(titles(1, 21), 5, true)), 0);
    QCOMPARE(intProperty("repeatedPages"), 0);
    QVERIFY(m_model->property("hasMore").toBool());
    QCOMPARE(count(), 21);
    QCOMPARE(beginNextPage(), 5);
}

void DiscoveryListModelTest::samePageIsNeverReadAgain()
{
    seed(titles(1, 20), 2, true);
    QCOMPARE(beginNextPage(), 2);
    // A cursor that does not move ends the row instead of looping.
    QCOMPARE(acceptPage(2, row(titles(1, 20), 2, true)), 0);
    QVERIFY(!m_model->property("hasMore").toBool());
    QCOMPARE(beginNextPage(), 0);

    // Without a cursor, the page after the one read follows.
    cleanup();
    init();
    seed(titles(1, 20), 2, true);
    QCOMPARE(beginNextPage(), 2);
    QCOMPARE(acceptPage(2, row(titles(21, 40), 0, true)), 0);
    QCOMPARE(intProperty("nextPage"), 3);
}

void DiscoveryListModelTest::onlyThePageBeingReadIsTaken()
{
    seed(titles(1, 20), 2, true);
    // Nothing asked for: a page that arrives is not this row's.
    QCOMPARE(acceptPage(2, row(titles(21, 40), 3, true)), 0);
    QCOMPARE(count(), 20);

    QCOMPARE(beginNextPage(), 2);
    QCOMPARE(acceptPage(5, row(titles(21, 40), 6, true)), 0);
    QCOMPARE(count(), 20);
    QCOMPARE(intProperty("loadingPage"), 2);
    QVERIFY(!failPage(5));
    QCOMPARE(intProperty("loadingPage"), 2);
}

void DiscoveryListModelTest::failedPageStopsPaging()
{
    seed(titles(1, 20), 2, true);
    QCOMPARE(beginNextPage(), 2);
    QVERIFY(failPage(2));
    QCOMPARE(intProperty("loadingPage"), 0);
    QVERIFY(!m_model->property("hasMore").toBool());
    QCOMPARE(beginNextPage(), 0);
}

QTEST_MAIN(DiscoveryListModelTest)
#include "DiscoveryListModelTest.moc"
