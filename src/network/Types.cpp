#include "Types.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtMath>

Q_LOGGING_CATEGORY(jellyfinNetwork, "jellyfin.network")

/**
 * @brief Register Qt metatypes for network data structures
 * 
 * Registers custom types with Qt's meta-object system to enable:
 * - Signal/slot connections with these types
 * - Queued connections across threads
 * - QVariant conversions for QML exposure
 * 
 * Called once during application initialization. Thread-safe via static flag.
 */
void registerNetworkMetaTypes()
{
    static bool registered = false;
    if (registered) return;
    registered = true;
    qRegisterMetaType<MediaStreamInfo>("MediaStreamInfo");
    qRegisterMetaType<MediaSourceInfo>("MediaSourceInfo");
    qRegisterMetaType<PlaybackInfoResponse>("PlaybackInfoResponse");
    qRegisterMetaType<MediaSegmentInfo>("MediaSegmentInfo");
    qRegisterMetaType<TrickplayTileInfo>("TrickplayTileInfo");
    qRegisterMetaType<QList<MediaSegmentInfo>>("QList<MediaSegmentInfo>");
    qRegisterMetaType<TrickplayTileInfoMap>("QMap<int,TrickplayTileInfo>");
}

// ============================================================================
// MediaStreamInfo Implementation
// ============================================================================

/**
 * @brief Parse MediaStreamInfo from Jellyfin API JSON response
 * 
 * Deserializes a single media stream (audio/video/subtitle) from the Jellyfin API.
 * Typically found in the "MediaStreams" array within MediaSource objects.
 * 
 * Jellyfin API reference: /Items/{itemId}/PlaybackInfo response
 * Endpoint: GET /Items/{itemId}/PlaybackInfo
 * 
 * Key fields:
 * - Index: Stream index for mpv selection (e.g., --aid=1, --sid=2)
 * - Type: "Video", "Audio", or "Subtitle"
 * - Codec: Codec identifier (e.g., "h264", "aac", "srt")
 * - DisplayTitle: Human-readable stream description for UI
 * - IsDefault/IsForced: Stream selection hints from server
 * - Language: ISO 639 language code (e.g., "eng", "jpn")
 * 
 * @param json JSON object representing a single MediaStream from Jellyfin
 * @return Populated MediaStreamInfo struct
 */
MediaStreamInfo MediaStreamInfo::fromJson(const QJsonObject &json)
{
    MediaStreamInfo info;
    info.index = json["Index"].toInt(-1);
    info.type = json["Type"].toString();
    info.codec = json["Codec"].toString();
    info.language = json["Language"].toString();
    info.title = json["Title"].toString();
    info.displayTitle = json["DisplayTitle"].toString();
    info.isDefault = json["IsDefault"].toBool();
    info.isForced = json["IsForced"].toBool();
    info.isExternal = json["IsExternal"].toBool();
    info.isHearingImpaired = json["IsHearingImpaired"].toBool();
    info.channels = json["Channels"].toInt();
    info.channelLayout = json["ChannelLayout"].toString();
    info.bitRate = json["BitRate"].toInt();
    info.width = json["Width"].toInt();
    info.height = json["Height"].toInt();
    info.averageFrameRate = json["AverageFrameRate"].toDouble();
    info.realFrameRate = json["RealFrameRate"].toDouble();
    info.profile = json["Profile"].toString();
    info.videoRange = json["VideoRange"].toString();
    return info;
}

/**
 * @brief Convert MediaStreamInfo to QVariantMap for QML exposure
 * 
 * Converts C++ struct to QML-compatible map for use in ListView delegates
 * and ComboBox models. All fields are exposed with camelCase keys.
 * 
 * @return QVariantMap suitable for QML consumption
 */
