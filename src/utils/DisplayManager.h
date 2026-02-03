#pragma once

#include <QObject>
#include <QString>
#include "ConfigManager.h"

class DisplayManager : public QObject
{
    Q_OBJECT
    
    /**
     * @brief DPI scale factor for content sizing across different screen resolutions.
     * 
     * Baseline is 1.0 for 1080p/1440p screens. For 4K displays, this will be ~1.3
     * so that content scales to maintain the same visual ratio.
     * 
     * Usage in QML Theme.qml:
     *   property int posterWidth: Math.round(340 / dpiScale)
     * This makes items proportionally smaller on higher resolution displays.
     */
    Q_PROPERTY(qreal dpiScale READ dpiScale NOTIFY dpiScaleChanged)

public:
    explicit DisplayManager(ConfigManager *config, QObject *parent = nullptr);
    ~DisplayManager();
    
    /**
     * @brief Returns the DPI scale factor based on screen resolution.
     * @return Scale factor (1.0 for 1080p/1440p, ~1.3 for 4K)
     */
    qreal dpiScale() const;
    
    /**
     * @brief Recalculates DPI scale based on current primary screen.
     * Call this when screen configuration changes.
     */
    Q_INVOKABLE void updateDpiScale();

    /**
     * @brief Sets the display refresh rate to the specified Hz.
     * @param hz The target refresh rate (supports fractional rates like 23.976).
     * @return true if successful, false otherwise.
     * 
     * The method will attempt to match the exact rate if the display supports it.
     * Many modern TVs (e.g., LG) support exact 23.976Hz. If exact matching fails,
     * it will try the nearest integer rate (e.g., 24Hz for 23.976fps content).
     */
    bool setRefreshRate(double hz);

    /**
     * @brief Restores the original display refresh rate.
     * @return true if successful, false otherwise.
     */
    bool restoreRefreshRate();

    /**
     * @brief Toggles HDR on or off.
     * @param enabled true to enable HDR, false to disable.
     * @return true if successful, false otherwise.
     */
    bool setHDR(bool enabled);

    /**
     * @brief Gets the current refresh rate of the primary display.
     * @return The refresh rate in Hz (fractional), or 0 if failed.
     */
    double getCurrentRefreshRate();

signals:
    void dpiScaleChanged();

private:
    ConfigManager *m_config;
    qreal m_dpiScale = 1.0;
    
    // State tracking
    bool m_refreshRateChanged = false;
    double m_originalRefreshRate = 0.0;
    bool m_hdrChanged = false;
    bool m_originalHDRState = false;

    // Platform-specific helpers
#ifdef Q_OS_WIN
    bool setRefreshRateWindows(double hz);
    bool restoreRefreshRateWindows();
    bool setHDRWindows(bool enabled);
#else
    bool setRefreshRateLinux(double hz);
    bool restoreRefreshRateLinux();
    bool setHDRLinux(bool enabled);
#endif
};
