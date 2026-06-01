#include <gtest/gtest.h>
#include "utils/LoggingConfig.h"

TEST(LoggingConfigTest, levelFromStringRecognizesAliases)
{
    EXPECT_EQ(LoggingConfig::levelFromString(QStringLiteral("info")), LoggingConfig::Level::Info);
    EXPECT_EQ(LoggingConfig::levelFromString(QStringLiteral("INFO")), LoggingConfig::Level::Info);
    EXPECT_EQ(LoggingConfig::levelFromString(QStringLiteral("debug")), LoggingConfig::Level::Debug);
    EXPECT_EQ(LoggingConfig::levelFromString(QStringLiteral("verbose")), LoggingConfig::Level::Debug);
    EXPECT_EQ(LoggingConfig::levelFromString(QStringLiteral("quiet")), LoggingConfig::Level::Quiet);
    EXPECT_EQ(LoggingConfig::levelFromString(QStringLiteral("unknown")), LoggingConfig::Level::Info);
}

TEST(LoggingConfigTest, defaultQtRulesSilencesImageCacheInInfoMode)
{
    const QString rules = LoggingConfig::defaultQtRules(LoggingConfig::Level::Info);
    EXPECT_TRUE(rules.contains(QStringLiteral("bloom.imagecache.debug=false")));
}
