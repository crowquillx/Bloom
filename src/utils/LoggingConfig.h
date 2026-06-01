#pragma once

#include "Logger.h"
#include <QString>

/**
 * @brief Applies Bloom log level and Qt logging category filter rules.
 *
 * Default ("info") keeps warnings/errors visible while suppressing noisy
 * cache, image, and trace categories. Use level "debug" or --verbose for
 * full diagnostics. Optional rules in app.json append to the defaults.
 */
namespace LoggingConfig {

enum class Level {
    Quiet,  ///< Warnings and errors only
    Info,   ///< Default: info+ without noisy debug categories
    Debug   ///< Full verbosity (all categories)
};

Level levelFromString(const QString &value);

/** Qt filter rules applied for the given level (before user overrides). */
QString defaultQtRules(Level level);

/**
 * @brief Configure Logger minimum level and QLoggingCategory filters.
 *
 * @param level          Resolved log level (info unless overridden).
 * @param extraQtRules   Optional rules from settings.logging.qt_rules (appended).
 * @param forceVerbose   When true (--verbose), enables full debug output.
 */
void apply(Level level, const QString &extraQtRules = QString(), bool forceVerbose = false);

} // namespace LoggingConfig
