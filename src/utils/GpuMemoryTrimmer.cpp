// SPDX-License-Identifier: GPL-3.0-or-later
#include "GpuMemoryTrimmer.h"

#include <QLoggingCategory>
#include <QQuickWindow>
#include <QPixmapCache>

#include "ConfigManager.h"
#include "ui/ImageCacheProvider.h"

Q_LOGGING_CATEGORY(lcGpuTrim, "bloom.gpu.trim")

GpuMemoryTrimmer::GpuMemoryTrimmer(ConfigManager *config,
                                   ImageCacheProvider *imageCache,
                                   QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_imageCache(imageCache)
{
}

void GpuMemoryTrimmer::setWindow(QQuickWindow *window)
{
    if (window == m_window) {
        return;
    }

    m_window = window;
    if (m_window) {
        qCInfo(lcGpuTrim) << "Attached QQuickWindow for GPU trimming";
    }
}

void GpuMemoryTrimmer::onPlaybackActiveChanged(bool active)
{
    if (active) {
        trimForPlayback();
    } else {
        restoreAfterPlayback();
    }
}

void GpuMemoryTrimmer::trimForPlayback()
{
    if (m_trimmed) {
        return;
    }

    m_trimmed = true;
    trimWindowResources();
    dropUiCaches();
}

void GpuMemoryTrimmer::restoreAfterPlayback()
{
    if (!m_trimmed) {
        return;
    }

    restoreWindowState();
    m_trimmed = false;
}

void GpuMemoryTrimmer::trimWindowResources()
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    if (!m_window) {
        qCInfo(lcGpuTrim) << "No window bound; skipping GPU trim";
        return;
    }

    if (!m_originalPersistentGraphics.has_value()) {
        m_originalPersistentGraphics = m_window->isPersistentGraphics();
    }
    if (!m_originalPersistentSceneGraph.has_value()) {
        m_originalPersistentSceneGraph = m_window->isPersistentSceneGraph();
    }

    m_window->setPersistentGraphics(false);
    m_window->setPersistentSceneGraph(false);
    m_window->releaseResources();

    trimRhiResources();
    m_window->update();
#else
    Q_UNUSED(m_window);
#endif
}

void GpuMemoryTrimmer::trimRhiResources()
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    if (!m_window) {
        return;
    }

    QSGRendererInterface *ri = m_window->rendererInterface();
    if (ri) {
        qCInfo(lcGpuTrim) << "Trimming GPU resources; backend:" << backendName(ri->graphicsApi());
    } else {
        qCInfo(lcGpuTrim) << "RendererInterface unavailable; skipping backend-specific trim";
    }
#endif
}

void GpuMemoryTrimmer::dropUiCaches()
{
    QPixmapCache::clear();

    if (m_imageCache) {
        m_imageCache->clearMemoryCache();
        if (m_performanceMode) {
            // For performance mode we also evict disk-backed thumbnails to
            // prevent re-upload churn during playback.
            m_imageCache->clearCache();
        }
    }
}

void GpuMemoryTrimmer::restoreWindowState()
{
    if (!m_window) {
        return;
    }

    if (m_originalPersistentGraphics.has_value()) {
        m_window->setPersistentGraphics(*m_originalPersistentGraphics);
    }
    if (m_originalPersistentSceneGraph.has_value()) {
        m_window->setPersistentSceneGraph(*m_originalPersistentSceneGraph);
    }

    m_window->update();
}

QString GpuMemoryTrimmer::backendName(QSGRendererInterface::GraphicsApi api) const
{
    switch (api) {
    case QSGRendererInterface::Vulkan:
        return QStringLiteral("Vulkan");
    case QSGRendererInterface::Direct3D11:
        return QStringLiteral("D3D11");
    case QSGRendererInterface::OpenGL:
        return QStringLiteral("OpenGL");
    case QSGRendererInterface::Metal:
        return QStringLiteral("Metal");
    case QSGRendererInterface::Software:
        return QStringLiteral("Software");
    default:
        return QStringLiteral("Unknown");
    }
}
