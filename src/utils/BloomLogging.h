#pragma once

#include <QLoggingCategory>

// Image & disk cache (suppress info/debug by default; warnings/errors always show)
Q_DECLARE_LOGGING_CATEGORY(lcImageCache)       // bloom.imagecache
Q_DECLARE_LOGGING_CATEGORY(lcLibraryCache)     // bloom.librarycache
Q_DECLARE_LOGGING_CATEGORY(lcCache)            // bloom.cache — detail-view JSON cache

// Library / API
Q_DECLARE_LOGGING_CATEGORY(lcLibrary)          // bloom.library
Q_DECLARE_LOGGING_CATEGORY(lcViewModels)       // bloom.viewmodels
Q_DECLARE_LOGGING_CATEGORY(lcJellyfinNetwork)  // jellyfin.network

// Playback
Q_DECLARE_LOGGING_CATEGORY(lcPlayback)         // bloom.playback
Q_DECLARE_LOGGING_CATEGORY(lcPlaybackTrace)    // bloom.playback.trace
Q_DECLARE_LOGGING_CATEGORY(lcPlaybackIpc)      // bloom.playback.ipc
Q_DECLARE_LOGGING_CATEGORY(lcPlaybackTrickplay) // bloom.playback.trickplay
Q_DECLARE_LOGGING_CATEGORY(lcMediaSegments)    // bloom.mediaSegments
Q_DECLARE_LOGGING_CATEGORY(lcLinuxLibmpvBackend)   // bloom.playback.backend.linux.libmpv
Q_DECLARE_LOGGING_CATEGORY(lcWindowsLibmpvBackend) // bloom.playback.backend.windows.libmpv
Q_DECLARE_LOGGING_CATEGORY(lcPlayerBackendFactory) // bloom.playback.backend.factory
Q_DECLARE_LOGGING_CATEGORY(lcDisplayTrace)     // bloom.playback.displaytrace
Q_DECLARE_LOGGING_CATEGORY(lcGpuTrim)          // bloom.gpu.trim

// App / UI / auth
Q_DECLARE_LOGGING_CATEGORY(lcAuth)             // bloom.auth
Q_DECLARE_LOGGING_CATEGORY(lcConfig)           // bloom.config
Q_DECLARE_LOGGING_CATEGORY(lcApp)              // bloom.app
Q_DECLARE_LOGGING_CATEGORY(lcUi)               // bloom.ui
Q_DECLARE_LOGGING_CATEGORY(lcUiSceneGraph)     // bloom.ui.scenegraph
Q_DECLARE_LOGGING_CATEGORY(lcTest)             // bloom.test
