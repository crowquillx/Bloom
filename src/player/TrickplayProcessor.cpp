#include "TrickplayProcessor.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QImage>
#include <QBuffer>
#include <QFileInfo>
#if __has_include(<span>)
#include <span>
#endif
#include "../network/AuthenticationService.h"
#include "../network/PlaybackService.h"

TrickplayProcessor::TrickplayProcessor(AuthenticationService *authService,
                                       PlaybackService *playbackService,
                                       QObject *parent)
    : QObject(parent)
    , m_nam(authService ? authService->networkManager() : nullptr)
    , m_authService(authService)
    , m_playbackService(playbackService)
    , m_totalTiles(0)
    , m_tilesDownloaded(0)
    , m_isReady(false)
    , m_isProcessing(false)
{
    qDebug() << "TrickplayProcessor: Initialized";
}

TrickplayProcessor::~TrickplayProcessor()
{
    clear();
}

void TrickplayProcessor::startProcessing(const QString &itemId, const TrickplayTileInfo &info)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_authService || !m_playbackService || !m_nam) {
        qWarning() << "TrickplayProcessor: Missing services, cannot process trickplay";
        emit processingFailed(itemId, "Missing playback/auth services");
        return;
    }
    
    // If already processing the same item, skip
    if (m_isProcessing && m_currentItemId == itemId) {
        qDebug() << "TrickplayProcessor: Already processing item" << itemId;
        return;
    }
    
    // Clear any previous data
    locker.unlock();
    clear();
    locker.relock();
    
    m_currentItemId = itemId;
    m_trickplayInfo = info;
    m_isProcessing = true;
    m_isReady = false;
    
    // Calculate number of tiles
    int thumbnailsPerTile = info.tileWidth * info.tileHeight;
    if (thumbnailsPerTile <= 0) {
        qWarning() << "TrickplayProcessor: Invalid tile dimensions";
        m_isProcessing = false;
        emit processingFailed(itemId, "Invalid tile dimensions");
        return;
    }
    
    m_totalTiles = (info.thumbnailCount + thumbnailsPerTile - 1) / thumbnailsPerTile;
    m_tilesDownloaded = 0;
    m_downloadedTiles.clear();
    
    qDebug() << "TrickplayProcessor: Starting processing for item" << itemId
             << "- thumbnails:" << info.thumbnailCount
             << "tiles:" << m_totalTiles
             << "tile size:" << info.tileWidth << "x" << info.tileHeight
             << "thumb size:" << info.width << "x" << info.height
             << "interval:" << info.interval << "ms";
    
    // Ensure cache directory exists
    QString cacheDir = getCacheDir();
    QDir().mkpath(cacheDir);
    
    // Set up output file path
    m_binaryFilePath = QString("%1/%2.bin").arg(cacheDir, itemId);
    
    // Store copies of data needed for async operations
    QString currentItemId = m_currentItemId;
    int width = m_trickplayInfo.width;
    
    locker.unlock();
    
    // Download all tiles
    for (int i = 0; i < m_totalTiles; ++i) {
        QString tileUrl = m_playbackService->getTrickplayTileUrl(currentItemId, width, i);
        QNetworkRequest request(tileUrl);
        request.setAttribute(QNetworkRequest::User, i);  // Store tile index
        
        QNetworkReply *reply = m_nam->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            onTileDownloaded(reply);
        });
    }
}

void TrickplayProcessor::clear()
{
    QMutexLocker locker(&m_mutex);
    
    m_currentItemId.clear();
    m_downloadedTiles.clear();
    m_totalTiles = 0;
    m_tilesDownloaded = 0;
    m_isReady = false;
    m_isProcessing = false;
    
    // Remove the binary file if it exists
    if (!m_binaryFilePath.isEmpty() && QFile::exists(m_binaryFilePath)) {
        QFile::remove(m_binaryFilePath);
        qDebug() << "TrickplayProcessor: Removed binary file" << m_binaryFilePath;
    }
    m_binaryFilePath.clear();
    
    locker.unlock();
    emit cleared();
}

QString TrickplayProcessor::getBinaryFilePath() const
{
    QMutexLocker locker(&m_mutex);
    return m_isReady ? m_binaryFilePath : QString();
}

bool TrickplayProcessor::isReady() const
{
    QMutexLocker locker(&m_mutex);
    return m_isReady;
}

