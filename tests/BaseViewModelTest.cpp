#include <QtTest/QtTest>
#include <QtTest/QSignalSpy>
#include "viewmodels/BaseViewModel.h"

class TestViewModel : public BaseViewModel
{
    Q_OBJECT

public:
    explicit TestViewModel(QObject *parent = nullptr) : BaseViewModel(parent) {}

    void reload() override { reloadCalled = true; }

    void triggerLoading(bool loading) { setLoading(loading); }
    void triggerError(const QString &msg) { setError(msg); }
    void triggerClearError() { clearError(); }

    bool reloadCalled = false;
};

class BaseViewModelTest : public QObject
{
    Q_OBJECT

private slots:
    void loadingSignals();
    void errorSignals();
    void reloadNoop();
};

void BaseViewModelTest::loadingSignals()
{
    TestViewModel vm;
    QSignalSpy loadingSpy(&vm, &BaseViewModel::isLoadingChanged);

    vm.triggerLoading(true);
    QCOMPARE(vm.isLoading(), true);
    QCOMPARE(loadingSpy.count(), 1);

    // no duplicate signal when value unchanged
    vm.triggerLoading(true);
    QCOMPARE(loadingSpy.count(), 1);

    vm.triggerLoading(false);
    QCOMPARE(vm.isLoading(), false);
    QCOMPARE(loadingSpy.count(), 2);
}

void BaseViewModelTest::errorSignals()
{
    TestViewModel vm;
    QSignalSpy hasErrorSpy(&vm, &BaseViewModel::hasErrorChanged);
    QSignalSpy messageSpy(&vm, &BaseViewModel::errorMessageChanged);

    vm.triggerError("boom");
    QCOMPARE(vm.hasError(), true);
    QCOMPARE(vm.errorMessage(), QStringLiteral("boom"));
    QCOMPARE(hasErrorSpy.count(), 1);
    QCOMPARE(messageSpy.count(), 1);

    // no duplicate when same error
    vm.triggerError("boom");
    QCOMPARE(hasErrorSpy.count(), 1);
    QCOMPARE(messageSpy.count(), 1);

    vm.triggerClearError();
    QCOMPARE(vm.hasError(), false);
    QCOMPARE(vm.errorMessage(), QString());
    QCOMPARE(hasErrorSpy.count(), 2);
    QCOMPARE(messageSpy.count(), 2);
}

void BaseViewModelTest::reloadNoop()
{
    TestViewModel vm;
    QSignalSpy loadingSpy(&vm, &BaseViewModel::isLoadingChanged);

    vm.reload();
    QVERIFY(vm.reloadCalled);
    QCOMPARE(loadingSpy.count(), 0);
}

QTEST_MAIN(BaseViewModelTest)
#include "BaseViewModelTest.moc"







