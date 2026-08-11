#include "DisplayManager.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QProcess>
#include <QScreen>
#include <QThread>
#include <QThreadPool>
#include <QtMath>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <vector>

#include "BloomLogging.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// Windows 10 SDK 10.0.26100.0+ already includes the necessary definitions.
// For older SDKs, we'd need to define DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE,
// but since we're targeting newer SDKs, we can rely on wingdi.h providing them.

namespace {
struct HdrAsyncFallbackResult
{
    bool success = false;
    bool preState = false;
};

#ifdef Q_OS_WIN
using NativeOperationGuard = std::function<bool()>;

bool setRefreshRateWindowsImpl(double hz,
                               const NativeOperationGuard &shouldContinue = {});
bool restoreRefreshRateWindowsImpl(double targetHz,
                                   double baselineHz,
                                   const NativeOperationGuard &shouldContinue = {});

QThreadPool *nativeDisplayThreadPool()
{
    // Deliberately process-wide: serialized native mutations prevent a stale
    // enable from physically overtaking a newer restore. The pool is not
    // destroyed during QObject teardown, so shutdown never waits on a display API.
    static QThreadPool *pool = []() {
        auto *created = new QThreadPool;
        created->setMaxThreadCount(1);
        created->setExpiryTimeout(-1);
        return created;
    }();
    return pool;
}

quintptr createCommandJob()
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        return 0;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits))) {
        CloseHandle(job);
        return 0;
    }
    return reinterpret_cast<quintptr>(job);
}

bool assignCommandToJob(QProcess *process, quintptr jobHandle)
{
    if (process == nullptr || process->processId() <= 0 || jobHandle == 0) {
        return false;
    }
    HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE,
                               FALSE, DWORD(process->processId()));
    if (child == nullptr) {
        return false;
    }
    const bool assigned = AssignProcessToJobObject(
        reinterpret_cast<HANDLE>(jobHandle), child) != FALSE;
    CloseHandle(child);
    return assigned;
}
#endif

QString hdrCommand(QString commandTemplate, bool enabled)
{
    if (!commandTemplate.isEmpty()) {
        commandTemplate.replace(QStringLiteral("{STATE}"),
                                enabled ? QStringLiteral("on")
                                        : QStringLiteral("off"));
    }
    return commandTemplate;
}

QString refreshRateCommand(QString commandTemplate, double hz)
{
    if (commandTemplate.isEmpty()) {
        return {};
    }
    QString rate = QString::number(hz, 'f', 3);
    while (rate.contains('.') && (rate.endsWith('0') || rate.endsWith('.'))) {
        rate.chop(1);
    }
    commandTemplate.replace(QStringLiteral("{RATE}"), rate);
    commandTemplate.replace(QStringLiteral("{RATE_INT}"),
                            QString::number(qRound(hz)));
    return commandTemplate;
}

#ifdef Q_OS_UNIX
QString quotePosixShellArgument(QString argument)
{
    argument.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QStringLiteral("'%1'").arg(argument);
}

QString detachedCommandSequence(const QStringList &commands)
{
    QStringList invocations;
    invocations.reserve(commands.size());
    for (const QString &command : commands) {
        const QStringList arguments = QProcess::splitCommand(command);
        QStringList quotedArguments;
        quotedArguments.reserve(arguments.size());
        for (const QString &argument : arguments) {
            quotedArguments.append(quotePosixShellArgument(argument));
        }
        if (!quotedArguments.isEmpty()) {
            invocations.append(quotedArguments.join(QLatin1Char(' ')));
        }
    }
    return invocations.join(QStringLiteral("; "));
}
#endif

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

void closeCommandJob(quintptr *jobHandle, bool terminate)
{
#ifdef Q_OS_WIN
    if (jobHandle != nullptr && *jobHandle != 0) {
        HANDLE job = reinterpret_cast<HANDLE>(*jobHandle);
        if (terminate) {
            (void) TerminateJobObject(job, 1);
        }
        CloseHandle(job);
        *jobHandle = 0;
    }
#else
    Q_UNUSED(jobHandle);
    Q_UNUSED(terminate);
#endif
}

void killProcessTree(QProcess *process, quintptr *jobHandle)
{
    if (process == nullptr) {
        return;
    }
#ifdef Q_OS_WIN
    if (jobHandle != nullptr && *jobHandle != 0) {
        closeCommandJob(jobHandle, true);
    }
#endif
#ifdef Q_OS_UNIX
    const qint64 processId = process->processId();
    if (processId > 0
        && ::kill(-static_cast<pid_t>(processId), SIGKILL) == 0) {
        return;
    }
#endif
    process->kill();
}

static bool isFractionalRateFamily(double targetHz, int reportedHz)
{
    if (targetHz > 23.0 && targetHz < 24.0) {
        return reportedHz == 23;
    }
    if (targetHz > 29.0 && targetHz < 30.0) {
        return reportedHz == 29;
    }
    if (targetHz > 59.0 && targetHz < 60.0) {
        return reportedHz == 59;
    }
    return false;
}

