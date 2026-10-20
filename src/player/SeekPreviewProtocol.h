#pragma once

#include <QtGlobal>

namespace Bloom::SeekPreviewProtocol {
inline constexpr int intervalMs = 10000;
inline constexpr qint64 maxDurationMs = 24LL * 60 * 60 * 1000;
inline constexpr qint64 maxImageBytes = 1024 * 1024;
inline constexpr qint64 maxTotalImageBytes = 128 * 1024 * 1024;
inline constexpr int maxRequestBytes = 65536;

enum ExitCode {
    Success = 0,
    InvalidRequest,
    Initialization,
    Source,
    Frame,
    FrameTimeout,
    Image,
    CacheLimit,
    ParentClosed
};
}
