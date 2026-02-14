#pragma once

#include <QObject>
#include <QString>
#include "ConfigManager.h"

class DisplayManager : public QObject
{
    Q_OBJECT

public:
    explicit DisplayManager(ConfigManager *config, QObject *parent = nullptr);
    ~DisplayManager();

public slots:
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

    /**
     * @brief Whether playback is currently using a temporary refresh-rate override.
     */
    bool hasActiveRefreshRateOverride() const { return m_refreshRateChanged; }

private:
    ConfigManager *m_config;
    
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