static bool isCommonFractionalTarget(double targetHz)
{
    // Extend this table if refresh matching gains support for additional
    // integer-reported fractional families such as 47.952 or 119.88.
    return (targetHz > 23.0 && targetHz < 24.0)
        || (targetHz > 29.0 && targetHz < 30.0)
        || (targetHz > 59.0 && targetHz < 60.0);
}

static bool isCurrentRefreshAlreadyTarget(double currentHz, double targetHz)
{
    if (currentHz <= 0.0 || targetHz <= 0.0) {
        return false;
    }

#ifdef Q_OS_WIN
    // Windows reports common fractional modes as integer 23/29/59 Hz. Treat only
    // those integer families as equivalent to 23.976/29.97/59.94. A broad 0.5 Hz
    // tolerance can otherwise mistake true 24/30/60 Hz modes for fractional modes.
    const int reportedHz = qRound(currentHz);
    if (isFractionalRateFamily(targetHz, reportedHz)) {
        return true;
    }
#endif

    if (isCommonFractionalTarget(targetHz)) {
        return qAbs(currentHz - targetHz) < 0.01;
    }

    return qAbs(currentHz - targetHz) < 0.1;
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

bool waitForAdvancedColorState(const DISPLAYCONFIG_PATH_INFO &pathInfo,
                               bool enabled,
                               int timeoutMs,
                               int pollMs,
                               const NativeOperationGuard &shouldContinue)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        if (shouldContinue && !shouldContinue()) {
            return false;
        }
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

bool setHDRWindowsImpl(bool enabled,
                       int settleDeadlineMs,
                       const NativeOperationGuard &shouldContinue = {})
{
    if (shouldContinue && !shouldContinue()) {
        return false;
    }
    // Use undocumented API to toggle HDR.
    UINT32 numPathArrayElements = 0;
    UINT32 numModeInfoArrayElements = 0;

    const LONG sizeRet = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPathArrayElements, &numModeInfoArrayElements);
    qCInfo(lcDisplayTrace) << "setHDRWindows buffer-sizes"
                           << "requested=" << enabled
                           << "ret=" << sizeRet
                           << "paths=" << numPathArrayElements
                           << "modes=" << numModeInfoArrayElements;
    if (sizeRet != ERROR_SUCCESS) {
        qCWarning(lcDisplayTrace) << "DisplayManager: GetDisplayConfigBufferSizes failed";
        return false;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> pathArray(numPathArrayElements);
    std::vector<DISPLAYCONFIG_MODE_INFO> modeInfoArray(numModeInfoArrayElements);

    const LONG queryRet = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,
                                             &numPathArrayElements,
                                             pathArray.data(),
                                             &numModeInfoArrayElements,
                                             modeInfoArray.data(),
                                             nullptr);
    qCInfo(lcDisplayTrace) << "setHDRWindows query-display-config"
                           << "requested=" << enabled
                           << "ret=" << queryRet
                           << "paths=" << numPathArrayElements
                           << "modes=" << numModeInfoArrayElements;
    if (queryRet != ERROR_SUCCESS) {
        qCWarning(lcDisplayTrace) << "DisplayManager: QueryDisplayConfig failed";
        return false;
    }

    bool success = true;
    bool handledAnyPath = false;
    QElapsedTimer settleDeadline;
    settleDeadline.start();

    for (UINT32 i = 0; i < numPathArrayElements; ++i) {
        if (shouldContinue && !shouldContinue()) {
            return false;
        }
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
            handledAnyPath = true;
            continue;
        }
        if (!preState.ok) {
            success = false;
        }

        DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setAdvancedColorState = {};
        setAdvancedColorState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
        setAdvancedColorState.header.size = sizeof(DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE);
        setAdvancedColorState.header.adapterId = pathArray[i].targetInfo.adapterId;
        setAdvancedColorState.header.id = pathArray[i].targetInfo.id;
        setAdvancedColorState.value = enabled ? 1 : 0;

        if (shouldContinue && !shouldContinue()) {
            return false;
        }
        const LONG ret = DisplayConfigSetDeviceInfo(&setAdvancedColorState.header);
        qCInfo(lcDisplayTrace) << "setHDRWindows path"
                               << i
                               << "adapter=" << formatAdapterId(pathArray[i].targetInfo.adapterId)
                               << "targetId=" << pathArray[i].targetInfo.id
                               << "requested=" << enabled
                               << "ret=" << ret;
        if (ret == ERROR_SUCCESS) {
            handledAnyPath = true;
            qCDebug(lcDisplayTrace) << "DisplayManager: Successfully set HDR to" << enabled << "for path" << i;
            static constexpr int kHdrSettlePollMs = 50;
            const int remainingMs = std::max(
                0, settleDeadlineMs - int(settleDeadline.elapsed()));
            const bool settled = remainingMs > 0
                && waitForAdvancedColorState(pathArray[i], enabled,
                                             remainingMs, kHdrSettlePollMs,
                                             shouldContinue);
            const AdvancedColorStateQueryResult postState = queryAdvancedColorState(pathArray[i]);
            qCInfo(lcDisplayTrace) << "setHDRWindows post-state"
                                   << "path=" << i
                                   << "settled=" << settled
                                   << "queryRet=" << postState.ret
                                   << "enabled=" << postState.enabled;
            if (!settled) {
                success = false;
                qCWarning(lcDisplayTrace) << "setHDRWindows settle-timeout"
                                          << "path=" << i
                                          << "requested=" << enabled
                                          << "deadlineMs=" << settleDeadlineMs;
                continue;
            }
        } else {
            success = false;
            qCWarning(lcDisplayTrace) << "DisplayManager: Failed to set HDR for path" << i << "error:" << ret;
        }
    }

    return handledAnyPath && success;
}

