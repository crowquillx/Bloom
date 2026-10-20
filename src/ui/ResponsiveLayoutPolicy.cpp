#include "ResponsiveLayoutPolicy.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Bloom::ResponsiveLayoutPolicy {

namespace {

// Height of the virtual design canvas, in Theme design units. Every Theme
// token is authored against this canvas, so a design unit always covers
// 1/REFERENCE_HEIGHT of the screen. 1272 reproduces the layout Bloom used at
// 2560x1440 logical (a 4K panel at 150%), which is the tuned 10-foot look.
constexpr qreal REFERENCE_HEIGHT = 1272.0;
constexpr qreal REFERENCE_ASPECT = 16.0 / 9.0;
constexpr qreal REFERENCE_WIDTH = REFERENCE_HEIGHT * REFERENCE_ASPECT;

// Grid columns and home row items on the 16:9 reference canvas. Wider
// viewports add items in proportion to their extra width so cards keep the
// same on-screen size.
constexpr int GRID_COLUMNS_REFERENCE = 7;
constexpr int HOME_ROW_VISIBLE_ITEMS_REFERENCE = 6;

} // namespace

LayoutMetrics computeLayout(int logicalWidth, int logicalHeight, qreal manualScaleOverride)
{
    Q_ASSERT(logicalHeight > 0);

    LayoutMetrics metrics;
    metrics.aspectRatio = static_cast<qreal>(logicalWidth) / static_cast<qreal>(logicalHeight);

    // Fit the reference canvas inside the viewport: height-driven on 16:9 and
    // wider screens, width-driven on narrower windows so nothing overflows.
    metrics.baseLayoutScale = std::min(logicalHeight / REFERENCE_HEIGHT,
                                       logicalWidth / REFERENCE_WIDTH);
    metrics.layoutScale = metrics.baseLayoutScale * manualScaleOverride;

    const qreal widthInReferenceCanvases = std::max(1.0, metrics.aspectRatio / REFERENCE_ASPECT);
    metrics.gridColumns = qRound(GRID_COLUMNS_REFERENCE * widthInReferenceCanvases);
    metrics.homeRowVisibleItems = qRound(HOME_ROW_VISIBLE_ITEMS_REFERENCE * widthInReferenceCanvases);

    metrics.sidebarDefaultMode = QStringLiteral("rail");
    return metrics;
}

QRect toNativePixelRect(const QRectF &logicalRect, qreal devicePixelRatio, const QSize &nativeClientSize)
{
    const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;

    int left = qRound(logicalRect.left() * dpr);
    int top = qRound(logicalRect.top() * dpr);
    int right = qRound(logicalRect.right() * dpr);
    int bottom = qRound(logicalRect.bottom() * dpr);

    if (nativeClientSize.isValid()) {
        const int slack = static_cast<int>(std::ceil(dpr));
        const auto snap = [slack](int edge, int bound) {
            return std::abs(edge - bound) <= slack ? bound : edge;
        };
        left = std::clamp(snap(left, 0), 0, nativeClientSize.width());
        top = std::clamp(snap(top, 0), 0, nativeClientSize.height());
        right = std::clamp(snap(right, nativeClientSize.width()), left, nativeClientSize.width());
        bottom = std::clamp(snap(bottom, nativeClientSize.height()), top, nativeClientSize.height());
    }

    return QRect(left, top, right - left, bottom - top);
}

} // namespace Bloom::ResponsiveLayoutPolicy
