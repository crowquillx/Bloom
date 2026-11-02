#pragma once

#include <QMetaType>
#include <QString>
#include <QVariantMap>

namespace Bloom {

/**
 * @brief A server-side timing correction of an external subtitle.
 *
 * Original cue time t plays at `t * scale + offsetMs`. The identity
 * (0 ms, scale 1) is the subtitle's own timing.
 */
struct SubtitleTiming {
    qint64 offsetMs = 0;
    double scale = 1.0;

    bool isIdentity() const;
    bool operator==(const SubtitleTiming &other) const;
    bool operator!=(const SubtitleTiming &other) const { return !(*this == other); }
    QVariantMap toVariantMap() const;
};

enum class SubtitleSyncStatus {
    None,          // no job has run
    Pending,       // queued on the server
    Running,
    Synced,        // a correction was stored
    AlreadySynced, // the subtitle already matched the audio
    NoMatch,       // the subtitle most likely belongs to another release
    Failed
};

/// Canonical name: "none", "pending", "running", "synced", "alreadySynced",
/// "noMatch" or "failed".
QString subtitleSyncStatusName(SubtitleSyncStatus status);

/**
 * @brief The latest job that aligned a subtitle to the audio.
 *
 * `phase` is "queued", "analyzing" or "matching" while the job is active.
 * `failure` is "subtitleChanged", "noAudio", "unavailable" or "error" for a
 * failed job.
 */
struct SubtitleSyncJob {
    QString id;
    SubtitleSyncStatus status = SubtitleSyncStatus::None;
    QString phase;
    double progress = 0.0; // 0..1
    QString failure;

    bool isActive() const;
    QVariantMap toVariantMap() const;
};

/**
 * @brief One external or downloaded subtitle whose timing the server can
 * correct, with its current timing and latest sync job.
 *
 * `key` is the provider's sync key from the track inventory (`syncKey` on
 * the canonical subtitle track); `mediaFileId` is the file it belongs to.
 */
struct SubtitleSyncState {
    QString key;
    QString mediaFileId;
    SubtitleTiming timing;
    SubtitleSyncJob job;

    bool isValid() const { return !key.isEmpty(); }
    bool isActive() const { return job.isActive(); }
    QVariantMap toVariantMap() const;
};

} // namespace Bloom

Q_DECLARE_METATYPE(Bloom::SubtitleSyncState)
