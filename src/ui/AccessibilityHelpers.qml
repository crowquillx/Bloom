pragma Singleton
import QtQuick

/**
 * AccessibilityHelpers - Shared logic for screen reader support
 */
QtObject {
    /**
     * Ensures a meaningful name for interactive elements.
     * @param {string} text Primary label or title
     * @param {string} fallback Alternative text if primary is empty
     */
    function a11yName(text, fallback) {
        if (text && text.trim().length > 0) return text;
        return fallback || "";
    }

    /**
     * Formats duration for screen reader announcement.
     * @param {number} ms Duration in milliseconds
     */
    function a11yDuration(ms) {
        if (!ms || ms <= 0) return "";
        var totalSeconds = Math.floor(ms / 1000);
        var hours = Math.floor(totalSeconds / 3600);
        var minutes = Math.floor((totalSeconds % 3600) / 60);
        
        var parts = [];
        if (hours > 0) parts.push(hours + (hours === 1 ? " hour" : " hours"));
        if (minutes > 0) parts.push(minutes + (minutes === 1 ? " minute" : " minutes"));
        
        return parts.join(", ");
    }

    // Role mapping helpers for consistency
    readonly property int roleButton: Accessible.Button
    readonly property int roleLink: Accessible.Link
    readonly property int roleMenuItem: Accessible.MenuItem
    readonly property int roleCheckBox: Accessible.CheckBox
    readonly property int roleSlider: Accessible.Slider
    readonly property int roleEditableText: Accessible.EditableText
}