QVariantMap MediaStreamInfo::toVariantMap() const
{
    QVariantMap streamMap;
    streamMap["index"] = index;
    streamMap["type"] = type;
    streamMap["codec"] = codec;
    streamMap["language"] = language;
    streamMap["title"] = title;
    streamMap["displayTitle"] = displayTitle;
    streamMap["isDefault"] = isDefault;
    streamMap["isForced"] = isForced;
    streamMap["isExternal"] = isExternal;
    streamMap["isHearingImpaired"] = isHearingImpaired;
    streamMap["channels"] = channels;
    streamMap["channelLayout"] = channelLayout;
    streamMap["bitRate"] = bitRate;
    streamMap["width"] = width;
    streamMap["height"] = height;
    streamMap["averageFrameRate"] = averageFrameRate;
    streamMap["realFrameRate"] = realFrameRate;
    streamMap["profile"] = profile;
    streamMap["videoRange"] = videoRange;
    return streamMap;
}

// ============================================================================
// MediaSourceInfo Implementation
// ============================================================================

/**
 * @brief Parse MediaSourceInfo from Jellyfin API JSON response
 * 
 * Deserializes a media source container from the Jellyfin PlaybackInfo response.
 * A MediaSource represents a single playable version of an item (e.g., different
 * qualities, direct play vs. transcode).
 * 
 * Jellyfin API reference: /Items/{itemId}/PlaybackInfo response
 * Endpoint: GET /Items/{itemId}/PlaybackInfo
 * 
 * Key fields:
 * - Id: Unique identifier for this media source
 * - Container: File container format (e.g., "mkv", "mp4")
 * - RunTimeTicks: Duration in ticks (1 tick = 100ns, divide by 10,000,000 for seconds)
 * - MediaStreams: Array of audio/video/subtitle streams (parsed recursively)
 * - DefaultAudioStreamIndex/DefaultSubtitleStreamIndex: Server-recommended defaults
 * 
 * The MediaStreams array is parsed into MediaStreamInfo objects for stream selection.
 * 
 * @param json JSON object representing a MediaSource from Jellyfin
 * @return Populated MediaSourceInfo struct with nested MediaStreamInfo list
 */
MediaSourceInfo MediaSourceInfo::fromJson(const QJsonObject &json)
{
    MediaSourceInfo info;
    info.id = json["Id"].toString();
    info.name = json["Name"].toString();
    info.container = json["Container"].toString();
    info.runTimeTicks = static_cast<qint64>(json["RunTimeTicks"].toDouble());
    info.defaultAudioStreamIndex = json["DefaultAudioStreamIndex"].toInt(-1);
    info.defaultSubtitleStreamIndex = json["DefaultSubtitleStreamIndex"].toInt(-1);
    
    QJsonArray streamsArray = json["MediaStreams"].toArray();
    for (const QJsonValue &streamVal : streamsArray) {
        info.mediaStreams.append(MediaStreamInfo::fromJson(streamVal.toObject()));
    }
    
    return info;
}

/**
 * @brief Filter and return only video streams from mediaStreams
 * 
 * Used for video track selection UI and mpv --vid parameter.
 * 
 * @return List of video streams (Type == "Video")
 */
QList<MediaStreamInfo> MediaSourceInfo::getVideoStreams() const
{
    QList<MediaStreamInfo> result;
    for (const auto &stream : mediaStreams) {
        if (stream.type == "Video") result.append(stream);
    }
    return result;
}

/**
 * @brief Filter and return only audio streams from mediaStreams
 * 
 * Used for audio track selection UI and mpv --aid parameter.
 * 
 * @return List of audio streams (Type == "Audio")
 */
QList<MediaStreamInfo> MediaSourceInfo::getAudioStreams() const
{
    QList<MediaStreamInfo> result;
    for (const auto &stream : mediaStreams) {
        if (stream.type == "Audio") result.append(stream);
    }
    return result;
}

/**
 * @brief Filter and return only subtitle streams from mediaStreams
 * 
 * Used for subtitle track selection UI and mpv --sid parameter.
 * 
 * @return List of subtitle streams (Type == "Subtitle")
 */
QList<MediaStreamInfo> MediaSourceInfo::getSubtitleStreams() const
{
    QList<MediaStreamInfo> result;
    for (const auto &stream : mediaStreams) {
        if (stream.type == "Subtitle") result.append(stream);
    }
    return result;
}

QVariantList MediaSourceInfo::getMediaStreamsVariant() const
{
    QVariantList result;
    for (const auto &stream : mediaStreams) {
        result.append(stream.toVariantMap());
    }
    return result;
}

// ============================================================================
// PlaybackInfoResponse Implementation
// ============================================================================

