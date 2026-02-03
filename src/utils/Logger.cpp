#include "Logger.h"
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QCoreApplication>
#include <cstdio>

// Constants for default values
static constexpr int DEFAULT_MAX_FILE_SIZE_MB = 5;
static constexpr int DEFAULT_MAX_ROTATED_FILES = 5;
static constexpr int DEFAULT_MAX_LOG_AGE_DAYS = 7;

Logger::Logger()
    : m_initialized(false)
    , m_minLogLevel(LogLevel::Info)
    , m_consoleOutputEnabled(false)
    , m_maxFileSizeMB(DEFAULT_MAX_FILE_SIZE_MB)
    , m_maxRotatedFiles(DEFAULT_MAX_ROTATED_FILES)
    , m_maxLogAgeDays(DEFAULT_MAX_LOG_AGE_DAYS)
    , m_currentFileSize(0)
{
}

Logger::~Logger()
{
    shutdown();
}

Logger& Logger::instance()
{
    static Logger instance;
    return instance;
}

bool Logger::initialize(const QString &logFileName)
{
    QMutexLocker locker(&m_mutex);

    if (m_initialized) {
        qWarning() << "Logger already initialized";
        return false;
    }

    m_logFileName = logFileName;

    // Ensure log directory exists
    if (!ensureLogDirectoryExists()) {
        qWarning() << "Failed to create log directory";
        return false;
    }

    // Set up log file path
    m_logFilePath = getLogDirectory() + "/" + m_logFileName;

    // Open log file
    if (!openLogFile()) {
        qWarning() << "Failed to open log file:" << m_logFilePath;
        return false;
    }

    // Perform initial cleanup of old logs
    deleteOldLogs();

    // Log initialization
    writeLog(LogLevel::Info, QString("Logger initialized. Log file: %1").arg(m_logFilePath));
    writeLog(LogLevel::Info, QString("Application: %1 %2")
        .arg(QCoreApplication::applicationName())
        .arg(QCoreApplication::applicationVersion()));

    m_initialized = true;
    return true;
}

void Logger::debug(const QString &message)
{
    log(LogLevel::Debug, message);
}

void Logger::info(const QString &message)
{
    log(LogLevel::Info, message);
}

void Logger::warning(const QString &message)
{
    log(LogLevel::Warning, message);
}

void Logger::error(const QString &message)
{
    log(LogLevel::Error, message);
}

void Logger::log(LogLevel level, const QString &message)
{
    QMutexLocker locker(&m_mutex);

    if (!m_initialized) {
        // Fallback to qDebug if not initialized
        qDebug() << "[" << levelToString(level) << "]" << message;
        return;
    }

    // Check if message should be logged based on minimum level
    // Log if the message level is >= the minimum level (DEBUG=0 is most verbose)
    if (level >= m_minLogLevel) {
        // Check if rotation is needed
        if (needsRotation()) {
            performRotation();
        }

        writeLog(level, message);
    }
}

QString Logger::getLogFilePath() const
{
    QMutexLocker locker(&m_mutex);
    return m_logFilePath;
}

void Logger::setMinLogLevel(LogLevel level)
{
    QMutexLocker locker(&m_mutex);
    m_minLogLevel = level;
}

Logger::LogLevel Logger::getMinLogLevel() const
{
    QMutexLocker locker(&m_mutex);
    return m_minLogLevel;
}

void Logger::setConsoleOutputEnabled(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_consoleOutputEnabled = enabled;
}

bool Logger::isConsoleOutputEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_consoleOutputEnabled;
}

void Logger::setMaxFileSize(int sizeMB)
{
    QMutexLocker locker(&m_mutex);
    if (sizeMB > 0) {
        m_maxFileSizeMB = sizeMB;
    }
}

void Logger::setMaxRotatedFiles(int count)
{
    QMutexLocker locker(&m_mutex);
    if (count >= 0) {
        m_maxRotatedFiles = count;
    }
}

void Logger::setMaxLogAge(int days)
{
    QMutexLocker locker(&m_mutex);
    if (days > 0) {
        m_maxLogAgeDays = days;
    }
}

void Logger::rotateLog()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        performRotation();
    }
}

void Logger::cleanupOldLogs()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        deleteOldLogs();
    }
}

void Logger::flush()
{
    QMutexLocker locker(&m_mutex);
    if (m_logFile.isOpen()) {
        m_logStream.flush();
        m_logFile.flush();
    }
}

void Logger::shutdown()
{
    QMutexLocker locker(&m_mutex);

    if (!m_initialized) {
        return;
    }

    if (m_logFile.isOpen()) {
        writeLog(LogLevel::Info, "Logger shutting down");
        m_logStream.flush();
        m_logFile.close();
    }

    m_initialized = false;
}

QString Logger::getLogDirectory() const
{
    // Use GenericDataLocation for consistent behavior across platforms
    // Windows: C:/Users/<user>/AppData/Local
    // Linux:   ~/.local/share
    // macOS:   ~/Library/Application Support
    QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return dataHome + "/Bloom/logs";
}

