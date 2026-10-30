#include "SidebarSettings.h"
#include <QGuiApplication>

SidebarSettings::SidebarSettings(QObject *parent)
    : QObject(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope, 
                 QGuiApplication::organizationName().isEmpty() ? "Bloom" : QGuiApplication::organizationName(),
                 QGuiApplication::applicationName().isEmpty() ? "Bloom" : QGuiApplication::applicationName())
    , m_reduceMotion(m_settings.value(kKeyReduceMotion, false).toBool())
    , m_libraryOrder(m_settings.value(kKeyLibraryOrder, QStringList()).toStringList())
{
}

bool SidebarSettings::reduceMotion() const
{
    return m_reduceMotion;
}

void SidebarSettings::setReduceMotion(bool reduce)
{
    if (m_reduceMotion != reduce) {
        m_reduceMotion = reduce;
        m_settings.setValue(kKeyReduceMotion, reduce);
        emit reduceMotionChanged();
    }
}

QStringList SidebarSettings::libraryOrder() const
{
    return m_libraryOrder;
}

void SidebarSettings::setLibraryOrder(const QStringList &order)
{
    if (m_libraryOrder == order)
        return;

    m_libraryOrder = order;
    m_settings.setValue(kKeyLibraryOrder, m_libraryOrder);
    emit libraryOrderChanged();
}
