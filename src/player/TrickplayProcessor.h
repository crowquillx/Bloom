#pragma once

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QNetworkAccessManager>
#include <QString>
#include <QImage>
#include <QFile>
#include <QMap>
#include <QPointer>
#include "../network/PlaybackService.h" // For TrickplayTileInfo data struct

class AuthenticationService;
class PlaybackService;

/**
 * @class TrickplayProcessor
 * @brief Downloads server trickplay sheets and converts them to BGRA frames
 * for the native playback filmstrip.
 *
 * The binary file format is:
 * - Sequential frames, each frame is width * height * 4 bytes (BGRA)
 * - Frame N starts at offset: N * width * height * 4
 */
class TrickplayProcessor : public QObject
{
    Q_OBJECT

public:
    explicit TrickplayProcessor(AuthenticationService *authService,
                                PlaybackService *playbackService,
                                QObject *parent = nullptr);
    ~TrickplayProcessor();

    /**
     * @brief Start processing trickplay data for an item
     * @param itemId The provider item ID
     * @param info The trickplay tile information
     */
    void startProcessing(const QString &itemId, const TrickplayTileInfo &info);

    /**
     * @brief Clear any cached trickplay data and stop processing
     */
    void clear();

    /**
     * @brief Get the path to the processed binary file
     * @return Path to the binary file, or empty if not ready
     */
    QString getBinaryFilePath() const;

    /**
     * @brief Check if processing is complete
     * @return true if the binary file is ready
     */
    bool isReady() const;

signals:
    /**
     * @brief Emitted when trickplay processing is complete
     * @param itemId The item ID that was processed
     * @param count Number of thumbnails
     * @param intervalMs Interval between thumbnails in milliseconds
     * @param width Thumbnail width
     * @param height Thumbnail height
     * @param filePath Path to the binary file
     */
    void processingComplete(const QString &itemId, int count, int intervalMs,
                            int width, int height, const QString &filePath);

    /**
     * @brief Emitted if processing fails
     * @param itemId The item ID
     * @param error Error message
     */
    void processingFailed(const QString &itemId, const QString &error);

    /**
     * @brief Emitted when trickplay data is cleared
     */
    void cleared();

private slots:
    void onTileDownloaded(QNetworkReply *reply);

private:
    /**
     * @brief Process all downloaded tiles and write to binary file
     * @return true if successful
     */
    bool processAllTiles();

    /**
     * @brief Extract thumbnails from a single tile image and write to output
     * @param tileImage The tile image containing multiple thumbnails
     * @param tileIndex Index of this tile (for calculating which thumbnails)
     * @param outputFile The output file to write to
     * @param thumbnailsWritten Running count of thumbnails written
     * @return true if successful
     */
    bool processTileImage(const QImage &tileImage, int tileIndex, QFile &outputFile, int &thumbnailsWritten);

    /**
     * @brief Convert RGBA image data to BGRA format
     * @param image The image in RGBA format
     * @return Image data in BGRA format
     */
    static QByteArray convertToBGRA(const QImage &image);

    /**
     * @brief Get the cache directory for trickplay data
     * @return Path to the cache directory
     */
    static QString getCacheDir();

    // Network
    QNetworkAccessManager *m_nam; // Not owned; provided by AuthenticationService
    AuthenticationService *m_authService; // Not owned
    PlaybackService *m_playbackService;   // Not owned
    
    // Current processing state
    QString m_currentItemId;
    TrickplayTileInfo m_trickplayInfo;
    
    // Downloaded tile data
    QMap<int, QByteArray> m_downloadedTiles;
    QList<QPointer<QNetworkReply>> m_replies;
    quint64 m_generation = 0;
    int m_totalTiles;
    int m_tilesDownloaded;
    
    // Output
    QString m_binaryFilePath;
    bool m_isReady;
    bool m_isProcessing;
    
    // Thread safety
    mutable QMutex m_mutex;
};