HdrAsyncFallbackResult runBlockingWindowsHdrToggle(bool enabled,
                                                   int settleDeadlineMs,
                                                   const NativeOperationGuard &shouldContinue)
{
    HdrAsyncFallbackResult result;
    result.preState = isAnyAdvancedColorEnabled();
    result.success = setHDRWindowsImpl(enabled, settleDeadlineMs,
                                       shouldContinue);
    return result;
}
#endif
}

DisplayManager::DisplayManager(ConfigManager *config, QObject *parent)
    : DisplayManager(config, DisplayManagerOptions{}, parent)
{
}

DisplayManager::DisplayManager(ConfigManager *config,
                               const DisplayManagerOptions &options,
                               QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_options(options)
{
    m_options.commandDeadlineMs = std::max(1, m_options.commandDeadlineMs);
    m_options.maximumCommandOutputBytes = std::max<qint64>(
        1024, m_options.maximumCommandOutputBytes);
#ifdef Q_OS_WIN
    m_nativeOperationGeneration =
        std::make_shared<std::atomic<quint64>>(m_operationGeneration);
#endif
    m_baselineRefreshRate = getCurrentRefreshRate();
    m_commandDeadlineTimer.setSingleShot(true);
    connect(&m_commandDeadlineTimer, &QTimer::timeout, this, [this]() {
        if (!m_commandProcess || m_commandProcess->state() == QProcess::NotRunning) {
            return;
        }
        QProcess *process = m_commandProcess;
        const QString operation = m_activeOperation;
        const qint64 elapsed = m_operationElapsed.elapsed();
        const std::function<void(bool, bool)> completion =
            std::move(m_commandCompletion);
        m_commandProcess.clear();
        m_activeOperation.clear();
        QObject::disconnect(process, nullptr, this, nullptr);
        process->setParent(nullptr);
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                process, &QObject::deleteLater);
        qCWarning(lcDisplayTrace) << "Display operation exceeded its deadline"
                                  << "operation=" << operation
                                  << "deadlineMs=" << m_options.commandDeadlineMs;
        killProcessTree(process, &m_commandJobHandle);
        recordOperationResult(operation, elapsed, false, true);
        if (completion) {
            completion(false, true);
        }
    });
}

DisplayManager::~DisplayManager()
{
    cancelPendingOperations();
    launchBestEffortShutdownRestore();
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
        qCWarning(lcDisplayTrace) << "DisplayManager: Failed to capture original refresh rate (current:" << current << ")";
        return;
    }

    m_originalRefreshRate = current;
    m_hasCapturedOriginalRefreshRate = true;
    qCDebug(lcDisplayTrace) << "DisplayManager: Captured original refresh rate:" << m_originalRefreshRate << "Hz";
    qCInfo(lcDisplayTrace) << "captureOriginalRefreshRate"
                           << "capturedHz=" << m_originalRefreshRate
                           << "refreshOverrideActive=" << m_refreshRateChanged;
}

void DisplayManager::updateHdrRestoreTracking(bool requestedState, bool preState)
{
    if (!m_hasCapturedOriginalHDRState) {
        m_originalHDRState = preState;
        m_hasCapturedOriginalHDRState = true;
    }

    m_hdrChanged = (requestedState != m_originalHDRState);
    if (!m_hdrChanged) {
        m_hasCapturedOriginalHDRState = false;
        m_originalHDRState = requestedState;
    }
    qCInfo(lcDisplayTrace) << "updateHdrRestoreTracking"
                           << "requestedState=" << requestedState
                           << "originalState=" << m_originalHDRState
                           << "restoreNeeded=" << m_hdrChanged
                           << "capturedOriginalState=" << m_hasCapturedOriginalHDRState;
}

QVariantMap DisplayManager::diagnostics() const
{
    return {
        {QStringLiteral("activeOperation"), m_activeOperation},
        {QStringLiteral("generation"), QVariant::fromValue(m_operationGeneration)},
        {QStringLiteral("operationsStarted"), QVariant::fromValue(m_operationsStarted)},
        {QStringLiteral("operationsSucceeded"), QVariant::fromValue(m_operationsSucceeded)},
        {QStringLiteral("operationsTimedOut"), QVariant::fromValue(m_operationsTimedOut)},
        {QStringLiteral("operationsCanceled"), QVariant::fromValue(m_operationsCanceled)},
        {QStringLiteral("lastOperationLatencyMs"), m_lastOperationLatencyMs},
        {QStringLiteral("capturedStdoutBytes"), m_commandStandardOutput.size()},
        {QStringLiteral("capturedStderrBytes"), m_commandStandardError.size()},
    };
}

