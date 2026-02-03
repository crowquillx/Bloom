#pragma once

#include <QObject>

class FontLoader : public QObject
{
    Q_OBJECT

public:
    explicit FontLoader(QObject *parent = nullptr);

    void load();
};
