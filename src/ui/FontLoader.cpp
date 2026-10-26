#include "FontLoader.h"
#include <QFontDatabase>
#include <QFont>
#include <QFontMetrics>
#include <QDebug>
#include "../utils/BloomLogging.h"

FontLoader::FontLoader(QObject *parent)
    : QObject(parent)
{
}

void FontLoader::load()
{
    // Bundled UI typefaces so every platform renders the same type: Inter for
    // text and controls, Inter Display for page titles and heroes.
    static const char *const kTypefaces[] = {
        ":/fonts/inter/Inter-Regular.ttf",
        ":/fonts/inter/Inter-Medium.ttf",
        ":/fonts/inter/Inter-SemiBold.ttf",
        ":/fonts/inter/Inter-Bold.ttf",
        ":/fonts/inter/InterDisplay-SemiBold.ttf",
        ":/fonts/inter/InterDisplay-Bold.ttf",
        ":/fonts/inter/InterDisplay-ExtraBold.ttf",
    };
    for (const char *path : kTypefaces) {
        if (QFontDatabase::addApplicationFont(QString::fromLatin1(path)) == -1) {
            qCWarning(lcUi) << "Failed to load bundled typeface" << path;
        }
    }

    int fontId = QFontDatabase::addApplicationFont(":/fonts/MaterialSymbolsOutlined.ttf");
    if (fontId == -1) {
        qCWarning(lcUi) << "Failed to load Material Symbols font";
    } else {
        QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        qCDebug(lcUi) << "Loaded Material Symbols font families:" << families;
        
        // Test if the font can render our icon codepoints
        if (!families.isEmpty()) {
            QFont testFont(families.first());
            testFont.setPixelSize(24);
            
            // Test codepoint U+E88A (home icon)
            QFontMetrics fm(testFont);
            bool hasHome = fm.inFont(QChar(0xE88A));
            bool hasMenu = fm.inFont(QChar(0xE5D2));
            bool hasSettings = fm.inFont(QChar(0xE8B8));
            qCDebug(lcUi) << "Font glyph check - home (U+E88A):" << hasHome 
                     << "menu (U+E5D2):" << hasMenu 
                     << "settings (U+E8B8):" << hasSettings;
        }
    }
}
