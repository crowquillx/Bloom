#include "LibraryService.h"
#include "AuthenticationService.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(libraryService, "bloom.library")

LibraryService::LibraryService(AuthenticationService *authService, QObject *parent)
    : QObject(parent)
    , m_authService(authService)
    , m_retryPolicy{3, 1000, true}
{
}

// ============================================================================
// Request Helpers
// ============================================================================

void LibraryService::sendRequestWithRetry(const QString &endpoint,
                                           RequestFactory requestFactory,
                                           ResponseHandler responseHandler,
                                           int attemptNumber)
{
    qCDebug(libraryService) << "Sending request to:" << endpoint 
                            << "attempt:" << (attemptNumber + 1) << "/" << m_retryPolicy.maxRetries;
    
    QNetworkReply *reply = requestFactory();
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, endpoint, requestFactory, responseHandler, attemptNumber]() {
        handleReplyWithRetry(reply, endpoint, requestFactory, responseHandler, attemptNumber);
    });
}

void LibraryService::handleReplyWithRetry(QNetworkReply *reply,
                                           const QString &endpoint,
                                           RequestFactory requestFactory,
                                           ResponseHandler responseHandler,
                                           int attemptNumber)
{
    reply->deleteLater();
    
    if (reply->error() == QNetworkReply::NoError) {
        qCDebug(libraryService) << "Request succeeded:" << endpoint;
        responseHandler(reply);
        return;
    }
    
    // Get HTTP status code
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    
    // Check for 401 Unauthorized - session expired
    if (httpStatus == 401) {
        qCWarning(libraryService) << "Session expired (401) for endpoint:" << endpoint;
        return;
    }
    
    // Create structured error
    NetworkError netError = ErrorHandler::createError(reply, endpoint);
    
    qCWarning(libraryService) << "Request failed:" << endpoint
                              << "Error:" << reply->error()
                              << "HTTP Status:" << httpStatus
                              << "Attempt:" << (attemptNumber + 1);
    
    // Check if we should retry
    bool shouldRetry = m_retryPolicy.retryOnTransient 
                       && ErrorHandler::isTransientError(reply->error())
                       && !ErrorHandler::isClientError(httpStatus)
                       && attemptNumber < m_retryPolicy.maxRetries - 1;
    
    if (shouldRetry) {
        int delayMs = ErrorHandler::calculateBackoffDelay(attemptNumber, m_retryPolicy);
        qCInfo(libraryService) << "Retrying request to:" << endpoint << "in" << delayMs << "ms";
        
        QTimer::singleShot(delayMs, this, [this, endpoint, requestFactory, responseHandler, attemptNumber]() {
            sendRequestWithRetry(endpoint, requestFactory, responseHandler, attemptNumber + 1);
        });
    } else {
        emitError(netError);
    }
}

void LibraryService::emitError(const NetworkError &error)
{
    qCWarning(libraryService) << "Emitting error for endpoint:" << error.endpoint
                              << "User message:" << error.userMessage;
    emit errorOccurred(error.endpoint, error.userMessage);
    emit networkError(error);
}

// ============================================================================
// Library Views
// ============================================================================

void LibraryService::getViews()
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getViews";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        return;
    }
    
    QString endpoint = QString("/Users/%1/Views").arg(m_authService->getUserId());
    
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            
            if (JsonParser::shouldParseAsync(data)) {
                emit parsingStarted("views");
                
                auto *watcher = new QFutureWatcher<ParsedItemsResult>(this);
                connect(watcher, &QFutureWatcher<ParsedItemsResult>::finished, this, [this, watcher]() {
                    ParsedItemsResult result = watcher->result();
                    watcher->deleteLater();
                    
                    emit parsingFinished("views");
                    
                    if (result.success) {
                        emit viewsLoaded(result.items);
                    } else {
                        NetworkError error;
                        error.endpoint = "getViews";
                        error.code = -2;
                        error.userMessage = tr("Failed to parse server response");
                        emitError(error);
                    }
                });
                
                QFuture<ParsedItemsResult> future = QtConcurrent::run([data]() {
                    return JsonParser::parseItemsResponse(data, QString());
                });
                watcher->setFuture(future);
            } else {
                QJsonDocument doc = QJsonDocument::fromJson(data);
                QJsonObject obj = doc.object();
                QJsonArray items = obj["Items"].toArray();
                emit viewsLoaded(items);
            }
        });
}

