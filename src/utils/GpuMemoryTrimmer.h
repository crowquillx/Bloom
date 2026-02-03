// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <optional>

class ConfigManager;
class ImageCacheProvider;

Q_DECLARE_LOGGING_CATEGORY(lcGpuTrim)

/**
 * @brief Trims GPU/scenegraph usage while mpv handles playback.
 *
 * This service releases QML GPU resources when playback is active to
 * prioritize VRAM for mpv. It is backend-aware (Vulkan/GL/D3D11) and keeps
 * restoration paths for when playback ends.
 */
class GpuMemoryTrimmer : public QObject
{
    Q_OBJECT
public:
    explicit GpuMemoryTrimmer(ConfigManager *config,
                              ImageCacheProvider *imageCache,
                              QObject *parent = nullptr);

    void setWindow(QQuickWindow *window);
    void setPerformanceModeEnabled(bool enabled) { m_performanceMode = enabled; }
    bool isTrimmed() const noexcept { return m_trimmed; }

public slots:
    void onPlaybackActiveChanged(bool active);
    void trimForPlayback();
    void restoreAfterPlayback();

private:
    void trimWindowResources();
    void trimRhiResources();
    void dropUiCaches();
    void restoreWindowState();
    QString backendName(QSGRendererInterface::GraphicsApi api) const;

    ConfigManager *m_config;
    ImageCacheProvider *m_imageCache;
    QPointer<QQuickWindow> m_window;
    bool m_trimmed = false;
    bool m_performanceMode = false;
    std::optional<bool> m_originalPersistentGraphics;
    std::optional<bool> m_originalPersistentSceneGraph;
};
