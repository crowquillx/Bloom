#pragma once

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#if defined(BLOOM_HAS_LIBMPV)
extern "C" {
#include <mpv/client.h>
}

namespace BloomBackend {

// Convert mpv's "audio-device-list" node (an array of {name, description} maps)
// into a QVariantList of QVariantMaps for consumption by the rest of Bloom.
inline QVariantList parseMpvAudioDeviceList(const mpv_node *node)
{
    QVariantList devices;
    if (!node || node->format != MPV_FORMAT_NODE_ARRAY || !node->u.list) {
        return devices;
    }

    const mpv_node_list *list = node->u.list;
    for (int i = 0; i < list->num; ++i) {
        const mpv_node &entry = list->values[i];
        if (entry.format != MPV_FORMAT_NODE_MAP || !entry.u.list) {
            continue;
        }

        const mpv_node_list *map = entry.u.list;
        QVariantMap device;
        for (int j = 0; j < map->num; ++j) {
            const char *key = map->keys ? map->keys[j] : nullptr;
            const mpv_node &val = map->values[j];
            if (!key || val.format != MPV_FORMAT_STRING) {
                continue;
            }
            device[QString::fromUtf8(key)] = QString::fromUtf8(val.u.string ? val.u.string : "");
        }

        if (!device.isEmpty()) {
            devices.append(device);
        }
    }

    return devices;
}

// Recursive mpv_node -> QVariant conversion (maps -> QVariantMap, arrays ->
// QVariantList) for structured diagnostic properties.
inline QVariant mpvNodeToVariant(const mpv_node *node)
{
    if (!node) {
        return {};
    }
    switch (node->format) {
    case MPV_FORMAT_STRING:
        return QString::fromUtf8(node->u.string ? node->u.string : "");
    case MPV_FORMAT_FLAG:
        return node->u.flag != 0;
    case MPV_FORMAT_INT64:
        return static_cast<qlonglong>(node->u.int64);
    case MPV_FORMAT_DOUBLE:
        return node->u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList list;
        if (node->u.list) {
            for (int i = 0; i < node->u.list->num; ++i) {
                list.append(mpvNodeToVariant(&node->u.list->values[i]));
            }
        }
        return list;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap map;
        if (node->u.list) {
            for (int i = 0; i < node->u.list->num; ++i) {
                const char *key = node->u.list->keys ? node->u.list->keys[i] : nullptr;
                if (key) {
                    map.insert(QString::fromUtf8(key), mpvNodeToVariant(&node->u.list->values[i]));
                }
            }
        }
        return map;
    }
    default:
        return {};
    }
}

} // namespace BloomBackend
#endif