quint64 DisplayManager::beginOperation(const QString &operation)
{
    cancelPendingOperations();
    m_activeOperation = operation;
    m_operationElapsed.restart();
    ++m_operationsStarted;
    qCInfo(lcDisplayTrace) << "Display operation started"
                           << "operation=" << operation
                           << "generation=" << m_operationGeneration;
    return m_operationGeneration;
}

void DisplayManager::recordOperationResult(const QString &operation,
                                           qint64 elapsedMs,
                                           bool success,
                                           bool timedOut)
{
    m_lastOperationLatencyMs = elapsedMs;
    if (success) {
        ++m_operationsSucceeded;
    }
    if (timedOut) {
        ++m_operationsTimedOut;
    }
    qCInfo(lcDisplayTrace) << "Display operation finished"
                           << "operation=" << operation
                           << "success=" << success
                           << "timedOut=" << timedOut
                           << "elapsedMs=" << elapsedMs;
    emit displayOperationMeasured(operation, elapsedMs, success, timedOut);
}

void DisplayManager::cancelCommandProcess()
{
    m_commandDeadlineTimer.stop();
    m_commandCompletion = {};
    if (!m_commandProcess) {
        closeCommandJob(&m_commandJobHandle, false);
        return;
    }
    QProcess *process = m_commandProcess;
    m_commandProcess.clear();
    QObject::disconnect(process, nullptr, this, nullptr);
    if (process->state() == QProcess::NotRunning) {
        closeCommandJob(&m_commandJobHandle, false);
        process->deleteLater();
        return;
    }
    process->setParent(nullptr);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            process, &QObject::deleteLater);
    killProcessTree(process, &m_commandJobHandle);
}

void DisplayManager::cancelPendingOperations()
{
    if (m_hdrOperationPending) {
        // The command/native request may already have changed the display even
        // though its completion is stale. Preserve the earliest state as a
        // conservative restore target without accepting any stale playback result.
        updateHdrRestoreTracking(m_pendingHdrRequestedState,
                                 m_pendingHdrPreState);
        m_hdrOperationPending = false;
    }
    if (!m_activeOperation.isEmpty() || m_commandProcess) {
        ++m_operationsCanceled;
    }
    ++m_operationGeneration;
#ifdef Q_OS_WIN
    m_nativeOperationGeneration->store(m_operationGeneration,
                                       std::memory_order_release);
#endif
    cancelCommandProcess();
    m_activeOperation.clear();
}

void DisplayManager::startExternalCommand(const QString &operation,
                                          const QString &command,
                                          quint64 generation,
                                          std::function<void(bool, bool)> completion)
{
    if (generation != m_operationGeneration || command.trimmed().isEmpty()) {
        return;
    }
    auto *process = new QProcess(this);
#ifdef Q_OS_UNIX
    // Put the configured command and every child it launches in an isolated
    // process group so cancellation cannot leave a stale compositor command
    // running after replacement playback has started.
    process->setChildProcessModifier([]() {
        (void) ::setpgid(0, 0);
    });
#endif
#ifdef Q_OS_WIN
    m_commandJobHandle = createCommandJob();
    connect(process, &QProcess::started, this, [this, process]() {
        if (process != m_commandProcess || m_commandJobHandle == 0) {
            return;
        }
        if (!assignCommandToJob(process, m_commandJobHandle)) {
            qCWarning(lcDisplayTrace)
                << "Could not isolate external display command in a Windows job";
            closeCommandJob(&m_commandJobHandle, false);
        }
    });
#endif
    m_commandProcess = process;
    m_commandCompletion = std::move(completion);
    m_commandStandardOutput.clear();
    m_commandStandardError.clear();

    auto drainBounded = [this](QByteArray chunk, QByteArray *destination) {
        const qint64 remaining = m_options.maximumCommandOutputBytes
            - destination->size();
        if (remaining > 0) {
            destination->append(chunk.left(remaining));
        }
    };
    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, process, drainBounded]() {
                drainBounded(process->readAllStandardOutput(),
                             &m_commandStandardOutput);
            });
    connect(process, &QProcess::readyReadStandardError, this,
            [this, process, drainBounded]() {
                drainBounded(process->readAllStandardError(),
                             &m_commandStandardError);
            });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, generation](int exitCode,
                                               QProcess::ExitStatus exitStatus) {
                finishExternalCommand(process, generation,
                                      exitStatus == QProcess::NormalExit
                                          && exitCode == 0);
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, generation](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    finishExternalCommand(process, generation, false);
                }
            });

    qCInfo(lcDisplayTrace) << "Starting external display command"
                           << "operation=" << operation
                           << "deadlineMs=" << m_options.commandDeadlineMs;
    process->startCommand(command);
    m_commandDeadlineTimer.start(m_options.commandDeadlineMs);
}

void DisplayManager::finishExternalCommand(QProcess *process,
                                           quint64 generation,
                                           bool success)
{
    if (process != m_commandProcess) {
        return;
    }
    m_commandDeadlineTimer.stop();
    const QString operation = m_activeOperation;
    const qint64 elapsed = m_operationElapsed.elapsed();
    const std::function<void(bool, bool)> completion =
        std::move(m_commandCompletion);
    const auto appendRemaining = [this](QByteArray chunk, QByteArray *destination) {
        const qint64 remaining = m_options.maximumCommandOutputBytes
            - destination->size();
        if (remaining > 0) {
            destination->append(chunk.left(remaining));
        }
    };
    appendRemaining(process->readAllStandardOutput(), &m_commandStandardOutput);
    appendRemaining(process->readAllStandardError(), &m_commandStandardError);
    closeCommandJob(&m_commandJobHandle, false);
    m_commandProcess.clear();
    process->deleteLater();
    m_activeOperation.clear();

    if (generation != m_operationGeneration) {
        return;
    }
    recordOperationResult(operation, elapsed, success, false);
    if (completion) {
        completion(success, false);
    }
}

