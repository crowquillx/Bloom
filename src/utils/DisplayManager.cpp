#include "DisplayManager.h"
#include <QDebug>
#include <QProcess>
#include <QGuiApplication>
#include <QScreen>
#include <QtMath>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QThread>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Windows 10 SDK 10.0.26100.0+ already includes the necessary definitions.
// For older SDKs, we'd need to define DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE,
// but since we're targeting newer SDKs, we can rely on wingdi.h providing them.

namespace {
Q_LOGGING_CATEGORY(lcDisplayTrace, "bloom.playback.displaytrace")

bool isCadenceCompatible(double currentHz, double targetHz)
{
    if (currentHz <= 0.0 || targetHz <= 0.0 || currentHz <= targetHz) {
        return false;
    }

    const double ratio = currentHz / targetHz;
    const int nearestIntegerMultiple = qRound(ratio);
    if (nearestIntegerMultiple < 2) {
        return false;
    }

    // Allow small drift for common fractional rates (23.976/29.97/59.94).
    return qAbs(ratio - static_cast<double>(nearestIntegerMultiple)) <= 0.01;
}

#ifdef Q_OS_WIN
QString formatAdapterId(const LUID &adapterId)
{
    return QStringLiteral("%1:%2")
        .arg(static_cast<qulonglong>(adapterId.HighPart))
        .arg(static_cast<qulonglong>(adapterId.LowPart));
}

struct AdvancedColorStateQueryResult
{
    bool ok = false;
    bool enabled = false;
    LONG ret = ERROR_GEN_FAILURE;
};

AdvancedColorStateQueryResult queryAdvancedColorState(const DISPLAYCONFIG_PATH_INFO &pathInfo)
{
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO getAdvancedColorInfo = {};
    getAdvancedColorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
    getAdvancedColorInfo.header.size = sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO);
    getAdvancedColorInfo.header.adapterId = pathInfo.targetInfo.adapterId;
    getAdvancedColorInfo.header.id = pathInfo.targetInfo.id;

    const LONG ret = DisplayConfigGetDeviceInfo(&getAdvancedColorInfo.header);
    AdvancedColorStateQueryResult result;
    result.ok = (ret == ERROR_SUCCESS);
    result.enabled = getAdvancedColorInfo.advancedColorEnabled != 0;
    result.ret = ret;
    return result;
}

bool waitForAdvancedColorState(const DISPLAYCONFIG_PATH_INFO &pathInfo, bool enabled, int timeoutMs, int pollMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        const AdvancedColorStateQueryResult state = queryAdvancedColorState(pathInfo);
        if (state.ok && state.enabled == enabled) {
            return true;
        }
        QThread::msleep(static_cast<unsigned long>(pollMs));
    }

    const AdvancedColorStateQueryResult finalState = queryAdvancedColorState(pathInfo);
    return finalState.ok && finalState.enabled == enabled;
}

bool isAnyAdvancedColorEnabled()
{
    UINT32 numPathArrayElements = 0;
    UINT32 numModeInfoArrayElements = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPathArrayElements, &numModeInfoArrayElements) != ERROR_SUCCESS) {
        return false;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> pathArray(numPathArrayElements);
    std::vector<DISPLAYCONFIG_MODE_INFO> modeInfoArray(numModeInfoArrayElements);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,
                           &numPathArrayElements,
                           pathArray.data(),
                           &numModeInfoArrayElements,
                           modeInfoArray.data(),
                           nullptr) != ERROR_SUCCESS) {
        return false;
    }

    for (UINT32 i = 0; i < numPathArrayElements; ++i) {
        const AdvancedColorStateQueryResult state = queryAdvancedColorState(pathArray[i]);
        if (state.ok && state.enabled) {
            return true;
        }
    }

    return false;
}
#endif
}

DisplayManager::DisplayManager(ConfigManager *config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    // Baseline target used for restore if runtime capture happens while HDR is already on.
    m_baselineRefreshRate = getCurrentRefreshRate();
}

DisplayManager::~DisplayManager()
{
    if (m_refreshRateChanged) {
        restoreRefreshRate();
    }
    if (m_hdrChanged) {
        setHDR(m_originalHDRState);
    }
}

