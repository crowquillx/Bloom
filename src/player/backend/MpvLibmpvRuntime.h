#pragma once

#include <QHash>
#include <QDeadlineTimer>
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