void DisplayManager::queueRefreshRateResult(double requestedHz,
                                            bool success,
                                            bool changed,
                                            bool skippedCompatibleMultiple,
                                            double effectiveRate,
                                            quint64 generation)
{
    QMetaObject::invokeMethod(this, [this, requestedHz, success, changed,
                                     skippedCompatibleMultiple, effectiveRate,
                                     generation]() {
        if (generation != m_operationGeneration) {
            return;
        }
        const QString operation = m_activeOperation;
        const qint64 elapsed = m_operationElapsed.elapsed();
        m_activeOperation.clear();
        m_lastRefreshRateSwitchChanged = success && changed;
        m_lastRefreshRateSwitchSkippedCompatibleMultiple =
            success && skippedCompatibleMultiple;
        m_lastRefreshRateSwitchEffectiveRate = success ? effectiveRate : 0.0;
        if (success && changed) {
            m_refreshRateChanged = true;
        }
        recordOperationResult(operation, elapsed, success, false);
        emit refreshRateChangeFinished(requestedHz, success);
    }, Qt::QueuedConnection);
}

void DisplayManager::setRefreshRateAsync(double hz)
{
    const quint64 generation = beginOperation(QStringLiteral("set-refresh-rate"));
    m_lastRefreshRateSwitchChanged = false;
    m_lastRefreshRateSwitchSkippedCompatibleMultiple = false;
    m_lastRefreshRateSwitchEffectiveRate = 0.0;

    if (hz <= 0.0) {
        queueRefreshRateResult(hz, false, false, false, 0.0, generation);
        return;
    }
    const double current = getCurrentRefreshRate();
    if (isCurrentRefreshAlreadyTarget(current, hz)) {
        queueRefreshRateResult(hz, true, false, false, hz, generation);
        return;
    }
    if (m_config->getSkipRefreshRateOnCompatibleMultiple()
        && isCadenceCompatible(current, hz)) {
        queueRefreshRateResult(hz, true, false, true, current, generation);
        return;
    }
    if (!m_refreshRateChanged && !m_hasCapturedOriginalRefreshRate) {
        m_originalRefreshRate = current;
        m_hasCapturedOriginalRefreshRate = current > 0.0;
    }

#ifdef Q_OS_WIN
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, watcher, hz, generation]() {
                const bool success = watcher->result();
                watcher->deleteLater();
                if (generation != m_operationGeneration) {
                    return;
                }
                const QString operation = m_activeOperation;
                const qint64 elapsed = m_operationElapsed.elapsed();
                m_activeOperation.clear();
                const double current = getCurrentRefreshRate();
                m_lastRefreshRateSwitchChanged = success;
                m_lastRefreshRateSwitchSkippedCompatibleMultiple = false;
                m_lastRefreshRateSwitchEffectiveRate = success
                    ? (isCurrentRefreshAlreadyTarget(current, hz) ? hz : current)
                    : 0.0;
                if (success) {
                    m_refreshRateChanged = true;
                }
                recordOperationResult(operation, elapsed, success, false);
                emit refreshRateChangeFinished(hz, success);
            });
    const auto nativeGeneration = m_nativeOperationGeneration;
    watcher->setFuture(QtConcurrent::run(nativeDisplayThreadPool(),
                                         [hz, generation, nativeGeneration]() {
        if (nativeGeneration->load(std::memory_order_acquire) != generation) {
            return false;
        }
        const NativeOperationGuard shouldContinue =
            [nativeGeneration, generation]() {
                return nativeGeneration->load(std::memory_order_acquire)
                    == generation;
            };
        return setRefreshRateWindowsImpl(hz, shouldContinue);
    }));
    QTimer::singleShot(m_options.commandDeadlineMs, this,
                       [this, generation, hz]() {
        if (generation != m_operationGeneration
            || m_activeOperation != QStringLiteral("set-refresh-rate")) {
            return;
        }
        ++m_operationGeneration;
        const QString operation = m_activeOperation;
        const qint64 elapsed = m_operationElapsed.elapsed();
        m_activeOperation.clear();
        recordOperationResult(operation, elapsed, false, true);
        emit refreshRateChangeFinished(hz, false);
    });
#else
    const QString command = refreshRateCommand(
        m_config->getLinuxRefreshRateCommand(), hz);
    if (command.isEmpty()) {
        queueRefreshRateResult(hz, false, false, false, 0.0, generation);
        return;
    }
    startExternalCommand(QStringLiteral("set-refresh-rate"), command, generation,
                         [this, hz, generation](bool success, bool) {
        if (generation != m_operationGeneration) {
            return;
        }
        const double current = getCurrentRefreshRate();
        m_lastRefreshRateSwitchChanged = success;
        m_lastRefreshRateSwitchSkippedCompatibleMultiple = false;
        m_lastRefreshRateSwitchEffectiveRate = success
            ? (isCurrentRefreshAlreadyTarget(current, hz) ? hz : current)
            : 0.0;
        if (success) {
            m_refreshRateChanged = true;
        }
        emit refreshRateChangeFinished(hz, success);
    });
