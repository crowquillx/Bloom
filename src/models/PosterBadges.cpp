#include "models/PosterBadges.h"

#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

#include <array>
#include <cmath>

namespace Bloom {

namespace {

struct ResolvedValue {
    QString text;
    QString icon;
    QString ratingSource;
    QString score;
};

QString trimmedString(const QVariant &value)
{
    return value.toString().trimmed();
}

QString firstNonEmpty(const QVariant &values)
{
    for (const QVariant &value : values.toList()) {
        const QString text = trimmedString(value);
        if (!text.isEmpty()) {
            return text;
        }
    }
    return {};
}

QString prettyResolution(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty()) {
        return {};
    }
    if (normalized == QStringLiteral("2160p") || normalized == QStringLiteral("4k")
        || normalized == QStringLiteral("uhd")) {
        return QStringLiteral("4K");
    }
    if (normalized == QStringLiteral("4320p") || normalized == QStringLiteral("8k")) {
        return QStringLiteral("8K");
    }
    static const QRegularExpression progressive(QStringLiteral("^\\d+p$"));
    if (progressive.match(normalized).hasMatch()) {
        return normalized;
    }
    return value.trimmed().toUpper();
}

QString outOfTen(double value)
{
    return QString::number(std::round(value * 10.0) / 10.0, 'f', 1);
}

QString percent(double value)
{
    return QStringLiteral("%1%").arg(qRound(value));
}

QString runtimeText(int minutes)
{
    if (minutes <= 0) {
        return {};
    }
    const int hours = minutes / 60;
    const int rest = minutes % 60;
    return hours > 0 ? QStringLiteral("%1h %2m").arg(hours).arg(rest)
                     : QStringLiteral("%1m").arg(rest);
}

QString languageName(const QString &code)
{
    const QString trimmed = code.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    const QLocale::Language language = QLocale(trimmed).language();
    if (language == QLocale::C || language == QLocale::AnyLanguage) {
        return trimmed.toUpper();
    }
    return QLocale::languageToString(language);
}

QString showStatusText(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty()) {
        return {};
    }
    if (normalized == QStringLiteral("returning")
        || normalized == QStringLiteral("returning series")
        || normalized == QStringLiteral("continuing")
        || normalized == QStringLiteral("in_production")
        || normalized == QStringLiteral("in production")) {
        return QStringLiteral("Returning");
    }
    if (normalized == QStringLiteral("ended")) {
        return QStringLiteral("Ended");
    }
    if (normalized == QStringLiteral("cancelled") || normalized == QStringLiteral("canceled")) {
        return QStringLiteral("Cancelled");
    }
    if (normalized == QStringLiteral("upcoming") || normalized == QStringLiteral("planned")) {
        return QStringLiteral("Upcoming");
    }
    return value.trimmed();
}

ResolvedValue rating(const QString &mark, const QString &source, double value, bool outOfHundred)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return {};
    }
    const QString score = outOfHundred ? percent(value) : outOfTen(value);
    return {QStringLiteral("%1 %2").arg(mark, score), {}, source, score};
}

ResolvedValue plain(const QString &text, const QString &icon = {})
{
    return {text, text.isEmpty() ? QString() : icon, {}, {}};
}

using Resolver = ResolvedValue (*)(const QVariantMap &item, const QVariantMap &summary);

struct KindDefinition {
    const char *kind;
    Resolver resolve;
};

