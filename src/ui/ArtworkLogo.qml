import QtQuick
import QtQuick.Effects

import BloomUI

// Provider title logo. Logos are usually authored white for dark artwork, so
// light themes add a soft dark shadow that keeps them legible over light
// scrims and glass. Dark themes render the logo untouched.
Image {
    id: root

    readonly property bool shadowed: Theme.themeIsLight
                                     && GraphicsInfo.api !== GraphicsInfo.Software
                                     && status === Image.Ready

    fillMode: Image.PreserveAspectFit
    asynchronous: true
    cache: true

    layer.enabled: root.shadowed
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: Theme.logoShadowColor
        shadowOpacity: 1.0
        shadowBlur: 0.45
        shadowHorizontalOffset: 0
        shadowVerticalOffset: Math.round(2 * Theme.layoutScale)
        autoPaddingEnabled: true
    }
}
