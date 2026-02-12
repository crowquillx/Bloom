#include "ResponsiveLayoutManager.h"
#include "../utils/ConfigManager.h"
#include "../core/ServiceLocator.h"
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>

ResponsiveLayoutManager::ResponsiveLayoutManager(QObject *parent)
    : QObject(parent)
{
    qDebug() << "ResponsiveLayoutManager: Initialized with defaults";
    
    // Connect to ConfigManager for manual DPI scale override changes
    connectToConfigManager();
}

ResponsiveLayoutManager::~ResponsiveLayoutManager()
{
    // Window reference is not owned, no cleanup needed
}

void ResponsiveLayoutManager::connectToConfigManager()
{
    // Get ConfigManager from ServiceLocator
    ConfigManager* configManager = ServiceLocator::tryGet<ConfigManager>();
    if (configManager) {
        connect(configManager, &ConfigManager::manualDpiScaleOverrideChanged,
                this, &ResponsiveLayoutManager::onManualDpiScaleOverrideChanged);
        qDebug() << "ResponsiveLayoutManager: Connected to ConfigManager for manualDpiScaleOverride changes";
    } else {
        qWarning() << "ResponsiveLayoutManager: ConfigManager not available in ServiceLocator";
    }
}

void ResponsiveLayoutManager::onManualDpiScaleOverrideChanged()
{
    qDebug() << "ResponsiveLayoutManager: manualDpiScaleOverride changed, updating layout";
    updateLayout();
}

void ResponsiveLayoutManager::setWindow(QQuickWindow* window)
{
    if (m_window) {
        // Disconnect from old window
        disconnect(m_window, &QQuickWindow::widthChanged, this, &ResponsiveLayoutManager::updateLayout);
        disconnect(m_window, &QQuickWindow::heightChanged, this, &ResponsiveLayoutManager::updateLayout);
        disconnect(m_window, &QQuickWindow::screenChanged, this, &ResponsiveLayoutManager::updateLayout);
    }
    
    m_window = window;
    
    if (m_window) {
        // Connect to new window geometry signals
        connect(m_window, &QQuickWindow::widthChanged, this, &ResponsiveLayoutManager::updateLayout);
        connect(m_window, &QQuickWindow::heightChanged, this, &ResponsiveLayoutManager::updateLayout);
        
        // Connect to screen changes for multi-monitor support
        // This detects when the window moves to a different screen
        connect(m_window, &QQuickWindow::screenChanged, this, &ResponsiveLayoutManager::updateLayout);
        
        // Initial layout calculation
        updateLayout();
        
        qDebug() << "ResponsiveLayoutManager: Window set, connected to geometry and screen change signals";
    }
}

QString ResponsiveLayoutManager::breakpoint() const
{
    return m_breakpoint;
}

qreal ResponsiveLayoutManager::layoutScale() const
{
    return m_layoutScale;
}

int ResponsiveLayoutManager::gridColumns() const
{
    return m_gridColumns;
}

int ResponsiveLayoutManager::homeRowVisibleItems() const
{
    return m_homeRowVisibleItems;
}

QString ResponsiveLayoutManager::sidebarDefaultMode() const
{
    return m_sidebarDefaultMode;
}

qreal ResponsiveLayoutManager::aspectRatio() const
{
    return m_aspectRatio;
}

int ResponsiveLayoutManager::viewportWidth() const
{
    return m_viewportWidth;
}

int ResponsiveLayoutManager::viewportHeight() const
{
    return m_viewportHeight;
}

