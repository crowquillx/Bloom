#pragma once

#include <QHash>
#include <QDeadlineTimer>
#include <QFuture>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

#include "player/DolbyVisionPolicy.h"

#if defined(BLOOM_HAS_LIBMPV)
extern "C" {
#include <mpv/client.h>
}
#endif

// Shared libmpv helpers for the embedded backends (win-libmpv,
// linux-libmpv-opengl): runtime provenance/capability discovery, video filter
// list operations, and asynchronous diagnostic property snapshots.
namespace BloomBackend {

struct VideoFilterListOperation
{
    QString operation; // "set", "append", "add", "pre", "remove", "toggle", "clr"
    QString value;
};

// Removes `--vf[=]/set/append/add/pre/remove/toggle/clr` args from `args` and returns
// them in order. mpv_set_option_string() parses into an empty list (it would
// replace the whole chain or reject the suffix), so these must be replayed
// after mpv_initialize() through the `vf <operation>` command instead.
[[nodiscard]] QList<VideoFilterListOperation> takeVideoFilterListOperations(QStringList *args);

// True for an mpv log line reporting a network stream failure, from either
// FFmpeg's http protocol or mpv's libcurl stream backend.
[[nodiscard]] bool isStreamFailureLog(const QString &level, const QString &prefix, const QString &text);

// Decides whether an EOF is the stream dying rather than the file ending.
// After a stream fails, the demuxer keeps playing its cache, so the EOF can
// arrive minutes after the failure. An EOF counts as premature when it lands
// shortly after the failure or no later than where the cache ended at
// failure time. An EOF at the known duration is always genuine.
class StreamFailureTracker
{
public:
    void observePosition(double seconds);
    void observeDuration(double seconds);
    void observeCacheEnd(double seconds);
    // Records `text` when it is a stream failure; returns whether it was.
    bool recordLog(const QString &level, const QString &prefix, const QString &text, qint64 nowMs);
    [[nodiscard]] bool explainsEof(qint64 nowMs) const;
    [[nodiscard]] bool hasFailure() const { return !m_failureText.isEmpty(); }
    [[nodiscard]] QString failureText() const { return m_failureText; }
    // Forgets the failure and the observed timeline (new file or teardown).
    void clear();

private:
    double m_position = -1.0;
    double m_duration = -1.0;
    double m_cacheEnd = -1.0;
    double m_cacheEndAtFailure = -1.0;
    qint64 m_failureTimeMs = 0;
    QString m_failureText;
};

#if defined(BLOOM_HAS_LIBMPV)
// Cached per process: creates an uninitialized handle, test-parses the
// vf=format Dolby Vision options, and destroys it. Cheap (no VO/playback).
[[nodiscard]] Bloom::PlaybackPolicy::FormatFilterSupport probeLinkedFormatFilterSupport();

// Path of the loaded libmpv module, where the platform can report it.
[[nodiscard]] QString loadedLibmpvPath();

// Provenance of an initialized handle (version properties + client API).
[[nodiscard]] Bloom::PlaybackPolicy::MpvRuntimeInfo readRuntimeInfo(mpv_handle *handle,
                                                                    const QString &backendName);

// Replays filter operations in order with `vf <op> <value>`. If the runtime
// rejects the Bloom Dolby Vision entry with enhancement-layer, it retries the
// dolbyvision-only form. Returns human-readable failures (empty on success).
QStringList applyVideoFilterListOperations(mpv_handle *handle,
                                           const QList<VideoFilterListOperation> &operations);

// Reads audio-device-list from a temporary, unconfigured core (no file, VO or
// scripts) on a serialized process-lifetime worker, because driver enumeration
// can block and cannot be cancelled. Callers share an in-flight request.
// Call from the UI thread.
[[nodiscard]] QFuture<QVariantList> enumerateAudioDevicesAsync();

// Asynchronous property snapshots (mpv_get_property_async) so diagnostics
// never block the UI thread on the mpv core lock.
class LibmpvPropertySnapshots
{
public:
    using Callback = std::function<void(const QString &stage, const QVariantMap &properties)>;

    void request(mpv_handle *handle, const QString &stage, const QStringList &properties);
    // Returns true when `event` was a snapshot reply (and consumes it).
    bool handleEvent(const mpv_event *event, const Callback &callback);
    void reset();

private:
    struct Snapshot
    {
        QDeadlineTimer deadline{2000};
        QString stage;
        QStringList names;
        QVariantMap values;
        int outstanding = 0;
    };

    quint64 m_nextSnapshot = 1;
    QHash<quint64, Snapshot> m_snapshots;
};
#endif

} // namespace BloomBackend
