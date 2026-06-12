#pragma once

#include <QStringList>

#if defined(BLOOM_HAS_LIBMPV)
struct mpv_handle;
#endif

namespace MpvEmbeddedShaderUtils {

#if defined(BLOOM_HAS_LIBMPV)
bool applyShaderList(mpv_handle *handle, const QStringList &resolvedShaderPaths);
#endif

} // namespace MpvEmbeddedShaderUtils