void ResponsiveLayoutManager::updateLayout()
{
    if (!m_window) {
        qWarning() << "ResponsiveLayoutManager: No window set, using defaults";
        return;
    }
    
    // Get window geometry
    int newWidth = m_window->width();
    int newHeight = m_window->height();
    
    // Get device pixel ratio for high-DPI handling
    qreal dpr = m_window->devicePixelRatio();
    
    // Calculate effective height (handles high-DPI scenarios)
    int effectiveHeight = calculateEffectiveHeight(newHeight, dpr);
    
    // Calculate aspect ratio (guard against division by zero)
    if (newHeight == 0) {
        qWarning() << "ResponsiveLayoutManager: newHeight is 0, skipping layout update";
        return;
    }
    qreal newAspectRatio = static_cast<qreal>(newWidth) / static_cast<qreal>(newHeight);
    
    // Calculate new values
    QString newBreakpoint = calculateBreakpoint(effectiveHeight);
    qreal baseLayoutScale = calculateBaseLayoutScale(effectiveHeight, newBreakpoint);
    int newGridColumns = calculateGridColumns(newBreakpoint, newAspectRatio);
    QString newSidebarMode = calculateSidebarMode(newBreakpoint);
    
    // Apply manual DPI scale override from ConfigManager
    qreal manualOverride = 1.0;
    ConfigManager* configManager = ServiceLocator::tryGet<ConfigManager>();
    if (configManager) {
        manualOverride = configManager->getManualDpiScaleOverride();
    }
    qreal newLayoutScale = baseLayoutScale * manualOverride;
    
    // Track what changed (using _changed suffix to avoid shadowing signal names)
    bool breakpoint_changed = (m_breakpoint != newBreakpoint);
    bool layoutScale_changed = !qFuzzyCompare(m_layoutScale, newLayoutScale);
    bool gridColumns_changed = (m_gridColumns != newGridColumns);
    bool sidebarMode_changed = (m_sidebarDefaultMode != newSidebarMode);
    bool aspectRatio_changed = !qFuzzyCompare(m_aspectRatio, newAspectRatio);
    bool width_changed = (m_viewportWidth != newWidth);
    bool height_changed = (m_viewportHeight != newHeight);
    
    // Update member variables
    m_viewportWidth = newWidth;
    m_viewportHeight = newHeight;
    m_aspectRatio = newAspectRatio;
    m_breakpoint = newBreakpoint;
    m_layoutScale = newLayoutScale;
    m_gridColumns = newGridColumns;
    m_sidebarDefaultMode = newSidebarMode;
    
    // Emit signals for changed properties
    if (width_changed) {
        emit viewportWidthChanged();
    }
    if (height_changed) {
        emit viewportHeightChanged();
    }
    if (aspectRatio_changed) {
        emit aspectRatioChanged();
    }
    if (breakpoint_changed) {
        qDebug() << "ResponsiveLayoutManager: Breakpoint changed to" << newBreakpoint;
        emit breakpointChanged();
    }
    if (layoutScale_changed) {
        emit layoutScaleChanged();
    }
    if (gridColumns_changed) {
        qDebug() << "ResponsiveLayoutManager: Grid columns changed to" << newGridColumns;
        emit gridColumnsChanged();
    }
    if (sidebarMode_changed) {
        qDebug() << "ResponsiveLayoutManager: Sidebar mode changed to" << newSidebarMode;
        emit sidebarDefaultModeChanged();
    }
    
    // Debug output
    qDebug() << "ResponsiveLayoutManager: Layout updated -"
             << "viewport:" << newWidth << "x" << newHeight
             << "effectiveHeight:" << effectiveHeight
             << "DPR:" << dpr
             << "breakpoint:" << newBreakpoint
             << "baseScale:" << baseLayoutScale
             << "manualOverride:" << manualOverride
             << "finalLayoutScale:" << newLayoutScale
             << "gridColumns:" << newGridColumns
             << "aspectRatio:" << newAspectRatio;
}

QString ResponsiveLayoutManager::calculateBreakpoint(int effectiveHeight) const
{
    if (effectiveHeight < BREAKPOINT_SMALL_MAX) {
        return "Small";
    } else if (effectiveHeight < BREAKPOINT_MEDIUM_MAX) {
        return "Medium";
    } else if (effectiveHeight < BREAKPOINT_LARGE_MAX) {
        return "Large";
    } else {
        return "XL";
    }
}