#endif
}

void DisplayManager::restoreRefreshRateAsync()
{
    const quint64 generation = beginOperation(QStringLiteral("restore-refresh-rate"));
    const bool hasTarget = m_hasCapturedOriginalRefreshRate
        && m_originalRefreshRate > 0.0;
    if (!m_refreshRateChanged && !hasTarget) {
        QMetaObject::invokeMethod(this, [this, generation]() {
            if (generation != m_operationGeneration) {
                return;
            }
            const QString operation = m_activeOperation;
            const qint64 elapsed = m_operationElapsed.elapsed();
            m_activeOperation.clear();
            recordOperationResult(operation, elapsed, true, false);
            emit refreshRateRestoreFinished(true);
        }, Qt::QueuedConnection);
        return;
    }
    const double target = m_originalRefreshRate > 0.0
        ? m_originalRefreshRate : m_baselineRefreshRate;

#ifdef Q_OS_WIN
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, watcher, generation]() {
                const bool success = watcher->result();
                watcher->deleteLater();
                if (generation != m_operationGeneration) {
                    return;
                }
                const QString operation = m_activeOperation;
                const qint64 elapsed = m_operationElapsed.elapsed();
                m_activeOperation.clear();
                if (success) {
                    m_refreshRateChanged = false;
                    m_hasCapturedOriginalRefreshRate = false;
                    m_originalRefreshRate = 0.0;
                }
                recordOperationResult(operation, elapsed, success, false);
                emit refreshRateRestoreFinished(success);
            });
    const double baseline = m_baselineRefreshRate;
    const auto nativeGeneration = m_nativeOperationGeneration;
    watcher->setFuture(QtConcurrent::run(nativeDisplayThreadPool(),
                                         [target, baseline, generation,
                                          nativeGeneration]() {
        if (nativeGeneration->load(std::memory_order_acquire) != generation) {
            return false;
        }
        const NativeOperationGuard shouldContinue =
            [nativeGeneration, generation]() {
                return nativeGeneration->load(std::memory_order_acquire)
                    == generation;
            };
        return restoreRefreshRateWindowsImpl(target, baseline,
                                             shouldContinue);
    }));
    QTimer::singleShot(m_options.commandDeadlineMs, this,
                       [this, generation]() {
        if (generation != m_operationGeneration
            || m_activeOperation != QStringLiteral("restore-refresh-rate")) {
            return;
        }
        ++m_operationGeneration;
        const QString operation = m_activeOperation;
        const qint64 elapsed = m_operationElapsed.elapsed();
        m_activeOperation.clear();
        recordOperationResult(operation, elapsed, false, true);
        emit refreshRateRestoreFinished(false);
    });
#else
    const QString command = refreshRateCommand(
        m_config->getLinuxRefreshRateCommand(), target);
    if (command.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, generation]() {
            if (generation != m_operationGeneration) {
                return;
            }
            const QString operation = m_activeOperation;
            const qint64 elapsed = m_operationElapsed.elapsed();
            m_activeOperation.clear();
            recordOperationResult(operation, elapsed, false, false);
            emit refreshRateRestoreFinished(false);
        }, Qt::QueuedConnection);
        return;
    }
    startExternalCommand(QStringLiteral("restore-refresh-rate"), command,
                         generation, [this, generation](bool success, bool) {
        if (generation != m_operationGeneration) {
            return;
        }
        if (success) {
            m_refreshRateChanged = false;
            m_hasCapturedOriginalRefreshRate = false;
            m_originalRefreshRate = 0.0;
        }
        emit refreshRateRestoreFinished(success);
    });
#endif
}

