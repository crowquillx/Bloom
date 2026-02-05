pragma Singleton
import QtQuick

QtObject {
    // ============================
    // DPI-Aware Scaling System
    // ============================
    // Scales content sizes for different screen resolutions while keeping UI chrome fixed
    // Baseline is 1440p (1.0) - 4K uses ~1.5 to maintain visual ratio
    // The scale value comes from DisplayManager C++ class which detects screen resolution
    property real dpiScale: typeof DisplayManager !== 'undefined' ? DisplayManager.dpiScale : 1.0
    
    // Manual DPI scale override from user settings (1.0 = automatic)
    property real manualDpiScaleOverride: typeof ConfigManager !== 'undefined' ? ConfigManager.manualDpiScaleOverride : 1.0
    
    // ============================
    // Theme System
    // ============================
    
    property string currentTheme: ConfigManager.theme || "Jellyfin"
    property var themeNames: ["Jellyfin", "Rosé Pine"]
    
    // Theme Definitions
    property var themes: {
        "Jellyfin": {
            "backgroundPrimary": "#000B25",
            "backgroundSecondary": "#101010",
            "backgroundGlass": Qt.rgba(0.1, 0.1, 0.1, 0.6),
            "accentPrimary": "#00A4DC",
            "accentSecondary": "#AA5CC3",
            "accentGradientStart": "#AA5CC3",
            "accentGradientEnd": "#00A4DC",
            "textPrimary": "#FFFFFF",
            "textSecondary": "#B3B3B3",
            "textDisabled": "#666666",
            "cardBackground": Qt.rgba(0.15, 0.15, 0.18, 0.65),
            "cardBackgroundHover": Qt.rgba(0.2, 0.2, 0.23, 0.7),
            "cardBackgroundFocused": Qt.rgba(0.25, 0.25, 0.28, 0.75),
            "cardBorder": Qt.rgba(1, 1, 1, 0.12),
            "cardBorderHover": Qt.rgba(1, 1, 1, 0.2),
            "cardBorderFocused": "#00A4DC", // accentPrimary
            "buttonPrimaryBackground": Qt.rgba(0, 0.64, 0.86, 0.85),
            "buttonPrimaryBackgroundHover": Qt.rgba(0, 0.72, 0.94, 0.9),
            "buttonPrimaryBackgroundPressed": Qt.rgba(0, 0.50, 0.70, 0.95),
            "buttonPrimaryBorder": Qt.rgba(1, 1, 1, 0.25),
            "buttonPrimaryBorderFocused": Qt.rgba(1, 1, 1, 0.9),
            "buttonSecondaryBackground": Qt.rgba(1, 1, 1, 0.08),
            "buttonSecondaryBackgroundHover": Qt.rgba(1, 1, 1, 0.15),
            "buttonSecondaryBackgroundPressed": Qt.rgba(1, 1, 1, 0.2),
            "buttonSecondaryBorder": Qt.rgba(1, 1, 1, 0.15),
            "buttonSecondaryBorderHover": Qt.rgba(1, 1, 1, 0.25),
            "buttonSecondaryBorderFocused": "#00A4DC" // accentPrimary
        },
        "Rosé Pine": {
            "backgroundPrimary": "#191724", // Base
            "backgroundSecondary": "#1f1d2e", // Surface
            "backgroundGlass": Qt.rgba(0.1, 0.09, 0.14, 0.6), // Base with alpha
            "accentPrimary": "#9ccfd8", // Foam
            "accentSecondary": "#c4a7e7", // Iris
            "accentGradientStart": "#c4a7e7", // Iris
            "accentGradientEnd": "#9ccfd8", // Foam
            "textPrimary": "#e0def4", // Text
            "textSecondary": "#908caa", // Subtle
            "textDisabled": "#6e6a86", // Muted
            "cardBackground": Qt.rgba(0.12, 0.11, 0.18, 0.65), // Surface with alpha
            "cardBackgroundHover": Qt.rgba(0.16, 0.14, 0.23, 0.7), // Overlay with alpha
            "cardBackgroundFocused": Qt.rgba(0.20, 0.18, 0.28, 0.75), // Highlight Low with alpha
            "cardBorder": Qt.rgba(1, 1, 1, 0.12),
            "cardBorderHover": Qt.rgba(1, 1, 1, 0.2),
            "cardBorderFocused": "#9ccfd8", // Foam
            "buttonPrimaryBackground": Qt.rgba(0.61, 0.81, 0.85, 0.85), // Foam with alpha
            "buttonPrimaryBackgroundHover": Qt.rgba(0.61, 0.81, 0.85, 0.95),
            "buttonPrimaryBackgroundPressed": Qt.rgba(0.61, 0.81, 0.85, 1.0),
            "buttonPrimaryBorder": Qt.rgba(1, 1, 1, 0.25),
            "buttonPrimaryBorderFocused": Qt.rgba(1, 1, 1, 0.9),
            "buttonSecondaryBackground": Qt.rgba(1, 1, 1, 0.08),
            "buttonSecondaryBackgroundHover": Qt.rgba(1, 1, 1, 0.15),
            "buttonSecondaryBackgroundPressed": Qt.rgba(1, 1, 1, 0.2),
            "buttonSecondaryBorder": Qt.rgba(1, 1, 1, 0.15),
            "buttonSecondaryBorderHover": Qt.rgba(1, 1, 1, 0.25),
            "buttonSecondaryBorderFocused": "#9ccfd8" // Foam
        }
    }
    
    // Helper to get current theme colors
    property var activeTheme: themes[currentTheme] || themes["Jellyfin"]

    // Background colors
    property color backgroundPrimary: activeTheme.backgroundPrimary
    property color backgroundSecondary: activeTheme.backgroundSecondary
    property color backgroundGlass: activeTheme.backgroundGlass

    // Accent colors
    property color accentPrimary: activeTheme.accentPrimary
    property color accentSecondary: activeTheme.accentSecondary
    property color accentGradientStart: activeTheme.accentGradientStart
    property color accentGradientEnd: activeTheme.accentGradientEnd

    // Text colors
    property color textPrimary: activeTheme.textPrimary
    property color textSecondary: activeTheme.textSecondary
    property color textDisabled: activeTheme.textDisabled
    
    // Overlays & Gradients
    property color overlayDark: "#60000000"
    property color overlayLight: "#40ffffff"
    property color overlayHighlight: "#10ffffff"
    property color overlayTextBackground: "#aa000000"
    property color gradientOverlayStart: "#400f3880"
    property color gradientOverlayMiddle: "#1c0c2099"
    property color gradientOverlayEnd: "#0a060ccc"

    // UI element properties
    property color focusBorder: accentPrimary
    property color hoverOverlay: Qt.rgba(1, 1, 1, 0.1)
    property color inputBackground: Qt.rgba(1, 1, 1, 0.1)
    property color inputBorder: Qt.rgba(1, 1, 1, 0.2)

    // Border properties
    property color borderLight: Qt.rgba(1, 1, 1, 0.15)
    property int borderWidth: 1

    // Radius values
    property int radiusSmall: 8
    property int radiusMedium: 12
    property int radiusLarge: 16
    property int radiusXLarge: 20

    // Blur and effects
    property int blurRadius: 40
    
    // Frosted Glass Card Properties
    property color cardBackground: activeTheme.cardBackground
    property color cardBackgroundHover: activeTheme.cardBackgroundHover
    property color cardBackgroundFocused: activeTheme.cardBackgroundFocused
    property color cardBorder: activeTheme.cardBorder
    property color cardBorderHover: activeTheme.cardBorderHover
    property color cardBorderFocused: activeTheme.cardBorderFocused
    property int cardBlurRadius: 24
    // Rounded corners for posters/thumbnails; use large for visible rounding
    property int imageRadius: radiusLarge
    
    // Fonts
    property string fontPrimary: "Inter" // Fallback to system if not available
    property string fontIcon: "Material Symbols Outlined" // Icon font for UI elements
    
    property int fontSizeDisplay: 42
    property int fontSizeIcon: 24
    property int fontSizeHeader: 36
    property int fontSizeTitle: 32
    property int fontSizeBody: 24
    property int fontSizeSmall: 20
    property int fontSizeMedium: 28
    
    // Spacing
    property int spacingSmall: 8
    property int spacingMedium: 20
    property int spacingLarge: 32
    property int spacingXLarge: 40
    property int paddingLarge: 40
    
    // Dimensions
    property int cardWidthLarge: 520
    property int cardHeightLarge: 320
    property int cardWidthMedium: 420
    property int cardHeightMedium: 240
    property int cardWidthSmall: 220
    property int cardHeightSmall: 320
    property int posterWidth: 240
    property int posterHeight: 420
    
    // Content Dimensions (DPI-Scaled for different screen resolutions)
    // Baseline is 1440p (dpiScale=1.0) - dimensions multiply by dpiScale for higher resolutions
    // On 4K (dpiScale≈1.5), content gets 1.5x pixels to maintain the same visual size
    property int posterWidthLarge: Math.round(430 * dpiScale)      // ~645px on 4K
    property int posterHeightLarge: Math.round(750 * dpiScale)     // ~1125px on 4K
    property int seasonPosterWidth: Math.round(380 * dpiScale)     // ~570px on 4K
    property int seasonPosterHeight: Math.round(640 * dpiScale)    // ~960px on 4K
    property int episodeCardHeight: Math.round(373 * dpiScale)     // ~560px on 4K
    property int episodeThumbWidth: Math.round(664 * dpiScale)     // ~996px on 4K
    property int nextUpHeight: Math.round(560 * dpiScale)          // ~840px on 4K
    property int nextUpImageHeight: Math.round(280 * dpiScale)     // ~420px on 4K
    
    // Minimum dimensions to prevent content from becoming too small
    property int episodeCardMinHeight: Math.round(300 * dpiScale)
    property int episodeThumbMinWidth: Math.round(400 * dpiScale)
    property int episodeListMinHeight: Math.round(280 * dpiScale)
    
    // Series Details View dimensions (DPI-scaled)
    property int seriesLogoHeight: Math.round(426 * dpiScale)      // Logo height (2x 1440p baseline)
    property int seriesLogoMaxWidth: Math.round(1334 * dpiScale)   // Logo max width (2x baseline)
    property int seriesOverviewMaxHeight: Math.round(160 * dpiScale) // Synopsis collapsed height
    
    // HomeScreen dimensions (DPI-scaled)
    property int homeCardWidthLarge: Math.round(520 * dpiScale)    // ~780px on 4K
    property int homeCardHeightLarge: Math.round(320 * dpiScale)   // ~480px on 4K
    property int homeCardWidthMedium: Math.round(420 * dpiScale)   // ~630px on 4K
    property int homeCardHeightMedium: Math.round(240 * dpiScale)  // ~360px on 4K
    property int recentlyAddedPosterWidth: Math.round(300 * dpiScale)  // ~450px on 4K
    property int recentlyAddedPosterHeight: Math.round(450 * dpiScale) // ~675px on 4K
    
    // Animations
    property int durationShort: 150
    property int durationNormal: 200
    property int durationLong: 300
    property int durationFade: 500
    
    // ============================
    // Glassmorphic Button System
    // ============================
    
    // Primary Button (Play, main actions) - Accent colored with glass effect
    property color buttonPrimaryBackground: activeTheme.buttonPrimaryBackground
    property color buttonPrimaryBackgroundHover: activeTheme.buttonPrimaryBackgroundHover
    property color buttonPrimaryBackgroundPressed: activeTheme.buttonPrimaryBackgroundPressed
    property color buttonPrimaryBorder: activeTheme.buttonPrimaryBorder
    property color buttonPrimaryBorderFocused: activeTheme.buttonPrimaryBorderFocused
    
    // Secondary Button (Mark Watched, Back, etc.) - Frosted glass effect
    property color buttonSecondaryBackground: activeTheme.buttonSecondaryBackground
    property color buttonSecondaryBackgroundHover: activeTheme.buttonSecondaryBackgroundHover
    property color buttonSecondaryBackgroundPressed: activeTheme.buttonSecondaryBackgroundPressed
    property color buttonSecondaryBorder: activeTheme.buttonSecondaryBorder
    property color buttonSecondaryBorderHover: activeTheme.buttonSecondaryBorderHover
    property color buttonSecondaryBorderFocused: activeTheme.buttonSecondaryBorderFocused
    
    // Icon Button (Bookmark, Favorite, etc.) - Subtle glass circles
    property color buttonIconBackground: Qt.rgba(1, 1, 1, 0.06)
    property color buttonIconBackgroundHover: Qt.rgba(1, 1, 1, 0.12)
    property color buttonIconBackgroundPressed: Qt.rgba(1, 1, 1, 0.18)
    property color buttonIconBorder: Qt.rgba(1, 1, 1, 0.12)
    property color buttonIconBorderHover: Qt.rgba(1, 1, 1, 0.2)
    property color buttonIconBorderFocused: accentPrimary
    
    // Tab/Filter Button - For filter tabs in library
    property color buttonTabBackground: Qt.rgba(1, 1, 1, 0.05)
    property color buttonTabBackgroundHover: Qt.rgba(1, 1, 1, 0.1)
    property color buttonTabBackgroundActive: Qt.rgba(0.67, 0.36, 0.76, 0.5)  // accentSecondary with transparency
    property color buttonTabBackgroundPressed: accentPrimary
    property color buttonTabBorder: Qt.rgba(1, 1, 1, 0.1)
    property color buttonTabBorderHover: Qt.rgba(1, 1, 1, 0.2)
    property color buttonTabBorderActive: accentSecondary
    property color buttonTabBorderFocused: accentPrimary
    
    // Button dimensions
    property int buttonHeightLarge: 64
    property int buttonHeightMedium: 56
    property int buttonHeightSmall: 48
    property int buttonIconSize: 56
    property int buttonFocusBorderWidth: 3
    property int buttonBorderWidth: 1
}