void TrickplayProcessor::onTileDownloaded(QNetworkReply *reply)
{
    reply->deleteLater();
    
    int tileIndex = reply->request().attribute(QNetworkRequest::User).toInt();
    
    QMutexLocker locker(&m_mutex);
    
    // Check if we've been cleared while downloading
    if (!m_isProcessing || m_currentItemId.isEmpty()) {
        return;
    }
    
    QString itemId = m_currentItemId;
    
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "TrickplayProcessor: Failed to download tile" << tileIndex
                   << "for item" << itemId << ":" << reply->errorString();
        m_isProcessing = false;
        locker.unlock();
        emit processingFailed(itemId, QString("Failed to download tile %1: %2")
                              .arg(tileIndex).arg(reply->errorString()));
        return;
    }
    
    // Store the downloaded tile data
    m_downloadedTiles[tileIndex] = reply->readAll();
    m_tilesDownloaded++;
    
    qDebug() << "TrickplayProcessor: Downloaded tile" << tileIndex 
             << "(" << m_tilesDownloaded << "/" << m_totalTiles << ")";
    
    // Check if all tiles are downloaded
    if (m_tilesDownloaded >= m_totalTiles) {
        locker.unlock();
        
        // Process all tiles
        if (processAllTiles()) {
            QMutexLocker locker2(&m_mutex);
            m_isReady = true;
            m_isProcessing = false;
            
            emit processingComplete(itemId, m_trickplayInfo.thumbnailCount,
                                    m_trickplayInfo.interval,
                                    m_trickplayInfo.width, m_trickplayInfo.height,
                                    m_binaryFilePath);
        }
    }
}

bool TrickplayProcessor::processAllTiles()
{
    // Copy data under lock to avoid holding lock during file I/O
    QString itemId;
    QString filePath;
    TrickplayTileInfo info;
    QMap<int, QByteArray> tiles;
    int totalTilesExpected;
    
    {
        QMutexLocker locker(&m_mutex);
        itemId = m_currentItemId;
        filePath = m_binaryFilePath;
        info = m_trickplayInfo;
        tiles = m_downloadedTiles;
        totalTilesExpected = m_totalTiles;
    }
    
    qDebug() << "TrickplayProcessor: Processing" << tiles.size() << "tiles into binary file";
    
    QFile outputFile(filePath);
    if (!outputFile.open(QIODevice::WriteOnly)) {
        qWarning() << "TrickplayProcessor: Failed to open output file" << filePath;
        emit processingFailed(itemId, "Failed to create output file");
        return false;
    }
    
    int thumbnailsWritten = 0;
    
    // Process tiles in order - iterate by expected tile index to ensure sequential order
    for (int i = 0; i < totalTilesExpected; ++i) {
        if (!tiles.contains(i)) {
            qWarning() << "TrickplayProcessor: Missing tile" << i;
            outputFile.close();
            QFile::remove(filePath);
            emit processingFailed(itemId, QString("Missing tile %1").arg(i));
            return false;
        }
        
        // Load tile image
        QImage tileImage;
        if (!tileImage.loadFromData(tiles[i])) {
            qWarning() << "TrickplayProcessor: Failed to decode tile" << i;
            outputFile.close();
            QFile::remove(filePath);
            emit processingFailed(itemId, QString("Failed to decode tile %1").arg(i));
            return false;
        }
        
        // Process this tile
        if (!processTileImage(tileImage, i, outputFile, thumbnailsWritten)) {
            outputFile.close();
            QFile::remove(filePath);
            emit processingFailed(itemId, QString("Failed to process tile %1").arg(i));
            return false;
        }
    }
    
    outputFile.close();
    
    qDebug() << "TrickplayProcessor: Successfully wrote" << thumbnailsWritten 
             << "thumbnails to" << filePath
             << "(" << QFileInfo(filePath).size() << "bytes)";
    
    // Clean up downloaded tiles to free memory
    {
        QMutexLocker locker(&m_mutex);
        m_downloadedTiles.clear();
    }
    
    return true;
}

