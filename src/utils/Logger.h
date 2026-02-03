#pragma once

#include <QString>
#include <QTextStream>
#include <QFile>
#include <QMutex>
#include <QDateTime>

/**
 * @brief Shared thread_local guard for detecting Qt message handler reentrancy
 *
 * This flag is used by both ApplicationInitializer::qtMessageHandler and Logger::writeLog
 * to prevent recursive logging that could cause deadlock. When the Qt message handler
 * is active, Logger::writeLog can detect this and avoid any operations that might
 * cause reentrancy issues.
 */
inline thread_local bool inQtMessageHandler = false;

/**
 * @brief Cross-platform logging system with file rotation and auto-cleanup
 * 
 * Provides thread-safe logging to files with automatic rotation based on size
 * and cleanup of old log files based on age.
 */
class Logger
{
public:
    /**
     * @brief Log severity levels
     */
    enum class LogLevel {
        Debug,
        Info,
        Warning,
        Error
    };

    /**
     * @brief Get the singleton instance of Logger
     * @return Reference to the Logger instance
     */
    static Logger& instance();

    /**
     * @brief Initialize the logger with a specific log file name
     * @param logFileName Base name of the log file (e.g., "bloom.log")
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize(const QString &logFileName = "bloom.log");

    /**
     * @brief Log a debug message
     * @param message The message to log
     */
    void debug(const QString &message);

    /**
     * @brief Log an info message
     * @param message The message to log
     */
    void info(const QString &message);

    /**
     * @brief Log a warning message
     * @param message The message to log
     */
    void warning(const QString &message);

    /**
     * @brief Log an error message
     * @param message The message to log
     */
    void error(const QString &message);

    /**
     * @brief Log a message with a specific level
     * @param level The log level
     * @param message The message to log
     */
    void log(LogLevel level, const QString &message);

    /**
     * @brief Get the full path to the current log file
     * @return Full path to the log file
     */
    QString getLogFilePath() const;

    /**
     * @brief Set the minimum log level to output
     * @param level The minimum log level
     */
    void setMinLogLevel(LogLevel level);

    /**
     * @brief Get the current minimum log level
     * @return The current minimum log level
     */
    LogLevel getMinLogLevel() const;

    /**
     * @brief Enable or disable console output
     * @param enabled true to enable console output, false to disable
     */
    void setConsoleOutputEnabled(bool enabled);

    /**
     * @brief Check if console output is enabled
     * @return true if console output is enabled, false otherwise
     */
    bool isConsoleOutputEnabled() const;

    /**
     * @brief Set the maximum log file size before rotation
     * @param sizeMB Maximum size in megabytes
     */
    void setMaxFileSize(int sizeMB);

    /**
     * @brief Set the maximum number of rotated log files to keep
     * @param count Maximum number of rotated files
     */
    void setMaxRotatedFiles(int count);

    /**
     * @brief Set the maximum age of log files before deletion
     * @param days Maximum age in days
     */
    void setMaxLogAge(int days);

    /**
     * @brief Manually trigger log rotation
     */
    void rotateLog();

    /**
     * @brief Manually trigger cleanup of old log files
     */
    void cleanupOldLogs();

    /**
     * @brief Flush any buffered log messages to disk
     */
    void flush();

    /**
     * @brief Shutdown the logger and close the log file
     */
    void shutdown();

    // Delete copy constructor and assignment operator
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger();

    /**
     * @brief Get the platform-appropriate log directory
     * @return Path to the log directory
     */
    QString getLogDirectory() const;

    /**
     * @brief Ensure the log directory exists
     * @return true if directory exists or was created successfully
     */
    bool ensureLogDirectoryExists();

    /**
     * @brief Open the log file for writing
     * @return true if successful, false otherwise
     */
    bool openLogFile();

    /**
     * @brief Write a formatted log message to the file
     * @param level The log level
     * @param message The message to log
     */
    void writeLog(LogLevel level, const QString &message);

    /**
     * @brief Check if the log file needs rotation
     * @return true if rotation is needed
     */
    bool needsRotation() const;

    /**
     * @brief Perform log rotation
     */
    void performRotation();

    /**
     * @brief Delete log files older than the maximum age
     */
    void deleteOldLogs();

    /**
     * @brief Convert log level to string
     * @param level The log level
     * @return String representation of the log level
     */
    QString levelToString(LogLevel level) const;

    /**
     * @brief Get the list of log files matching the base name
     * @return List of log file paths
     */
    QStringList getLogFiles() const;

private:
    mutable QMutex m_mutex;
    QFile m_logFile;
    QTextStream m_logStream;
    QString m_logFileName;
    QString m_logFilePath;
    bool m_initialized;
    LogLevel m_minLogLevel;
    bool m_consoleOutputEnabled;
    int m_maxFileSizeMB;
    int m_maxRotatedFiles;
    int m_maxLogAgeDays;
    qint64 m_currentFileSize;
};
