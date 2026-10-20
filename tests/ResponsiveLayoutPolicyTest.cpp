#include <QTest>

#include <cmath>

#include "ui/ResponsiveLayoutPolicy.h"

using Bloom::ResponsiveLayoutPolicy::computeLayout;
using Bloom::ResponsiveLayoutPolicy::toNativePixelRect;

namespace {

// Fraction of the physical screen height covered by one Theme design unit
// for a panel of physicalHeight at the given OS scale factor. Qt renders
// logical pixels at osScale and Bloom multiplies design units by layoutScale.
qreal screenFractionPerDesignUnit(int physicalWidth, int physicalHeight, qreal osScale)
{
    const int logicalWidth = qRound(physicalWidth / osScale);
    const int logicalHeight = qRound(physicalHeight / osScale);
    const qreal layoutScale = computeLayout(logicalWidth, logicalHeight, 1.0).layoutScale;
    return layoutScale * osScale / physicalHeight;
}

} // namespace

class ResponsiveLayoutPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    // The old breakpoint system applied OS scaling on top of its own scale,
    // so a 4K TV at 300% rendered the UI 2.3x larger than at 150%. Every
    // resolution and scale combination must now cover the same fraction.
    void sizeIsIndependentOfResolutionAndOsScaling()
    {
        const qreal reference = screenFractionPerDesignUnit(3840, 2160, 1.5);

        const QList<QSize> panels{{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
        const QList<qreal> osScales{1.0, 1.25, 1.5, 1.75, 2.0, 2.25, 2.5, 3.0};
        for (const QSize& panel : panels) {
            for (qreal osScale : osScales) {
                const qreal fraction = screenFractionPerDesignUnit(panel.width(), panel.height(), osScale);
                // Logical sizes are rounded to whole pixels, so allow 0.5%.
                QVERIFY2(std::abs(fraction / reference - 1.0) < 0.005,
                         qPrintable(QStringLiteral("%1x%2 at %3: %4 vs %5")
                                        .arg(panel.width()).arg(panel.height())
                                        .arg(osScale).arg(fraction).arg(reference)));
            }
        }
    }

    void preservesTunedLayoutAt4kOneHundredFiftyPercent()
    {
        // 2560x1440 logical is a 4K panel at 150%, the layout Bloom was tuned
        // for under the old breakpoint system (scale ~1.132).
        const auto metrics = computeLayout(2560, 1440, 1.0);
        QVERIFY(std::abs(metrics.layoutScale - 1.1318) < 0.002);
        QCOMPARE(metrics.gridColumns, 7);
        QCOMPARE(metrics.sidebarDefaultMode, QStringLiteral("rail"));
    }

    void wideViewportsAddColumnsInsteadOfGrowing()
    {
        const auto reference = computeLayout(2560, 1440, 1.0);
        const auto ultrawide = computeLayout(3440, 1440, 1.0);
        const auto superUltrawide = computeLayout(5120, 1440, 1.0);

        QCOMPARE(ultrawide.layoutScale, reference.layoutScale);
        QCOMPARE(superUltrawide.layoutScale, reference.layoutScale);
        QCOMPARE(ultrawide.gridColumns, 9);
        QCOMPARE(superUltrawide.gridColumns, 14);
        QCOMPARE(reference.homeRowVisibleItems, 6);
        QCOMPARE(ultrawide.homeRowVisibleItems, 8);
        QCOMPARE(superUltrawide.homeRowVisibleItems, 12);
    }

    void narrowViewportsFitWidth()
    {
        // 4:3 window: the 16:9 canvas is fitted by width so nothing overflows.
        const auto metrics = computeLayout(1440, 1080, 1.0);
        QVERIFY(std::abs(metrics.layoutScale - 1440.0 / (1272.0 * 16.0 / 9.0)) < 1e-9);
        QCOMPARE(metrics.gridColumns, 7);
    }

    void appliesManualOverride()
    {
        const auto automatic = computeLayout(1920, 1080, 1.0);
        const auto overridden = computeLayout(1920, 1080, 0.5);
        QCOMPARE(overridden.baseLayoutScale, automatic.baseLayoutScale);
        QVERIFY(std::abs(overridden.layoutScale - automatic.layoutScale * 0.5) < 1e-9);
    }

    // The Windows mpv host is a native child window positioned in physical
    // pixels. Passing logical pixels made video cover 1/3 x 1/3 of a 4K
    // screen at 300% and 2/3 x 2/3 at 150%.
    void fullWindowViewportCoversNativeClient_data()
    {
        QTest::addColumn<QSize>("nativeClient");
        QTest::addColumn<qreal>("osScale");

        QTest::newRow("4k-100") << QSize(3840, 2160) << 1.0;
        QTest::newRow("4k-150") << QSize(3840, 2160) << 1.5;
        QTest::newRow("4k-300") << QSize(3840, 2160) << 3.0;
        QTest::newRow("4k-work-area-300") << QSize(3840, 2016) << 3.0;
        QTest::newRow("1080p-125") << QSize(1920, 1080) << 1.25;
        QTest::newRow("odd-150") << QSize(1003, 565) << 1.5;
        QTest::newRow("odd-175") << QSize(1001, 667) << 1.75;
    }

    void fullWindowViewportCoversNativeClient()
    {
        QFETCH(QSize, nativeClient);
        QFETCH(qreal, osScale);

        // Qt rounds the logical window size, so it may not divide exactly.
        const QRectF logical(0, 0, qRound(nativeClient.width() / osScale),
                             qRound(nativeClient.height() / osScale));
        QCOMPARE(toNativePixelRect(logical, osScale, nativeClient), QRect(QPoint(0, 0), nativeClient));
    }

    void insetViewportScalesPositionAndSize()
    {
        // Credits shrink mode: 72% of the window, inset from the bottom right.
        const QRectF logical(339.2, 155.52, 921.6, 518.4);
        QCOMPARE(toNativePixelRect(logical, 3.0, QSize(3840, 2160)), QRect(1018, 467, 2764, 1555));
        QCOMPARE(toNativePixelRect(logical, 1.0, QSize()), QRect(339, 156, 922, 518));
    }

    void adjacentRectsTileWithoutGaps()
    {
        const QRect leftHalf = toNativePixelRect(QRectF(0, 0, 333.5, 100), 1.5, QSize());
        const QRect rightHalf = toNativePixelRect(QRectF(333.5, 0, 333.5, 100), 1.5, QSize());
        QCOMPARE(leftHalf.right() + 1, rightHalf.left());
    }

    void clampsToNativeClient()
    {
        const QRect native = toNativePixelRect(QRectF(-20, -20, 2000, 2000), 2.0, QSize(1920, 1080));
        QCOMPARE(native, QRect(0, 0, 1920, 1080));
        QCOMPARE(toNativePixelRect(QRectF(0, 0, 640, 360), 0.0, QSize()), QRect(0, 0, 640, 360));
    }
};

QTEST_GUILESS_MAIN(ResponsiveLayoutPolicyTest)
#include "ResponsiveLayoutPolicyTest.moc"
