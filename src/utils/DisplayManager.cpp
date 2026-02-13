#include "DisplayManager.h"
#include <QDebug>
#include <QProcess>
#include <QGuiApplication>
#include <QScreen>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Windows 10 SDK 10.0.26100.0+ already includes the necessary definitions.
// For older SDKs, we'd need to define DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE,
// but since we're targeting newer SDKs, we can rely on wingdi.h providing them.

DisplayManager::DisplayManager(ConfigManager *config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
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

    if (!m_refreshRateChanged) {
        m_originalRefreshRate = current;
        qDebug() << "DisplayManager: Stored original refresh rate:" << m_originalRefreshRate << "Hz";
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
    if (!m_refreshRateChanged) return true;

    bool success = false;
#ifdef Q_OS_WIN
    success = restoreRefreshRateWindows();
#else
    success = restoreRefreshRateLinux();
#endif

    if (success) {
        m_refreshRateChanged = false;
    }
    return success;
}

bool DisplayManager::setHDR(bool enabled)
{
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
        return process.exitCode() == 0;
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
    return false;
}

double DisplayManager::getCurrentRefreshRate()
{
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
    qDebug() << "DisplayManager: Restoring display settings to registry defaults (original was" << m_originalRefreshRate << "Hz)";
    // Restore to registry settings (which we didn't modify since we don't use CDS_UPDATEREGISTRY)
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
    
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPathArrayElements, &numModeInfoArrayElements) != ERROR_SUCCESS) {
        qWarning() << "DisplayManager: GetDisplayConfigBufferSizes failed";
        return false;
    }
    
    std::vector<DISPLAYCONFIG_PATH_INFO> pathArray(numPathArrayElements);
    std::vector<DISPLAYCONFIG_MODE_INFO> modeInfoArray(numModeInfoArrayElements);
    
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &numPathArrayElements, pathArray.data(), &numModeInfoArrayElements, modeInfoArray.data(), nullptr) != ERROR_SUCCESS) {
        qWarning() << "DisplayManager: QueryDisplayConfig failed";
        return false;
    }
    
    bool success = false;
    
    // Try to set for all active paths (usually just one for primary)
    for (UINT32 i = 0; i < numPathArrayElements; ++i) {
        DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setAdvancedColorState = {};
        setAdvancedColorState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
        setAdvancedColorState.header.size = sizeof(DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE);
        setAdvancedColorState.header.adapterId = pathArray[i].targetInfo.adapterId;
        setAdvancedColorState.header.id = pathArray[i].targetInfo.id;
        
        setAdvancedColorState.value = enabled ? 1 : 0;
        
        LONG ret = DisplayConfigSetDeviceInfo(&setAdvancedColorState.header);
        if (ret == ERROR_SUCCESS) {
            qDebug() << "DisplayManager: Successfully set HDR to" << enabled << "for path" << i;
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
