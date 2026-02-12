pragma Singleton
import QtQuick

QtObject {
    // ============================
    // Responsive Layout System
    // ============================
    // The layoutScale comes from ResponsiveLayoutManager C++ class which:
    // - Detects viewport height and devicePixelRatio
    // - Calculates breakpoint (Small/Medium/Large/XL)
    // - Provides continuous scaling within breakpoint ranges
    // - Handles ultrawide aspect ratio adjustments
    
    // Layout scale from ResponsiveLayoutManager (0.6 - 1.5)
    property real layoutScale: typeof ResponsiveLayoutManager !== 'undefined' ? ResponsiveLayoutManager.layoutScale : 1.0
    
    // Breakpoint info from ResponsiveLayoutManager
    property string breakpoint: typeof ResponsiveLayoutManager !== 'undefined' ? ResponsiveLayoutManager.breakpoint : "Large"
    property int gridColumns: typeof ResponsiveLayoutManager !== 'undefined' ? ResponsiveLayoutManager.gridColumns : 6
    property int homeRowVisibleItems: typeof ResponsiveLayoutManager !== 'undefined' ? ResponsiveLayoutManager.homeRowVisibleItems : 6
    property string sidebarDefaultMode: typeof ResponsiveLayoutManager !== 'undefined' ? ResponsiveLayoutManager.sidebarDefaultMode : "rail"
    property real aspectRatio: typeof ResponsiveLayoutManager !== 'undefined' ? ResponsiveLayoutManager.aspectRatio : 1.778
    property int viewportWidth: typeof ResponsiveLayoutManager !== 'undefined' ? ResponsiveLayoutManager.viewportWidth : 1920
    property int viewportHeight: typeof ResponsiveLayoutManager !== 'undefined' ? ResponsiveLayoutManager.viewportHeight : 1080
    
    // Backward compatibility alias
    property real dpiScale: layoutScale
    
    // Manual DPI scale override from user settings (1.0 = automatic)
    property real manualDpiScaleOverride: typeof ConfigManager !== 'undefined' ? ConfigManager.manualDpiScaleOverride : 1.0
    
    // Animation control from ConfigManager
    property bool uiAnimationsEnabled: typeof ConfigManager !== 'undefined' ? ConfigManager.uiAnimationsEnabled : true
    
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

    // Radius values (responsive)
    property int radiusSmall: Math.round(8 * layoutScale)
    property int radiusMedium: Math.round(12 * layoutScale)
    property int radiusLarge: Math.round(16 * layoutScale)
    property int radiusXLarge: Math.round(20 * layoutScale)

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
    
    // ============================
    // Responsive Typography
    // ============================
    // Font sizes scale with layoutScale for consistent visual proportions
    property string fontPrimary: "Inter" // Fallback to system if not available
    property string fontIcon: "Material Symbols Outlined" // Icon font for UI elements
    
    // Base font sizes (at layoutScale 1.0)
    property int fontSizeDisplayBase: 42
    property int fontSizeIconBase: 24
    property int fontSizeHeaderBase: 36
    property int fontSizeTitleBase: 32
    property int fontSizeBodyBase: 24
    property int fontSizeSmallBase: 20
    property int fontSizeMediumBase: 28
    
    // Scaled font sizes
    property int fontSizeDisplay: Math.round(fontSizeDisplayBase * layoutScale)
    property int fontSizeIcon: Math.round(fontSizeIconBase * layoutScale)
    property int fontSizeHeader: Math.round(fontSizeHeaderBase * layoutScale)
    property int fontSizeTitle: Math.round(fontSizeTitleBase * layoutScale)
    property int fontSizeBody: Math.round(fontSizeBodyBase * layoutScale)
    property int fontSizeSmall: Math.round(fontSizeSmallBase * layoutScale)
    property int fontSizeMedium: Math.round(fontSizeMediumBase * layoutScale)
    
    // ============================
    // Responsive Spacing
    // ============================
    // Base spacing values (at layoutScale 1.0)
    property int spacingSmallBase: 8
    property int spacingMediumBase: 20
    property int spacingLargeBase: 32
    property int spacingXLargeBase: 40
    property int paddingLargeBase: 40
    
    // Scaled spacing
    property int spacingSmall: Math.round(spacingSmallBase * layoutScale)
    property int spacingMedium: Math.round(spacingMediumBase * layoutScale)
    property int spacingLarge: Math.round(spacingLargeBase * layoutScale)
    property int spacingXLarge: Math.round(spacingXLargeBase * layoutScale)
    property int paddingLarge: Math.round(paddingLargeBase * layoutScale)
    
    // ============================
    // Responsive Dimensions
    // ============================
    // Base card dimensions (at layoutScale 1.0)
    property int cardWidthLargeBase: 520
    property int cardHeightLargeBase: 320
    property int cardWidthMediumBase: 420
    property int cardHeightMediumBase: 240
    property int cardWidthSmallBase: 220
    property int cardHeightSmallBase: 320
    property int posterWidthBase: 240
    property int posterHeightBase: 420
    
    // Scaled card dimensions
    property int cardWidthLarge: Math.round(cardWidthLargeBase * layoutScale)
    property int cardHeightLarge: Math.round(cardHeightLargeBase * layoutScale)
    property int cardWidthMedium: Math.round(cardWidthMediumBase * layoutScale)
    property int cardHeightMedium: Math.round(cardHeightMediumBase * layoutScale)
    property int cardWidthSmall: Math.round(cardWidthSmallBase * layoutScale)
    property int cardHeightSmall: Math.round(cardHeightSmallBase * layoutScale)
    property int posterWidth: Math.round(posterWidthBase * layoutScale)
    property int posterHeight: Math.round(posterHeightBase * layoutScale)
    
    // ============================
    // Content Dimensions (Responsive)
    // ============================
    // Min/max constraints for large content dimensions (Fixed pixel limits)
    property int posterWidthLargeMin: 300
    property int posterWidthLargeMax: 700
    property int seasonPosterHeightMin: 480
    property int seasonPosterHeightMax: 1100
    property int episodeCardHeightMin: 280
    property int episodeCardHeightMax: 700

    // Base content dimensions (at layoutScale 1.0)
    property int posterWidthLargeBase: 430
    property int posterHeightLargeBase: 750
    property int seasonPosterWidthBase: 380
    property int seasonPosterHeightBase: 640
    property int episodeCardHeightBase: 373
    property int episodeThumbWidthBase: 664
    property int nextUpHeightBase: 560
    property int nextUpImageHeightBase: 280
    property int episodeCardMinHeightBase: 300
    property int episodeThumbMinWidthBase: 400
    property int episodeListMinHeightBase: 280
    property int seriesLogoHeightBase: 426
    property int seriesLogoMaxWidthBase: 1334
    property int seriesOverviewMaxHeightBase: 160
    property int homeCardWidthLargeBase: 520
    property int homeCardHeightLargeBase: 320
    property int homeCardWidthMediumBase: 420
    property int homeCardHeightMediumBase: 240
    property int recentlyAddedPosterWidthBase: 300
    property int recentlyAddedPosterHeightBase: 450

    // Scaled content dimensions
    property int posterWidthLarge: Math.min(posterWidthLargeMax, Math.max(posterWidthLargeMin, Math.round(posterWidthLargeBase * layoutScale)))
    property int posterHeightLarge: Math.round(posterHeightLargeBase * layoutScale)
    property int seasonPosterWidth: Math.round(seasonPosterWidthBase * layoutScale)
    property int seasonPosterHeight: Math.min(seasonPosterHeightMax, Math.max(seasonPosterHeightMin, Math.round(seasonPosterHeightBase * layoutScale)))
    property int episodeCardHeight: Math.min(episodeCardHeightMax, Math.max(episodeCardHeightMin, Math.round(episodeCardHeightBase * layoutScale)))
    property int episodeThumbWidth: Math.round(episodeThumbWidthBase * layoutScale)
    property int nextUpHeight: Math.round(nextUpHeightBase * layoutScale)
    property int nextUpImageHeight: Math.round(nextUpImageHeightBase * layoutScale)
    
    // Minimum dimensions to prevent content from becoming too small
    property int episodeCardMinHeight: Math.round(episodeCardMinHeightBase * layoutScale)
    
    // placeholder for future responsive rules
    property int episodeThumbMinWidth: Math.round(episodeThumbMinWidthBase * layoutScale)
    // placeholder for future responsive rules
    property int episodeListMinHeight: Math.round(episodeListMinHeightBase * layoutScale)
    
    // Episode list height to accommodate full 16:9 cards with text labels
    property int episodeListHeight: Math.round(episodeThumbWidth * 9 / 16 + 50 * layoutScale)
    
    // Series Details View dimensions (responsive)
    property int seriesLogoHeight: Math.round(seriesLogoHeightBase * layoutScale)
    property int seriesLogoMaxWidth: Math.round(seriesLogoMaxWidthBase * layoutScale)
    property int seriesOverviewMaxHeight: Math.round(seriesOverviewMaxHeightBase * layoutScale)
    
    // HomeScreen dimensions (responsive)
    property int homeCardWidthLarge: Math.round(homeCardWidthLargeBase * layoutScale)
    property int homeCardHeightLarge: Math.round(homeCardHeightLargeBase * layoutScale)
    property int homeCardWidthMedium: Math.round(homeCardWidthMediumBase * layoutScale)
    property int homeCardHeightMedium: Math.round(homeCardHeightMediumBase * layoutScale)
    property int recentlyAddedPosterWidth: Math.round(recentlyAddedPosterWidthBase * layoutScale)
    property int recentlyAddedPosterHeight: Math.round(recentlyAddedPosterHeightBase * layoutScale)
    
    // ============================
    // Animations
    // ============================
    // Animation durations are fixed (not scaled) for consistent feel
    property int durationShort: uiAnimationsEnabled ? 150 : 0
    property int durationNormal: uiAnimationsEnabled ? 200 : 0
    property int durationLong: uiAnimationsEnabled ? 300 : 0
    property int durationFade: uiAnimationsEnabled ? 500 : 0
    
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
    
    // Button dimensions (responsive)
    property int buttonHeightLarge: Math.round(64 * layoutScale)
    property int buttonHeightMedium: Math.round(56 * layoutScale)
    property int buttonHeightSmall: Math.round(48 * layoutScale)
    property int buttonIconSize: Math.round(56 * layoutScale)
    property int buttonFocusBorderWidth: Math.max(2, Math.round(3 * layoutScale))
    property int buttonBorderWidth: 1
}
