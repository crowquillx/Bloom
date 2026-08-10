#include "Types.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QtMath>
#include <mutex>
#include "../utils/BloomLogging.h"

/**
 * @brief Register Qt metatypes for network data structures
 * 
 * Registers custom types with Qt's meta-object system to enable:
 * - Signal/slot connections with these types
 * - Queued connections across threads
 * - QVariant conversions for QML exposure
 * 
 * Safe to call from multiple initialization paths and threads.
 */
void registerNetworkMetaTypes()
{
    static std::once_flag registrationFlag;
    std::call_once(registrationFlag, [] {
        qRegisterMetaType<MediaStreamInfo>("MediaStreamInfo");
        qRegisterMetaType<MediaSourceInfo>("MediaSourceInfo");
        qRegisterMetaType<PlaybackInfoResponse>("PlaybackInfoResponse");
        qRegisterMetaType<MediaSegmentInfo>("MediaSegmentInfo");
        qRegisterMetaType<TrickplayTileInfo>("TrickplayTileInfo");
        qRegisterMetaType<QList<MediaSegmentInfo>>("QList<MediaSegmentInfo>");
        qRegisterMetaType<TrickplayTileInfoMap>("QMap<int,TrickplayTileInfo>");
    });
}

// ============================================================================
// MediaStreamInfo Implementation
// ============================================================================

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
    streamMap["videoRangeType"] = videoRangeType;
    streamMap["codecTag"] = codecTag;
    streamMap["codecTagString"] = codecTagString;
    streamMap["codecId"] = codecId;
    streamMap["dolbyVisionProfile"] = dolbyVisionProfile;
    streamMap["dolbyVisionLevel"] = dolbyVisionLevel;
    streamMap["dolbyVisionBlSignalCompatibilityId"] = dolbyVisionBlSignalCompatibilityId;
    streamMap["videoDoViTitle"] = videoDoViTitle;
    return streamMap;
}

// ============================================================================
// MediaSourceInfo Implementation
// ============================================================================

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

QVariantList PlaybackInfoResponse::getMediaSourcesVariant() const
{
    QVariantList result;
    for (const auto &source : mediaSources) {
        QVariantMap sourceMap;
        sourceMap["id"] = source.id;
        sourceMap["name"] = source.name;
        sourceMap["path"] = source.path;
        sourceMap["directStreamUrl"] = source.directStreamUrl;
        sourceMap["transcodingUrl"] = source.transcodingUrl;
        sourceMap["container"] = source.container;
        sourceMap["size"] = source.size;
        sourceMap["bitRate"] = source.bitRate;
        sourceMap["videoType"] = source.videoType;
        sourceMap["durationMs"] = source.durationMs;
        sourceMap["defaultAudioStreamIndex"] = source.defaultAudioStreamIndex;
        sourceMap["defaultSubtitleStreamIndex"] = source.defaultSubtitleStreamIndex;
        sourceMap["playbackVariantId"] = source.playbackVariantId;
        sourceMap["presentationPartIndex"] = source.presentationPartIndex;
        sourceMap["presentationPartTotal"] = source.presentationPartTotal;
        sourceMap["mediaStreams"] = source.getMediaStreamsVariant();
        result.append(sourceMap);
    }
    return result;
}

// ============================================================================
// TrickplayTileInfo Implementation
// ============================================================================

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

bool ErrorHandler::isRetryableHttpStatus(int statusCode)
{
    switch (statusCode) {
    case 408:
    case 429:
    case 500:
    case 502:
    case 503:
    case 504:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Convert network error to user-friendly message
 * 
 * Maps technical QNetworkReply errors to localized, human-readable strings
 * for display in error dialogs.
 * 
 * @param error QNetworkReply error code
 * @param httpStatusCode HTTP status code reported by the server
 * @return Localized error message suitable for UI display
 */
QString ErrorHandler::mapErrorToUserMessage(QNetworkReply::NetworkError error, int httpStatusCode)
{
    if (httpStatusCode == 408 || httpStatusCode == 504) {
        return QObject::tr("Request timed out. Please try again.");
    }
    if (httpStatusCode == 429) {
        return QObject::tr("The server is busy. Please try again shortly.");
    }
    if (httpStatusCode >= 500) {
        return QObject::tr("The server is temporarily unavailable. Please try again.");
    }
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
    const qint64 maximum = qMax(0, policy.maxDelayMs);
    qint64 delay = qBound<qint64>(
        qint64{0}, static_cast<qint64>(policy.baseDelayMs), maximum);
    for (int exponent = 0; exponent < qMax(0, attemptNumber); ++exponent) {
        delay = qMin(maximum, delay * 2);
    }

    const double jitterRatio = qBound(0.0, policy.jitterRatio, 1.0);
    if (delay > 0 && jitterRatio > 0.0) {
        const double centered =
            (QRandomGenerator::global()->generateDouble() * 2.0) - 1.0;
        delay = qRound64(static_cast<double>(delay)
                         * (1.0 + centered * jitterRatio));
    }
    return static_cast<int>(
        qBound<qint64>(qint64{0}, delay, maximum));
}

int ErrorHandler::retryAfterDelayMs(const QNetworkReply *reply, int maxDelayMs)
{
    if (!reply) {
        return -1;
    }
    const QByteArray value = reply->rawHeader("Retry-After").trimmed();
    if (value.isEmpty()) {
        return -1;
    }

    bool secondsOk = false;
    const qint64 seconds = value.toLongLong(&secondsOk);
    const qint64 maximum = qMax(0, maxDelayMs);
    qint64 delayMs = -1;
    if (secondsOk && seconds >= 0) {
        delayMs = seconds > maximum / 1000
            ? maximum
            : seconds * 1000;
    } else {
        QString httpDate = QString::fromLatin1(value);
        if (httpDate.endsWith(QStringLiteral(" GMT"), Qt::CaseInsensitive)) {
            httpDate.chop(4);
            httpDate.append(QStringLiteral(" +0000"));
        }
        const QDateTime retryAt =
            QDateTime::fromString(httpDate, Qt::RFC2822Date);
        if (retryAt.isValid()) {
            delayMs = qMax<qint64>(
                0, QDateTime::currentDateTimeUtc().msecsTo(retryAt.toUTC()));
        }
    }

    if (delayMs < 0) {
        return -1;
    }
    return static_cast<int>(
        qMin<qint64>(delayMs, maximum));
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
    
    error.networkErrorCode = static_cast<int>(reply->error());
    error.httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    error.code = error.httpStatus > 0
        ? error.httpStatus
        : error.networkErrorCode;
    error.endpoint = endpoint;
    
    // Preserve a bounded envelope for provider-owned parsing. The legacy
    // Message/ErrorCode extraction remains for current Jellyfin-facing error
    // text while providers migrate that policy to their adapters.
    constexpr qsizetype kMaxErrorResponseBytes = 64 * 1024;
    error.responseBody = reply->read(kMaxErrorResponseBytes);
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(error.responseBody, &parseError);
    
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        QJsonObject obj = doc.object();
        error.userMessage = obj["Message"].toString();
        error.providerErrorCode = obj["ErrorCode"].toString();
        error.technicalDetails = error.providerErrorCode;
    }
    
    if (error.userMessage.isEmpty()) {
        error.userMessage =
            mapErrorToUserMessage(reply->error(), error.httpStatus);
    }
    
    return error;
}
