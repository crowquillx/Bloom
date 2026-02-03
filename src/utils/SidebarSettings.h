#pragma once

#include <QObject>
#include <QSettings>

/**
 * @brief Manages sidebar UI state persistence via QSettings.
 * 
 * This lightweight class stores sidebar-related preferences:
 * - Expanded/collapsed state
 * - Reduced motion preference (respects system setting)
 * 
 * Exposed to QML via ServiceLocator and context property.
 */
class SidebarSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool sidebarExpanded READ sidebarExpanded WRITE setSidebarExpanded NOTIFY sidebarExpandedChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion WRITE setReduceMotion NOTIFY reduceMotionChanged)
    Q_PROPERTY(QStringList libraryOrder READ libraryOrder WRITE setLibraryOrder NOTIFY libraryOrderChanged)

public:
    explicit SidebarSettings(QObject *parent = nullptr);
    ~SidebarSettings() override = default;

    /// Returns whether the sidebar is expanded (true) or collapsed (false)
    bool sidebarExpanded() const;
    
    /// Sets the sidebar expanded state and persists to QSettings
    void setSidebarExpanded(bool expanded);
    
    /// Returns whether reduced motion is enabled
    bool reduceMotion() const;
    
    /// Sets the reduced motion preference
    void setReduceMotion(bool reduce);

    /// Returns the persisted order of library IDs (empty = natural order)
    QStringList libraryOrder() const;

    /// Sets the persisted order of library IDs
    void setLibraryOrder(const QStringList &order);

    /// Move an entry in the library order list and persist
    Q_INVOKABLE void moveLibrary(int fromIndex, int toIndex);
    
    /// Toggle sidebar expanded state
    Q_INVOKABLE void toggleSidebar();

signals:
    void sidebarExpandedChanged();
    void reduceMotionChanged();
    void libraryOrderChanged();

private:
    QSettings m_settings;
    bool m_sidebarExpanded;
    bool m_reduceMotion;
    QStringList m_libraryOrder;
    
    static constexpr const char* kKeyExpanded = "ui/sidebarExpanded";
    static constexpr const char* kKeyReduceMotion = "ui/reduceMotion";
    static constexpr const char* kKeyLibraryOrder = "ui/libraryOrder";
};
