#include "LoggingConfig.h"

#include <QLoggingCategory>

namespace LoggingConfig {

namespace {

QString joinRules(const QStringList &parts)
{
    QStringList nonEmpty;
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            nonEmpty.append(trimmed);
        }
    }
    return nonEmpty.join(QLatin1Char('\n'));
}

} // namespace

Level levelFromString(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("debug") || normalized == QStringLiteral("verbose")) {
        return Level::Debug;
    }
    if (normalized == QStringLiteral("quiet") || normalized == QStringLiteral("warn")
        || normalized == QStringLiteral("warning")) {
        return Level::Quiet;
    }
    return Level::Info;
}

QString defaultQtRules(Level level)
{
    if (level == Level::Debug) {
        return QStringLiteral("*.debug=true\n*.info=true");
    }

    if (level == Level::Quiet) {
        return QStringLiteral("*.debug=false\n*.info=false");
    }

    // Info: warnings/errors everywhere; suppress routine image/cache/view-model noise.
    return QStringLiteral(
        "*.debug=false\n"
        "*.info=true\n"
        "*.warning=true\n"
        "*.critical=true\n"
        "bloom.imagecache.debug=false\n"
        "bloom.imagecache.info=false\n"
        "bloom.librarycache.debug=false\n"
        "bloom.librarycache.info=false\n"
        "bloom.cache.debug=false\n"
        "bloom.cache.info=false\n"
        "bloom.viewmodels.debug=false\n"
        "bloom.viewmodels.info=false\n"
        "bloom.library.debug=false\n"
        "bloom.library.info=false\n"
        "bloom.auth.debug=false\n"
        "bloom.config.debug=false\n"
        "bloom.ui.debug=false\n"
        "bloom.playback.debug=false\n"
        "bloom.playback.ipc.debug=false\n"
        "bloom.playback.trickplay.debug=false\n"
        "bloom.playback.trace.debug=false\n"
        "bloom.playback.trace.info=false\n"
        "bloom.playback.displaytrace.debug=false\n"
        "bloom.playback.backend.*.debug=false\n"
        "bloom.ui.scenegraph.debug=false\n"
        "bloom.gpu.trim.debug=false\n"
        "bloom.mediaSegments.debug=false\n"
        "jellyfin.network.debug=false\n"
        "bloom.test.debug=false");
}

void apply(Level level, const QString &extraQtRules, bool forceVerbose)
{
    const Level effectiveLevel = forceVerbose ? Level::Debug : level;

    Logger::LogLevel loggerLevel = Logger::LogLevel::Info;
    switch (effectiveLevel) {
    case Level::Quiet:
        loggerLevel = Logger::LogLevel::Warning;
        break;
    case Level::Debug:
        loggerLevel = Logger::LogLevel::Debug;
        break;
    case Level::Info:
    default:
        loggerLevel = Logger::LogLevel::Info;
        break;
    }

    Logger::instance().setMinLogLevel(loggerLevel);

    const QString rules = joinRules({defaultQtRules(effectiveLevel), extraQtRules});
    if (!rules.isEmpty()) {
        QLoggingCategory::setFilterRules(rules);
    }
}

} // namespace LoggingConfig