// ============================================================================
// Items with Pagination
// ============================================================================

void LibraryService::getItems(const QString &parentId, int startIndex, int limit,
                               const QStringList &genres, const QStringList &networks,
                               const QString &sortBy, const QString &sortOrder,
                               bool includeHeavyFields, bool useCacheValidation)
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getItems";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        return;
    }
    
    QStringList fields = {
        // ordering keeps common small fields first
        "Type",
        "ParentIndexNumber",
        "IndexNumber",
        "LocationType",  // used to filter virtual/missing episodes
        "ImageTags",
        "BackdropImageTags",
        "ParentBackdropImageTags",
        "ParentBackdropImageItemId",
        "ParentBackdropItemId",
        "ParentPrimaryImageTag",
        "SeriesPrimaryImageTag",
        "ProductionYear",
        "PremiereDate",
        "ChildCount",
        "ParentId",
        "SeriesId",
        "UserData",
        "RunTimeTicks",
        "Overview",
        "CommunityRating",
        "SpecialEpisodeNumbers",
        // Special placement fields (needed for specials ordering)
        "AirsBeforeSeasonNumber",
        "AirsAfterSeasonNumber",
        "AirsBeforeEpisodeNumber"
    };

    if (includeHeavyFields) {
        fields.prepend("Path");
        fields.prepend("MediaSources");
        fields.append("Genres");
    }

    QString endpoint = QString("/Users/%1/Items?ParentId=%2&Fields=%3&EnableImageTypes=Primary,Backdrop,Thumb")
                           .arg(m_authService->getUserId(), parentId, fields.join(","));
    
    if (startIndex > 0) endpoint += QString("&StartIndex=%1").arg(startIndex);
    if (limit > 0) endpoint += QString("&Limit=%1").arg(limit);
    if (!genres.isEmpty()) endpoint += QString("&Genres=%1").arg(genres.join("|"));
    if (!networks.isEmpty()) endpoint += QString("&Networks=%1").arg(networks.join("|"));
    
    QString sortByParam = sortBy.isEmpty() ? "ParentIndexNumber,IndexNumber,SortName" : sortBy;
    endpoint += QString("&SortBy=%1").arg(sortByParam);
    if (!sortOrder.isEmpty()) endpoint += QString("&SortOrder=%1").arg(sortOrder);
    
    sendRequestWithRetry(endpoint,
        [this, endpoint, useCacheValidation, parentId]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            if (useCacheValidation) {
                if (m_etags.contains(endpoint)) {
                    request.setRawHeader("If-None-Match", m_etags.value(endpoint).toUtf8());
                }
                if (m_lastModified.contains(endpoint)) {
                    request.setRawHeader("If-Modified-Since", m_lastModified.value(endpoint).toUtf8());
                }
            }
            return m_authService->networkManager()->get(request);
        },
        [this, parentId, endpoint, useCacheValidation](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (httpStatus == 304 && useCacheValidation) {
                emit itemsNotModified(parentId);
                return;
            }

            if (useCacheValidation) {
                QByteArray etag = reply->rawHeader("ETag");
                if (!etag.isEmpty()) {
                    m_etags[endpoint] = QString::fromUtf8(etag);
                }
                QByteArray lastMod = reply->rawHeader("Last-Modified");
                if (!lastMod.isEmpty()) {
                    m_lastModified[endpoint] = QString::fromUtf8(lastMod);
                }
            }
            
            if (JsonParser::shouldParseAsync(data)) {
                emit parsingStarted("library");
                
                auto *watcher = new QFutureWatcher<ParsedItemsResult>(this);
                connect(watcher, &QFutureWatcher<ParsedItemsResult>::finished, this, [this, watcher]() {
                    ParsedItemsResult result = watcher->result();
                    watcher->deleteLater();
                    emit parsingFinished("library");
                    
                    if (result.success) {
                        emit itemsLoaded(result.parentId, result.items);
                        emit itemsLoadedWithTotal(result.parentId, result.items, result.totalRecordCount);
                    } else {
                        NetworkError error;
                        error.endpoint = "getItems";
                        error.code = -2;
                        error.userMessage = tr("Failed to parse library data");
                        emitError(error);
                    }
                });
                
                QFuture<ParsedItemsResult> future = QtConcurrent::run([data, parentId]() {
                    return JsonParser::parseItemsResponse(data, parentId);
                });
                watcher->setFuture(future);
            } else {
                QJsonDocument doc = QJsonDocument::fromJson(data);
                QJsonObject obj = doc.object();
                QJsonArray items = obj["Items"].toArray();
                int totalRecordCount = obj["TotalRecordCount"].toInt();
                emit itemsLoaded(parentId, items);
                emit itemsLoadedWithTotal(parentId, items, totalRecordCount);
            }
        });
}

