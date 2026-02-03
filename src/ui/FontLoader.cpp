#include "FontLoader.h"
#include <QFontDatabase>
#include <QFont>
#include <QFontMetrics>
#include <QDebug>

FontLoader::FontLoader(QObject *parent)
    : QObject(parent)
{
}

void FontLoader::load()
{
    int fontId = QFontDatabase::addApplicationFont(":/fonts/MaterialSymbolsOutlined.ttf");
    if (fontId == -1) {
        qWarning() << "Failed to load Material Symbols font";
    } else {
        QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        qDebug() << "Loaded Material Symbols font families:" << families;
        
        // Test if the font can render our icon codepoints
        if (!families.isEmpty()) {
            QFont testFont(families.first());
            testFont.setPixelSize(24);
            
            // Test codepoint U+E88A (home icon)
            QFontMetrics fm(testFont);
            bool hasHome = fm.inFont(QChar(0xE88A));
            bool hasMenu = fm.inFont(QChar(0xE5D2));
            bool hasSettings = fm.inFont(QChar(0xE8B8));
            qDebug() << "Font glyph check - home (U+E88A):" << hasHome 
                     << "menu (U+E5D2):" << hasMenu 
                     << "settings (U+E8B8):" << hasSettings;
        }
    }
}
