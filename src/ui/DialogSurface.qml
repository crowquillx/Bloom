import QtQuick

import BloomUI

// Background for dialogs, popups and menus. Near-opaque so modal copy never
// competes with the page behind it; one hairline keeps the edge crisp.
Rectangle {
    property bool floating: false

    radius: floating ? Theme.radiusLarge : Theme.radiusDialog
    color: Theme.surfaceDialog
    border.width: Theme.borderWidth
    border.color: Theme.surfaceBorderStrong
    antialiasing: true
}
