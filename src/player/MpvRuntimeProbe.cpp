#include "MpvRuntimeProbe.h"

#include <QFileInfo>
#include <QStandardPaths>

#include "utils/BloomLogging.h"

#include <algorithm>

namespace PlayerPolicy = Bloom::PlaybackPolicy;

namespace {

constexpr int kStageCount = 3;

QStringList stageArguments(int stage)
{
    QStringList arguments{QStringLiteral("--no-config")};
    switch (stage) {
    case 0:
        arguments << QStringLiteral("--vf=format=dolbyvision=yes:enhancement-layer=yes");
        break;
    case 1:
        arguments << QStringLiteral("--vf=format=dolbyvision=yes");
        break;
    default:
        break;
    }
    arguments << QStringLiteral("--version");
    return arguments;
}

PlayerPolicy::FormatFilterSupport supportForStage(int stage)
{
    switch (stage) {
    case 0: return PlayerPolicy::FormatFilterSupport::DolbyVisionAndEnhancementLayer;
    case 1: return PlayerPolicy::FormatFilterSupport::DolbyVisionOnly;
    default: return PlayerPolicy::FormatFilterSupport::Unsupported;
    }
}

} // namespace

MpvRuntimeProbe::MpvRuntimeProbe(QObject *parent)
    : MpvRuntimeProbe(MpvRuntimeProbeOptions{}, parent)
{
}

MpvRuntimeProbe::MpvRuntimeProbe(const MpvRuntimeProbeOptions &options, QObject *parent)
    : QObject(parent)
    , m_options(options)
{
    m_options.stageDeadlineMs = std::max(1, m_options.stageDeadlineMs);
    m_options.maximumOutputBytes = std::max<qint64>(1024, m_options.maximumOutputBytes);
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, &MpvRuntimeProbe::onStageTimeout);
}

MpvRuntimeProbe::~MpvRuntimeProbe()
{
    m_deadline.stop();
    resetProcess();
}

void MpvRuntimeProbe::probe(const QString &executable, const QStringList &extraArguments)
{
    const QString trimmed = executable.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (trimmed == m_executable && extraArguments == m_extraArguments
        && (m_finished || isRunning())) {
        return;
    }

    ++m_generation;
    m_deadline.stop();
    resetProcess();
    m_executable = trimmed;
    m_extraArguments = extraArguments;
    m_finished = false;
    m_failureReason.clear();
    m_result = {};
    m_result.backend = QStringLiteral("external-mpv-ipc");

    const QFileInfo info(trimmed);
    m_resolvedExecutable = info.isAbsolute() || trimmed.contains(QLatin1Char('/'))
        ? info.absoluteFilePath()
        : QStandardPaths::findExecutable(trimmed);
    m_result.executablePath = m_resolvedExecutable.isEmpty() ? trimmed : m_resolvedExecutable;
    if (m_resolvedExecutable.isEmpty()) {
        m_stage = 0;
        finish(PlayerPolicy::FormatFilterSupport::Unknown,
               QStringLiteral("executable not found on PATH"));
        return;
    }
    startStage(0);
}

void MpvRuntimeProbe::resetProcess()
{
    if (!m_process) {
        return;
    }
    QProcess *process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        // Let the child finish (or be reaped) without blocking the UI thread.
        process->setParent(nullptr);
        connect(process, &QProcess::finished, process, &QObject::deleteLater);
        process->kill();
    } else {
        process->deleteLater();
    }
}

void MpvRuntimeProbe::startStage(int stage)
{
    resetProcess();
    m_stage = stage;
    m_output.clear();
    m_outputTruncated = false;

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    const quint64 generation = m_generation;
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this, generation]() {
        if (generation == m_generation) {
            readOutput();
        }
    });
    connect(m_process, &QProcess::finished, this,
            [this, generation](int exitCode, QProcess::ExitStatus status) {
                if (generation == m_generation) {
                    onStageFinished(exitCode, status);
                }
            });
    connect(m_process, &QProcess::errorOccurred, this,
            [this, generation](QProcess::ProcessError error) {
                if (generation == m_generation) {
                    onStageError(error);
                }
            });

    m_deadline.start(m_options.stageDeadlineMs);
    m_process->start(m_resolvedExecutable, m_extraArguments + stageArguments(stage));
}

void MpvRuntimeProbe::readOutput()
{
    if (!m_process) {
        return;
    }
    const QByteArray chunk = m_process->readAll();
    const qint64 remaining = m_options.maximumOutputBytes - m_output.size();
    if (remaining <= 0) {
        m_outputTruncated = true;
        return;
    }
    m_output.append(chunk.left(remaining));
    if (chunk.size() > remaining) {
        m_outputTruncated = true;
    }
}

void MpvRuntimeProbe::onStageFinished(int exitCode, QProcess::ExitStatus status)
{
    m_deadline.stop();
    readOutput();
    if (status == QProcess::NormalExit && exitCode == 0) {
        finish(supportForStage(m_stage));
        return;
    }
    advanceOrFinish();
}

void MpvRuntimeProbe::onStageError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart) {
        // Crash/read errors are followed by finished(); handle them there.
        return;
    }
    m_deadline.stop();
    finish(PlayerPolicy::FormatFilterSupport::Unknown,
           QStringLiteral("failed to start: %1").arg(m_process ? m_process->errorString() : QString()));
}

void MpvRuntimeProbe::onStageTimeout()
{
    qCWarning(lcPlayback) << "mpv runtime probe stage" << m_stage << "exceeded"
                          << m_options.stageDeadlineMs << "ms for" << m_resolvedExecutable;
    resetProcess();
    advanceOrFinish();
}

void MpvRuntimeProbe::advanceOrFinish()
{
    if (m_stage + 1 < kStageCount) {
        startStage(m_stage + 1);
        return;
    }
    finish(PlayerPolicy::FormatFilterSupport::Unknown,
           QStringLiteral("mpv --version did not succeed"));
}

void MpvRuntimeProbe::finish(PlayerPolicy::FormatFilterSupport support, const QString &failure)
{
    m_deadline.stop();
    const QString executablePath = m_result.executablePath;
    PlayerPolicy::MpvRuntimeInfo parsed =
        PlayerPolicy::parseMpvVersionOutput(QString::fromUtf8(m_output));
    parsed.backend = QStringLiteral("external-mpv-ipc");
    parsed.executablePath = executablePath;
    parsed.formatFilter = support;
    m_result = parsed;
    m_failureReason = failure;
    m_finished = true;
    resetProcess();

    if (!failure.isEmpty()) {
        qCWarning(lcPlayback) << "mpv runtime probe failed for" << executablePath << ":" << failure;
    }
    if (m_outputTruncated) {
        qCDebug(lcPlayback) << "mpv runtime probe output truncated at"
                            << m_options.maximumOutputBytes << "bytes";
    }
    emit finished(m_result);
}