/**
 * @brief Parse PlaybackInfoResponse from Jellyfin API JSON response
 * 
 * Deserializes the top-level response from the Jellyfin PlaybackInfo endpoint.
 * This endpoint is called before starting playback to retrieve available media
 * sources, streams, and the play session ID for progress reporting.
 * 
 * Jellyfin API reference:
 * Endpoint: POST /Items/{itemId}/PlaybackInfo
 * Response contains:
 * - PlaySessionId: Unique session identifier for progress reporting
 * - MediaSources: Array of available sources (direct play, transcode options)
 * 
 * The PlaySessionId is used in subsequent /Sessions/Playing/* endpoints to report
 * playback progress, pause, and stop events.
 * 
 * @param json JSON object from Jellyfin PlaybackInfo response
 * @return Populated PlaybackInfoResponse with MediaSources and session ID
 */
PlaybackInfoResponse PlaybackInfoResponse::fromJson(const QJsonObject &json)
{
    PlaybackInfoResponse response;
    response.playSessionId = json["PlaySessionId"].toString();
    
    QJsonArray sourcesArray = json["MediaSources"].toArray();
    for (const QJsonValue &sourceVal : sourcesArray) {
        response.mediaSources.append(MediaSourceInfo::fromJson(sourceVal.toObject()));
    }
    
    return response;
}

QVariantList PlaybackInfoResponse::getMediaSourcesVariant() const
{
    QVariantList result;
    for (const auto &source : mediaSources) {
        QVariantMap sourceMap;
        sourceMap["id"] = source.id;
        sourceMap["name"] = source.name;
        sourceMap["container"] = source.container;
        sourceMap["runTimeTicks"] = source.runTimeTicks;
        sourceMap["defaultAudioStreamIndex"] = source.defaultAudioStreamIndex;
        sourceMap["defaultSubtitleStreamIndex"] = source.defaultSubtitleStreamIndex;
        sourceMap["mediaStreams"] = source.getMediaStreamsVariant();
        result.append(sourceMap);
    }
    return result;
}

// ============================================================================
// MediaSegmentInfo Implementation
// ============================================================================

/**
 * @brief Parse MediaSegmentInfo from Jellyfin API JSON response
 * 
 * Deserializes media segment markers (intro/outro/credits) from Jellyfin plugins
 * like "Intro Skipper". These segments define time ranges for UI skip buttons.
 * 
 * Jellyfin API reference:
 * Endpoint: GET /Episode/{itemId}/IntroTimestamps (plugin-specific)
 * 
 * Key fields:
 * - StartTicks/EndTicks: Time range in ticks (1 tick = 100ns)
 * - Type: Segment type string ("IntroStart", "IntroEnd", "OutroStart", etc.)
 * 
 * The Type string is parsed into a MediaSegmentType enum for easier handling.
 * Ticks are converted to seconds via startSeconds()/endSeconds() helpers.
 * 
 * @param json JSON object representing a media segment
 * @return Populated MediaSegmentInfo with parsed type enum
 */
MediaSegmentInfo MediaSegmentInfo::fromJson(const QJsonObject &json)
{
    MediaSegmentInfo info;
    info.id = json["Id"].toString();
    info.itemId = json["ItemId"].toString();
    info.startTicks = static_cast<qint64>(json["StartTicks"].toDouble());
    info.endTicks = static_cast<qint64>(json["EndTicks"].toDouble());
    
    QString typeStr = json["Type"].toString();
    info.typeString = typeStr;
    
    if (typeStr.compare("IntroStart", Qt::CaseInsensitive) == 0) {
        info.type = MediaSegmentType::IntroStart;
    } else if (typeStr.compare("IntroEnd", Qt::CaseInsensitive) == 0) {
        info.type = MediaSegmentType::IntroEnd;
    } else if (typeStr.compare("OutroStart", Qt::CaseInsensitive) == 0) {
        info.type = MediaSegmentType::OutroStart;
    } else if (typeStr.compare("OutroEnd", Qt::CaseInsensitive) == 0) {
        info.type = MediaSegmentType::OutroEnd;
    } else {
        info.type = MediaSegmentType::Unknown;
    }
    
    return info;
}

