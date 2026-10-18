#include <QCoreApplication>
#include <QEventLoop>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QSemaphore>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest/QtTest>

#include <atomic>
#include <memory>

#include "audio/AudioOutputService.h"
#include "utils/ConfigManager.h"

#include "TestConfigIsolation.h"

namespace {

struct ProbeReply {
    explicit ProbeReply(Bloom::AudioOutputCapabilities value, bool shouldBlock)
        : capabilities(std::move(value))
        , blocked(shouldBlock)
    {
    }

    QSemaphore entered;
    QSemaphore release;
    Bloom::AudioOutputCapabilities capabilities;
    bool blocked;
};

class ProbeHarness
{
public:
    std::shared_ptr<ProbeReply> enqueue(const QString &device,
                                        Bloom::AudioOutputCapabilities capabilities,
                                        bool blocked = false)
    {
        auto reply = std::make_shared<ProbeReply>(std::move(capabilities), blocked);
        QMutexLocker lock(&m_mutex);
        m_replies[device].enqueue(reply);
        m_allReplies.append(reply);
        return reply;
    }

    Bloom::AudioOutputCapabilities probe(const QString &device)
    {
        auto current = m_active.fetch_add(1) + 1;
        auto maximum = m_maximumActive.load();
        while (current > maximum && !m_maximumActive.compare_exchange_weak(maximum, current)) {
        }

        std::shared_ptr<ProbeReply> reply;
        {
            QMutexLocker lock(&m_mutex);
            m_calls.append(device);
            auto it = m_replies.find(device);
            if (it != m_replies.end() && !it->isEmpty())
                reply = it->dequeue();
        }

        if (reply) {
            reply->entered.release();
            if (reply->blocked)
                reply->release.acquire();
        }

        m_active.fetch_sub(1);
        if (reply)
            return reply->capabilities;

        Bloom::AudioOutputCapabilities fallback;
        fallback.device = device;
        return fallback;
    }

    int callCount() const
    {
        QMutexLocker lock(&m_mutex);
        return m_calls.size();
    }

    int maximumConcurrent() const { return m_maximumActive.load(); }

    void releaseAll()
    {
        QMutexLocker lock(&m_mutex);
        for (const auto &reply : m_allReplies)
            reply->release.release();
    }

private:
    mutable QMutex m_mutex;
    QHash<QString, QQueue<std::shared_ptr<ProbeReply>>> m_replies;
    QList<QString> m_calls;
    QList<std::shared_ptr<ProbeReply>> m_allReplies;
    std::atomic_int m_active = 0;
    std::atomic_int m_maximumActive = 0;
};

class ProbeReleaseGuard
{
public:
    explicit ProbeReleaseGuard(std::shared_ptr<ProbeHarness> harness)
        : m_harness(std::move(harness))
    {
    }

    ~ProbeReleaseGuard() { m_harness->releaseAll(); }

private:
    std::shared_ptr<ProbeHarness> m_harness;
};

class ConfigFixture
{
private:
    QTemporaryDir m_directory;
    std::unique_ptr<ScopedConfigIsolation> m_isolation;

public:
    ConfigFixture()
        : m_isolation(std::make_unique<ScopedConfigIsolation>(m_directory.path()))
        , config(std::make_unique<ConfigManager>())
    {
        config->load();
    }

    bool valid() const { return m_directory.isValid(); }

    std::unique_ptr<ConfigManager> config;
};

Bloom::AudioOutputCapabilities capabilitiesFor(const QString &device,
                                               const QString &endpointId,
                                               bool available = true)
{
    Bloom::AudioOutputCapabilities capabilities;
    capabilities.device = device;
    capabilities.endpointId = endpointId;
    capabilities.endpointAvailable = available;
    capabilities.evidence = QStringLiteral("wasapi-exclusive-format-query");
    capabilities.passthroughCodecs = {QStringLiteral("ac3")};
    return capabilities;
}

bool processInitialRefresh(const std::shared_ptr<ProbeReply> &reply)
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
    return reply->entered.tryAcquire(1, 1000);
}

} // namespace

class AudioOutputServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void prepareWaitsForProbe();
    void deadlinePublishesUnknownAndDropsLateResult();
    void stalledProbeDoesNotBlockGlobalPoolShutdown();
    void staleProbeCannotOverwriteNewDevice();
    void onlyOneProbeRunsAtATime();
    void explicitMpvDeviceDisappearanceAndReconnect();
    void defaultEndpointChangeEmitsCapabilitiesChanged();
    void refreshCanRecoverEndpointAfterResume();
    void failedDeviceFallsBackUntilHotplug();
    void heldEndpointKeepsFormatsWhenQueriesFail();
};

void AudioOutputServiceTest::prepareWaitsForProbe()
{
    ConfigFixture fixture;
    QVERIFY(fixture.valid());
    fixture.config->setAudioOutputDevice(QStringLiteral("wasapi/prepare-test"));

    auto harness = std::make_shared<ProbeHarness>();
    auto reply = harness->enqueue(QStringLiteral("wasapi/prepare-test"),
                                  capabilitiesFor(QStringLiteral("wasapi/prepare-test"),
                                                  QStringLiteral("endpoint-prepare")),
                                  true);
    Bloom::AudioOutputService service(fixture.config.get(),
                                      [harness](const QString &device) { return harness->probe(device); },
                                      1000);

    int callbackCount = 0;
    ProbeReleaseGuard releaseGuard(harness);
    service.prepare([&callbackCount] { ++callbackCount; });
    QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
    QTRY_VERIFY_WITH_TIMEOUT(reply->entered.available() > 0, 1000);
    QVERIFY(reply->entered.tryAcquire(1, 0));
    QCOMPARE(callbackCount, 0);
    QCOMPARE(service.capabilities().evidence, QStringLiteral("unknown"));

    reply->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(callbackCount, 1, 1000);
    QCOMPARE(service.capabilities().endpointId, QStringLiteral("endpoint-prepare"));
    QCOMPARE(service.capabilities().device, QStringLiteral("wasapi/prepare-test"));
}

void AudioOutputServiceTest::deadlinePublishesUnknownAndDropsLateResult()
{
    ConfigFixture fixture;
    QVERIFY(fixture.valid());
    fixture.config->setAudioOutputDevice(QStringLiteral("wasapi/deadline-test"));

    auto harness = std::make_shared<ProbeHarness>();
    auto reply = harness->enqueue(QStringLiteral("wasapi/deadline-test"),
                                  capabilitiesFor(QStringLiteral("wasapi/deadline-test"),
                                                  QStringLiteral("late-endpoint")),
                                  true);
    Bloom::AudioOutputService service(fixture.config.get(),
                                      [harness](const QString &device) { return harness->probe(device); },
                                      30);

    int callbackCount = 0;
    ProbeReleaseGuard releaseGuard(harness);
    service.prepare([&callbackCount] { ++callbackCount; });
    QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
    QTRY_VERIFY_WITH_TIMEOUT(reply->entered.available() > 0, 1000);
    QVERIFY(reply->entered.tryAcquire(1, 0));
    QTRY_COMPARE_WITH_TIMEOUT(callbackCount, 1, 1000);

    const auto timedOutCapabilities = service.capabilities();
    QCOMPARE(timedOutCapabilities.device, QStringLiteral("wasapi/deadline-test"));
    QCOMPARE(timedOutCapabilities.evidence, QStringLiteral("unknown"));
    QCOMPARE(timedOutCapabilities.endpointAvailable, true);

    reply->release.release();
    QTest::qWait(30);
    QCOMPARE(callbackCount, 1);
    QCOMPARE(service.capabilities().evidence, QStringLiteral("unknown"));
    QVERIFY(service.capabilities().endpointId.isEmpty());
}

