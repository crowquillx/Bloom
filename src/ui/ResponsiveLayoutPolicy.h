#pragma once

#include <QRect>
#include <QRectF>
#include <QSize>
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

/**
 * @brief Maps a rect in a window's logical pixels to its native client pixels.
 *
 * Native child windows (the Windows embedded mpv host) are positioned in
 * physical pixels, while Qt Quick reports item geometry in logical pixels.
 * Edges are rounded independently so the result tiles without gaps, then
 * snapped to the client bounds when within one logical pixel: Qt rounds the
 * native window size, so a full-window viewport can otherwise land a pixel
 * short of, or past, the client edge.
 *
 * @param logicalRect Rect in window-relative logical pixels.
 * @param devicePixelRatio The window's device pixel ratio; values <= 0 mean 1.
 * @param nativeClientSize Native client size; an invalid size skips snapping.
 */
QRect toNativePixelRect(const QRectF &logicalRect, qreal devicePixelRatio, const QSize &nativeClientSize);

} // namespace Bloom::ResponsiveLayoutPolicy
