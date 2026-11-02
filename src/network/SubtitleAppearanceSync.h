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
 * The appearance is one per device (ConfigManager), and so is the user's
 * "changed in Bloom" mark. A scope is a connection and profile with
 * ProviderCapability::SubtitleAppearance; each keeps its own device
 * override on the server. When a scope becomes live (sign-in, profile
 * choice, space switch), the service reads the appearance that applies to
 * the acting profile on this device. While the user has not changed the
 * appearance in Bloom, that appearance becomes the local one, or the
 * defaults when the scope has none.
 *
 * A change the user makes in Bloom marks the live scope as needing a write
 * (kept in ConfigManager across restarts) and is written, debounced, as
 * that scope's device override with the current local appearance. A reset
 * marks the scope as needing its override removed, and removes it before
 * anything else is read or written for that scope. Requests go one at a
 * time, so a removal never overtakes a write still on the wire. A write is
 * sent only after a read of the same scope, so the server's fields Bloom
 * does not map survive. A failure keeps the mark and is retried when the
 * scope becomes live again; it never blocks or reverts the local change.
 * Providers without the capability (Jellyfin) keep the appearance local.
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

    /// Brings the live scope in step: a pending removal first, then a
    /// pending write, else a read that is adopted while untouched.
    void refresh();
    /// Sends a debounced write now (tests and shutdown).
    void flushPendingWrite();
    bool writePending() const { return m_writeTimer.isActive(); }

private:
    enum class Request { None, Read, Write, Remove };

    void onEdited();
    void onReset();
    // Sends the next request the live scope needs, unless one is out.
    void pump();
    void sendRead(const QString &scope);
    void sendWrite(const QString &scope);
    void sendRemove(const QString &scope);
    // Common bookkeeping for an answer; false when it is to be ignored.
    bool finish(quint64 generation, const QVariantMap &result, bool ok);

    ConfigManager *m_config = nullptr;
    LibraryService *m_library = nullptr;
    QTimer m_writeTimer;
    QTimer m_refreshTimer;
    // The request out and the scope it is for.
    Request m_inFlight = Request::None;
    QString m_inFlightScope;
    // Bumped when an answer is to be ignored (its scope is no longer live).
    quint64 m_generation = 0;
    // A refresh asked for a read of this scope.
    QString m_readScope;
    // Bumped on every user change, so a write answer clears the mark only
    // when nothing changed while it was out.
    quint64 m_editSerial = 0;
};
