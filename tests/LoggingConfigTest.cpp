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
    EXPECT_FALSE(rules.contains(QStringLiteral("bloom.imagecache.debug=true")));
}

TEST(LoggingConfigTest, defaultInfoRulesSilenceImageCacheInfo)
{
    const QString rules = LoggingConfig::defaultQtRules(LoggingConfig::Level::Info);
    EXPECT_TRUE(rules.contains(QStringLiteral("bloom.imagecache.info=false")));
    EXPECT_TRUE(rules.contains(QStringLiteral("bloom.viewmodels.info=false")));
    EXPECT_TRUE(rules.contains(QStringLiteral("*.info=true")));
    EXPECT_TRUE(rules.contains(QStringLiteral("*.warning=true")));
    EXPECT_TRUE(rules.contains(QStringLiteral("*.critical=true")));
}

TEST(LoggingConfigTest, defaultDebugRulesEnableVerboseCategories)
{
    const QString rules = LoggingConfig::defaultQtRules(LoggingConfig::Level::Debug);
    EXPECT_EQ(rules, QStringLiteral("*.debug=true\n*.info=true"));
    EXPECT_FALSE(rules.contains(QStringLiteral("bloom.imagecache.debug=false")));
    EXPECT_FALSE(rules.contains(QStringLiteral("=false")));
}

TEST(LoggingConfigTest, defaultQuietRulesDisableInfoAndDebug)
{
    const QString rules = LoggingConfig::defaultQtRules(LoggingConfig::Level::Quiet);
    EXPECT_EQ(rules, QStringLiteral("*.debug=false\n*.info=false"));
    EXPECT_FALSE(rules.contains(QStringLiteral("bloom.imagecache.debug=true")));
    EXPECT_FALSE(rules.contains(QStringLiteral("*.info=true")));
}