qreal ResponsiveLayoutManager::calculateBaseLayoutScale(int effectiveHeight, const QString& breakpoint) const
{
    // Calculate continuous scale within breakpoint range
    // Scale ranges from 0.6 (small) to 1.5 (XL)
    
    qreal scale;
    
    if (breakpoint == "Small") {
        // Small: 0px to 850px -> scale 0.6 to 0.8
        // Linear interpolation
        scale = 0.6 + (static_cast<qreal>(effectiveHeight) / BREAKPOINT_SMALL_MAX) * 0.2;
    } else if (breakpoint == "Medium") {
        // Medium: 850px to 1150px -> scale 0.8 to 1.0
        qreal rangePosition = static_cast<qreal>(effectiveHeight - BREAKPOINT_SMALL_MAX) 
                            / static_cast<qreal>(BREAKPOINT_MEDIUM_MAX - BREAKPOINT_SMALL_MAX);
        scale = 0.8 + rangePosition * 0.2;
    } else if (breakpoint == "Large") {
        // Large: 1150px to 1700px -> scale 1.0 to 1.25
        qreal rangePosition = static_cast<qreal>(effectiveHeight - BREAKPOINT_MEDIUM_MAX) 
                            / static_cast<qreal>(BREAKPOINT_LARGE_MAX - BREAKPOINT_MEDIUM_MAX);
        scale = 1.0 + rangePosition * 0.25;
    } else {
        // XL: 1700px+ -> scale 1.25 to 1.5 (capped)
        // Use logarithmic scaling for very large displays
        qreal extraHeight = static_cast<qreal>(effectiveHeight - BREAKPOINT_LARGE_MAX);
        qreal extraScale = qMin(0.25, extraHeight / 2000.0 * 0.25);
        scale = 1.25 + extraScale;
    }
    
    // Clamp to bounds
    return qBound(LAYOUT_SCALE_MIN, scale, LAYOUT_SCALE_MAX);
}

int ResponsiveLayoutManager::calculateGridColumns(const QString& breakpoint, qreal aspectRatio) const
{
    // Base columns from breakpoint
    int baseColumns;
    if (breakpoint == "Small") {
        baseColumns = GRID_COLUMNS_SMALL;
    } else if (breakpoint == "Medium") {
        baseColumns = GRID_COLUMNS_MEDIUM;
    } else if (breakpoint == "Large") {
        baseColumns = GRID_COLUMNS_LARGE;
    } else {
        baseColumns = GRID_COLUMNS_XL;
    }
    
    // Ultrawide adjustment: add columns if aspect ratio > 2.2
    if (aspectRatio > ULTRAWIDE_THRESHOLD) {
        // Calculate extra columns based on how much wider than threshold
        qreal extraWidth = aspectRatio - ULTRAWIDE_THRESHOLD;
        int extraColumns = qMin(ULTRAWIDE_MAX_EXTRA_COLUMNS, static_cast<int>(extraWidth * 2));
        baseColumns += extraColumns;
        qDebug() << "ResponsiveLayoutManager: Ultrawide detected (aspectRatio:" << aspectRatio 
                 << "), adding" << extraColumns << "columns";
    }
    
    return baseColumns;
}

QString ResponsiveLayoutManager::calculateSidebarMode(const QString& breakpoint) const
{
    if (breakpoint == "Small") {
        return "overlay";
    } else if (breakpoint == "XL") {
        return "expanded";
    } else {
        return "rail";
    }
}

int ResponsiveLayoutManager::calculateEffectiveHeight(int logicalHeight, qreal devicePixelRatio) const
{
    // On Windows with high-DPI (e.g., 4K@300% scaling), Qt reports logical height
    // which would incorrectly result in a smaller breakpoint.
    // Solution: When devicePixelRatio > 1.5, use physical height for calculation.
    
    if (devicePixelRatio > HIGH_DPI_THRESHOLD) {
        int physicalHeight = qRound(logicalHeight * devicePixelRatio);
        qDebug() << "ResponsiveLayoutManager: High-DPI detected (DPR:" << devicePixelRatio 
                 << "), using physical height:" << physicalHeight;
        return physicalHeight;
    }
    
    return logicalHeight;
}