// ============================================================================
// Next Up & Latest Media
// ============================================================================

void LibraryService::getNextUp()
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getNextUp";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        return;
    }
    
    QString endpoint = QString("/Shows/NextUp?UserId=%1&Limit=10&Fields=Path,Overview,ImageTags,ParentId,SeriesId,SeriesPrimaryImageTag,SeriesThumbImageTag,ParentThumbImageTag,ParentPrimaryImageTag,BackdropImageTags,ParentBackdropImageTags,ParentBackdropItemId,UserData,RunTimeTicks&EnableImageTypes=Primary,Thumb,Backdrop")
        .arg(m_authService->getUserId());
    
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonObject obj = doc.object();
            QJsonArray items = obj["Items"].toArray();
            emit nextUpLoaded(items);
        });
}

void LibraryService::getLatestMedia(const QString &parentId)
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getLatestMedia";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        return;
    }
    
    QString endpoint = QString("/Users/%1/Items/Latest?ParentId=%2&Limit=10&Fields=Path,Overview,ImageTags,ParentId,SeriesId,SeriesPrimaryImageTag,ParentPrimaryImageTag,BackdropImageTags,ParentBackdropImageTags,ParentBackdropItemId,ProductionYear,Status,EndDate,ParentIndexNumber,IndexNumber,UserData")
        .arg(m_authService->getUserId(), parentId);
    
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, parentId](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            QJsonArray items = QJsonDocument::fromJson(data).array();
            emit latestMediaLoaded(parentId, items);
        });
}

// ============================================================================
// Series Details
// ============================================================================

void LibraryService::getSeriesDetails(const QString &seriesId)
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getSeriesDetails";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        return;
    }
    
    const QStringList fields = {
        "Overview", "ImageTags", "BackdropImageTags", "ParentBackdropImageTags",
        "Genres", "Studios", "People", "ChildCount", "UserData",
        "ProductionYear", "PremiereDate", "EndDate"
    };
    
    QString endpoint = QString("/Users/%1/Items/%2?Fields=%3")
        .arg(m_authService->getUserId(), seriesId, fields.join(","));
    
    sendRequestWithRetry(endpoint,
        [this, endpoint, seriesId]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            if (m_etags.contains(endpoint)) {
                request.setRawHeader("If-None-Match", m_etags.value(endpoint).toUtf8());
            }
            if (m_lastModified.contains(endpoint)) {
                request.setRawHeader("If-Modified-Since", m_lastModified.value(endpoint).toUtf8());
            }
            return m_authService->networkManager()->get(request);
        },
        [this, seriesId, endpoint](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (httpStatus == 304) {
                emit seriesDetailsNotModified(seriesId);
                return;
            }

            QByteArray etag = reply->rawHeader("ETag");
            if (!etag.isEmpty()) {
                m_etags[endpoint] = QString::fromUtf8(etag);
            }
            QByteArray lastMod = reply->rawHeader("Last-Modified");
            if (!lastMod.isEmpty()) {
                m_lastModified[endpoint] = QString::fromUtf8(lastMod);
            }

            QJsonDocument doc = QJsonDocument::fromJson(data);
            emit seriesDetailsLoaded(seriesId, doc.object());
        });
}

