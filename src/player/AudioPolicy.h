#pragma once

#include "models/AudioTypes.h"

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
}
