#pragma once

#include "models/MediaModels.h"
#include "SeekPreviewProtocol.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

struct SeekPreviewCache;

class LocalSeekPreview final : public QObject
{
    Q_OBJECT

public:
    struct Options {
        QString helperProgram;
        int timeoutMs = 10000;
        QString cacheRoot;
        int paceMs = Bloom::SeekPreviewProtocol::framePaceMs;
        int requestDelayMs = 400;
        int retryDelayMs = 30000;
    };

    explicit LocalSeekPreview(QObject *parent = nullptr);
    explicit LocalSeekPreview(Options options, QObject *parent = nullptr);
    ~LocalSeekPreview() override;

    static constexpr int intervalMs = Bloom::SeekPreviewProtocol::intervalMs;
    void setSource(const Bloom::PlaybackDescriptor &descriptor, bool toneMap);
    void updateStream(const Bloom::StreamRequest &stream);
    void clear();
    // Asks for the frame at `seconds`. Requests made close together form one
    // batch; generation starts after they settle and only covers that batch.
    void request(double seconds);
    // Suspension stops the helper and holds pending frames until resumed.
    void setSuspended(bool suspended);
    bool isSuspended() const { return m_suspended; }
    bool isRunning() const { return m_process != nullptr; }
    QString frameUrlAt(double seconds) const;
    bool isAvailable() const { return m_available; }

signals:
    void framesChanged();
    void unavailable();
    // Emitted when a helper run ends; `complete` is true when its batch is done.
    void finished(bool complete);

private:
    void launchBatch();
    void launch();
    void readProgress();
    void cancelWork();
    void fail(const char *stage, int detail = 0, bool skipFrame = false);
    qint64 frameIndex(double seconds) const;
    bool isPending(qint64 index) const;

    Options m_options;
    Bloom::PlaybackDescriptor m_descriptor;
    bool m_toneMap = false;
    bool m_available = false;
    bool m_suspended = false;
    bool m_stopped = false;
    QProcess *m_process = nullptr;
    QTimer m_deadline;
    QTimer m_requestDelay;
    QTimer m_retryDelay;
    QElapsedTimer m_clock;
    std::shared_ptr<SeekPreviewCache> m_cache;
    std::shared_ptr<void> m_processJob;
    qint64 m_totalImageBytes = 0;
    QByteArray m_progress;
    QHash<qint64, QString> m_frames;
    QSet<qint64> m_skippedFrames;
    QHash<qint64, qint64> m_requestedAt;
    QList<qint64> m_order;
    qint64 m_runningFrame = -1;
    int m_exitingProcesses = 0;
    int m_failures = 0;
};
