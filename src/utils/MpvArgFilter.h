#pragma once

#include <QString>
#include <QStringList>

namespace MpvArgFilter {

QString optionNameForArg(const QString &arg);
bool isBloomManagedOptionName(const QString &name);
QStringList filterBloomManagedArgs(const QStringList &args, QStringList *filteredArgs = nullptr);

} // namespace MpvArgFilter
