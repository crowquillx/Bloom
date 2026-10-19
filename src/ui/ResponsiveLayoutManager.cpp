#include "ResponsiveLayoutManager.h"
#include "ResponsiveLayoutPolicy.h"
#include "../utils/ConfigManager.h"
#include "../core/ServiceLocator.h"
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include "../utils/BloomLogging.h"

ResponsiveLayoutManager::ResponsiveLayoutManager(QObject *parent)
    : QObject(parent)
{
    qCDebug(lcUi) << "ResponsiveLayoutManager: Initialized with defaults";
    
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
        qCDebug(lcUi) << "ResponsiveLayoutManager: Connected to ConfigManager for manualDpiScaleOverride changes";
    } else {
        qCWarning(lcUi) << "ResponsiveLayoutManager: ConfigManager not available in ServiceLocator";
    }
}

void ResponsiveLayoutManager::onManualDpiScaleOverrideChanged()
{
    qCDebug(lcUi) << "ResponsiveLayoutManager: manualDpiScaleOverride changed, updating layout";
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
        
        qCDebug(lcUi) << "ResponsiveLayoutManager: Window set, connected to geometry and screen change signals";
    }
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
        qCWarning(lcUi) << "ResponsiveLayoutManager: No window set, using defaults";
        return;
    }
    
    // Window geometry is in logical pixels. Qt Quick already applies the
    // device pixel ratio (OS display scaling) when rendering, so layout must
    // not convert to physical pixels or OS scaling is applied twice.
    int newWidth = m_window->width();
    int newHeight = m_window->height();
    
    if (newHeight == 0) {
        qCWarning(lcUi) << "ResponsiveLayoutManager: newHeight is 0, skipping layout update";
        return;
    }
    
    // Apply manual DPI scale override from ConfigManager
    qreal manualOverride = 1.0;
    ConfigManager* configManager = ServiceLocator::tryGet<ConfigManager>();
    if (configManager) {
        manualOverride = configManager->getManualDpiScaleOverride();
    }
    
    const auto metrics = Bloom::ResponsiveLayoutPolicy::computeLayout(newWidth, newHeight, manualOverride);
    qreal newLayoutScale = metrics.layoutScale;
    int newGridColumns = metrics.gridColumns;
    int newHomeRowVisibleItems = metrics.homeRowVisibleItems;
    const QString& newSidebarMode = metrics.sidebarDefaultMode;
    qreal newAspectRatio = metrics.aspectRatio;
    
    // Track what changed (prefix with 'b' to avoid shadowing signal names on MSVC)
    bool bLayoutScaleChanged = !qFuzzyCompare(m_layoutScale, newLayoutScale);
    bool bGridColumnsChanged = (m_gridColumns != newGridColumns);
    bool bHomeRowVisibleItemsChanged = (m_homeRowVisibleItems != newHomeRowVisibleItems);
    bool bSidebarModeChanged = (m_sidebarDefaultMode != newSidebarMode);
    bool bAspectRatioChanged = !qFuzzyCompare(m_aspectRatio, newAspectRatio);
    bool bWidthChanged = (m_viewportWidth != newWidth);
    bool bHeightChanged = (m_viewportHeight != newHeight);
    
    // Update member variables
    m_viewportWidth = newWidth;
    m_viewportHeight = newHeight;
    m_aspectRatio = newAspectRatio;
    m_layoutScale = newLayoutScale;
    m_gridColumns = newGridColumns;
    m_homeRowVisibleItems = newHomeRowVisibleItems;
    m_sidebarDefaultMode = newSidebarMode;
    
    // Emit signals for changed properties
    if (bWidthChanged) {
        emit viewportWidthChanged();
    }
    if (bHeightChanged) {
        emit viewportHeightChanged();
    }
    if (bAspectRatioChanged) {
        emit aspectRatioChanged();
    }
    if (bLayoutScaleChanged) {
        emit layoutScaleChanged();
    }
    if (bGridColumnsChanged) {
        qCDebug(lcUi) << "ResponsiveLayoutManager: Grid columns changed to" << newGridColumns;
        emit gridColumnsChanged();
    }
    if (bHomeRowVisibleItemsChanged) {
        emit homeRowVisibleItemsChanged();
    }
    if (bSidebarModeChanged) {
        qCDebug(lcUi) << "ResponsiveLayoutManager: Sidebar mode changed to" << newSidebarMode;
        emit sidebarDefaultModeChanged();
    }
    
    // Debug output
    qCDebug(lcUi) << "ResponsiveLayoutManager: Layout updated -"
             << "viewport:" << newWidth << "x" << newHeight
             << "DPR:" << m_window->devicePixelRatio()
             << "baseScale:" << metrics.baseLayoutScale
             << "manualOverride:" << manualOverride
             << "finalLayoutScale:" << newLayoutScale
             << "gridColumns:" << newGridColumns
             << "aspectRatio:" << newAspectRatio;
}

