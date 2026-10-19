#pragma once

#include <QString>

namespace Bloom::ResponsiveLayoutPolicy {

struct LayoutMetrics
{
    qreal baseLayoutScale = 1.0;
    qreal layoutScale = 1.0;
    int gridColumns = 7;
    int homeRowVisibleItems = 6;
    QString sidebarDefaultMode;
    qreal aspectRatio = 16.0 / 9.0;
};

/**
 * @brief Computes resolution-independent layout metrics, 10-foot style.
 *
 * Theme tokens are authored against a fixed 16:9 design canvas. layoutScale
 * maps that canvas onto the window so each element covers the same fraction
 * of the screen at any resolution and any OS display scaling (Kodi-style).
 *
 * Inputs are Qt logical pixels. Qt Quick already multiplies every logical
 * pixel by the device pixel ratio, so a 4K panel at 100%, 150% or 300%
 * produces the same physical result. The device pixel ratio is deliberately
 * not an input.
 *
 * @param logicalWidth Window width in logical pixels.
 * @param logicalHeight Window height in logical pixels; must be > 0.
 * @param manualScaleOverride User override multiplied into layoutScale.
 */
LayoutMetrics computeLayout(int logicalWidth, int logicalHeight, qreal manualScaleOverride);

} // namespace Bloom::ResponsiveLayoutPolicy
