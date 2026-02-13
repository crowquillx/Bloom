#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QSize>

/**
 * @brief Controller for visual regression test mode.
 * 
 * When test mode is enabled, the application:
 * - Loads deterministic test data from a fixture file
 * - Bypasses network requests to Jellyfin server
 * - Uses local placeholder images
 * - Sets a fixed viewport resolution for consistent screenshots
 * 
 * This enables reliable visual regression testing without requiring
 * a live Jellyfin server connection.
 */
class TestModeController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool testMode READ isTestMode CONSTANT)
    Q_PROPERTY(QString fixturePath READ fixturePath CONSTANT)
    Q_PROPERTY(QSize testResolution READ testResolution CONSTANT)
    
public:
    /**
     * @brief Get the singleton instance.
     * @return Pointer to the TestModeController instance.
     */
    static TestModeController* instance();
    
    /**
     * @brief Check if test mode is enabled.
     * @return true if running in test mode.
     */
    bool isTestMode() const { return m_testMode; }
    
    /**
     * @brief Get the path to the test fixture file.
     * @return Path to the fixture JSON file.
     */
    QString fixturePath() const { return m_fixturePath; }
    
    /**
     * @brief Get the test viewport resolution.
     * @return The fixed resolution for test screenshots.
     */
    QSize testResolution() const { return m_testResolution; }
    
    /**
     * @brief Initialize test mode with the given fixture path.
     * @param fixturePath Path to the test fixture JSON file.
     * @param resolution The viewport resolution for screenshots.
     */
    void initialize(const QString& fixturePath, const QSize& resolution);
    
    /**
     * @brief Load and return the fixture JSON data.
     * @return The parsed JSON object from the fixture file.
     */
    QJsonObject loadFixture() const;
    
    /**
     * @brief Get the path to the test images directory.
     * @return Path to the directory containing placeholder images.
     */
    QString testImagesPath() const;
    
private:
    explicit TestModeController(QObject* parent = nullptr);
    
    bool m_testMode = false;
    QString m_fixturePath;
    QSize m_testResolution{1920, 1080};
};
