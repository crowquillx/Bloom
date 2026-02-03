#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantList>
#include <QVariantMap>
#include <QNetworkReply>
#include <QLoggingCategory>
#include <QMap>
#include <QList>
#include <QMetaType>

// ============================================================================
// Media Stream / Source / Playback Info
// ============================================================================

struct MediaStreamInfo
{
    Q_GADGET
    Q_PROPERTY(int index MEMBER index)
    Q_PROPERTY(QString type MEMBER type)
    Q_PROPERTY(QString codec MEMBER codec)
    Q_PROPERTY(QString language MEMBER language)
    Q_PROPERTY(QString title MEMBER title)
    Q_PROPERTY(QString displayTitle MEMBER displayTitle)
    Q_PROPERTY(bool isDefault MEMBER isDefault)
    Q_PROPERTY(bool isForced MEMBER isForced)
    Q_PROPERTY(bool isExternal MEMBER isExternal)
    Q_PROPERTY(bool isHearingImpaired MEMBER isHearingImpaired)
    Q_PROPERTY(int channels MEMBER channels)
    Q_PROPERTY(QString channelLayout MEMBER channelLayout)
    Q_PROPERTY(int bitRate MEMBER bitRate)
    Q_PROPERTY(int width MEMBER width)
    Q_PROPERTY(int height MEMBER height)
    Q_PROPERTY(double averageFrameRate MEMBER averageFrameRate)
    Q_PROPERTY(double realFrameRate MEMBER realFrameRate)
    Q_PROPERTY(QString profile MEMBER profile)
    Q_PROPERTY(QString videoRange MEMBER videoRange)

public:
    int index = -1;
    QString type;
    QString codec;
    QString language;
    QString title;
    QString displayTitle;
    bool isDefault = false;
    bool isForced = false;
    bool isExternal = false;
    bool isHearingImpaired = false;
    int channels = 0;
    QString channelLayout;
    int bitRate = 0;
    int width = 0;
    int height = 0;
    double averageFrameRate = 0.0;
    double realFrameRate = 0.0;
    QString profile;
    QString videoRange;

    [[nodiscard]] static MediaStreamInfo fromJson(const QJsonObject &json);
    [[nodiscard]] QVariantMap toVariantMap() const;
};

struct MediaSourceInfo
{
    Q_GADGET
    Q_PROPERTY(QString id MEMBER id)
    Q_PROPERTY(QString name MEMBER name)
    Q_PROPERTY(QString container MEMBER container)
    Q_PROPERTY(qint64 runTimeTicks MEMBER runTimeTicks)
    Q_PROPERTY(int defaultAudioStreamIndex MEMBER defaultAudioStreamIndex)
    Q_PROPERTY(int defaultSubtitleStreamIndex MEMBER defaultSubtitleStreamIndex)
    Q_PROPERTY(QVariantList mediaStreams READ getMediaStreamsVariant)

public:
    QString id;
    QString name;
    QString container;
    qint64 runTimeTicks = 0;
    int defaultAudioStreamIndex = -1;
    int defaultSubtitleStreamIndex = -1;
    QList<MediaStreamInfo> mediaStreams;

    [[nodiscard]] static MediaSourceInfo fromJson(const QJsonObject &json);

    [[nodiscard]] QList<MediaStreamInfo> getVideoStreams() const;
    [[nodiscard]] QList<MediaStreamInfo> getAudioStreams() const;
    [[nodiscard]] QList<MediaStreamInfo> getSubtitleStreams() const;
    [[nodiscard]] QVariantList getMediaStreamsVariant() const;
};

struct PlaybackInfoResponse
{
    Q_GADGET
    Q_PROPERTY(QString playSessionId MEMBER playSessionId)
    Q_PROPERTY(QVariantList mediaSources READ getMediaSourcesVariant)

public:
    QString playSessionId;
    QList<MediaSourceInfo> mediaSources;

    [[nodiscard]] static PlaybackInfoResponse fromJson(const QJsonObject &json);
    [[nodiscard]] QVariantList getMediaSourcesVariant() const;
};

// ============================================================================
// Media Segments / Trickplay
// ============================================================================

enum class MediaSegmentType {
    Unknown,
    Intro,
    Outro,
    Recap,
    Preview,
    Commercial,
    IntroStart,
    IntroEnd,
    OutroStart,
    OutroEnd
};