const std::array<KindDefinition, 24> &kindDefinitions()
{
    static const std::array<KindDefinition, 24> definitions{{
        {"resolution", [](const QVariantMap &, const QVariantMap &summary) {
             return plain(prettyResolution(summary.value(QStringLiteral("resolution")).toString()),
                          QStringLiteral("monitor"));
         }},
        {"hdr", [](const QVariantMap &, const QVariantMap &summary) {
             return plain(trimmedString(summary.value(QStringLiteral("hdr"))), QStringLiteral("hdr"));
         }},
        {"resolutionHdr", [](const QVariantMap &, const QVariantMap &summary) {
             const QString resolution =
                 prettyResolution(summary.value(QStringLiteral("resolution")).toString());
             if (resolution.isEmpty()) {
                 return ResolvedValue{};
             }
             const QString hdr = trimmedString(summary.value(QStringLiteral("hdr")));
             if (hdr.isEmpty()) {
                 return plain(resolution, QStringLiteral("monitor"));
             }
             const QString suffix = hdr.contains(QStringLiteral("DV")) ? QStringLiteral("DV")
                                                                         : QStringLiteral("HDR");
             return plain(QStringLiteral("%1 %2").arg(resolution, suffix), QStringLiteral("monitor"));
         }},
        {"audio", [](const QVariantMap &, const QVariantMap &summary) {
             return plain(trimmedString(summary.value(QStringLiteral("audio"))), QStringLiteral("audio"));
         }},
        {"audioChannels", [](const QVariantMap &, const QVariantMap &summary) {
             return plain(trimmedString(summary.value(QStringLiteral("audioChannels"))),
                          QStringLiteral("surround"));
         }},
        {"videoCodec", [](const QVariantMap &, const QVariantMap &summary) {
             return plain(trimmedString(summary.value(QStringLiteral("videoCodec"))),
                          QStringLiteral("film"));
         }},
        {"container", [](const QVariantMap &, const QVariantMap &summary) {
             return plain(trimmedString(summary.value(QStringLiteral("container"))));
         }},
        {"aspectRatio", [](const QVariantMap &, const QVariantMap &summary) {
             return plain(trimmedString(summary.value(QStringLiteral("aspectRatio"))),
                          QStringLiteral("aspectRatio"));
         }},
        {"releaseType", [](const QVariantMap &, const QVariantMap &summary) {
             return plain(trimmedString(summary.value(QStringLiteral("releaseType"))));
         }},
        {"edition", [](const QVariantMap &, const QVariantMap &summary) {
             return plain(trimmedString(summary.value(QStringLiteral("edition"))));
         }},
        {"multiAudio", [](const QVariantMap &, const QVariantMap &summary) {
             return plain(summary.value(QStringLiteral("multiAudio")).toBool()
                              ? QStringLiteral("Multi-Audio") : QString(),
                          QStringLiteral("languages"));
         }},
        {"multiSub", [](const QVariantMap &, const QVariantMap &summary) {
             return plain(summary.value(QStringLiteral("multiSub")).toBool()
                              ? QStringLiteral("CC") : QString(),
                          QStringLiteral("subtitles"));
         }},
        {"ratingImdb", [](const QVariantMap &item, const QVariantMap &) {
             return rating(QStringLiteral("IMDb"), QStringLiteral("imdb"),
                           item.value(QStringLiteral("ratings")).toMap()
                               .value(QStringLiteral("imdb")).toDouble(), false);
         }},
        {"ratingTmdb", [](const QVariantMap &item, const QVariantMap &) {
             return rating(QStringLiteral("TMDB"), QStringLiteral("tmdb"),
                           item.value(QStringLiteral("ratings")).toMap()
                               .value(QStringLiteral("tmdb")).toDouble(), false);
         }},
        {"ratingRtCritic", [](const QVariantMap &item, const QVariantMap &) {
             return rating(QStringLiteral("RT"), QStringLiteral("tomatoes"),
                           item.value(QStringLiteral("ratings")).toMap()
                               .value(QStringLiteral("rottenTomatoesCritic")).toDouble(), true);
         }},
        {"ratingRtAudience", [](const QVariantMap &item, const QVariantMap &) {
             return rating(QStringLiteral("RT Audience"), QStringLiteral("audience"),
                           item.value(QStringLiteral("ratings")).toMap()
                               .value(QStringLiteral("rottenTomatoesAudience")).toDouble(), true);
         }},
        {"contentRating", [](const QVariantMap &item, const QVariantMap &) {
             return plain(trimmedString(item.value(QStringLiteral("officialRating"))),
                          QStringLiteral("shield"));
         }},
        {"advisoryAge", [](const QVariantMap &item, const QVariantMap &) {
             const int age = item.value(QStringLiteral("advisoryAge")).toInt();
             return plain(age > 0 ? QStringLiteral("%1+").arg(age) : QString(),
                          QStringLiteral("group"));
         }},
        {"year", [](const QVariantMap &item, const QVariantMap &) {
             const int year = item.value(QStringLiteral("productionYear")).toInt();
             return plain(year > 0 ? QString::number(year) : QString());
         }},
        {"runtime", [](const QVariantMap &item, const QVariantMap &) {
             return plain(runtimeText(item.value(QStringLiteral("runtimeMinutes")).toInt()),
                          QStringLiteral("clock"));
         }},
        {"originalLanguage", [](const QVariantMap &item, const QVariantMap &) {
             return plain(languageName(item.value(QStringLiteral("originalLanguage")).toString()),
                          QStringLiteral("globe"));
         }},
        {"studio", [](const QVariantMap &item, const QVariantMap &) {
             return plain(firstNonEmpty(item.value(QStringLiteral("studios"))),
                          QStringLiteral("business"));
         }},
        {"network", [](const QVariantMap &item, const QVariantMap &) {
             return plain(firstNonEmpty(item.value(QStringLiteral("networks"))),
                          QStringLiteral("tv"));
         }},
        {"showStatus", [](const QVariantMap &item, const QVariantMap &) {
             return plain(showStatusText(item.value(QStringLiteral("showStatus")).toString()),
                          QStringLiteral("tv"));
         }},
    }};
    return definitions;
}

