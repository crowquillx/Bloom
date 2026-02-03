#include "SidebarSettings.h"
#include <QGuiApplication>

SidebarSettings::SidebarSettings(QObject *parent)
    : QObject(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope, 
                 QGuiApplication::organizationName().isEmpty() ? "Bloom" : QGuiApplication::organizationName(),
                 QGuiApplication::applicationName().isEmpty() ? "Bloom" : QGuiApplication::applicationName())
    , m_sidebarExpanded(m_settings.value(kKeyExpanded, false).toBool())
    , m_reduceMotion(m_settings.value(kKeyReduceMotion, false).toBool())
    , m_libraryOrder(m_settings.value(kKeyLibraryOrder, QStringList()).toStringList())
{
}

bool SidebarSettings::sidebarExpanded() const
{
    return m_sidebarExpanded;
}

void SidebarSettings::setSidebarExpanded(bool expanded)
{
    if (m_sidebarExpanded != expanded) {
        m_sidebarExpanded = expanded;
        m_settings.setValue(kKeyExpanded, expanded);
        emit sidebarExpandedChanged();
    }
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

void SidebarSettings::moveLibrary(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || toIndex < 0 || fromIndex == toIndex)
        return;

    if (fromIndex >= m_libraryOrder.size() || toIndex >= m_libraryOrder.size())
        return;

    QStringList newOrder = m_libraryOrder;
    QString id = newOrder.takeAt(fromIndex);
    newOrder.insert(toIndex, id);
    setLibraryOrder(newOrder);
}

void SidebarSettings::toggleSidebar()
{
    setSidebarExpanded(!m_sidebarExpanded);
}
