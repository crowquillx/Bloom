#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <functional>

#include "ConfigManager.h"

class QProcess;

struct DisplayManagerOptions
{
    int commandDeadlineMs = 5000;
    qint64 maximumCommandOutputBytes = 64 * 1024;
};

class DisplayManager : public QObject
{
    Q_OBJECT

public:
    explicit DisplayManager(ConfigManager *config, QObject *parent = nullptr);
    explicit DisplayManager(ConfigManager *config,
                            const DisplayManagerOptions &options,
                            QObject *parent = nullptr);
    ~DisplayManager() override;

signals:
    void hdrChangeFinished(bool enabled, bool success);
    void refreshRateChangeFinished(double requestedHz, bool success);
    void refreshRateRestoreFinished(bool success);
    void displayOperationMeasured(const QString &operation,
                                  qint64 milliseconds,
                                  bool success,
                                  bool timedOut);

public slots:
    /**
     * @brief Captures the current refresh rate as the restore target for this playback session.
     *
     * Use this before operations like HDR toggles that may force a temporary mode change.
     */
    void captureOriginalRefreshRate();

    void setRefreshRateAsync(double hz);

    void restoreRefreshRateAsync();

    /**
     * @brief Toggles HDR on or off without blocking the GUI thread.
     * @param enabled true to enable HDR, false to disable.
     */
    void setHDRAsync(bool enabled);

    /**
     * @brief Cancels any pending display operation and invalidates its completion.
     */
    void cancelPendingOperations();

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
     * @brief Whether the most recent setRefreshRateAsync() call performed a real mode switch.
     */
    bool lastRefreshRateSwitchChanged() const { return m_lastRefreshRateSwitchChanged; }
    /**
     * @brief Whether the most recent setRefreshRateAsync() call skipped because the current mode was a compatible multiple.
     */
    bool lastRefreshRateSwitchSkippedCompatibleMultiple() const { return m_lastRefreshRateSwitchSkippedCompatibleMultiple; }
    /**
     * @brief Effective refresh rate from the most recent setRefreshRateAsync() call.
     *
     * For integer-reported fractional Windows modes, this is normalized to the requested
     * fractional rate only when the current mode belongs to that exact fractional family.
     */
    double lastRefreshRateSwitchEffectiveRate() const { return m_lastRefreshRateSwitchEffectiveRate; }
    bool needsRefreshRestore() const { return m_refreshRateChanged || (m_hasCapturedOriginalRefreshRate && m_originalRefreshRate > 0.0); }
    bool needsHdrRestore() const { return m_hdrChanged; }
    QVariantMap diagnostics() const;

private:
    ConfigManager *m_config;
    DisplayManagerOptions m_options;
    
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
    quint64 m_operationGeneration = 0;
    QPointer<QProcess> m_commandProcess;
    QTimer m_commandDeadlineTimer;
    QElapsedTimer m_operationElapsed;
    QByteArray m_commandStandardError;
    QByteArray m_commandStandardOutput;
    QString m_activeOperation;
    bool m_hdrOperationPending = false;
    bool m_pendingHdrRequestedState = false;
    bool m_pendingHdrPreState = false;
    std::function<void(bool, bool)> m_commandCompletion;
    quint64 m_operationsStarted = 0;
    quint64 m_operationsSucceeded = 0;
    quint64 m_operationsTimedOut = 0;
    quint64 m_operationsCanceled = 0;
    qint64 m_lastOperationLatencyMs = -1;

    quint64 beginOperation(const QString &operation);
    void startExternalCommand(const QString &operation,
                              const QString &command,
                              quint64 generation,
                              std::function<void(bool, bool)> completion);
    void finishExternalCommand(QProcess *process,
                               quint64 generation,
                               bool success);
    void cancelCommandProcess();
    void recordOperationResult(const QString &operation,
                               qint64 elapsedMs,
                               bool success,
                               bool timedOut);
    void queueRefreshRateResult(double requestedHz,
                                bool success,
                                bool changed,
                                bool skippedCompatibleMultiple,
                                double effectiveRate,
                                quint64 generation);
    void launchBestEffortShutdownRestore();
    void updateHdrRestoreTracking(bool requestedState, bool preState);
};
