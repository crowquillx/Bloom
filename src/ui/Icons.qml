pragma Singleton
import QtQuick

/**
 * Icons - Material Symbols icon codepoints
 * 
 * This singleton provides easy access to Material Symbols Outlined icons.
 * Icons are referenced by their Unicode codepoint.
 * 
 * Usage in QML:
 *   Text {
 *       text: Icons.home
 *       font.family: Theme.fontIcon
 *       font.pixelSize: Theme.fontSizeIcon
 *   }
 * 
 * Browse all icons at: https://fonts.google.com/icons
 * Find codepoints at: https://github.com/AlejandroAkbal/Google-Material-Symbols-Icons-Codepoints-JSON
 */
QtObject {
    // Navigation - Using Unicode codepoints from Material Symbols
    readonly property string home: "\ue88a"
    readonly property string menu: "\ue5d2"
    readonly property string close: "\ue5cd"
    readonly property string arrowBack: "\ue5c4"
    readonly property string arrowForward: "\ue5c8"
    readonly property string chevronLeft: "\ue5cb"
    readonly property string chevronRight: "\ue5cc"
    readonly property string expandMore: "\ue5cf"
    readonly property string expandLess: "\ue5ce"
    
    // Media
    readonly property string movie: "\ue02c"
    readonly property string tvShows: "\ue333"
    readonly property string videoLibrary: "\ue04a"
    readonly property string playArrow: "\ue037"
    readonly property string pause: "\ue034"
    readonly property string stop: "\ue047"
    readonly property string skipNext: "\ue044"
    readonly property string skipPrevious: "\ue045"
    readonly property string fastForward: "\ue01f"
    readonly property string fastRewind: "\ue020"
    readonly property string replay: "\ue042"
    readonly property string shuffle: "\ue043"
    readonly property string repeat: "\ue040"
    readonly property string volumeUp: "\ue050"
    readonly property string volumeDown: "\ue04d"
    readonly property string volumeMute: "\ue04e"
    readonly property string volumeOff: "\ue04f"
    readonly property string fullscreen: "\ue5d0"
    readonly property string fullscreenExit: "\ue5d1"
    readonly property string subtitles: "\ue048"
    readonly property string closedCaption: "\ue01c"
    readonly property string audiotrack: "\ue3a1"
    readonly property string speed: "\ue9e4"
    readonly property string hd: "\ue052"
    readonly property string fourK: "\ue072"
    
    // Actions
    readonly property string check: "\ue5ca"
    readonly property string checkCircle: "\ue86c"
    readonly property string add: "\ue145"
    readonly property string remove: "\ue15b"
    readonly property string edit: "\ue3c9"
    readonly property string delete_: "\ue872"
    readonly property string search: "\ue8b6"
    readonly property string refresh: "\ue5d5"
    readonly property string moreVert: "\ue5d4"
    readonly property string moreHoriz: "\ue5d3"
    readonly property string share: "\ue80d"
    readonly property string download: "\ue2c4"
    readonly property string upload: "\ue2c6"
    readonly property string sort: "\ue164"
    readonly property string filter: "\ue152"
    
    // Status & State
    readonly property string favorite: "\ue87d"
    readonly property string favoriteBorder: "\ue87e"
    readonly property string bookmark: "\ue866"
    readonly property string bookmarkBorder: "\ue867"
    readonly property string visibility: "\ue8f4"
    readonly property string visibilityOff: "\ue8f5"
    readonly property string star: "\ue838"
    readonly property string starBorder: "\ue83a"
    readonly property string starHalf: "\ue839"
    readonly property string schedule: "\ue8b5"
    readonly property string history: "\ue889"
    readonly property string newReleases: "\ue031"
    readonly property string info: "\ue88e"
    readonly property string infoOutline: "\ue88f"
    readonly property string error: "\ue000"
    readonly property string warning: "\ue002"
    readonly property string help: "\ue887"
    
    // Settings & System
    readonly property string settings: "\ue8b8"
    readonly property string tune: "\ue429"
    readonly property string account: "\ue853"
    readonly property string person: "\ue7fd"
    readonly property string group: "\ue7ef"
    readonly property string notifications: "\ue7f4"
    readonly property string power: "\ue8ac"
    readonly property string logout: "\ue9ba"
    readonly property string login: "\uea77"
    readonly property string language: "\ue894"
    readonly property string palette: "\ue40a"
    readonly property string brightness: "\ue3a9"
    readonly property string darkMode: "\ue51c"
    readonly property string lightMode: "\ue518"
    readonly property string storage: "\ue1db"
    readonly property string cloud: "\ue2bd"
    readonly property string wifi: "\ue63e"
    readonly property string wifi_off: "\ue648"
    
    // Content & Layout  
    readonly property string folder: "\ue2c7"
    readonly property string folderOpen: "\ue2c8"
    readonly property string image: "\ue3f4"
    readonly property string photo: "\ue410"
    readonly property string music: "\ue3d4"
    readonly property string album: "\ue019"
    readonly property string category: "\ue574"
    readonly property string label: "\ue892"
    readonly property string viewList: "\ue8ef"
    readonly property string viewGrid: "\ue8f0"
    readonly property string viewModule: "\ue8f1"
    readonly property string dashboard: "\ue871"
}
