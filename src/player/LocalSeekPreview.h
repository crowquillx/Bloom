#pragma once

#include "models/MediaModels.h"
#include "SeekPreviewProtocol.h"

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
    };

    explicit LocalSeekPreview(QObject *parent = nullptr);
    explicit LocalSeekPreview(Options options, QObject *parent = nullptr);
    ~LocalSeekPreview() override;

    static constexpr int intervalMs = Bloom::SeekPreviewProtocol::intervalMs;
    void setSource(const Bloom::PlaybackDescriptor &descriptor, bool toneMap);
    void updateStream(const Bloom::StreamRequest &stream);
    void clear();
    void start();
    QString frameUrlAt(double seconds) const;
    bool isAvailable() const { return m_available; }

signals:
    void framesChanged();
    void unavailable();
    void finished(bool complete);

private:
    void readProgress();
    void cancelWork(bool restart = false);
    void fail(const char *stage, int detail = 0, bool skipFrame = false);
    qint64 frameIndex(double seconds) const;

    Options m_options;
    Bloom::PlaybackDescriptor m_descriptor;
    bool m_toneMap = false;
    bool m_available = false;
    QProcess *m_process = nullptr;
    QTimer m_deadline;
    std::shared_ptr<SeekPreviewCache> m_cache;
    std::shared_ptr<void> m_processJob;
    qint64 m_totalImageBytes = 0;
    QByteArray m_progress;
    QHash<qint64, QString> m_frames;
    QSet<qint64> m_skippedFrames;
    qint64 m_runningFrame = -1;
    quint64 m_generation = 0;
    int m_failures = 0;
    bool m_complete = false;
};
