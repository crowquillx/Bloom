#pragma once

#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QGuiApplication>
#include <memory>

class ApplicationInitializer;
class ConfigManager;
class IArtworkProvider;
class ImageCacheProvider;
class GpuMemoryTrimmer;

class WindowManager : public QObject
{
    Q_OBJECT

public:
    explicit WindowManager(QGuiApplication* app, QObject *parent = nullptr);
    ~WindowManager();

    void setup(ConfigManager* configManager);
    void load();
    void exposeContextProperties(ApplicationInitializer& appInit);
    QQmlApplicationEngine& engine() { return m_engine; }

private:
    QGuiApplication* m_app;
    std::unique_ptr<IArtworkProvider> m_artworkProvider;
    QQmlApplicationEngine m_engine;
    ImageCacheProvider* m_imageCacheProvider = nullptr;
    GpuMemoryTrimmer* m_gpuMemoryTrimmer = nullptr;
};