bool TrickplayProcessor::processTileImage(const QImage &tileImage, int tileIndex, 
                                           QFile &outputFile, int &thumbnailsWritten)
{
    // Validate tile image dimensions
    int expectedWidth = m_trickplayInfo.width * m_trickplayInfo.tileWidth;
    int expectedHeight = m_trickplayInfo.height * m_trickplayInfo.tileHeight;
    
    if (tileImage.width() != expectedWidth || tileImage.height() != expectedHeight) {
        qWarning() << "TrickplayProcessor: Tile" << tileIndex 
                   << "has unexpected dimensions:" << tileImage.size()
                   << "expected:" << expectedWidth << "x" << expectedHeight;
        // Continue anyway - some tiles might be smaller if they're the last one
    }
    
    // Convert entire tile to RGBA32 format for consistent processing
    QImage rgbaImage = tileImage.convertToFormat(QImage::Format_RGBA8888);
    
    // Convert RGBA to BGRA (swap R and B channels)
    // We do this on the entire tile first, then extract thumbnails
    QByteArray bgraData = convertToBGRA(rgbaImage);
    
    int thumbWidth = m_trickplayInfo.width;
    int thumbHeight = m_trickplayInfo.height;
    int tileWidthThumbs = m_trickplayInfo.tileWidth;
    int tileHeightThumbs = m_trickplayInfo.tileHeight;
    int totalThumbnails = m_trickplayInfo.thumbnailCount;
    
    // Row stride of the tile in BGRA (4 bytes per pixel)
    int tileStride = rgbaImage.width() * 4;
    
    // Extract thumbnails in row-major order (left-to-right, top-to-bottom)
    for (int y = 0; y < tileHeightThumbs; ++y) {
        for (int x = 0; x < tileWidthThumbs; ++x) {
            // Check if we've written all thumbnails
            if (thumbnailsWritten >= totalThumbnails) {
                return true;
            }
            
            // Calculate top-left corner of thumbnail in source image
            int thumbTop = y * thumbHeight;
            int thumbLeft = x * thumbWidth;
            // Check if the entire thumbnail is within bounds
            if (thumbTop + thumbHeight > rgbaImage.height() || thumbLeft + thumbWidth > rgbaImage.width()) {
                qWarning() << "TrickplayProcessor: Thumbnail" << thumbnailsWritten 
                           << "is out of bounds, writing blank thumbnail";
                QByteArray blankThumb(thumbWidth * thumbHeight * 4, 0);
                outputFile.write(blankThumb);
                thumbnailsWritten++;
                continue;
            }
            // Extract this thumbnail's data row by row
            for (int row = 0; row < thumbHeight; ++row) {
                // Calculate position in tile image
                // The tile's BGRA data is stored row by row
                int srcY = thumbTop + row;
                int srcX = thumbLeft;
                // Calculate byte offset in BGRA data
                int byteOffset = srcY * tileStride + srcX * 4;
                int rowBytes = thumbWidth * 4;
                if (byteOffset + rowBytes <= bgraData.size()) {
                    outputFile.write(bgraData.mid(byteOffset, rowBytes));
                } else {
                    // Write zeros for out-of-bounds data
                    outputFile.write(QByteArray(rowBytes, 0));
                }
            }
            
            thumbnailsWritten++;
        }
    }
    
    return true;
}

QByteArray TrickplayProcessor::convertToBGRA(const QImage &image)
{
    // Ensure image is in RGBA8888 format
    QImage rgbaImage = image.format() == QImage::Format_RGBA8888 
                       ? image 
                       : image.convertToFormat(QImage::Format_RGBA8888);
    
#if defined(__cpp_lib_span)
    const auto byteCount = rgbaImage.sizeInBytes();
    const auto pixelCount = static_cast<size_t>(byteCount / 4);

    std::span<const uint32_t> src{
        reinterpret_cast<const uint32_t*>(rgbaImage.constBits()),
        pixelCount
    };

    QByteArray bgra(byteCount, Qt::Uninitialized);
    std::span<uint32_t> dst{
        reinterpret_cast<uint32_t*>(bgra.data()),
        pixelCount
    };

    for (size_t i = 0; i < src.size(); ++i) {
        uint32_t pixel = src[i];
        // RGBA8888: [R][G][B][A] (little endian: A B G R)
        // BGRA8888: [B][G][R][A] (little endian: A R G B)
        dst[i] = (pixel & 0xFF00FF00) | ((pixel & 0x000000FF) << 16) | ((pixel & 0x00FF0000) >> 16);
    }
    return bgra;
#else
    // Fallback for toolchains without std::span (should not be used under C++23)
    const uchar *data = rgbaImage.constBits();
    const int size = rgbaImage.sizeInBytes();

    QByteArray bgra(size, Qt::Uninitialized);
    auto *dst = reinterpret_cast<uint32_t *>(bgra.data());
    const auto *src = reinterpret_cast<const uint32_t *>(data);
    const int numPixels = size / 4;
    for (int i = 0; i < numPixels; ++i) {
        const uint32_t pixel = src[i];
        dst[i] = (pixel & 0xFF00FF00) | ((pixel & 0x000000FF) << 16) | ((pixel & 0x00FF0000) >> 16);
    }
    return bgra;
#endif
}

QString TrickplayProcessor::getCacheDir()
{
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return cacheDir + "/trickplay";
}
