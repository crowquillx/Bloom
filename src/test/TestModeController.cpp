#include "TestModeController.h"
#include <QFile>
#include <QJsonDocument>
#include <QDir>
#include <QDebug>

TestModeController* TestModeController::instance()
{
    static TestModeController s_instance;
    return &s_instance;
}

TestModeController::TestModeController(QObject* parent)
    : QObject(parent)
{
}

void TestModeController::initialize(const QString& fixturePath, const QSize& resolution)
{
    m_testMode = true;
    m_fixturePath = fixturePath;
    m_testResolution = resolution;
    
    qDebug() << "Test mode enabled:";
    qDebug() << "  Fixture path:" << m_fixturePath;
    qDebug() << "  Resolution:" << m_testResolution.width() << "x" << m_testResolution.height();
}

QJsonObject TestModeController::loadFixture() const
{
    if (m_fixturePath.isEmpty()) {
        qWarning() << "TestModeController: No fixture path set";
        return QJsonObject();
    }
    
    QFile file(m_fixturePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "TestModeController: Failed to open fixture file:" << m_fixturePath;
        return QJsonObject();
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "TestModeController: Failed to parse fixture JSON:" << parseError.errorString();
        return QJsonObject();
    }
    
    return doc.object();
}

QString TestModeController::testImagesPath() const
{
    if (m_fixturePath.isEmpty()) {
        return QString();
    }
    
    // The test images are expected to be in a sibling directory to the fixture
    // e.g., if fixture is tests/fixtures/test_library.json, images are in tests/fixtures/test_images/
    QFileInfo fixtureInfo(m_fixturePath);
    QDir fixtureDir = fixtureInfo.dir();
    
    return fixtureDir.filePath("test_images");
}