void LibraryService::getNextUnplayedEpisode(const QString &seriesId)
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getNextUnplayedEpisode";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        return;
    }
    
    QString endpoint = QString("/Shows/%1/Episodes?UserId=%2&Fields=Overview,UserData,RunTimeTicks&IsPlayed=false&Limit=1&SortBy=ParentIndexNumber,IndexNumber")
        .arg(seriesId, m_authService->getUserId());
    
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, seriesId](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonArray items = doc.object()["Items"].toArray();
            
            if (!items.isEmpty()) {
                emit nextUnplayedEpisodeLoaded(seriesId, items.first().toObject());
            } else {
                emit nextUnplayedEpisodeLoaded(seriesId, QJsonObject());
            }
        });
}

void LibraryService::markSeriesWatched(const QString &seriesId)
{
    if (!m_authService->isAuthenticated()) return;
    
    QString endpoint = QString("/Users/%1/PlayedItems/%2").arg(m_authService->getUserId(), seriesId);
    QNetworkRequest request = m_authService->createRequest(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply *reply = m_authService->networkManager()->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, seriesId]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            emit seriesWatchedStatusChanged(seriesId);
        }
    });
}

void LibraryService::markSeriesUnwatched(const QString &seriesId)
{
    if (!m_authService->isAuthenticated()) return;
    
    QString endpoint = QString("/Users/%1/PlayedItems/%2").arg(m_authService->getUserId(), seriesId);
    QNetworkRequest request = m_authService->createRequest(endpoint);
    
    QNetworkReply *reply = m_authService->networkManager()->deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, seriesId]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            emit seriesWatchedStatusChanged(seriesId);
        }
    });
}

void LibraryService::markItemPlayed(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) return;
    
    QString endpoint = QString("/Users/%1/PlayedItems/%2").arg(m_authService->getUserId(), itemId);
    QNetworkRequest request = m_authService->createRequest(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply *reply = m_authService->networkManager()->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            emit itemPlayedStatusChanged(itemId, true);
        }
    });
}

void LibraryService::markItemUnplayed(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) return;
    
    QString endpoint = QString("/Users/%1/PlayedItems/%2").arg(m_authService->getUserId(), itemId);
    QNetworkRequest request = m_authService->createRequest(endpoint);
    
    QNetworkReply *reply = m_authService->networkManager()->deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            emit itemPlayedStatusChanged(itemId, false);
        }
    });
}

void LibraryService::markItemFavorite(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) return;
    
    QString endpoint = QString("/Users/%1/FavoriteItems/%2").arg(m_authService->getUserId(), itemId);
    QNetworkRequest request = m_authService->createRequest(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply *reply = m_authService->networkManager()->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            emit favoriteStatusChanged(itemId, true);
        }
    });
}

void LibraryService::markItemUnfavorite(const QString &itemId)
{
    if (!m_authService->isAuthenticated()) return;
    
    QString endpoint = QString("/Users/%1/FavoriteItems/%2").arg(m_authService->getUserId(), itemId);
    QNetworkRequest request = m_authService->createRequest(endpoint);
    
    QNetworkReply *reply = m_authService->networkManager()->deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            emit favoriteStatusChanged(itemId, false);
        }
    });
}

void LibraryService::toggleFavorite(const QString &itemId, bool isFavorite)
{
    if (isFavorite) {
        markItemFavorite(itemId);
    } else {
        markItemUnfavorite(itemId);
    }
}

void LibraryService::getThemeSongs(const QString &seriesId)
{
    if (!m_authService->isAuthenticated()) return;
    
    QString endpoint = QString("/Items/%1/ThemeSongs?UserId=%2").arg(seriesId, m_authService->getUserId());
    
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, seriesId](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonArray items = doc.object()["Items"].toArray();
            
            QStringList urls;
            for (const auto &item : items) {
                QString itemId = item.toObject()["Id"].toString();
                if (!itemId.isEmpty()) {
                    urls.append(getStreamUrl(itemId));
                }
            }
            emit themeSongsLoaded(seriesId, urls);
        });
}

// ============================================================================
// Search
// ============================================================================

