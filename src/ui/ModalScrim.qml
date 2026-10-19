import QtQuick

import BloomUI

// Dimming layer behind modal dialogs (assign to Popup's Overlay.modal).
Rectangle {
    color: Theme.scrimModal

    Behavior on opacity {
        enabled: Theme.uiAnimationsEnabled
        NumberAnimation { duration: Theme.durationShort }
    }
}
