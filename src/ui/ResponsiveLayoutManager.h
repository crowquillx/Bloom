#pragma once

#include <QObject>
#include <QString>
#include <QQuickWindow>

class ConfigManager;

/**
 * @brief Central responsive layout calculation engine for the Bloom HTPC client.
 * 
 * ResponsiveLayoutManager serves as the single source of truth for all responsive
 * layout calculations. It replaces the previous dpiScale system with a comprehensive
 * layoutScale approach that provides:
 * - Unified scaling for both content AND UI chrome
 * - Breakpoint detection and management
 * - Aspect-ratio awareness for ultrawide displays
 * - Clean separation from display hardware concerns (refresh rate, HDR)
 * - Manual DPI scale override support via ConfigManager
 * 
 * ## Breakpoint Model
 * 
 * Height-first breakpoints (using effective viewport height) with aspect-ratio adjustment:
 * 
 * | Breakpoint | Height Range    | Base Columns | Sidebar Default |
 * |-----------|-----------------|--------------|-----------------|
 * | Small     | < 850px         | 4            | Overlay         |
 * | Medium    | 850-1150px      | 6            | Rail            |
 * | Large     | 1150-1700px     | 7            | Rail            |
 * | XL        | >= 1700px       | 8            | Expanded        |
 * 
 * ## High-DPI Handling
 * 
 * When devicePixelRatio > 1.5, uses physical height for calculations:
 * effectiveHeight = logicalHeight * devicePixelRatio
 * 
 * ## Ultrawide Adjustment
 * 
 * If aspectRatio > 2.2, adds 1-2 columns (cap at +2).
 * 
 * ## Manual DPI Scale Override
 * 
 * Users can override the automatic layout scale via ConfigManager::manualDpiScaleOverride.
 * The final layoutScale = calculatedScale * manualDpiScaleOverride
 * 
 * ## Usage in QML
 * 
 * Access via Theme.qml which exposes responsive tokens:
 * - Theme.breakpoint
 * - Theme.layoutScale
 * - Theme.gridColumns
 * - etc.
 */
class ResponsiveLayoutManager : public QObject
{
    Q_OBJECT

    /**
     * @brief Current breakpoint name: "Small", "Medium", "Large", or "XL"
     */
    Q_PROPERTY(QString breakpoint READ breakpoint NOTIFY breakpointChanged)

    /**
     * @brief Continuous scaling factor (0.6 - 1.5) within breakpoint ranges.
     * 
     * This value incorporates the manualDpiScaleOverride from ConfigManager.
     * Final scale = calculatedScale * manualDpiScaleOverride
     * 
     * Used for proportional sizing of UI elements.
     */
    Q_PROPERTY(qreal layoutScale READ layoutScale NOTIFY layoutScaleChanged)

    /**
     * @brief Current grid column count (4-10).
     * 
     * Base columns from breakpoint + ultrawide adjustment.
     */
    Q_PROPERTY(int gridColumns READ gridColumns NOTIFY gridColumnsChanged)

    /**
     * @brief Number of visible items per home row (constant at 6 for now).
     */
    Q_PROPERTY(int homeRowVisibleItems READ homeRowVisibleItems NOTIFY homeRowVisibleItemsChanged)

    /**
     * @brief Default sidebar mode for current breakpoint.
     * 
     * Values: "overlay", "rail", or "expanded"
     */
    Q_PROPERTY(QString sidebarDefaultMode READ sidebarDefaultMode NOTIFY sidebarDefaultModeChanged)

    /**
     * @brief Current viewport aspect ratio (width / height).
     */
    Q_PROPERTY(qreal aspectRatio READ aspectRatio NOTIFY aspectRatioChanged)

    /**
     * @brief Current viewport width in logical pixels.
     */
    Q_PROPERTY(int viewportWidth READ viewportWidth NOTIFY viewportWidthChanged)

    /**
     * @brief Current viewport height in logical pixels.
     */
    Q_PROPERTY(int viewportHeight READ viewportHeight NOTIFY viewportHeightChanged)

public:
    explicit ResponsiveLayoutManager(QObject *parent = nullptr);
    ~ResponsiveLayoutManager();

    /**
     * @brief Sets the window reference for geometry monitoring.
     * 
     * Called by WindowManager after QML engine creates the window.
     * Connects to window geometry signals for automatic updates.
     * 
     * @param window The main QQuickWindow instance
     */
    void setWindow(QQuickWindow* window);

    // Property getters
    QString breakpoint() const;
    qreal layoutScale() const;
    int gridColumns() const;
    int homeRowVisibleItems() const;
    QString sidebarDefaultMode() const;
    qreal aspectRatio() const;
    int viewportWidth() const;
    int viewportHeight() const;

signals:
    void breakpointChanged();
    void layoutScaleChanged();
    void gridColumnsChanged();
    void homeRowVisibleItemsChanged();
    void sidebarDefaultModeChanged();
    void aspectRatioChanged();
    void viewportWidthChanged();
    void viewportHeightChanged();

private slots:
    void updateLayout();
    
    /**
     * @brief Handles manualDpiScaleOverride changes from ConfigManager.
     */
    void onManualDpiScaleOverrideChanged();

private:
    // Calculation methods
    QString calculateBreakpoint(int effectiveHeight) const;
    qreal calculateBaseLayoutScale(int effectiveHeight, const QString& breakpoint) const;
    int calculateGridColumns(const QString& breakpoint, qreal aspectRatio) const;
    QString calculateSidebarMode(const QString& breakpoint) const;
    int calculateEffectiveHeight(int logicalHeight, qreal devicePixelRatio) const;
    
    /**
     * @brief Connects to ConfigManager signals for setting changes.
     */
    void connectToConfigManager();

    // Member variables
    QQuickWindow* m_window = nullptr;
    
    QString m_breakpoint = "Medium";
    qreal m_layoutScale = 1.0;
    int m_gridColumns = 6;
    int m_homeRowVisibleItems = 6;
    QString m_sidebarDefaultMode = "rail";
    qreal m_aspectRatio = 16.0 / 9.0;
    int m_viewportWidth = 1920;
    int m_viewportHeight = 1080;

    // Breakpoint thresholds (effective viewport height)
    static constexpr int BREAKPOINT_SMALL_MAX = 850;
    static constexpr int BREAKPOINT_MEDIUM_MAX = 1150;
    static constexpr int BREAKPOINT_LARGE_MAX = 1700;

    // Grid column targets (16:9 baseline)
    static constexpr int GRID_COLUMNS_SMALL = 4;
    static constexpr int GRID_COLUMNS_MEDIUM = 6;
    static constexpr int GRID_COLUMNS_LARGE = 7;
    static constexpr int GRID_COLUMNS_XL = 8;

    // Home row visible items (modular for future configurability)
    static constexpr int HOME_ROW_VISIBLE_ITEMS = 6;

    // Ultrawide adjustment
    static constexpr qreal ULTRAWIDE_THRESHOLD = 2.2;
    static constexpr int ULTRAWIDE_MAX_EXTRA_COLUMNS = 2;

    // Layout scale bounds (before manual override)
    static constexpr qreal LAYOUT_SCALE_MIN = 0.6;
    static constexpr qreal LAYOUT_SCALE_MAX = 1.5;

    // High-DPI threshold
    static constexpr qreal HIGH_DPI_THRESHOLD = 1.5;
};
