#include "MpvArgFilter.h"

namespace MpvArgFilter {

QString optionNameForArg(const QString &arg)
{
    if (!arg.startsWith(QStringLiteral("--"))) {
        return QString();
    }

    const QString option = arg.mid(2);
    const int equalsIndex = option.indexOf('=');
    return equalsIndex >= 0 ? option.left(equalsIndex).toLower() : option.toLower();
}

bool isSafeBuiltinProfileArg(const QString &arg)
{
    const QString trimmed = arg.trimmed();
    const int equalsIndex = trimmed.indexOf(QLatin1Char('='));
    if (equalsIndex < 0 || optionNameForArg(trimmed) != QStringLiteral("profile")) {
        return false;
    }

    const QString value = trimmed.mid(equalsIndex + 1).trimmed().toLower();
    return value == QStringLiteral("fast") || value == QStringLiteral("high-quality");
}

bool isBloomManagedOptionName(const QString &name)
{
    const QString normalized = name.toLower();
    if (normalized == QStringLiteral("config-dir")
        || normalized == QStringLiteral("config")
        || normalized == QStringLiteral("input-conf")
        || normalized == QStringLiteral("include")
        || normalized == QStringLiteral("script")
        || normalized == QStringLiteral("script-opts")
        || normalized == QStringLiteral("scripts")
        || normalized == QStringLiteral("osc")
        || normalized == QStringLiteral("no-osc")
        || normalized == QStringLiteral("profile")
        || normalized == QStringLiteral("fullscreen")
        || normalized == QStringLiteral("wid")
        || normalized == QStringLiteral("input-ipc-server")
        || normalized == QStringLiteral("input-ipc-client")
        || normalized == QStringLiteral("idle")
        || normalized.startsWith(QStringLiteral("bloom-"))
        || normalized == QStringLiteral("vo")
        || normalized == QStringLiteral("hwdec")
        || normalized == QStringLiteral("gpu-context")
        || normalized == QStringLiteral("gpu-api")) {
        return true;
    }

    return normalized.startsWith(QStringLiteral("vulkan-"))
        || normalized.startsWith(QStringLiteral("d3d11-"))
        || normalized.startsWith(QStringLiteral("opengl-"))
        || normalized.startsWith(QStringLiteral("wayland-"))
        || normalized.startsWith(QStringLiteral("x11-"));
}

QString sanitizeArg(const QString &arg)
{
    QString trimmed = arg.trimmed();
    if (!trimmed.startsWith(QStringLiteral("--"))) {
        return trimmed;
    }

    const QString name = optionNameForArg(trimmed);
    if (name == QStringLiteral("glsl-shaders-clr")) {
        return QString();
    }

    const int equalsIndex = trimmed.indexOf(QLatin1Char('='));
    if (equalsIndex < 0) {
        return trimmed;
    }

    QString value = trimmed.mid(equalsIndex + 1).trimmed();
    if (value.size() >= 2) {
        const QChar first = value.front();
        const QChar last = value.back();
        if ((first == QLatin1Char('"') && last == QLatin1Char('"'))
            || (first == QLatin1Char('\'') && last == QLatin1Char('\''))) {
            value = value.mid(1, value.size() - 2);
        }
    }

    QString option = trimmed.mid(2, equalsIndex - 2);
    if (name == QStringLiteral("glsl-shader")
        || name == QStringLiteral("glsl-shader-append")
        || name == QStringLiteral("glsl-shaders")
        || name == QStringLiteral("glsl-shaders-append")) {
        option = QStringLiteral("glsl-shaders-append");
    }

    return QStringLiteral("--") + option + QStringLiteral("=") + value;
}

QStringList sanitizeArgs(const QStringList &args)
{
    QStringList result;
    result.reserve(args.size());
    for (const QString &arg : args) {
        const QString sanitized = sanitizeArg(arg);
        if (!sanitized.isEmpty()) {
            result.append(sanitized);
        }
    }
    return result;
}

QStringList expandShaderListArgs(const QStringList &args)
{
    QStringList result;
    result.reserve(args.size() + 1);

    bool insertedShaderClear = false;
    for (const QString &arg : args) {
        const QString name = optionNameForArg(arg);
        if (name == QStringLiteral("glsl-shaders")
            || name == QStringLiteral("glsl-shader")
            || name == QStringLiteral("glsl-shader-append")
            || name == QStringLiteral("glsl-shaders-append")) {
            if (!insertedShaderClear) {
                result.append(QStringLiteral("--glsl-shaders-clr"));
                insertedShaderClear = true;
            }

            const QString sanitized = sanitizeArg(arg);
            if (!sanitized.isEmpty()) {
                result.append(sanitized);
            }
            continue;
        }

        if (name == QStringLiteral("glsl-shaders-clr")) {
            continue;
        }

        result.append(arg);
    }

    return result;
}

QStringList filterBloomManagedArgs(const QStringList &args, QStringList *filteredArgs)
{
    QStringList result;
    result.reserve(args.size());

    for (const QString &arg : args) {
        const QString name = optionNameForArg(arg);
        if (name == QStringLiteral("profile") && isSafeBuiltinProfileArg(arg)) {
            result.append(arg);
            continue;
        }
        if (!name.isEmpty() && isBloomManagedOptionName(name)) {
            if (filteredArgs) {
                filteredArgs->append(arg);
            }
            continue;
        }
        result.append(arg);
    }

    return result;
}

} // namespace MpvArgFilter
