#pragma once

#include "profiles/BloomProfile.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class AuthenticationService;
class BloomProfileRepository;
class ConfigManager;

/**
 * @brief QML facade for Bloom profiles ("spaces" in the UI): switching, editing,
 * adding and removing connections, and routing work to an item's owner.
 *
 * AuthenticationService keeps exactly one live session. This controller keeps
 * that session on a member of the active Bloom profile: switching profiles
 * activates the profile's default member (falling back to the other enabled
 * members), and routeTo() moves the session to the member that owns a merged
 * item before details, playback, or writes run. Credentials are never copied
 * into profile data and switching never deletes them.
 */
class WorkspaceController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList connections READ connections NOTIFY connectionsChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY activeProfileChanged)
    Q_PROPERTY(QString activeProfileName READ activeProfileName NOTIFY activeProfileChanged)
    Q_PROPERTY(bool merged READ isMerged NOTIFY activeProfileChanged)
    Q_PROPERTY(QString activeConnectionId READ activeConnectionId NOTIFY activeConnectionChanged)
    Q_PROPERTY(bool switching READ isSwitching NOTIFY switchingChanged)
    Q_PROPERTY(bool addingConnection READ isAddingConnection NOTIFY addingConnectionChanged)

public:
    WorkspaceController(AuthenticationService *authService,
                        BloomProfileRepository *repository,
                        ConfigManager *configManager,
                        QObject *parent = nullptr);

    QVariantList profiles() const;
    QVariantList connections() const;
    QString activeProfileId() const;
    QString activeProfileName() const;
    bool isMerged() const;
    QString activeConnectionId() const;
    bool isSwitching() const { return m_switching; }
    bool isAddingConnection() const { return m_addingConnection; }

    Q_INVOKABLE bool switchProfile(const QString &profileId);
    Q_INVOKABLE QString createProfile(const QString &name,
                                      const QStringList &connectionIds,
                                      bool merged);
    Q_INVOKABLE bool renameProfile(const QString &profileId, const QString &name);
    Q_INVOKABLE bool setProfileMerged(const QString &profileId, bool merged);
    Q_INVOKABLE bool removeProfile(const QString &profileId);
    Q_INVOKABLE bool moveProfile(const QString &profileId, int delta);

    Q_INVOKABLE bool addMember(const QString &profileId, const QString &connectionId);
    Q_INVOKABLE bool removeMember(const QString &profileId, const QString &memberId);
    Q_INVOKABLE bool moveMember(const QString &profileId, const QString &memberId, int delta);
    Q_INVOKABLE bool setMemberEnabled(const QString &profileId,
                                      const QString &memberId,
                                      bool enabled);
    Q_INVOKABLE bool setDefaultMember(const QString &profileId, const QString &memberId);
    Q_INVOKABLE bool setMemberLabel(const QString &profileId,
                                    const QString &memberId,
                                    const QString &label);

    /// Leaves the current session (credentials kept) for the sign-in screen.
    /// An empty target creates a new single space for the new connection;
    /// otherwise the connection joins that space.
    Q_INVOKABLE void beginAddConnection(const QString &targetProfileId = QString());
    Q_INVOKABLE void cancelAddConnection();
    /// Deletes a connection's credentials and metadata. Memberships referring
    /// to it disappear; other members and their state are untouched.
    Q_INVOKABLE bool removeConnection(const QString &connectionId);

    /// Ensures the live session belongs to connectionId. Returns true when it
    /// already does; otherwise starts a switch and later emits routeReady or
    /// routeFailed.
    Q_INVOKABLE bool routeTo(const QString &connectionId);

signals:
    void profilesChanged();
    void connectionsChanged();
    void activeProfileChanged();
    void activeConnectionChanged();
    void switchingChanged();
    void addingConnectionChanged();
    /// A different space is active and its session is ready.
    void workspaceSwitched();
    void switchFailed(const QString &error);
    void routeReady(const QString &connectionId);
    void routeFailed(const QString &connectionId, const QString &error);

private:
    void handleActiveProfileChanged();
    void handleAuthenticated();
    void handleConnectionActivated(const QString &connectionId);
    void handleActivationFailed(const QString &connectionId, const QString &error);
    void handleSignInRequired(const QString &connectionId);
    void startSwitch(const BloomProfile &profile);
    void activateNextCandidate();
    void finishSwitch();
    void failSwitch(const QString &error);
    void finishAddConnection(const QString &connectionId);
    void setSwitching(bool switching);
    void setAddingConnection(bool adding);
    QString createSingleProfileFor(const QString &connectionId);
    bool profileContainsEnabled(const BloomProfile &profile, const QString &connectionId) const;
    QVariantMap connectionMap(const ServerConnection &connection) const;

    AuthenticationService *m_authService = nullptr;
    BloomProfileRepository *m_repository = nullptr;
    ConfigManager *m_configManager = nullptr;

    bool m_switching = false;
    bool m_suppressActivation = false;
    QStringList m_switchCandidates;
    QString m_switchingConnectionId;
    QString m_previousProfileId;
    QString m_previousConnectionId;
    // A switch handed to the login screen; finishes when this member signs in.
    QString m_switchAwaitingSignIn;

    QString m_routeTarget;

    bool m_addingConnection = false;
    QString m_addTargetProfileId;
    QString m_connectionBeforeAdd;
};
