#pragma once

#include <QJsonObject>
#include <QFlags>
#include <QList>
#include <QString>

/**
 * @brief Server implementation selected for a Bloom connection.
 */
enum class ProviderKind {
    Jellyfin,
    Silo
};

/**
 * @brief Protocol surface used for a server connection.
 *
 * Native means the provider's primary API. Compatibility identifies an
 * emulated protocol surface, such as Silo's optional MediaBrowser listener.
 */
enum class ProtocolMode {
    Native,
    Compatibility
};

/** Provider-advertised operations. Absence means unsupported, not unprobed. */
enum class ProviderCapability {
    RefreshAuthentication = 1 << 0,
    Profiles = 1 << 1,
    ProfilePin = 1 << 2,
    AuthSessions = 1 << 3,
    Catalog = 1 << 4,
    NativeState = 1 << 5,
    MediaSegments = 1 << 6,
    Playback = 1 << 7,
    PlaybackReporting = 1 << 8,
    ThemeSongs = 1 << 9,
    Trickplay = 1 << 10,
    // A per-profile watchlist distinct from favorites.
    Watchlist = 1 << 11,
    // Server-curated Home rows for the selected profile.
    HomeSections = 1 << 12,
    // Server-side media requests and request discovery. The viewer's
    // request capability document still gates every use.
    MediaRequests = 1 << 13,
    // Next Up honors ProviderCatalogQuery::nextUpRewatching and
    // nextUpCutoff (Jellyfin). Silo applies its own server rules.
    NextUpOptions = 1 << 14,
    // ProviderCatalogOperation::ResumeItems lists in-progress movies and
    // episodes beside Next Up (Jellyfin).
    ResumeItems = 1 << 15,
    // Marking a movie or series "Not interested" for the profile's
    // recommendations. The profile's capability document still gates it.
    NotInterested = 1 << 16,
    // The upcoming airings calendar.
    AiringCalendar = 1 << 17,
    // Profile-curated rows per library (ProviderCatalogOperation::
    // LibrarySections, Silo).
    LibrarySections = 1 << 18,
    // ProviderCatalogOperation::MovieRecommendations: personalised movie
    // rows per library (Jellyfin).
    MovieRecommendations = 1 << 19,
    // ProviderCatalogOperation::SetHomeDismissal hides a card from
    // Continue Watching or Next Up and can undo it (Silo).
    HomeDismissals = 1 << 20,
    // A personal 1-5 star rating per movie or series
    // (ProviderCatalogOperation::SetUserRating, Silo). The profile's
    // capability document still gates it.
    StarRatings = 1 << 21,
    // A personal like or dislike per item (SetUserRating with +1 or -1,
    // Jellyfin).
    LikeRatings = 1 << 22
};
Q_DECLARE_FLAGS(ProviderCapabilities, ProviderCapability)

struct ProviderDetectionResult {
    ProviderKind providerKind = ProviderKind::Jellyfin;
    ProtocolMode protocolMode = ProtocolMode::Native;
    QString serverId;
    QString serverName;
    ProviderCapabilities capabilities;

    [[nodiscard]] bool isValid() const { return !serverId.isEmpty(); }
};

struct ProviderProfile {
    QString id;
    QString name;
    QString avatarUrl;
    bool hasPin = false;
    bool isChild = false;
    bool isPrimary = false;

    [[nodiscard]] bool isValid() const { return !id.isEmpty(); }
};

struct ProviderAuthSession {
    QString id;
    QString deviceName;
    QString ipAddress;
    // Absolute Unix timestamps in milliseconds; negative means unavailable.
    qint64 createdAt = -1;
    qint64 expiresAt = -1;
    qint64 revokedAt = -1;
    bool isCurrent = false;

    [[nodiscard]] bool isValid() const { return !id.isEmpty(); }
};

/**
 * @brief Provider-neutral persisted identity for one server/account/profile.
 *
 * Secrets are never serialized here. credentialReference is an opaque key used
 * by CredentialStore to locate them in the platform secret store.
 */
struct ServerConnection {
    QString connectionId;
    ProviderKind providerKind = ProviderKind::Jellyfin;
    ProtocolMode protocolMode = ProtocolMode::Native;
    QString baseUrl;
    QString serverId;
    QString serverName;
    QString accountId;
    QString profileId;
    QString username;
    QString displayName;
    QJsonObject capabilities;
    QString credentialReference;

    bool isValid() const;
    QJsonObject toJson() const;

    static ServerConnection fromJson(const QJsonObject &json);
    static QString providerKindName(ProviderKind kind);
    static ProviderKind providerKindFromName(const QString &name);
    static QString protocolModeName(ProtocolMode mode);
    static ProtocolMode protocolModeFromName(const QString &name);
    static QString normalizeBaseUrl(const QString &url);
    static QString createConnectionId();
    static QString createDeterministicConnectionId(ProviderKind kind,
                                                   const QString &baseUrl,
                                                   const QString &accountId);
    static QString createCredentialReference(const QString &connectionId);
};

Q_DECLARE_METATYPE(ServerConnection)
Q_DECLARE_METATYPE(QList<ServerConnection>)
Q_DECLARE_METATYPE(ProviderDetectionResult)
Q_DECLARE_METATYPE(ProviderProfile)
Q_DECLARE_METATYPE(QList<ProviderProfile>)
Q_DECLARE_METATYPE(ProviderAuthSession)
Q_DECLARE_METATYPE(QList<ProviderAuthSession>)
Q_DECLARE_OPERATORS_FOR_FLAGS(ProviderCapabilities)