void DisplayManager::captureOriginalRefreshRate()
{
    // Capture once per playback flow; preserve the earliest (pre-HDR) mode.
    if (m_hasCapturedOriginalRefreshRate) {
        return;
    }

    double current = getCurrentRefreshRate();
#ifdef Q_OS_WIN
    if (isAnyAdvancedColorEnabled() && m_baselineRefreshRate > 0.0) {
        qCDebug(lcDisplayTrace) << "captureOriginalRefreshRate using baseline because HDR is already enabled"
                                << "baselineHz=" << m_baselineRefreshRate
                                << "currentHz=" << current;
        current = m_baselineRefreshRate;
    }
#endif
    if (current <= 0.0) {
        qWarning() << "DisplayManager: Failed to capture original refresh rate (current:" << current << ")";
        return;
    }

    m_originalRefreshRate = current;
    m_hasCapturedOriginalRefreshRate = true;
    qDebug() << "DisplayManager: Captured original refresh rate:" << m_originalRefreshRate << "Hz";
    qCInfo(lcDisplayTrace) << "captureOriginalRefreshRate"
                           << "capturedHz=" << m_originalRefreshRate
                           << "refreshOverrideActive=" << m_refreshRateChanged;
}

bool DisplayManager::setRefreshRate(double hz)
{
    qDebug() << "DisplayManager::setRefreshRate called with hz:" << hz;
    
    if (hz <= 0) {
        qDebug() << "DisplayManager: Invalid refresh rate" << hz << ", skipping";
        return false;
    }
    
    // Don't switch if already at target (approximate check - within 0.5Hz)
    double current = getCurrentRefreshRate();
    qDebug() << "DisplayManager: Current refresh rate:" << current << "Hz, target:" << hz << "Hz";
    
    if (qAbs(current - hz) < 0.5) {
        qDebug() << "DisplayManager: Already at target refresh rate" << current << "Hz";
        return true;
    }

    if (isCadenceCompatible(current, hz)) {
        const double ratio = current / hz;
        qDebug() << "DisplayManager: Current refresh rate" << current
                 << "Hz is cadence-compatible with target" << hz
                 << "Hz (ratio" << ratio << "), skipping mode switch";
        return true;
    }

    if (!m_refreshRateChanged) {
        if (m_hasCapturedOriginalRefreshRate && m_originalRefreshRate > 0.0) {
            qDebug() << "DisplayManager: Using captured original refresh rate for restore target:"
                     << m_originalRefreshRate << "Hz";
        } else {
            m_originalRefreshRate = current;
            m_hasCapturedOriginalRefreshRate = true;
            qDebug() << "DisplayManager: Stored original refresh rate:" << m_originalRefreshRate << "Hz";
        }
    }

#ifdef Q_OS_WIN
    if (setRefreshRateWindows(hz)) {
        m_refreshRateChanged = true;
        return true;
    }
#else
    if (setRefreshRateLinux(hz)) {
        m_refreshRateChanged = true;
        return true;
    }
#endif
    return false;
}

bool DisplayManager::restoreRefreshRate()
{
    const bool hasCapturedTarget = m_hasCapturedOriginalRefreshRate && m_originalRefreshRate > 0.0;
    qCInfo(lcDisplayTrace) << "restoreRefreshRate begin"
                           << "refreshChanged=" << m_refreshRateChanged
                           << "hasCapturedTarget=" << hasCapturedTarget
                           << "capturedHz=" << m_originalRefreshRate;
    if (!m_refreshRateChanged && !hasCapturedTarget) {
        qCInfo(lcDisplayTrace) << "restoreRefreshRate no-op";
        return true;
    }

    bool success = false;
#ifdef Q_OS_WIN
    success = restoreRefreshRateWindows();
#else
    success = restoreRefreshRateLinux();
#endif

    if (success) {
        m_refreshRateChanged = false;
        m_hasCapturedOriginalRefreshRate = false;
        m_originalRefreshRate = 0.0;
    }
    qCInfo(lcDisplayTrace) << "restoreRefreshRate done"
                           << "success=" << success
                           << "refreshChanged=" << m_refreshRateChanged
                           << "hasCapturedTarget=" << m_hasCapturedOriginalRefreshRate;
    return success;
}

