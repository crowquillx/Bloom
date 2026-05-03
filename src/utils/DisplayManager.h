#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QString>
#include <QTimer>
#include <QVector>
#include "ConfigManager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class DisplayManager : public QObject
{
    Q_OBJECT

public:
    explicit DisplayManager(ConfigManager *config, QObject *parent = nullptr);
    ~DisplayManager();

signals:
    void hdrChangeFinished(bool enabled, bool success);

public slots:
    /**
     * @brief Captures the current refresh rate as the restore target for this playback session.
     *
     * Use this before operations like HDR toggles that may force a temporary mode change.
     */
    void captureOriginalRefreshRate();

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
     * @brief Toggles HDR on or off without blocking the GUI thread.
     * @param enabled true to enable HDR, false to disable.
     */
    void setHDRAsync(bool enabled);

    /**
     * @brief Cancels any pending asynchronous HDR settle polling.
     */
    void cancelPendingHdrAsync();

    /**
     * @brief Gets the current refresh rate of the primary display.
     * @return The refresh rate in Hz (fractional), or 0 if failed.
     */
    double getCurrentRefreshRate();

    /**
     * @brief Whether playback is currently using a temporary refresh-rate override.
     */
    bool hasActiveRefreshRateOverride() const { return m_refreshRateChanged; }
    /**
     * @brief Whether the most recent setRefreshRate() call performed a real mode switch.
     */
    bool lastRefreshRateSwitchChanged() const { return m_lastRefreshRateSwitchChanged; }
    /**
     * @brief Whether the most recent setRefreshRate() call skipped because the current mode was a compatible multiple.
     */
    bool lastRefreshRateSwitchSkippedCompatibleMultiple() const { return m_lastRefreshRateSwitchSkippedCompatibleMultiple; }
    /**
     * @brief Effective refresh rate from the most recent setRefreshRate() call.
     *
     * For integer-reported fractional Windows modes, this is normalized to the requested
     * fractional rate only when the current mode belongs to that exact fractional family.
     */
    double lastRefreshRateSwitchEffectiveRate() const { return m_lastRefreshRateSwitchEffectiveRate; }
    bool needsRefreshRestore() const { return m_refreshRateChanged || (m_hasCapturedOriginalRefreshRate && m_originalRefreshRate > 0.0); }
    bool needsHdrRestore() const { return m_hdrChanged; }

private:
    ConfigManager *m_config;
    
    // State tracking
    bool m_refreshRateChanged = false;
    bool m_lastRefreshRateSwitchChanged = false;
    bool m_lastRefreshRateSwitchSkippedCompatibleMultiple = false;
    double m_lastRefreshRateSwitchEffectiveRate = 0.0;
    double m_originalRefreshRate = 0.0;
    double m_baselineRefreshRate = 0.0;
    bool m_hasCapturedOriginalRefreshRate = false;
    bool m_hdrChanged = false;
    bool m_originalHDRState = false;
    bool m_hasCapturedOriginalHDRState = false;
    QTimer m_hdrAsyncPollTimer;
    QElapsedTimer m_hdrAsyncElapsed;
    bool m_hdrAsyncPending = false;
    bool m_hdrAsyncRequestedState = false;
    bool m_hdrAsyncPreState = false;
    quint64 m_hdrAsyncGeneration = 0;
#ifdef Q_OS_WIN
    QVector<DISPLAYCONFIG_PATH_INFO> m_hdrAsyncPaths;
#endif

    // Platform-specific helpers
#ifdef Q_OS_WIN
    bool setRefreshRateWindows(double hz);
    bool restoreRefreshRateWindows();
    bool setHDRWindows(bool enabled);
    bool startHDRAsyncWindows(bool enabled);
#else
    bool setRefreshRateLinux(double hz);
    bool restoreRefreshRateLinux();
    bool setHDRLinux(bool enabled);
#endif
    void pollPendingHdrAsync();
    void updateHdrRestoreTracking(bool requestedState, bool preState);
};
