import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * MediaInfoPanel - Displays video information and audio/subtitle track selectors
 * 
 * Shows:
 * - Video resolution and codec (e.g., "1080p • HEVC • HDR10")
 * - Audio track selector dropdown
 * - Subtitle track selector dropdown
 */
FocusScope {
    id: root
    
    // Media source data from PlaybackInfo
    property var mediaSource: null
    
    // Current track selections
    property int selectedAudioIndex: -1
    property int selectedSubtitleIndex: -1
    
    // Signals when user changes selection
    signal audioTrackChanged(int index)
    signal subtitleTrackChanged(int index)
    
    // Computed properties from media source
    property var videoStream: {
        if (!mediaSource || !mediaSource.mediaStreams) return null
        for (var i = 0; i < mediaSource.mediaStreams.length; i++) {
            if (mediaSource.mediaStreams[i].type === "Video") {
                return mediaSource.mediaStreams[i]
            }
        }
        return null
    }
    
    property var audioTracks: {
        if (!mediaSource || !mediaSource.mediaStreams) return []
        var tracks = []
        for (var i = 0; i < mediaSource.mediaStreams.length; i++) {
            if (mediaSource.mediaStreams[i].type === "Audio") {
                tracks.push(mediaSource.mediaStreams[i])
            }
        }
        return tracks
    }
    
    property var subtitleTracks: {
        if (!mediaSource || !mediaSource.mediaStreams) return []
        var tracks = []
        for (var i = 0; i < mediaSource.mediaStreams.length; i++) {
            if (mediaSource.mediaStreams[i].type === "Subtitle") {
                tracks.push(mediaSource.mediaStreams[i])
            }
        }
        return tracks
    }
    
    // Format video info string
    property string videoInfoText: {
        if (!videoStream) return "Video info unavailable"
        
        var parts = []
        
        // Resolution
        if (videoStream.height) {
            var res = videoStream.height
            if (res >= 2160) parts.push("4K")
            else if (res >= 1080) parts.push("1080p")
            else if (res >= 720) parts.push("720p")
            else if (res >= 480) parts.push("480p")
            else parts.push(res + "p")
        }
        
        // Codec
        if (videoStream.codec) {
            var codec = videoStream.codec.toLowerCase()
            if (codec === "hevc" || codec === "h265") parts.push("HEVC")
            else if (codec === "avc" || codec === "h264") parts.push("AVC")
            else if (codec === "av1") parts.push("AV1")
            else if (codec === "vp9") parts.push("VP9")
            else parts.push(codec.toUpperCase())
        }
        
        // HDR info
        if (videoStream.videoRange) {
            var range = videoStream.videoRange
            if (range !== "SDR") {
                parts.push(range)
            }
        }
        
        // Framerate
        if (videoStream.averageFrameRate && videoStream.averageFrameRate > 0) {
            var fps = Math.round(videoStream.averageFrameRate)
            if (fps >= 24) {
                parts.push(fps + " fps")
            }
        }
        
        return parts.join(" • ")
    }
    
    implicitWidth: contentLayout.implicitWidth
    implicitHeight: contentLayout.implicitHeight
    
    ColumnLayout {
        id: contentLayout
        width: parent.width
        spacing: 16
        
        // Video Info Section
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: videoStream !== null
            
            Text {
                text: "Video"
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.fontPrimary
                color: Theme.textSecondary
            }
            
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                radius: Theme.radiusSmall
                color: Theme.buttonSecondaryBackground
                border.width: 1
                border.color: Theme.buttonSecondaryBorder
                
                Text {
                    anchors.fill: parent
                    anchors.margins: 12
                    text: videoInfoText
                    font.pixelSize: Theme.fontSizeBody
                    font.family: Theme.fontPrimary
                    color: Theme.textPrimary
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
        
        // Audio Track Selector
        TrackSelector {
            id: audioSelector
            Layout.fillWidth: true
            label: "Audio"
            tracks: root.audioTracks
            selectedIndex: root.selectedAudioIndex
            allowNone: false
            emptyText: "No audio tracks"
            focus: true
            
            KeyNavigation.up: root.KeyNavigation.up
            KeyNavigation.down: subtitleSelector
            
            onTrackSelected: function(index) {
                root.audioTrackChanged(index)
            }
        }
        
        // Subtitle Track Selector
        TrackSelector {
            id: subtitleSelector
            Layout.fillWidth: true
            label: "Subtitles"
            tracks: root.subtitleTracks
            selectedIndex: root.selectedSubtitleIndex
            allowNone: true
            noneText: "Off"
            emptyText: "No subtitles"
            
            KeyNavigation.up: audioSelector
            KeyNavigation.down: root.KeyNavigation.down
            
            onTrackSelected: function(index) {
                root.subtitleTrackChanged(index)
            }
        }
    }
    
    // When media source changes, find default selections if not already set
    onMediaSourceChanged: {
        if (!mediaSource) return
        
        // Set default audio track if not specified
        if (selectedAudioIndex < 0 && audioTracks.length > 0) {
            for (var i = 0; i < audioTracks.length; i++) {
                if (audioTracks[i].isDefault) {
                    selectedAudioIndex = audioTracks[i].index
                    break
                }
            }
            // If no default, use first track
            if (selectedAudioIndex < 0) {
                selectedAudioIndex = audioTracks[0].index
            }
        }
        
        // Set default subtitle track if available
        if (selectedSubtitleIndex < 0 && subtitleTracks.length > 0) {
            for (var j = 0; j < subtitleTracks.length; j++) {
                if (subtitleTracks[j].isDefault) {
                    selectedSubtitleIndex = subtitleTracks[j].index
                    break
                }
            }
        }
    }
}
