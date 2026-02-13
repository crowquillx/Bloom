#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

class IPlayerBackend : public QObject
{
    Q_OBJECT

public:
    explicit IPlayerBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~IPlayerBackend() override = default;

    virtual void startMpv(const QString &mpvBin, const QStringList &args, const QString &mediaUrl) = 0;
    virtual void stopMpv() = 0;
    virtual bool isRunning() const = 0;

    virtual void sendCommand(const QStringList &command) = 0;
    virtual void sendVariantCommand(const QVariantList &command) = 0;

signals:
    void stateChanged(bool running);
    void errorOccurred(const QString &error);
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void pauseChanged(bool paused);
    void pausedForCacheChanged(bool paused);
    void playbackEnded();
    void audioTrackChanged(int trackIndex);
    void subtitleTrackChanged(int trackIndex);
    void scriptMessage(const QString &messageName, const QStringList &args);
};