struct MediaSegmentInfo
{
    Q_GADGET
    Q_PROPERTY(QString id MEMBER id)
    Q_PROPERTY(QString itemId MEMBER itemId)
    Q_PROPERTY(QString type MEMBER typeString)
    Q_PROPERTY(qint64 startTicks MEMBER startTicks)
    Q_PROPERTY(qint64 endTicks MEMBER endTicks)

public:
    QString id;
    QString itemId;
    MediaSegmentType type = MediaSegmentType::Unknown;
    QString typeString;
    qint64 startTicks = 0;
    qint64 endTicks = 0;

    [[nodiscard]] static MediaSegmentInfo fromJson(const QJsonObject &json);
    [[nodiscard]] constexpr double startSeconds() const { return static_cast<double>(startTicks) / 10000000.0; }
    [[nodiscard]] constexpr double endSeconds() const { return static_cast<double>(endTicks) / 10000000.0; }
};

struct TrickplayTileInfo
{
    Q_GADGET
    Q_PROPERTY(int width MEMBER width)
    Q_PROPERTY(int height MEMBER height)
    Q_PROPERTY(int tileWidth MEMBER tileWidth)
    Q_PROPERTY(int tileHeight MEMBER tileHeight)
    Q_PROPERTY(int thumbnailCount MEMBER thumbnailCount)
    Q_PROPERTY(int interval MEMBER interval)
    Q_PROPERTY(int bandwidth MEMBER bandwidth)

public:
    int width = 0;
    int height = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    int thumbnailCount = 0;
    int interval = 0;
    int bandwidth = 0;

    [[nodiscard]] static TrickplayTileInfo fromJson(const QJsonObject &json);
    [[nodiscard]] constexpr int getTileIndex(int thumbnailIndex) const
    {
        if (tileWidth <= 0 || tileHeight <= 0) return -1;
        const int thumbnailsPerTile = tileWidth * tileHeight;
        if (thumbnailsPerTile <= 0) return -1;
        return thumbnailIndex / thumbnailsPerTile;
    }

    [[nodiscard]] constexpr int getOffsetInTile(int thumbnailIndex) const
    {
        if (tileWidth <= 0 || tileHeight <= 0) return -1;
        const int thumbnailsPerTile = tileWidth * tileHeight;
        if (thumbnailsPerTile <= 0) return -1;
        return thumbnailIndex % thumbnailsPerTile;
    }
};

// ============================================================================
// Errors & Retry
// ============================================================================

struct NetworkError
{
    Q_GADGET
    Q_PROPERTY(int code MEMBER code)
    Q_PROPERTY(QString userMessage MEMBER userMessage)
    Q_PROPERTY(QString technicalDetails MEMBER technicalDetails)
    Q_PROPERTY(QString endpoint MEMBER endpoint)
public:
    int code = 0;
    QString userMessage;
    QString technicalDetails;
    QString endpoint;
};

struct RetryPolicy
{
    int maxRetries = 3;
    int baseDelayMs = 1000;
    bool retryOnTransient = true;
};

class ErrorHandler
{
public:
    static bool isTransientError(QNetworkReply::NetworkError error);
    static bool isClientError(int statusCode);
    static QString mapErrorToUserMessage(QNetworkReply::NetworkError error, int httpStatusCode = 0);
    static int calculateBackoffDelay(int attemptNumber, const RetryPolicy &policy);
    static NetworkError createError(QNetworkReply *reply, const QString &endpoint);
};

// ============================================================================
// JSON Parsing helpers for items
// ============================================================================

struct ParsedItemsResult
{
    bool success = false;
    QString parentId;
    QJsonArray items;
    int totalRecordCount = 0;
};

class JsonParser
{
public:
    static bool shouldParseAsync(const QByteArray &data);
    static ParsedItemsResult parseItemsResponse(const QByteArray &data, const QString &parentId);
};

// ============================================================================
// Meta type registration helper
// ============================================================================

void registerNetworkMetaTypes();

// Logging category used by network code
Q_DECLARE_LOGGING_CATEGORY(jellyfinNetwork)

// Meta type declarations
using TrickplayTileInfoMap = QMap<int, TrickplayTileInfo>;

Q_DECLARE_METATYPE(MediaStreamInfo)
Q_DECLARE_METATYPE(MediaSourceInfo)
Q_DECLARE_METATYPE(PlaybackInfoResponse)
Q_DECLARE_METATYPE(MediaSegmentInfo)
Q_DECLARE_METATYPE(TrickplayTileInfo)
Q_DECLARE_METATYPE(QList<MediaSegmentInfo>)
Q_DECLARE_METATYPE(TrickplayTileInfoMap)
