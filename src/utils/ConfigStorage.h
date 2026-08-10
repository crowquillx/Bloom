#pragma once

#include <QByteArray>
#include <QString>

/**
 * Filesystem repository for Bloom's JSON configuration document.
 *
 * ConfigStorage deliberately knows nothing about config schemas or migrations.
 * ConfigManager owns those policies; this class only provides checked reads,
 * atomic replacement writes, and portable recovery backups.
 */
class ConfigStorage
{
public:
    explicit ConfigStorage(QString path);

    QString path() const;
    bool read(QByteArray *data, QString *error) const;
    bool writeAtomically(const QByteArray &data, QString *error) const;
    bool backup(const QString &reason, QString *backupPath, QString *error) const;

private:
    QString m_path;
};
