#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "player/DolbyVisionPolicy.h"

struct MpvRuntimeProbeOptions {
    int stageDeadlineMs = 4000;
    qint64 maximumOutputBytes = 64 * 1024;
};

// Event-driven capability probe for an external mpv executable. Each stage
// asks mpv to parse a `vf=format` option set and print `--version`; mpv
// rejects unknown filter suboptions before printing, so the exit status proves
// which Dolby Vision options the runtime actually implements:
//   1. format=dolbyvision=yes:enhancement-layer=yes  (post-PR-#17932)
//   2. format=dolbyvision=yes                        (pre-FEL mpv)
//   3. plain --version                               (no DV filter options)
// Never blocks: no QProcess::waitFor*, bounded output, per-stage deadline.
class MpvRuntimeProbe : public QObject
{
    Q_OBJECT
public:
    explicit MpvRuntimeProbe(QObject *parent = nullptr);
    explicit MpvRuntimeProbe(const MpvRuntimeProbeOptions &options, QObject *parent = nullptr);
    ~MpvRuntimeProbe() override;

    // Starts probing `executable` unless the same executable is already
    // probed or in flight. `extraArguments` are prepended (used by tests).
    void probe(const QString &executable, const QStringList &extraArguments = {});

    bool isFinished() const { return m_finished; }
    bool isRunning() const { return m_stage >= 0 && !m_finished; }
    QString executable() const { return m_executable; }
    Bloom::PlaybackPolicy::MpvRuntimeInfo result() const { return m_result; }
    QString failureReason() const { return m_failureReason; }

signals:
    void finished(const Bloom::PlaybackPolicy::MpvRuntimeInfo &info);

private:
    void startStage(int stage);
    void onStageFinished(int exitCode, QProcess::ExitStatus status);
    void onStageError(QProcess::ProcessError error);
    void onStageTimeout();
    void readOutput();
    void finish(Bloom::PlaybackPolicy::FormatFilterSupport support, const QString &failure = {});
    void advanceOrFinish();
    void resetProcess();

    MpvRuntimeProbeOptions m_options;
    QProcess *m_process = nullptr;
    QTimer m_deadline;
    QString m_executable;
    QString m_resolvedExecutable;
    QStringList m_extraArguments;
    QByteArray m_output;
    bool m_outputTruncated = false;
    int m_stage = -1;
    quint64 m_generation = 0;
    bool m_finished = false;
    QString m_failureReason;
    Bloom::PlaybackPolicy::MpvRuntimeInfo m_result;
};