bool DisplayManager::setHDR(bool enabled)
{
    QElapsedTimer hdrTimer;
    hdrTimer.start();
    qCInfo(lcDisplayTrace) << "setHDR begin"
                           << "requested=" << enabled
                           << "hdrChanged=" << m_hdrChanged;

#ifdef Q_OS_WIN
    // Check if we have a custom command override
    QString customCmd = m_config->getWindowsCustomHDRCommand();
    if (!customCmd.isEmpty()) {
        QString cmd = customCmd;
        cmd.replace("{STATE}", enabled ? "on" : "off");
        qDebug() << "DisplayManager: Executing custom Windows HDR command:" << cmd;
        
        // Simple command execution
        QProcess process;
        process.startCommand(cmd);
        process.waitForFinished();
        const bool success = process.exitCode() == 0;
        qCInfo(lcDisplayTrace) << "setHDR custom-command result"
                               << "requested=" << enabled
                               << "exitCode=" << process.exitCode()
                               << "elapsedMs=" << hdrTimer.elapsed();
        if (success) {
            m_hdrChanged = true;
        }
        return success;
    }
    
    if (setHDRWindows(enabled)) {
        m_hdrChanged = true;
        // We don't track original state perfectly here as querying it is hard,
        // but we assume if we toggled it ON, we should toggle it OFF later.
        // Ideally we'd query first.
        return true;
    }
#else
    if (setHDRLinux(enabled)) {
        m_hdrChanged = true;
        return true;
    }
#endif
    qCWarning(lcDisplayTrace) << "setHDR failed"
                              << "requested=" << enabled
                              << "elapsedMs=" << hdrTimer.elapsed();
    return false;
}

double DisplayManager::getCurrentRefreshRate()
{
#ifdef Q_OS_WIN
    DEVMODE dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);

    if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
        if (dm.dmDisplayFrequency > 1) {
            return static_cast<double>(dm.dmDisplayFrequency);
        }
    } else {
        qWarning() << "DisplayManager: EnumDisplaySettings failed when reading current refresh rate";
    }
#endif

    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        return screen->refreshRate();  // Returns qreal (double) with fractional precision
    }
    return 60.0;
}

#ifdef Q_OS_WIN
bool DisplayManager::setRefreshRateWindows(double hz)
{
    qDebug() << "DisplayManager::setRefreshRateWindows called with hz:" << hz;
    
    DEVMODE dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);
    
    if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
        qDebug() << "DisplayManager: Current display settings - Width:" << dm.dmPelsWidth 
                 << "Height:" << dm.dmPelsHeight 
                 << "BitsPerPel:" << dm.dmBitsPerPel 
                 << "Frequency:" << dm.dmDisplayFrequency;
        
        // Windows DEVMODE uses integer Hz, but we can enumerate available modes
        // to find exact matches for rates like 23Hz (which Windows reports for 23.976)
        // or try to find a mode that best matches the requested fractional rate
        
        // First, try to enumerate all modes and find one that matches our target
        // Windows often lists 23Hz for 23.976fps capable displays
        int targetHz = qRound(hz);
        int exactHz = static_cast<int>(hz);  // Truncate, e.g., 23.976 -> 23
        
        // For film content (23.976), check if display supports 23Hz mode
        bool tryExactFirst = false;
        if (hz > 23.0 && hz < 24.0 && hz != 24.0) {
            // This is likely 23.976 content, try 23Hz first (how Windows reports 23.976)
            tryExactFirst = true;
            qDebug() << "DisplayManager: Detected film framerate" << hz << ", will try 23Hz mode first";
        } else if (hz > 29.0 && hz < 30.0 && hz != 30.0) {
            // 29.97 content
            tryExactFirst = true;
            qDebug() << "DisplayManager: Detected 29.97 framerate, will try 29Hz mode first";
        } else if (hz > 59.0 && hz < 60.0 && hz != 60.0) {
            // 59.94 content
            tryExactFirst = true;
            qDebug() << "DisplayManager: Detected 59.94 framerate, will try 59Hz mode first";
        }
        
        // Try exact truncated rate first if applicable (23 for 23.976, etc.)
        if (tryExactFirst && exactHz != targetHz) {
            dm.dmDisplayFrequency = exactHz;
            dm.dmFields = DM_DISPLAYFREQUENCY;
            
            LONG ret = ChangeDisplaySettingsEx(NULL, &dm, NULL, CDS_FULLSCREEN, NULL);
            if (ret == DISP_CHANGE_SUCCESSFUL) {
                qDebug() << "DisplayManager: Successfully set refresh rate to" << exactHz << "Hz (exact match for" << hz << ")";
                return true;
            }
            qDebug() << "DisplayManager: Exact" << exactHz << "Hz mode not available, trying" << targetHz << "Hz";
        }
        
        // Try rounded rate
        dm.dmDisplayFrequency = targetHz;
        dm.dmFields = DM_DISPLAYFREQUENCY;
        
        // Use CDS_FULLSCREEN without CDS_UPDATEREGISTRY so we can restore to registry settings later
        LONG ret = ChangeDisplaySettingsEx(NULL, &dm, NULL, CDS_FULLSCREEN, NULL);
        if (ret == DISP_CHANGE_SUCCESSFUL) {
            qDebug() << "DisplayManager: Successfully set refresh rate to" << targetHz << "Hz";
            qDebug() << "DisplayManager: Reported refresh after switch:" << getCurrentRefreshRate() << "Hz";
            return true;
        } else {
            QString errorMsg;
            switch (ret) {
                case DISP_CHANGE_BADDUALVIEW: errorMsg = "BADDUALVIEW"; break;
                case DISP_CHANGE_BADFLAGS: errorMsg = "BADFLAGS"; break;
                case DISP_CHANGE_BADMODE: errorMsg = "BADMODE (requested mode not supported)"; break;
                case DISP_CHANGE_BADPARAM: errorMsg = "BADPARAM"; break;
                case DISP_CHANGE_FAILED: errorMsg = "FAILED"; break;
                case DISP_CHANGE_NOTUPDATED: errorMsg = "NOTUPDATED"; break;
                case DISP_CHANGE_RESTART: errorMsg = "RESTART (reboot required)"; break;
                default: errorMsg = QString("Unknown error %1").arg(ret); break;
            }
            qWarning() << "DisplayManager: Failed to set refresh rate to" << targetHz << "Hz, error:" << errorMsg;
        }
    } else {
        qWarning() << "DisplayManager: EnumDisplaySettings failed";
    }
    return false;
}

