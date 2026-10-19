#pragma once

#include <QObject>
#include <QString>
#include <QQuickWindow>

class ConfigManager;

/**
 * @brief Central responsive layout calculation engine for the Bloom HTPC client.
 * 
 * ResponsiveLayoutManager tracks the main window and publishes layout tokens
 * computed by Bloom::ResponsiveLayoutPolicy:
 * - Resolution-independent layoutScale (10-foot / Kodi-style)
 * - Aspect-ratio-aware grid columns for ultrawide displays
 * - Clean separation from display hardware concerns (refresh rate, HDR)
 * - Manual DPI scale override support via ConfigManager
 * 
 * ## Scaling Model
 * 
 * Theme tokens are design units on a fixed 16:9 canvas (1272 units tall).
 * layoutScale maps that canvas onto the window, so each element covers the
 * same fraction of the screen at any resolution and any OS display scaling.
 * All inputs are logical pixels; Qt Quick already applies devicePixelRatio.
 * 
 * Windows wider than 16:9 keep the height-driven scale and add grid columns
 * (7 columns per 16:9 width); narrower windows fit the canvas by width.
 * 
 * ## Manual DPI Scale Override
 * 
 * Users can override the automatic layout scale via ConfigManager::manualDpiScaleOverride.
 * The final layoutScale = calculatedScale * manualDpiScaleOverride
 * 
 * ## Usage in QML
 * 
 * Access via Theme.qml which exposes responsive tokens:
 * - Theme.layoutScale
 * - Theme.gridColumns
 * - etc.
 */
class ResponsiveLayoutManager : public QObject
{
    Q_OBJECT

    /**
     * @brief Logical pixels per Theme design unit; proportional to window size.
     * 
     * This value incorporates the manualDpiScaleOverride from ConfigManager.
     * Final scale = calculatedScale * manualDpiScaleOverride
     * 
     * Used for proportional sizing of UI elements.
     */
    Q_PROPERTY(qreal layoutScale READ layoutScale NOTIFY layoutScaleChanged)

    /**
     * @brief Current grid column count (7 at 16:9, more on wider viewports).
     */
    Q_PROPERTY(int gridColumns READ gridColumns NOTIFY gridColumnsChanged)

    /**
     * @brief Number of visible items per home row (6 at 16:9, more on wider viewports).
     */
    Q_PROPERTY(int homeRowVisibleItems READ homeRowVisibleItems NOTIFY homeRowVisibleItemsChanged)

    /**
     * @brief Default sidebar mode.
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
    qreal layoutScale() const;
    int gridColumns() const;
    int homeRowVisibleItems() const;
    QString sidebarDefaultMode() const;
    qreal aspectRatio() const;
    int viewportWidth() const;
    int viewportHeight() const;

signals:
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
    /**
     * @brief Connects to ConfigManager signals for setting changes.
     */
    void connectToConfigManager();

    // Member variables
    QQuickWindow* m_window = nullptr;
    
    qreal m_layoutScale = 1.0;
    int m_gridColumns = 7;
    int m_homeRowVisibleItems = 6;
    QString m_sidebarDefaultMode = "rail";
    qreal m_aspectRatio = 16.0 / 9.0;
    int m_viewportWidth = 1920;
    int m_viewportHeight = 1080;
};