void LibraryService::search(const QString &searchTerm, int limit)
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "search";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        return;
    }
    
    if (searchTerm.trimmed().isEmpty()) {
        emit searchResultsLoaded(searchTerm, QJsonArray(), QJsonArray());
        return;
    }
    
    const QStringList fields = {"Path", "Overview", "ImageTags", "BackdropImageTags", "ProductionYear", "CommunityRating", "UserData"};
    
    QString endpoint = QString("/Users/%1/Items?SearchTerm=%2&IncludeItemTypes=Movie,Series&Recursive=true&Fields=%3&Limit=%4&EnableImageTypes=Primary,Backdrop")
                           .arg(m_authService->getUserId())
                           .arg(QString::fromUtf8(QUrl::toPercentEncoding(searchTerm.trimmed())))
                           .arg(fields.join(","))
                           .arg(limit);
    
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this, searchTerm](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonArray allItems = doc.object()["Items"].toArray();
            
            QJsonArray movies, series;
            for (const QJsonValue &itemVal : allItems) {
                QJsonObject item = itemVal.toObject();
                QString type = item["Type"].toString();
                if (type == "Movie") movies.append(item);
                else if (type == "Series") series.append(item);
            }
            
            emit searchResultsLoaded(searchTerm, movies, series);
        });
}

void LibraryService::getRandomItems(int limit)
{
    if (!m_authService->isAuthenticated()) {
        NetworkError error;
        error.endpoint = "getRandomItems";
        error.code = -1;
        error.userMessage = tr("Not authenticated");
        emitError(error);
        return;
    }
    
    const QStringList fields = {"Overview", "ImageTags", "BackdropImageTags", "ProductionYear"};
    
    QString endpoint = QString("/Users/%1/Items?IncludeItemTypes=Movie,Series&Recursive=true&SortBy=Random&Limit=%2&Fields=%3")
                           .arg(m_authService->getUserId())
                           .arg(limit)
                           .arg(fields.join(","));
    
    sendRequestWithRetry(endpoint,
        [this, endpoint]() {
            QNetworkRequest request = m_authService->createRequest(endpoint);
            return m_authService->networkManager()->get(request);
        },
        [this](QNetworkReply *reply) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            emit randomItemsLoaded(doc.object()["Items"].toArray());
        });
}

// ============================================================================
// URL Helpers
// ============================================================================

QString LibraryService::getStreamUrl(const QString &itemId)
{
    return QString("%1/Videos/%2/stream?Container=mp4,mkv&Static=true&api_key=%3")
        .arg(m_authService->getServerUrl(), itemId, m_authService->getAccessToken());
}

QString LibraryService::getStreamUrlWithTracks(const QString &itemId, const QString &mediaSourceId,
                                                int audioStreamIndex, int subtitleStreamIndex)
{
    QString url = QString("%1/Videos/%2/stream?Static=true&api_key=%3")
        .arg(m_authService->getServerUrl(), itemId, m_authService->getAccessToken());
    
    if (!mediaSourceId.isEmpty()) {
        url += QString("&MediaSourceId=%1").arg(mediaSourceId);
    }
    if (audioStreamIndex >= 0) {
        url += QString("&AudioStreamIndex=%1").arg(audioStreamIndex);
    }
    if (subtitleStreamIndex >= 0) {
        url += QString("&SubtitleStreamIndex=%1").arg(subtitleStreamIndex);
    }
    
    return url;
}

QString LibraryService::getImageUrl(const QString &itemId, const QString &imageType)
{
    return QString("%1/Items/%2/Images/%3?quality=90&fillWidth=400&api_key=%4")
        .arg(m_authService->getServerUrl(), itemId, imageType, m_authService->getAccessToken());
}

QString LibraryService::getImageUrlWithWidth(const QString &itemId, const QString &imageType, int width)
{
    if (width <= 0) width = 1920;
    return QString("%1/Items/%2/Images/%3?quality=95&fillWidth=%4&api_key=%5")
        .arg(m_authService->getServerUrl(), itemId, imageType, QString::number(width), m_authService->getAccessToken());
}

QString LibraryService::getCachedImageUrl(const QString &itemId, const QString &imageType)
{
    QString originalUrl = getImageUrl(itemId, imageType);
    return QString("image://cached/%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(originalUrl)));
}

QString LibraryService::getCachedImageUrlWithWidth(const QString &itemId, const QString &imageType, int width)
{
    QString originalUrl = getImageUrlWithWidth(itemId, imageType, width);
    return QString("image://cached/%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(originalUrl)));
}