bool Logger::ensureLogDirectoryExists()
{
    QString logDir = getLogDirectory();
    QDir dir(logDir);

    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "Failed to create log directory:" << logDir;
            return false;
        }
        qDebug() << "Created log directory:" << logDir;
    }

    return true;
}

bool Logger::openLogFile()
{
    // Close existing file if open
    if (m_logFile.isOpen()) {
        m_logStream.flush();
        m_logFile.close();
    }

    m_logFile.setFileName(m_logFilePath);

    // Open in append mode
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    m_logStream.setDevice(&m_logFile);
    m_logStream.setEncoding(QStringConverter::Utf8);

    // Get current file size
    m_currentFileSize = m_logFile.size();

    return true;
}

void Logger::writeLog(LogLevel level, const QString &message)
{
    // Note: We intentionally DO NOT check inQtMessageHandler here.
    // The guard in qtMessageHandler() (in ApplicationInitializer.cpp) is sufficient
    // to prevent recursive logging. This function uses fprintf for console output
    // (not Qt logging functions), so there's no risk of reentrancy here.
    // The previous check was incorrectly blocking ALL Qt-routed logs.

    if (!m_logFile.isOpen()) {
        return;
    }

    // Format: [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] message
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString levelStr = levelToString(level);

    QString logLine = QString("[%1] [%2] %3\n")
        .arg(timestamp)
        .arg(levelStr)
        .arg(message);

    m_logStream << logLine;
    m_logStream.flush();

    // Update file size using UTF-8 byte count (not UTF-16 character count)
    QByteArray utf8Line = logLine.toUtf8();
    m_currentFileSize += utf8Line.size();

    // Console output - only when enabled, using non-Qt API to avoid re-entry
    if (m_consoleOutputEnabled) {
        const char* output = utf8Line.constData();
        switch (level) {
        case LogLevel::Debug:
        case LogLevel::Info:
            fprintf(stdout, "%s", output);
            fflush(stdout);
            break;
        case LogLevel::Warning:
        case LogLevel::Error:
            fprintf(stderr, "%s", output);
            fflush(stderr);
            break;
        }
    }
}

bool Logger::needsRotation() const
{
    qint64 maxSizeBytes = static_cast<qint64>(m_maxFileSizeMB) * 1024 * 1024;
    return m_currentFileSize >= maxSizeBytes;
}

void Logger::performRotation()
{
    // Close current log file
    if (m_logFile.isOpen()) {
        m_logStream.flush();
        m_logFile.close();
    }

    // Delete oldest rotated file if we've reached the limit
    if (m_maxRotatedFiles > 0) {
        QString oldestFile = m_logFilePath + "." + QString::number(m_maxRotatedFiles);
        if (QFile::exists(oldestFile)) {
            QFile::remove(oldestFile);
        }
    }

    // Rotate existing files (N -> N+1)
    for (int i = m_maxRotatedFiles - 1; i >= 1; --i) {
        QString oldFile = m_logFilePath + "." + QString::number(i);
        QString newFile = m_logFilePath + "." + QString::number(i + 1);

        if (QFile::exists(oldFile)) {
            QFile::rename(oldFile, newFile);
        }
    }

    // Move current log to .1
    if (QFile::exists(m_logFilePath)) {
        QString rotatedFile = m_logFilePath + ".1";
        QFile::rename(m_logFilePath, rotatedFile);
    }

    // Re-open the log file (creates a new empty file)
    openLogFile();

    writeLog(LogLevel::Info, QString("Log rotated. Previous log saved as %1.1").arg(m_logFileName));
}

void Logger::deleteOldLogs()
{
    if (m_maxLogAgeDays <= 0) {
        return;
    }

    QDateTime cutoffDate = QDateTime::currentDateTime().addDays(-m_maxLogAgeDays);
    QStringList logFiles = getLogFiles();

    for (const QString &filePath : logFiles) {
        QFileInfo fileInfo(filePath);
        if (fileInfo.lastModified() < cutoffDate) {
            if (QFile::remove(filePath)) {
                qDebug() << "Deleted old log file:" << filePath;
            } else {
                qWarning() << "Failed to delete old log file:" << filePath;
            }
        }
    }
}

QString Logger::levelToString(LogLevel level) const
{
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

QStringList Logger::getLogFiles() const
{
    QStringList logFiles;
    QString logDir = getLogDirectory();
    QDir dir(logDir);

    // Get all files matching the base log name pattern
    QString baseName = QFileInfo(m_logFileName).baseName();
    QString pattern = baseName + "*";

    QStringList nameFilters;
    nameFilters << pattern;

    QStringList files = dir.entryList(nameFilters, QDir::Files, QDir::Name);

    for (const QString &fileName : files) {
        logFiles << dir.absoluteFilePath(fileName);
    }

    return logFiles;
}
