#pragma once

#include "models/AudioTypes.h"

namespace Bloom {

AudioOutputCapabilities probeAudioEndpoint(const QString &device);

} // namespace Bloom
