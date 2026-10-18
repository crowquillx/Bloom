#include "BloomLogging.h"

#include <QRegularExpression>

Q_LOGGING_CATEGORY(lcImageCache, "bloom.imagecache")
Q_LOGGING_CATEGORY(lcLibraryCache, "bloom.librarycache")
Q_LOGGING_CATEGORY(lcCache, "bloom.cache")

Q_LOGGING_CATEGORY(lcLibrary, "bloom.library")
Q_LOGGING_CATEGORY(lcViewModels, "bloom.viewmodels")
Q_LOGGING_CATEGORY(lcJellyfinNetwork, "jellyfin.network")

Q_LOGGING_CATEGORY(lcPlayback, "bloom.playback")
Q_LOGGING_CATEGORY(lcPlaybackTrace, "bloom.playback.trace")
Q_LOGGING_CATEGORY(lcPlaybackIpc, "bloom.playback.ipc")
Q_LOGGING_CATEGORY(lcPlaybackTrickplay, "bloom.playback.trickplay")
Q_LOGGING_CATEGORY(lcMediaSegments, "bloom.mediaSegments")
Q_LOGGING_CATEGORY(lcLinuxLibmpvBackend, "bloom.playback.backend.linux.libmpv")
Q_LOGGING_CATEGORY(lcWindowsLibmpvBackend, "bloom.playback.backend.windows.libmpv")
Q_LOGGING_CATEGORY(lcPlayerBackendFactory, "bloom.playback.backend.factory")
Q_LOGGING_CATEGORY(lcDisplayTrace, "bloom.playback.displaytrace")
Q_LOGGING_CATEGORY(lcGpuTrim, "bloom.gpu.trim")

Q_LOGGING_CATEGORY(lcAuth, "bloom.auth")
Q_LOGGING_CATEGORY(lcConfig, "bloom.config")
Q_LOGGING_CATEGORY(lcApp, "bloom.app")
Q_LOGGING_CATEGORY(lcUi, "bloom.ui")
Q_LOGGING_CATEGORY(lcUiSceneGraph, "bloom.ui.scenegraph")
Q_LOGGING_CATEGORY(lcTest, "bloom.test")

QString redactNetworkLogText(QString message)
{
    static const QRegularExpression urls(
        QStringLiteral(R"(https?://[^\s"'<>]+)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression credentials(
        QStringLiteral(R"(([?&](?:token|st|api_key|apikey|sig|signature|profile_token)=)[^\s&"'<>]+)"),
        QRegularExpression::CaseInsensitiveOption);
    message.replace(urls, QStringLiteral("[URL]"));
    return message.replace(credentials, QStringLiteral("\\1[REDACTED]"));
}