// ============================================================================
// TrickplayTileInfo Implementation
// ============================================================================

/**
 * @brief Parse TrickplayTileInfo from Jellyfin API JSON response
 * 
 * Deserializes trickplay (thumbnail preview) metadata from Jellyfin.
 * Trickplay tiles are sprite sheets containing multiple thumbnails for scrubbing.
 * 
 * Jellyfin API reference:
 * Endpoint: GET /Items/{itemId}/TrickplayInfo
 * 
 * Key fields:
 * - Width/Height: Total sprite sheet dimensions
 * - TileWidth/TileHeight: Grid dimensions (e.g., 10x10 = 100 thumbnails per sheet)
 * - Interval: Milliseconds between thumbnails
 * - ThumbnailCount: Total number of thumbnails across all tiles
 * 
 * Helper methods getTileIndex() and getOffsetInTile() calculate which sprite sheet
 * and position to use for a given thumbnail index during scrubbing.
 * 
 * @param json JSON object from Jellyfin TrickplayInfo response
 * @return Populated TrickplayTileInfo for thumbnail sprite calculations
 */
TrickplayTileInfo TrickplayTileInfo::fromJson(const QJsonObject &json)
{
    TrickplayTileInfo info;
    info.width = json["Width"].toInt();
    info.height = json["Height"].toInt();
    info.tileWidth = json["TileWidth"].toInt();
    info.tileHeight = json["TileHeight"].toInt();
    info.thumbnailCount = json["ThumbnailCount"].toInt();
    info.interval = json["Interval"].toInt();
    info.bandwidth = json["Bandwidth"].toInt();
    
    qDebug() << "TrickplayTileInfo::fromJson parsed:"
             << "Width:" << info.width
             << "Height:" << info.height
             << "TileWidth:" << info.tileWidth
             << "TileHeight:" << info.tileHeight
             << "ThumbnailCount:" << info.thumbnailCount
             << "Interval:" << info.interval
             << "Bandwidth:" << info.bandwidth;
    
    return info;
}

// ============================================================================
// ErrorHandler Implementation
// ============================================================================

/**
 * @brief Determine if a network error is transient (retryable)
 * 
 * Classifies QNetworkReply errors into transient (temporary, worth retrying)
 * vs. permanent (e.g., authentication failure, not found).
 * 
 * Used by retry logic to decide whether to attempt exponential backoff.
 * 
 * @param error QNetworkReply error code
 * @return true if error is likely temporary (network issues, timeouts)
 */
