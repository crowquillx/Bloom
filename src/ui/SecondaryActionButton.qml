import QtQuick

import BloomUI

// Supporting action button. Kept as a named variant of ActionButton so
// existing call sites keep their API (iconGlyph, iconColor, showLabel,
// accessibleLabel, toolTipText).
ActionButton {
    variant: "secondary"
}
