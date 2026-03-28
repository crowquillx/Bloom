#pragma once

#include <QString>

enum class UpdateApplySupport {
    Unsupported,
    NotifyOnly,
    Supported
};

struct UpdateAsset {
    QString url;
    QString filename;
    QString sha256;

    bool isValid() const
    {
        return !url.trimmed().isEmpty()
            && !filename.trimmed().isEmpty()
            && !sha256.trimmed().isEmpty();
    }
};

struct UpdateManifest {
    QString channel;
    QString version;
    QString buildId;
    QString releaseTag;
    QString publishedAt;
    QString notes;
    UpdateAsset installer;
    UpdateAsset portable;
    bool rolloutEnabled = true;
    QString minimumSupportedVersion;

    bool isValid() const
    {
        return !channel.trimmed().isEmpty()
            && !version.trimmed().isEmpty()
            && !publishedAt.trimmed().isEmpty()
            && (installer.isValid() || portable.isValid());
    }

    QString availabilityMarker() const
    {
        const QString effectiveBuildId = buildId.trimmed().isEmpty() ? publishedAt.trimmed() : buildId.trimmed();
        return channel.trimmed() + ":" + version.trimmed() + ":" + effectiveBuildId;
    }
};

struct InstallEligibility {
    UpdateApplySupport support = UpdateApplySupport::Unsupported;
    QString reason;
};