void DisplayManager::setHDRAsync(bool enabled)
{
    const quint64 generation = beginOperation(
        enabled ? QStringLiteral("enable-hdr") : QStringLiteral("restore-hdr"));
#ifdef Q_OS_WIN
    const bool preState = isAnyAdvancedColorEnabled();
#else
    const bool preState = m_hasCapturedOriginalHDRState
        ? m_originalHDRState : false;
#endif
#ifdef Q_OS_WIN
    m_hdrOperationPending = true;
    m_pendingHdrRequestedState = enabled;
    m_pendingHdrPreState = preState;
    const QString command = hdrCommand(m_config->getWindowsCustomHDRCommand(), enabled);
    if (!command.isEmpty()) {
        startExternalCommand(m_activeOperation, command, generation,
                             [this, enabled, preState, generation](bool success,
                                                                  bool timedOut) {
            if (generation != m_operationGeneration) {
                return;
            }
            m_hdrOperationPending = false;
            if (success || timedOut) {
                updateHdrRestoreTracking(enabled, preState);
            }
            emit hdrChangeFinished(enabled, success);
        });
        return;
    }
    auto *watcher = new QFutureWatcher<HdrAsyncFallbackResult>(this);
    connect(watcher, &QFutureWatcher<HdrAsyncFallbackResult>::finished, this,
            [this, watcher, enabled, generation]() {
        const HdrAsyncFallbackResult result = watcher->result();
        watcher->deleteLater();
        if (generation != m_operationGeneration) {
            return;
        }
        const QString operation = m_activeOperation;
        const qint64 elapsed = m_operationElapsed.elapsed();
        m_activeOperation.clear();
        m_hdrOperationPending = false;
        if (result.success) {
            updateHdrRestoreTracking(enabled, result.preState);
        }
        recordOperationResult(operation, elapsed, result.success, false);
        emit hdrChangeFinished(enabled, result.success);
    });
    const int settleDeadlineMs = m_options.commandDeadlineMs;
    const auto nativeGeneration = m_nativeOperationGeneration;
    watcher->setFuture(QtConcurrent::run(nativeDisplayThreadPool(),
                                         [enabled, settleDeadlineMs, generation,
                                          nativeGeneration]() {
        if (nativeGeneration->load(std::memory_order_acquire) != generation) {
            return HdrAsyncFallbackResult{};
        }
        const NativeOperationGuard shouldContinue =
            [nativeGeneration, generation]() {
                return nativeGeneration->load(std::memory_order_acquire)
                    == generation;
            };
        return runBlockingWindowsHdrToggle(enabled, settleDeadlineMs,
                                           shouldContinue);
    }));
    QTimer::singleShot(m_options.commandDeadlineMs, this,
                       [this, generation, enabled, preState]() {
        if (generation != m_operationGeneration
            || m_activeOperation.isEmpty()) {
            return;
        }
        ++m_operationGeneration;
        const QString operation = m_activeOperation;
        const qint64 elapsed = m_operationElapsed.elapsed();
        m_activeOperation.clear();
        m_hdrOperationPending = false;
        updateHdrRestoreTracking(enabled, preState);
        recordOperationResult(operation, elapsed, false, true);
        emit hdrChangeFinished(enabled, false);
    });
#else
    const QString command = hdrCommand(m_config->getLinuxHDRCommand(), enabled);
    if (command.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, enabled, generation]() {
            if (generation != m_operationGeneration) {
                return;
            }
            const QString operation = m_activeOperation;
            const qint64 elapsed = m_operationElapsed.elapsed();
            m_activeOperation.clear();
            m_hdrOperationPending = false;
            recordOperationResult(operation, elapsed, false, false);
            emit hdrChangeFinished(enabled, false);
        }, Qt::QueuedConnection);
        return;
    }
    m_hdrOperationPending = true;
    m_pendingHdrRequestedState = enabled;
    m_pendingHdrPreState = preState;
    startExternalCommand(m_activeOperation, command, generation,
                         [this, enabled, preState, generation](bool success,
                                                              bool timedOut) {
        if (generation != m_operationGeneration) {
            return;
        }
        m_hdrOperationPending = false;
        if (success || timedOut) {
            updateHdrRestoreTracking(enabled, preState);
        }
        emit hdrChangeFinished(enabled, success);
    });
#endif
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
        qCWarning(lcDisplayTrace) << "DisplayManager: EnumDisplaySettings failed when reading current refresh rate";
    }
#endif

    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        return screen->refreshRate();  // Returns qreal (double) with fractional precision
    }
    return 60.0;
}

namespace {
#ifdef Q_OS_WIN
bool setRefreshRateWindowsImpl(double hz,
                               const NativeOperationGuard &shouldContinue)
{
    if (shouldContinue && !shouldContinue()) {
        return false;
    }
    qCDebug(lcDisplayTrace) << "DisplayManager::setRefreshRateWindows called with hz:" << hz;
    
    DEVMODE dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);
    
    if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
        qCDebug(lcDisplayTrace) << "DisplayManager: Current display settings - Width:" << dm.dmPelsWidth 
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
            qCDebug(lcDisplayTrace) << "DisplayManager: Detected film framerate" << hz << ", will try 23Hz mode first";
        } else if (hz > 29.0 && hz < 30.0 && hz != 30.0) {
            // 29.97 content
            tryExactFirst = true;
            qCDebug(lcDisplayTrace) << "DisplayManager: Detected 29.97 framerate, will try 29Hz mode first";
        } else if (hz > 59.0 && hz < 60.0 && hz != 60.0) {
            // 59.94 content
            tryExactFirst = true;
            qCDebug(lcDisplayTrace) << "DisplayManager: Detected 59.94 framerate, will try 59Hz mode first";
        }
        
        // Try exact truncated rate first if applicable (23 for 23.976, etc.)
        if (tryExactFirst && exactHz != targetHz) {
            dm.dmDisplayFrequency = exactHz;
            dm.dmFields = DM_DISPLAYFREQUENCY;
            
            if (shouldContinue && !shouldContinue()) {
                return false;
            }
            LONG ret = ChangeDisplaySettingsEx(NULL, &dm, NULL, CDS_FULLSCREEN, NULL);
            if (ret == DISP_CHANGE_SUCCESSFUL) {
                qCDebug(lcDisplayTrace) << "DisplayManager: Successfully set refresh rate to" << exactHz << "Hz (exact match for" << hz << ")";
                return true;
            }
            qCDebug(lcDisplayTrace) << "DisplayManager: Exact" << exactHz << "Hz mode not available, trying" << targetHz << "Hz";
        }
        
        // Try rounded rate
        dm.dmDisplayFrequency = targetHz;
        dm.dmFields = DM_DISPLAYFREQUENCY;
        
        // Use CDS_FULLSCREEN without CDS_UPDATEREGISTRY so we can restore to registry settings later
        if (shouldContinue && !shouldContinue()) {
            return false;
        }
        LONG ret = ChangeDisplaySettingsEx(NULL, &dm, NULL, CDS_FULLSCREEN, NULL);
        if (ret == DISP_CHANGE_SUCCESSFUL) {
            qCDebug(lcDisplayTrace) << "DisplayManager: Successfully set refresh rate to" << targetHz << "Hz";
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
            qCWarning(lcDisplayTrace) << "DisplayManager: Failed to set refresh rate to" << targetHz << "Hz, error:" << errorMsg;
        }
    } else {
        qCWarning(lcDisplayTrace) << "DisplayManager: EnumDisplaySettings failed";
    }
    return false;
}

