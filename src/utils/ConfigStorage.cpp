#include "ConfigStorage.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <utility>

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

QString portableTimestamp()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'"));
}

} // namespace

ConfigStorage::ConfigStorage(QString path)
    : m_path(std::move(path))
{
}

QString ConfigStorage::path() const
{
    return m_path;
}

bool ConfigStorage::read(QByteArray *data, QString *error) const
{
    if (!data) {
        setError(error, QStringLiteral("No destination was provided for the configuration data"));
        return false;
    }

    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error,
                 QStringLiteral("Could not open %1 for reading: %2")
                     .arg(m_path, file.errorString()));
        return false;
    }

    *data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        setError(error,
                 QStringLiteral("Could not read %1: %2")
                     .arg(m_path, file.errorString()));
        return false;
    }

    if (error) {
        error->clear();
    }
    return true;
}

bool ConfigStorage::writeAtomically(const QByteArray &data, QString *error) const
{
    const QFileInfo info(m_path);
    QDir directory = info.absoluteDir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        setError(error,
                 QStringLiteral("Could not create configuration directory %1")
                     .arg(directory.absolutePath()));
        return false;
    }

    QSaveFile file(m_path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error,
                 QStringLiteral("Could not open %1 for atomic writing: %2")
                     .arg(m_path, file.errorString()));
        return false;
    }

    const qint64 written = file.write(data);
    if (written != data.size()) {
        const QString detail = file.errorString();
        file.cancelWriting();
        setError(error,
                 QStringLiteral("Could not write the complete configuration to %1 "
                                "(%2 of %3 bytes): %4")
                     .arg(m_path)
                     .arg(written)
                     .arg(data.size())
                     .arg(detail));
        return false;
    }

    if (!file.commit()) {
        setError(error,
                 QStringLiteral("Could not commit the atomic configuration write to %1: %2")
                     .arg(m_path, file.errorString()));
        return false;
    }

    if (error) {
        error->clear();
    }
    return true;
}

bool ConfigStorage::backup(const QString &reason, QString *backupPath, QString *error) const
{
    if (!QFileInfo::exists(m_path)) {
        setError(error, QStringLiteral("Configuration file %1 does not exist").arg(m_path));
        return false;
    }

    const QString normalizedReason = reason.trimmed().isEmpty()
        ? QStringLiteral("backup")
        : reason.trimmed();
    const QString basePath = QStringLiteral("%1.%2-%3")
                                 .arg(m_path, normalizedReason, portableTimestamp());
    QString candidate = basePath;
    for (int suffix = 1; QFileInfo::exists(candidate); ++suffix) {
        candidate = QStringLiteral("%1-%2").arg(basePath).arg(suffix);
    }

    if (!QFile::rename(m_path, candidate)) {
        setError(error,
                 QStringLiteral("Could not move %1 to recovery backup %2")
                     .arg(m_path, candidate));
        return false;
    }

    if (backupPath) {
        *backupPath = candidate;
    }
    if (error) {
        error->clear();
    }
    return true;
}
