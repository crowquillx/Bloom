#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>

namespace DetailListHelper {

inline QVariantList mapSimilarItems(const QJsonArray &items)
{
    QVariantList mappedItems;
    mappedItems.reserve(items.size());

    for (const auto &value : items) {
        const QJsonObject item = value.toObject();
        if (item.isEmpty()) {
            continue;
        }
        mappedItems.append(item.toVariantMap());
    }

    return mappedItems;
}

}