bool ErrorHandler::isTransientError(QNetworkReply::NetworkError error)
{
    switch (error) {
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::TimeoutError:
        case QNetworkReply::OperationCanceledError:
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::ProxyConnectionClosedError:
        case QNetworkReply::ProxyConnectionRefusedError:
        case QNetworkReply::ProxyNotFoundError:
        case QNetworkReply::ProxyTimeoutError:
        case QNetworkReply::ContentReSendError:
        case QNetworkReply::ProtocolUnknownError:
        case QNetworkReply::UnknownNetworkError:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Check if HTTP status code is a client error (4xx)
 * 
 * Client errors (400-499) indicate invalid requests that should not be retried.
 * 
 * @param statusCode HTTP status code
 * @return true if status is in 400-499 range
 */
bool ErrorHandler::isClientError(int statusCode)
{
    return statusCode >= 400 && statusCode < 500;
}

/**
 * @brief Convert network error to user-friendly message
 * 
 * Maps technical QNetworkReply errors to localized, human-readable strings
 * for display in error dialogs.
 * 
 * @param error QNetworkReply error code
 * @param httpStatusCode HTTP status code (currently unused, reserved for future)
 * @return Localized error message suitable for UI display
 */
QString ErrorHandler::mapErrorToUserMessage(QNetworkReply::NetworkError error, int httpStatusCode)
{
    Q_UNUSED(httpStatusCode);
    switch (error) {
        case QNetworkReply::AuthenticationRequiredError:
            return QObject::tr("Authentication failed. Please check your credentials.");
        case QNetworkReply::ContentNotFoundError:
            return QObject::tr("Requested content not found.");
        case QNetworkReply::TimeoutError:
            return QObject::tr("Request timed out. Please try again.");
        case QNetworkReply::HostNotFoundError:
            return QObject::tr("Server not found. Check your network connection.");
        default:
            return QObject::tr("Network error occurred. Please try again.");
    }
}

/**
 * @brief Calculate exponential backoff delay for retry attempts
 * 
 * Implements exponential backoff: delay = baseDelayMs * 2^attemptNumber
 * Example: baseDelayMs=1000 → 1s, 2s, 4s, 8s...
 * 
 * @param attemptNumber Current retry attempt (0-indexed)
 * @param policy RetryPolicy containing baseDelayMs
 * @return Delay in milliseconds before next retry
 */
int ErrorHandler::calculateBackoffDelay(int attemptNumber, const RetryPolicy &policy)
{
    return policy.baseDelayMs * static_cast<int>(qPow(2, attemptNumber));
}

/**
 * @brief Create a NetworkError from a failed QNetworkReply
 * 
 * Extracts error information from the reply and attempts to parse Jellyfin's
 * JSON error response for detailed error messages.
 * 
 * Jellyfin error responses typically contain:
 * - "Message": User-friendly error description
 * - "ErrorCode": Technical error identifier
 * 
 * Falls back to generic error messages if JSON parsing fails.
 * 
 * @param reply Failed QNetworkReply (must not be nullptr)
 * @param endpoint API endpoint that failed (for logging/debugging)
 * @return NetworkError struct with code, messages, and endpoint
 */
NetworkError ErrorHandler::createError(QNetworkReply *reply, const QString &endpoint)
{
    NetworkError error;
    if (!reply) return error;
    
    error.code = static_cast<int>(reply->error());
    error.endpoint = endpoint;
    
    // Try to parse error response for more details
    QByteArray data = reply->readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        QJsonObject obj = doc.object();
        error.userMessage = obj["Message"].toString();
        error.technicalDetails = obj["ErrorCode"].toString();
    }
    
    if (error.userMessage.isEmpty()) {
        error.userMessage = mapErrorToUserMessage(reply->error(), reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
    }
    
    return error;
}

// ============================================================================
// JsonParser Implementation
// ============================================================================

/**
 * @brief Determine if JSON parsing should be offloaded to a background thread
 * 
 * Large JSON responses (>250KB) can block the UI thread during parsing.
 * This heuristic decides when to use async parsing via QFuture/QtConcurrent.
 * 
 * Threshold chosen based on typical Jellyfin response sizes:
 * - Small: Single item details (~10KB)
 * - Medium: Library page (~50-100KB)
 * - Large: Full library scan (>250KB)
 * 
 * @param data Raw JSON byte array
 * @return true if data size exceeds async threshold (250KB)
 */
bool JsonParser::shouldParseAsync(const QByteArray &data)
{
    constexpr int kAsyncThresholdBytes = 250 * 1024; // 250KB
    return data.size() > kAsyncThresholdBytes;
}

/**
 * @brief Parse Jellyfin Items API response into structured result
 * 
 * Parses the standard Jellyfin Items list response format used by multiple endpoints:
 * - GET /Users/{userId}/Items (library items)
 * - GET /Shows/NextUp (next episodes)
 * - GET /Users/{userId}/Items/Resume (continue watching)
 * 
 * Expected JSON structure:
 * {
 *   "Items": [ {...}, {...} ],
 *   "TotalRecordCount": 123
 * }
 * 
 * The Items array contains full item objects (movies, episodes, series, etc.)
 * with fields like Id, Name, Type, ImageTags, UserData, etc.
 * 
 * @param data Raw JSON byte array from Jellyfin API
 * @param parentId Parent item ID (for context, e.g., series ID for episodes)
 * @return ParsedItemsResult with success flag, items array, and total count
 */
ParsedItemsResult JsonParser::parseItemsResponse(const QByteArray &data, const QString &parentId)
{
    ParsedItemsResult result;
    result.parentId = parentId;
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.success = false;
        return result;
    }
    
    QJsonObject obj = doc.object();
    result.items = obj["Items"].toArray();
    result.totalRecordCount = obj["TotalRecordCount"].toInt(result.items.size());
    result.success = true;
    return result;
}