bool DisplayManager::restoreRefreshRateWindows()
{
    const double targetHz = (m_originalRefreshRate > 0.0) ? m_originalRefreshRate : m_baselineRefreshRate;
    if (targetHz > 0.0) {
        qDebug() << "DisplayManager: Restoring display refresh to captured original rate"
                 << targetHz << "Hz";
        if (setRefreshRateWindows(targetHz)) {
            return true;
        }
        qWarning() << "DisplayManager: Failed to restore to captured rate, falling back to registry defaults";
    }

    qDebug() << "DisplayManager: Restoring display settings to registry defaults";
    LONG ret = ChangeDisplaySettingsEx(NULL, NULL, NULL, 0, NULL);
    if (ret == DISP_CHANGE_SUCCESSFUL) {
        qDebug() << "DisplayManager: Restored display settings";
        return true;
    }

    qWarning() << "DisplayManager: Failed to restore display settings, error:" << ret;
    return false;
}

bool DisplayManager::setHDRWindows(bool enabled)
{
    // Use undocumented API to toggle HDR
    // Note: This targets the primary display path.
    // A robust implementation would enumerate paths and find the active one.
    
    UINT32 numPathArrayElements = 0;
    UINT32 numModeInfoArrayElements = 0;
    
    const LONG sizeRet = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPathArrayElements, &numModeInfoArrayElements);
    qCInfo(lcDisplayTrace) << "setHDRWindows buffer-sizes"
                           << "requested=" << enabled
                           << "ret=" << sizeRet
                           << "paths=" << numPathArrayElements
                           << "modes=" << numModeInfoArrayElements;
    if (sizeRet != ERROR_SUCCESS) {
        qWarning() << "DisplayManager: GetDisplayConfigBufferSizes failed";
        return false;
    }
    
    std::vector<DISPLAYCONFIG_PATH_INFO> pathArray(numPathArrayElements);
    std::vector<DISPLAYCONFIG_MODE_INFO> modeInfoArray(numModeInfoArrayElements);
    
    const LONG queryRet = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &numPathArrayElements, pathArray.data(), &numModeInfoArrayElements, modeInfoArray.data(), nullptr);
    qCInfo(lcDisplayTrace) << "setHDRWindows query-display-config"
                           << "requested=" << enabled
                           << "ret=" << queryRet
                           << "paths=" << numPathArrayElements
                           << "modes=" << numModeInfoArrayElements;
    if (queryRet != ERROR_SUCCESS) {
        qWarning() << "DisplayManager: QueryDisplayConfig failed";
        return false;
    }
    
    bool success = false;
    
    // Try to set for all active paths (usually just one for primary)
    for (UINT32 i = 0; i < numPathArrayElements; ++i) {
        const AdvancedColorStateQueryResult preState = queryAdvancedColorState(pathArray[i]);
        qCInfo(lcDisplayTrace) << "setHDRWindows pre-state"
                               << "path=" << i
                               << "adapter=" << formatAdapterId(pathArray[i].targetInfo.adapterId)
                               << "targetId=" << pathArray[i].targetInfo.id
                               << "queryRet=" << preState.ret
                               << "enabled=" << preState.enabled;
        if (preState.ok && preState.enabled == enabled) {
            qCInfo(lcDisplayTrace) << "setHDRWindows no-op (already requested state)"
                                   << "path=" << i
                                   << "requested=" << enabled;
            success = true;
            continue;
        }

        DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setAdvancedColorState = {};
        setAdvancedColorState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
        setAdvancedColorState.header.size = sizeof(DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE);
        setAdvancedColorState.header.adapterId = pathArray[i].targetInfo.adapterId;
        setAdvancedColorState.header.id = pathArray[i].targetInfo.id;
        
        setAdvancedColorState.value = enabled ? 1 : 0;
        
        LONG ret = DisplayConfigSetDeviceInfo(&setAdvancedColorState.header);
        qCInfo(lcDisplayTrace) << "setHDRWindows path"
                               << i
                               << "adapter=" << formatAdapterId(pathArray[i].targetInfo.adapterId)
                               << "targetId=" << pathArray[i].targetInfo.id
                               << "requested=" << enabled
                               << "ret=" << ret;
        if (ret == ERROR_SUCCESS) {
            qDebug() << "DisplayManager: Successfully set HDR to" << enabled << "for path" << i;
            static constexpr int kHdrSettleTimeoutMs = 5000;
            static constexpr int kHdrSettlePollMs = 50;
            const bool settled = waitForAdvancedColorState(pathArray[i], enabled, kHdrSettleTimeoutMs, kHdrSettlePollMs);
            const AdvancedColorStateQueryResult postState = queryAdvancedColorState(pathArray[i]);
            qCInfo(lcDisplayTrace) << "setHDRWindows post-state"
                                   << "path=" << i
                                   << "settled=" << settled
                                   << "queryRet=" << postState.ret
                                   << "enabled=" << postState.enabled;
            if (!settled) {
                qCWarning(lcDisplayTrace) << "setHDRWindows settle-timeout"
                                          << "path=" << i
                                          << "requested=" << enabled
                                          << "timeoutMs=" << kHdrSettleTimeoutMs;
                continue;
            }
            success = true;
        } else {
            qWarning() << "DisplayManager: Failed to set HDR for path" << i << "error:" << ret;
        }
    }
    
    return success;
}