const QHash<QString, Resolver> &resolvers()
{
    static const QHash<QString, Resolver> byKind = [] {
        QHash<QString, Resolver> map;
        for (const KindDefinition &definition : kindDefinitions()) {
            map.insert(QString::fromLatin1(definition.kind), definition.resolve);
        }
        return map;
    }();
    return byKind;
}

} // namespace

QString posterBadgeCornerName(PosterBadgeCorner corner)
{
    switch (corner) {
    case PosterBadgeCorner::TopLeft: return QStringLiteral("topLeft");
    case PosterBadgeCorner::TopRight: return QStringLiteral("topRight");
    case PosterBadgeCorner::BottomLeft: return QStringLiteral("bottomLeft");
    case PosterBadgeCorner::BottomRight: return QStringLiteral("bottomRight");
    }
    return QStringLiteral("topLeft");
}

bool PosterBadgeLayout::operator==(const PosterBadgeLayout &other) const
{
    if (enabled != other.enabled || badges.size() != other.badges.size()) {
        return false;
    }
    for (qsizetype index = 0; index < badges.size(); ++index) {
        const PosterBadgeSlot &left = badges.at(index);
        const PosterBadgeSlot &right = other.badges.at(index);
        if (left.kind != right.kind || left.corner != right.corner
            || left.showIcon != right.showIcon) {
            return false;
        }
    }
    return true;
}

bool isKnownPosterBadgeKind(const QString &kind)
{
    return resolvers().contains(kind);
}

QVariantList resolvePosterBadges(const PosterBadgeLayout &layout, const QVariantMap &item)
{
    if (!layout.enabled || layout.badges.isEmpty()) {
        return {};
    }
    const QVariantMap summary = item.value(QStringLiteral("mediaSummary")).toMap();
    std::array<int, 4> perCorner{};
    QVariantList result;
    for (const PosterBadgeSlot &slot : layout.badges) {
        int &shown = perCorner.at(static_cast<std::size_t>(slot.corner));
        if (shown >= kMaxPosterBadgesPerCorner) {
            continue;
        }
        const Resolver resolve = resolvers().value(slot.kind);
        if (!resolve) {
            continue;
        }
        const ResolvedValue value = resolve(item, summary);
        if (value.text.isEmpty()) {
            continue;
        }
        ++shown;
        QVariantMap badge{
            {QStringLiteral("corner"), posterBadgeCornerName(slot.corner)},
            {QStringLiteral("kind"), slot.kind},
            {QStringLiteral("text"), value.text},
            {QStringLiteral("icon"), slot.showIcon ? value.icon : QString()},
        };
        if (!value.ratingSource.isEmpty()) {
            badge.insert(QStringLiteral("ratingSource"), value.ratingSource);
            badge.insert(QStringLiteral("score"), value.score);
        }
        result.append(badge);
    }
    return result;
}

} // namespace Bloom