bool restoreRefreshRateWindowsImpl(
    double targetHz,
    double baselineHz,
    const NativeOperationGuard &shouldContinue)
{
    if (shouldContinue && !shouldContinue()) {
        return false;
    }
    const double restoreTarget = targetHz > 0.0 ? targetHz : baselineHz;
    if (restoreTarget > 0.0) {
        qCDebug(lcDisplayTrace) << "DisplayManager: Restoring display refresh to captured original rate"
                 << restoreTarget << "Hz";
        if (setRefreshRateWindowsImpl(restoreTarget, shouldContinue)) {
            return true;
        }
        qCWarning(lcDisplayTrace) << "DisplayManager: Failed to restore to captured rate, falling back to registry defaults";
    }

    qCDebug(lcDisplayTrace) << "DisplayManager: Restoring display settings to registry defaults";
    if (shouldContinue && !shouldContinue()) {
        return false;
    }
    LONG ret = ChangeDisplaySettingsEx(NULL, NULL, NULL, 0, NULL);
    if (ret == DISP_CHANGE_SUCCESSFUL) {
        qCDebug(lcDisplayTrace) << "DisplayManager: Restored display settings";
        return true;
    }

    qCWarning(lcDisplayTrace) << "DisplayManager: Failed to restore display settings, error:" << ret;
    return false;
}

#endif
} // namespace

void DisplayManager::launchBestEffortShutdownRestore()
{
    if (!m_hdrChanged && !needsRefreshRestore()) {
        return;
    }

    qCWarning(lcDisplayTrace)
        << "DisplayManager destroyed with pending display restoration; issuing best-effort non-blocking restore"
        << "hdr=" << m_hdrChanged
        << "refresh=" << needsRefreshRestore();

#ifdef Q_OS_WIN
    const bool restoreHdr = m_hdrChanged;
    const bool originalHdrState = m_originalHDRState;
    const bool restoreRefresh = needsRefreshRestore();
    const double refreshTarget = m_originalRefreshRate;
    const double baseline = m_baselineRefreshRate;
    const QString customHdrCommand = restoreHdr
        ? hdrCommand(m_config->getWindowsCustomHDRCommand(), originalHdrState)
        : QString();
    const int deadlineMs = m_options.commandDeadlineMs;
    (void) QtConcurrent::run(nativeDisplayThreadPool(),
                            [restoreHdr, originalHdrState, restoreRefresh,
                             refreshTarget, baseline, customHdrCommand,
                             deadlineMs]() {
        if (restoreHdr) {
            if (customHdrCommand.isEmpty()) {
                setHDRWindowsImpl(originalHdrState, deadlineMs);
            } else {
                QProcess process;
                process.setStandardOutputFile(QProcess::nullDevice());
                process.setStandardErrorFile(QProcess::nullDevice());
                process.startCommand(customHdrCommand);
                if (!process.waitForFinished(deadlineMs)) {
                    process.kill();
                    process.waitForFinished(250);
                }
            }
        }
        if (restoreRefresh) {
            restoreRefreshRateWindowsImpl(refreshTarget, baseline);
        }
    });
#else
    QStringList commands;
    if (m_hdrChanged) {
        const QString command = hdrCommand(m_config->getLinuxHDRCommand(),
                                           m_originalHDRState);
        if (!command.isEmpty()) {
            commands.append(command);
        }
    }
    if (needsRefreshRestore()) {
        const double target = m_originalRefreshRate > 0.0
            ? m_originalRefreshRate : m_baselineRefreshRate;
        const QString command = refreshRateCommand(
            m_config->getLinuxRefreshRateCommand(), target);
        if (!command.isEmpty()) {
            commands.append(command);
        }
    }
    if (!commands.isEmpty()) {
        // Match QProcess::startCommand tokenization for each configured command,
        // then quote each token into one detached, ordered best-effort sequence.
        const QString sequence = detachedCommandSequence(commands);
        QProcess::startDetached(QStringLiteral("/bin/sh"),
                                {QStringLiteral("-c"), sequence});
    }
#endif
}
