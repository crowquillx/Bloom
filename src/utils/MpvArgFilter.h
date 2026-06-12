#pragma once

#include <QString>
#include <QStringList>

namespace MpvArgFilter {

QString optionNameForArg(const QString &arg);
bool isBloomManagedOptionName(const QString &name);
QString sanitizeArg(const QString &arg);
QStringList sanitizeArgs(const QStringList &args);
QStringList expandShaderListArgs(const QStringList &args);
QStringList filterBloomManagedArgs(const QStringList &args, QStringList *filteredArgs = nullptr);

} // namespace MpvArgFilter
