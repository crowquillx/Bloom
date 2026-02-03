#pragma once

#include <QObject>

class CacheMigrator : public QObject
{
    Q_OBJECT

public:
    explicit CacheMigrator(QObject *parent = nullptr);

    void migrate();
};
