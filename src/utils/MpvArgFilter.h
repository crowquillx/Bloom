#pragma once

#include <QString>
#include <QStringList>

namespace MpvArgFilter {

struct ShaderArgPartition {
    QStringList nonShaderArgs;
    QStringList shaderPaths;
};

QString optionNameForArg(const QString &arg);
bool isSafeBuiltinProfileArg(const QString &arg);
bool isBloomManagedOptionName(const QString &name);
bool isShaderListArg(const QString &arg);
QString sanitizeArg(const QString &arg);
QStringList sanitizeArgs(const QStringList &args);
QStringList expandShaderListArgs(const QStringList &args);
ShaderArgPartition partitionShaderArgs(const QStringList &args);
QString resolveMpvPortablePath(const QString &path, const QString &mpvConfigDir);
QStringList resolveEmbeddedShaderPaths(const QStringList &shaderPaths, const QString &mpvConfigDir);
QString joinMpvPathListOptionValue(const QStringList &paths);
QStringList filterBloomManagedArgs(const QStringList &args, QStringList *filteredArgs = nullptr);

} // namespace MpvArgFilter
