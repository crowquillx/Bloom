#pragma once

#include "models/AudioTypes.h"

#include <QVariantList>

namespace Bloom::AudioPolicy {
QStringList localDecodeCodecs();
QString transportCodec(const AudioSourceMetadata &source);
AudioOutputPlan plan(const AudioSourceMetadata &source,
                     const AudioOutputCapabilities &output,
                     const AudioPreferences &preferences);
QStringList mpvArguments(const AudioOutputPlan &plan);
QStringList diagnosticProperties();
QVariantMap verify(const AudioOutputPlan &plan, const QVariantMap &properties);
AudioSourceMetadata selectedSource(const QVariantMap &source, int trackIndex);

// mpv device ids are "<driver>/<endpoint>"; "auto" and bare driver defaults
// ("pipewire") have no endpoint part.
QString deviceDriver(const QString &device);
// Driver whose devices the user should choose from: the running AO when known,
// otherwise the first driver mpv's autoprobe order lists.
QString activeDriver(const QVariantList &devices, const QString &runtimeDriver);
bool driverSupportsExclusive(const QString &driver);
// Matches the saved preference against the live list. A missing preference is
// kept as preferred but resolves to "auto"; an identical, unique description
// under the same driver re-matches an endpoint whose id changed.
AudioDeviceResolution resolveDevice(const QString &preferred,
                                    const QString &preferredDescription,
                                    const QVariantList &devices,
                                    bool devicesKnown,
                                    const QStringList &unusableDevices = {});
// Selector entries: System Default, the active driver's endpoints, and a saved
// preference that is currently missing. Each entry has id, description, kind
// ("default"/"device"), available, selected and active.
QVariantList outputDeviceOptions(const QVariantList &devices,
                                 const QString &driver,
                                 const AudioDeviceResolution &resolution,
                                 const QString &preferredDescription);
// Parses `mpv --audio-device=help` output into {name, description} maps.
QVariantList parseDeviceHelp(const QString &output);
}
