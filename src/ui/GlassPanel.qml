import QtQuick

import BloomUI

/**
 * GlassPanel - restrained translucent surface used for grouped content that
 * sits over artwork (detail heroes, settings groups, info panels).
 *
 * `strong` is for floating chrome that must stay readable over anything
 * (menus, the expanded navigation rail).
 */
Rectangle {
    id: root

    property bool strong: false
    property bool bordered: true

    radius: Theme.radiusPanel
    color: strong ? Theme.surfaceGlassStrong : Theme.surfaceGlass
    border.width: bordered ? Theme.borderWidth : 0
    border.color: Theme.surfaceBorder
    antialiasing: true
}