void AudioOutputServiceTest::stalledProbeDoesNotBlockGlobalPoolShutdown()
{
    ConfigFixture fixture;
    QVERIFY(fixture.valid());
    auto harness = std::make_shared<ProbeHarness>();
    auto reply = harness->enqueue(QStringLiteral("auto"),
        capabilitiesFor(QStringLiteral("auto"), QStringLiteral("stalled-endpoint")), true);
    ProbeReleaseGuard releaseGuard(harness);
    auto service = std::make_unique<Bloom::AudioOutputService>(fixture.config.get(),
        [harness](const QString &device) { return harness->probe(device); }, 50);
    int callbackCount = 0;
    service->prepare([&callbackCount] { ++callbackCount; });
    QTRY_VERIFY_WITH_TIMEOUT(reply->entered.available() > 0, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(callbackCount, 1, 1000);
    QCOMPARE(service->capabilities().evidence, QStringLiteral("unknown"));
    service.reset();
    // Qt drains its global pool at shutdown, while the simulated driver is still blocked.
    QVERIFY(QThreadPool::globalInstance()->waitForDone(100));
    reply->release.release();
}

void AudioOutputServiceTest::staleProbeCannotOverwriteNewDevice()
{
    ConfigFixture fixture;
    QVERIFY(fixture.valid());
    const QString oldDevice = QStringLiteral("wasapi/old-device");
    const QString newDevice = QStringLiteral("wasapi/new-device");
    fixture.config->setAudioOutputDevice(oldDevice);

    auto harness = std::make_shared<ProbeHarness>();
    auto oldReply = harness->enqueue(oldDevice,
                                     capabilitiesFor(oldDevice, QStringLiteral("old-endpoint")),
                                     true);
    auto newReply = harness->enqueue(newDevice,
                                     capabilitiesFor(newDevice, QStringLiteral("new-endpoint"), false),
                                     true);
    Bloom::AudioOutputService service(fixture.config.get(),
                                      [harness](const QString &device) { return harness->probe(device); },
                                      1000);

    int callbackCount = 0;
    QString callbackDevice;
    QString callbackEvidence;
    ProbeReleaseGuard releaseGuard(harness);
    service.prepare([&] {
        ++callbackCount;
        const auto current = service.capabilities();
        callbackDevice = current.device;
        callbackEvidence = current.evidence;
    });
    QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
    QTRY_VERIFY_WITH_TIMEOUT(oldReply->entered.available() > 0, 1000);
    QVERIFY(oldReply->entered.tryAcquire(1, 0));

    fixture.config->setAudioOutputDevice(newDevice);
    QCOMPARE(service.capabilities().device, newDevice);
    oldReply->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(newReply->entered.available() > 0, 1000);
    QVERIFY(newReply->entered.tryAcquire(1, 0));
    QCOMPARE(callbackCount, 0);
    QCOMPARE(service.capabilities().device, newDevice);
    QCOMPARE(service.capabilities().evidence, QStringLiteral("unknown"));

    newReply->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(callbackCount, 1, 1000);
    QCOMPARE(callbackDevice, newDevice);
    QCOMPARE(callbackEvidence, QStringLiteral("wasapi-exclusive-format-query"));
    QCOMPARE(service.capabilities().endpointId, QStringLiteral("new-endpoint"));
    QCOMPARE(service.capabilities().endpointAvailable, false);
}

void AudioOutputServiceTest::onlyOneProbeRunsAtATime()
{
    ConfigFixture fixture;
    QVERIFY(fixture.valid());
    const QString device = QStringLiteral("wasapi/serial-test");
    fixture.config->setAudioOutputDevice(device);

    auto harness = std::make_shared<ProbeHarness>();
    auto reply = harness->enqueue(device, capabilitiesFor(device, QStringLiteral("serial-endpoint")), true);
    Bloom::AudioOutputService service(fixture.config.get(),
                                      [harness](const QString &requested) { return harness->probe(requested); },
                                      1000);
    ProbeReleaseGuard releaseGuard(harness);

    QVERIFY(processInitialRefresh(reply));
    service.refresh();
    service.refresh();
    service.prepare([] {});
    QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
    QCOMPARE(harness->callCount(), 1);
    QCOMPARE(harness->maximumConcurrent(), 1);

    reply->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(service.capabilities().endpointId, QStringLiteral("serial-endpoint"), 1000);
    QCOMPARE(harness->maximumConcurrent(), 1);
}

void AudioOutputServiceTest::explicitMpvDeviceDisappearanceAndReconnect()
{
    ConfigFixture fixture;
    QVERIFY(fixture.valid());
    const QString device = QStringLiteral("wasapi/mpv-device");
    fixture.config->setAudioOutputDevice(device);

    auto harness = std::make_shared<ProbeHarness>();
    auto reply = harness->enqueue(device, capabilitiesFor(device, QStringLiteral("mpv-endpoint")), true);
    Bloom::AudioOutputService service(fixture.config.get(),
                                      [harness](const QString &requested) { return harness->probe(requested); },
                                      1000);
    ProbeReleaseGuard releaseGuard(harness);

    QVERIFY(processInitialRefresh(reply));
    reply->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(service.capabilities().endpointId, QStringLiteral("mpv-endpoint"), 1000);
    QVERIFY(service.capabilities().endpointAvailable);

    Bloom::AudioSourceMetadata source;
    source.codec = QStringLiteral("ac3");
    source.channels = 6;
    source.channelLayout = QStringLiteral("5.1");

    QSignalSpy changedSpy(&service, &Bloom::AudioOutputService::capabilitiesChanged);
    service.setMpvDevices({});
    // The saved choice is kept, but playback resolves to the system default.
    QCOMPARE(fixture.config->getAudioOutputDevice(), device);
    QVERIFY(!service.deviceResolution().preferredAvailable);
    QCOMPARE(service.capabilities().device, QStringLiteral("auto"));
    const auto missingPlan = service.planForMetadata(source);
    QCOMPARE(missingPlan.device, QStringLiteral("auto"));
    QVERIFY(missingPlan.localDecodeSupported);
    QVERIFY(!missingPlan.providerConversionRequired);
    QCOMPARE(missingPlan.path, QStringLiteral("pcm"));
    QVERIFY(changedSpy.count() >= 1);

    service.setMpvDevices(QVariantList{QVariantMap{{QStringLiteral("name"), device}}});
    QVERIFY(service.deviceResolution().preferredAvailable);
    QCOMPARE(service.capabilities().device, device);
    QVERIFY(service.capabilities().endpointAvailable);
    const auto reconnectedPlan = service.planForMetadata(source);
    QCOMPARE(reconnectedPlan.device, device);
    QVERIFY(reconnectedPlan.localDecodeSupported);
    QVERIFY(!reconnectedPlan.providerConversionRequired);
}

void AudioOutputServiceTest::failedDeviceFallsBackUntilHotplug()
{
    ConfigFixture fixture;
    QVERIFY(fixture.valid());
    const QString device = QStringLiteral("wasapi/tv");
    fixture.config->setAudioOutputDeviceSelection(device, QStringLiteral("TV"));
    Bloom::AudioOutputService service(fixture.config.get(),
                                      [](const QString &requested) {
                                          Bloom::AudioOutputCapabilities result;
                                          result.device = requested;
                                          return result;
                                      },
                                      1000);
    const QVariantList list{QVariantMap{{QStringLiteral("name"), QStringLiteral("auto")}},
                            QVariantMap{{QStringLiteral("name"), device},
                                        {QStringLiteral("description"), QStringLiteral("TV")}}};
    service.setMpvDevices(list);
    QCOMPARE(service.deviceResolution().device, device);
    // WASAPI endpoints report exclusive support; the plan follows the preference.
    QVERIFY(service.capabilities().exclusiveModeSupported);
    Bloom::AudioSourceMetadata stereo;
    stereo.codec = QStringLiteral("aac");
    stereo.channels = 2;
    QVERIFY(service.planForMetadata(stereo).exclusivePcm);
    QVERIFY(!service.planForMetadata(stereo, false).exclusivePcm);
    fixture.config->setAudioExclusiveMode(false);
    QVERIFY(!service.planForMetadata(stereo).exclusivePcm);

    QSignalSpy devicesSpy(&service, &Bloom::AudioOutputService::devicesChanged);
    service.markDeviceFailed(device);
    QCOMPARE(service.deviceResolution().device, QStringLiteral("auto"));
    QCOMPARE(service.planForMetadata(stereo).device, QStringLiteral("auto"));
    QCOMPARE(fixture.config->getAudioOutputDevice(), device);
    QVERIFY(devicesSpy.count() >= 1);

    // An unchanged list keeps the failure; hotplug clears it.
    service.setMpvDevices(list);
    QCOMPARE(service.deviceResolution().device, QStringLiteral("auto"));
    service.setMpvDevices(QVariantList{list.at(1), list.at(0)});
    QCOMPARE(service.deviceResolution().device, device);

    service.markDeviceFailed(device);
    service.clearDeviceFailures();
    QCOMPARE(service.deviceResolution().device, device);
}

void AudioOutputServiceTest::heldEndpointKeepsFormatsWhenQueriesFail()
{
    ConfigFixture fixture;
    QVERIFY(fixture.valid());
    auto harness = std::make_shared<ProbeHarness>();
    auto first = harness->enqueue(QStringLiteral("auto"),
                                  capabilitiesFor(QStringLiteral("auto"), QStringLiteral("endpoint")), true);
    Bloom::AudioOutputCapabilities rejected = capabilitiesFor(QStringLiteral("auto"), QStringLiteral("endpoint"));
    rejected.passthroughCodecs.clear();
    auto second = harness->enqueue(QStringLiteral("auto"), rejected, true);
    auto third = harness->enqueue(QStringLiteral("auto"), rejected, true);
    Bloom::AudioOutputService service(fixture.config.get(),
                                      [harness](const QString &device) { return harness->probe(device); },
                                      1000);
    ProbeReleaseGuard releaseGuard(harness);

    QVERIFY(processInitialRefresh(first));
    first->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(service.capabilities().passthroughCodecs, QStringList{QStringLiteral("ac3")}, 1000);

    service.setEndpointHeld(true);
    service.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(second->entered.available() > 0, 1000);
    QVERIFY(second->entered.tryAcquire(1, 0));
    second->release.release();
    QTest::qWait(100); // let the watcher deliver the held-endpoint answer
    QCOMPARE(service.capabilities().passthroughCodecs, QStringList{QStringLiteral("ac3")});

    // Once released, the same answer is taken as real.
    service.setEndpointHeld(false);
    QTRY_VERIFY_WITH_TIMEOUT((service.refresh(), third->entered.available() > 0), 1000);
    QVERIFY(third->entered.tryAcquire(1, 0));
    third->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(service.capabilities().passthroughCodecs.isEmpty(), 1000);
}

void AudioOutputServiceTest::defaultEndpointChangeEmitsCapabilitiesChanged()
{
    ConfigFixture fixture;
    QVERIFY(fixture.valid());

    auto harness = std::make_shared<ProbeHarness>();
    auto firstReply = harness->enqueue(QStringLiteral("auto"),
                                       capabilitiesFor(QStringLiteral("auto"), QStringLiteral("endpoint-a")),
                                       true);
    auto secondReply = harness->enqueue(QStringLiteral("auto"),
                                        capabilitiesFor(QStringLiteral("auto"), QStringLiteral("endpoint-b")),
                                        true);
    Bloom::AudioOutputService service(fixture.config.get(),
                                      [harness](const QString &device) { return harness->probe(device); },
                                      1000);
    QSignalSpy changedSpy(&service, &Bloom::AudioOutputService::capabilitiesChanged);
    ProbeReleaseGuard releaseGuard(harness);

    QVERIFY(processInitialRefresh(firstReply));
    firstReply->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(service.capabilities().endpointId, QStringLiteral("endpoint-a"), 1000);

    service.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(secondReply->entered.available() > 0, 1000);
    QVERIFY(secondReply->entered.tryAcquire(1, 0));
    secondReply->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(service.capabilities().endpointId, QStringLiteral("endpoint-b"), 1000);
    QVERIFY(changedSpy.count() >= 2);
}

void AudioOutputServiceTest::refreshCanRecoverEndpointAfterResume()
{
    ConfigFixture fixture;
    QVERIFY(fixture.valid());

    auto harness = std::make_shared<ProbeHarness>();
    auto unavailable = harness->enqueue(QStringLiteral("auto"),
                                        capabilitiesFor(QStringLiteral("auto"),
                                                        QStringLiteral(""), false),
                                        true);
    auto recovered = harness->enqueue(QStringLiteral("auto"),
                                      capabilitiesFor(QStringLiteral("auto"),
                                                      QStringLiteral("endpoint-after-resume")),
                                      true);
    Bloom::AudioOutputService service(fixture.config.get(),
                                      [harness](const QString &device) { return harness->probe(device); },
                                      1000);
    ProbeReleaseGuard releaseGuard(harness);

    QVERIFY(processInitialRefresh(unavailable));
    unavailable->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(!service.capabilities().endpointAvailable, 1000);

    service.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(recovered->entered.available() > 0, 1000);
    QVERIFY(recovered->entered.tryAcquire(1, 0));
    QVERIFY(!service.capabilities().endpointAvailable);
    recovered->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(service.capabilities().endpointAvailable, 1000);
    QCOMPARE(service.capabilities().endpointId, QStringLiteral("endpoint-after-resume"));
}

QTEST_GUILESS_MAIN(AudioOutputServiceTest)
#include "AudioOutputServiceTest.moc"
