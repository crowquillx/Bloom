#pragma once

#include <QString>

namespace BloomTest {

inline QString shellCommand(const QString &script)
{
    QString escaped = script;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("/bin/sh -c \"%1\"").arg(escaped);
}

} // namespace BloomTest
