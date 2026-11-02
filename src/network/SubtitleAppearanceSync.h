#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class AuthenticationService;
class ConfigManager;
class LibraryService;

/**
 * @brief Keeps the local subtitle appearance and a Silo profile's in step.
 *
 * When a connection with ProviderCapability::SubtitleAppearance becomes
 * live (sign-in, profile choice, space switch), the service reads the
 * appearance that applies to the acting profile on this device. While the
 * user has not changed the appearance in Bloom, that appearance becomes the
 * local one. A change the user makes in Bloom is written as this device's
 * override, debounced; a reset removes the override and adopts the server's
 * appearance again. Writes are best effort: a failure is logged and never
 * blocks or reverts the local change. Providers without the capability
 * (Jellyfin) keep the appearance local.
 */
class SubtitleAppearanceSync : public QObject
{
    Q_OBJECT

public:
    static constexpr int kWriteDebounceMs = 1500;

    SubtitleAppearanceSync(ConfigManager *config,
                           LibraryService *library,
                           AuthenticationService *authService = nullptr,
                           QObject *parent = nullptr);

    /// Reads the live connection's appearance, adopting it while the
    /// appearance is untouched. Drops a pending write for another scope.
    void refresh();
    /// Sends a debounced write now (tests and shutdown).
    void flushPendingWrite();
    bool writePending() const { return m_writeTimer.isActive(); }

private:
    void onEdited();
    void onReset();
    void sendWrite();

    ConfigManager *m_config = nullptr;
    LibraryService *m_library = nullptr;
    QTimer m_writeTimer;
    QTimer m_refreshTimer;
    // The scope the last answer came from and its document, which a write
    // passes back so fields Bloom does not map survive.
    QString m_scope;
    QString m_document;
    // The scope a pending write belongs to.
    QString m_writeScope;
    // Bumped when the scope's state is dropped; older answers are ignored.
    quint64 m_generation = 0;
    bool m_writeInFlight = false;
    bool m_writeQueued = false;
};
