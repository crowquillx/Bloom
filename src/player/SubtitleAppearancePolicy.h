#pragma once

#include "models/SubtitleAppearance.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <utility>

namespace Bloom::SubtitleAppearancePolicy {

/// mpv property names with their values, in managedProperties() order.
using MpvProperties = QList<std::pair<QString, QString>>;

/// The subtitle properties Bloom manages, in the order they are applied:
/// sub-scale, sub-font, sub-color, sub-border-style, sub-border-color,
/// sub-border-size, sub-shadow-offset, sub-shadow-color, sub-pos and
/// sub-ass-override. The border and shadow names are the aliases every mpv
/// release accepts.
QStringList managedProperties();
/// mpv startup arguments for the fields that differ from their default.
QStringList mpvArguments(const SubtitleAppearance &appearance);
/// Every managed property with the value it has under `appearance`: the
/// appearance's own value, else the last value `baseArgs` (the arguments
/// mpv started with, before mpvArguments) gives it, else mpv's default.
MpvProperties mpvProperties(const SubtitleAppearance &appearance, const QStringList &baseArgs);
/// The entries of `to` whose value differs from `from`: what a live change
/// sets.
MpvProperties changedProperties(const MpvProperties &from, const MpvProperties &to);

}  // namespace Bloom::SubtitleAppearancePolicy