#else
bool DisplayManager::setRefreshRateLinux(double hz)
{
    QString cmdTemplate = m_config->getLinuxRefreshRateCommand();
    if (cmdTemplate.isEmpty()) {
        // Default to xrandr if not configured
        // This is a naive default, users likely need to configure it
        qWarning() << "DisplayManager: No Linux refresh rate command configured";
        return false;
    }
    
    QString cmd = cmdTemplate;
    
    // Support both {RATE} (fractional) and {RATE_INT} (integer) placeholders
    // This allows users to configure commands that need exact rates (like kwin/Wayland)
    // or integer rates (like some xrandr setups)
    // 
    // For exact matching (23.976, 59.94, etc.), use {RATE} with full precision
    // Example: kwin command might use exact rate
    // Example xrandr: xrandr --output HDMI-1 --rate {RATE}
    
    // Format rate with appropriate precision
    // 23.976023... -> "23.976" (3 decimal places is enough for display matching)
    QString rateStr = QString::number(hz, 'f', 3);
    // Remove trailing zeros for cleaner output: 24.000 -> 24
    while (rateStr.contains('.') && (rateStr.endsWith('0') || rateStr.endsWith('.'))) {
        rateStr.chop(1);
    }
    
    cmd.replace("{RATE}", rateStr);
    cmd.replace("{RATE_INT}", QString::number(qRound(hz)));
    
    qDebug() << "DisplayManager: Executing Linux refresh rate command:" << cmd;
    
    QProcess process;
    process.startCommand(cmd);
    process.waitForFinished();
    
    if (process.exitCode() == 0) {
        return true;
    }
    
    qWarning() << "DisplayManager: Command failed:" << process.readAllStandardError();
    return false;
}

bool DisplayManager::restoreRefreshRateLinux()
{
    if (m_originalRefreshRate > 0) {
        return setRefreshRateLinux(m_originalRefreshRate);
    }
    return false;
}

bool DisplayManager::setHDRLinux(bool enabled)
{
    QString cmdTemplate = m_config->getLinuxHDRCommand();
    if (cmdTemplate.isEmpty()) {
        qWarning() << "DisplayManager: No Linux HDR command configured";
        return false;
    }
    
    QString cmd = cmdTemplate;
    cmd.replace("{STATE}", enabled ? "on" : "off"); // Example replacement
    
    qDebug() << "DisplayManager: Executing Linux HDR command:" << cmd;
    
    QProcess process;
    process.startCommand(cmd);
    process.waitForFinished();
    
    return process.exitCode() == 0;
}
#endif
