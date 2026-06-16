#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#if defined(BLOOM_HAS_LIBMPV)
extern "C" {
#include <mpv/client.h>
}

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
#endif
