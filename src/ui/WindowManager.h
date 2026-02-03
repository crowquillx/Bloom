#pragma once

#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QGuiApplication>

class ApplicationInitializer;
class ConfigManager;
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
    QQmlApplicationEngine m_engine;
    ImageCacheProvider* m_imageCacheProvider = nullptr;
    GpuMemoryTrimmer* m_gpuMemoryTrimmer = nullptr;
};
