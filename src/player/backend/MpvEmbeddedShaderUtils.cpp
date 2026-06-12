#include "MpvEmbeddedShaderUtils.h"

#if defined(BLOOM_HAS_LIBMPV)
extern "C" {
#include <mpv/client.h>
}
#endif

namespace MpvEmbeddedShaderUtils {

#if defined(BLOOM_HAS_LIBMPV)
bool applyShaderList(mpv_handle *handle, const QStringList &resolvedShaderPaths)
{
    if (handle == nullptr || resolvedShaderPaths.isEmpty()) {
        return true;
    }

    const auto runChangeList = [handle](const char *action, const QByteArray &valueUtf8) {
        const char *command[] = {"change-list", "glsl-shaders", action, valueUtf8.constData(), nullptr};
        return mpv_command(handle, command) >= 0;
    };

    if (!runChangeList("clr", QByteArray())) {
        return false;
    }

    for (const QString &shaderPath : resolvedShaderPaths) {
        if (!runChangeList("append", shaderPath.toUtf8())) {
            return false;
        }
    }

    return true;
}
#endif

} // namespace MpvEmbeddedShaderUtils
