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
    
    property string currentTheme: typeof ConfigManager !== 'undefined' ? (ConfigManager.theme || "Jellyfin") : "Jellyfin"
    property string currentFlavor: validFlavorIdForTheme(currentTheme, typeof ConfigManager !== 'undefined' ? ConfigManager.themeFlavor : "")
    property string currentColorScheme: validColorSchemeIdForTheme(currentTheme, typeof ConfigManager !== 'undefined' ? ConfigManager.themeColorScheme : "blue")
    property var themeNames: ["Jellyfin", "Rosé Pine", "Catppuccin"]
    property var flavorNames: optionLabels(themeDefinition(currentTheme).flavors || [])
    property var colorSchemeNames: optionLabels(themeDefinition(currentTheme).colorSchemes || [])
    property string currentFlavorLabel: optionLabelForId(themeDefinition(currentTheme).flavors || [], currentFlavor)
    property string currentColorSchemeLabel: optionLabelForId(themeDefinition(currentTheme).colorSchemes || [], currentColorScheme)

    property var themeCatalog: ({
        "Jellyfin": {
            "id": "jellyfin",
            "label": "Jellyfin",
            "defaultFlavor": "",
            "defaultColorScheme": "blue",
            "colorSchemes": [
                { "id": "blue", "label": "Blue" },
                { "id": "purple", "label": "Purple" },
                { "id": "blend", "label": "Blend" }
            ]
        },
        "Rosé Pine": {
            "id": "rose-pine",
            "label": "Rosé Pine",
            "defaultFlavor": "main",
            "defaultColorScheme": "foam",
            "flavors": [
                { "id": "main", "label": "Main" },
                { "id": "moon", "label": "Moon" },
                { "id": "dawn", "label": "Dawn" }
            ],
            "colorSchemes": [
                { "id": "love", "label": "Love" },
                { "id": "gold", "label": "Gold" },
                { "id": "rose", "label": "Rose" },
                { "id": "pine", "label": "Pine" },
                { "id": "foam", "label": "Foam" },
                { "id": "iris", "label": "Iris" }
            ]
        },
        "Catppuccin": {
            "id": "catppuccin",
            "label": "Catppuccin",
            "defaultFlavor": "mocha",
            "defaultColorScheme": "mauve",
            "flavors": [
                { "id": "latte", "label": "Latte" },
                { "id": "frappe", "label": "Frappé" },
                { "id": "macchiato", "label": "Macchiato" },
                { "id": "mocha", "label": "Mocha" }
            ],
            "colorSchemes": [
                { "id": "rosewater", "label": "Rosewater" },
                { "id": "flamingo", "label": "Flamingo" },
                { "id": "pink", "label": "Pink" },
                { "id": "mauve", "label": "Mauve" },
                { "id": "red", "label": "Red" },
                { "id": "maroon", "label": "Maroon" },
                { "id": "peach", "label": "Peach" },
                { "id": "yellow", "label": "Yellow" },
                { "id": "green", "label": "Green" },
                { "id": "teal", "label": "Teal" },
                { "id": "sky", "label": "Sky" },
                { "id": "sapphire", "label": "Sapphire" },
                { "id": "blue", "label": "Blue" },
                { "id": "lavender", "label": "Lavender" }
            ]
        }
    })

    property var catppuccinPalettes: ({
        "latte": {
            "rosewater": "#dc8a78", "flamingo": "#dd7878", "pink": "#ea76cb", "mauve": "#8839ef",
            "red": "#d20f39", "maroon": "#e64553", "peach": "#fe640b", "yellow": "#df8e1d",
            "green": "#40a02b", "teal": "#179299", "sky": "#04a5e5", "sapphire": "#209fb5",
            "blue": "#1e66f5", "lavender": "#7287fd", "text": "#4c4f69", "subtext1": "#5c5f77",
            "subtext0": "#6c6f85", "overlay2": "#7c7f93", "overlay1": "#8c8fa1", "overlay0": "#9ca0b0",
            "surface2": "#acb0be", "surface1": "#bcc0cc", "surface0": "#ccd0da", "base": "#eff1f5",
            "mantle": "#e6e9ef", "crust": "#dce0e8", "isLight": true
        },
        "frappe": {
            "rosewater": "#f2d5cf", "flamingo": "#eebebe", "pink": "#f4b8e4", "mauve": "#ca9ee6",
            "red": "#e78284", "maroon": "#ea999c", "peach": "#ef9f76", "yellow": "#e5c890",
            "green": "#a6d189", "teal": "#81c8be", "sky": "#99d1db", "sapphire": "#85c1dc",
            "blue": "#8caaee", "lavender": "#babbf1", "text": "#c6d0f5", "subtext1": "#b5bfe2",
            "subtext0": "#a5adce", "overlay2": "#949cbb", "overlay1": "#838ba7", "overlay0": "#737994",
            "surface2": "#626880", "surface1": "#51576d", "surface0": "#414559", "base": "#303446",
            "mantle": "#292c3c", "crust": "#232634", "isLight": false
        },
        "macchiato": {
            "rosewater": "#f4dbd6", "flamingo": "#f0c6c6", "pink": "#f5bde6", "mauve": "#c6a0f6",
            "red": "#ed8796", "maroon": "#ee99a0", "peach": "#f5a97f", "yellow": "#eed49f",
            "green": "#a6da95", "teal": "#8bd5ca", "sky": "#91d7e3", "sapphire": "#7dc4e4",
            "blue": "#8aadf4", "lavender": "#b7bdf8", "text": "#cad3f5", "subtext1": "#b8c0e0",
            "subtext0": "#a5adcb", "overlay2": "#939ab7", "overlay1": "#8087a2", "overlay0": "#6e738d",
            "surface2": "#5b6078", "surface1": "#494d64", "surface0": "#363a4f", "base": "#24273a",
            "mantle": "#1e2030", "crust": "#181926", "isLight": false
        },
        "mocha": {
            "rosewater": "#f5e0dc", "flamingo": "#f2cdcd", "pink": "#f5c2e7", "mauve": "#cba6f7",
            "red": "#f38ba8", "maroon": "#eba0ac", "peach": "#fab387", "yellow": "#f9e2af",
            "green": "#a6e3a1", "teal": "#94e2d5", "sky": "#89dceb", "sapphire": "#74c7ec",
            "blue": "#89b4fa", "lavender": "#b4befe", "text": "#cdd6f4", "subtext1": "#bac2de",
            "subtext0": "#a6adc8", "overlay2": "#9399b2", "overlay1": "#7f849c", "overlay0": "#6c7086",
            "surface2": "#585b70", "surface1": "#45475a", "surface0": "#313244", "base": "#1e1e2e",
            "mantle": "#181825", "crust": "#11111b", "isLight": false
        }
    })

    property var rosePinePalettes: ({
        "main": {
            "base": "#191724", "surface": "#1f1d2e", "overlay": "#26233a", "muted": "#6e6a86",
            "subtle": "#908caa", "text": "#e0def4", "love": "#eb6f92", "gold": "#f6c177",
            "rose": "#ebbcba", "pine": "#31748f", "foam": "#9ccfd8", "iris": "#c4a7e7",
            "highlightLow": "#21202e", "highlightMed": "#403d52", "highlightHigh": "#524f67",
            "isLight": false
        },
        "moon": {
            "base": "#232136", "surface": "#2a273f", "overlay": "#393552", "muted": "#6e6a86",
            "subtle": "#908caa", "text": "#e0def4", "love": "#eb6f92", "gold": "#f6c177",
            "rose": "#ea9a97", "pine": "#3e8fb0", "foam": "#9ccfd8", "iris": "#c4a7e7",
            "highlightLow": "#2a283e", "highlightMed": "#44415a", "highlightHigh": "#56526e",
            "isLight": false
        },
        "dawn": {
            "base": "#faf4ed", "surface": "#fffaf3", "overlay": "#f2e9e1", "muted": "#9893a5",
            "subtle": "#797593", "text": "#575279", "love": "#b4637a", "gold": "#ea9d34",
            "rose": "#d7827e", "pine": "#286983", "foam": "#56949f", "iris": "#907aa9",
            "highlightLow": "#f4ede8", "highlightMed": "#dfdad9", "highlightHigh": "#cecacd",
            "isLight": true
        }
    })

    function themeDefinition(themeName) {
        return themeCatalog[themeName] || themeCatalog["Jellyfin"]
    }

    function optionLabels(options) {
        var labels = []
        for (var i = 0; i < options.length; ++i)
            labels.push(options[i].label)
        return labels
    }

    function optionIdForLabel(options, label) {
        for (var i = 0; i < options.length; ++i) {
            if (options[i].label === label)
                return options[i].id
        }
        return ""
    }

    function optionLabelForId(options, id) {
        for (var i = 0; i < options.length; ++i) {
            if (options[i].id === id)
                return options[i].label
        }
        return options.length > 0 ? options[0].label : ""
    }

    function hasOptionId(options, id) {
        for (var i = 0; i < options.length; ++i) {
            if (options[i].id === id)
                return true
        }
        return false
    }

    function defaultFlavorForTheme(themeName) {
        return themeDefinition(themeName).defaultFlavor || ""
    }

    function defaultColorSchemeForTheme(themeName) {
        return themeDefinition(themeName).defaultColorScheme || ""
    }

    function validFlavorIdForTheme(themeName, flavorId) {
        var def = themeDefinition(themeName)
        var options = def.flavors || []
        if (options.length === 0)
            return ""
        return hasOptionId(options, flavorId) ? flavorId : (def.defaultFlavor || options[0].id)
    }

    function validColorSchemeIdForTheme(themeName, colorSchemeId) {
        var def = themeDefinition(themeName)
        var options = def.colorSchemes || []
        if (options.length === 0)
            return ""
        return hasOptionId(options, colorSchemeId) ? colorSchemeId : (def.defaultColorScheme || options[0].id)
    }

    function colorWithAlpha(hex, alpha) {
        if (!hex || hex.length < 7)
            return Qt.rgba(1, 1, 1, alpha)
        return Qt.rgba(parseInt(hex.substr(1, 2), 16) / 255,
                       parseInt(hex.substr(3, 2), 16) / 255,
                       parseInt(hex.substr(5, 2), 16) / 255,
                       alpha)
    }

    function paletteTokens(palette, accentId, secondaryId) {
        var accent = palette[accentId] || palette.blue || palette.foam
        var secondary = palette[secondaryId] || palette.lavender || palette.iris || accent
        var isLight = palette.isLight === true
        return {
            "backgroundPrimary": palette.base,
            "backgroundSecondary": palette.mantle || palette.surface,
            "backgroundGlass": colorWithAlpha(palette.base, 0.60),
            "isLight": isLight,
            "accentPrimary": accent,
            "accentSecondary": secondary,
            "accentGradientStart": secondary,
            "accentGradientEnd": accent,
            "textPrimary": palette.text,
            "textSecondary": palette.subtext1 || palette.subtle,
            "textDisabled": palette.overlay0 || palette.muted,
            "textOnAccent": palette.base,
            "cardBackground": colorWithAlpha(palette.surface0 || palette.surface, 0.65),
            "cardBackgroundHover": colorWithAlpha(palette.surface1 || palette.overlay, 0.70),
            "cardBackgroundFocused": colorWithAlpha(palette.surface2 || palette.highlightMed, 0.75),
            "cardBorder": colorWithAlpha(palette.overlay0 || palette.muted, 0.35),
            "cardBorderHover": colorWithAlpha(palette.overlay1 || palette.subtle, 0.45),
            "cardBorderFocused": accent,
            "buttonPrimaryBackground": colorWithAlpha(accent, 0.85),
            "buttonPrimaryBackgroundHover": colorWithAlpha(accent, 0.95),
            "buttonPrimaryBackgroundPressed": colorWithAlpha(accent, 1.0),
            "buttonPrimaryBorder": colorWithAlpha(palette.text, 0.25),
            "buttonPrimaryBorderFocused": colorWithAlpha(palette.text, 0.9),
            "buttonSecondaryBackground": colorWithAlpha(palette.text, isLight ? 0.07 : 0.08),
            "buttonSecondaryBackgroundDisabled": colorWithAlpha(palette.crust || palette.muted, 0.18),
            "buttonSecondaryBackgroundHover": colorWithAlpha(palette.text, isLight ? 0.12 : 0.15),
            "buttonSecondaryBackgroundPressed": colorWithAlpha(palette.text, isLight ? 0.17 : 0.20),
            "buttonSecondaryBorder": colorWithAlpha(palette.text, 0.15),
            "buttonSecondaryBorderHover": colorWithAlpha(palette.text, 0.25),
            "buttonSecondaryBorderFocused": accent,
            "chipBackground": colorWithAlpha(palette.crust || palette.surface, isLight ? 0.12 : 0.28),
            "chipBorder": colorWithAlpha(palette.text, 0.12),
            "hoverOverlay": colorWithAlpha(palette.text, isLight ? 0.08 : 0.10),
            "inputBackground": colorWithAlpha(palette.text, isLight ? 0.07 : 0.10),
            "inputBorder": colorWithAlpha(palette.text, 0.20),
            "borderLight": colorWithAlpha(palette.text, 0.15)
        }
    }

    function jellyfinTokens(colorScheme) {
        var primary = "#00A4DC"
        var secondary = "#AA5CC3"
        if (colorScheme === "purple") {
            primary = "#AA5CC3"
            secondary = "#00A4DC"
        } else if (colorScheme === "blend") {
            primary = "#5580D0"
            secondary = "#00A4DC"
        }
        return {
            "backgroundPrimary": "#000B25",
            "backgroundSecondary": "#101010",
            "backgroundGlass": Qt.rgba(0.1, 0.1, 0.1, 0.6),
            "isLight": false,
            "accentPrimary": primary,
            "accentSecondary": secondary,
            "accentGradientStart": secondary,
            "accentGradientEnd": primary,
            "textPrimary": "#FFFFFF",
            "textSecondary": "#B3B3B3",
            "textDisabled": "#666666",
            "textOnAccent": "#191724",
            "cardBackground": Qt.rgba(0.15, 0.15, 0.18, 0.65),
            "cardBackgroundHover": Qt.rgba(0.2, 0.2, 0.23, 0.7),
            "cardBackgroundFocused": Qt.rgba(0.25, 0.25, 0.28, 0.75),
            "cardBorder": Qt.rgba(1, 1, 1, 0.12),
            "cardBorderHover": Qt.rgba(1, 1, 1, 0.2),
            "cardBorderFocused": primary,
            "buttonPrimaryBackground": colorWithAlpha(primary, 0.85),
            "buttonPrimaryBackgroundHover": colorWithAlpha(primary, 0.9),
            "buttonPrimaryBackgroundPressed": colorWithAlpha(primary, 0.95),
            "buttonPrimaryBorder": Qt.rgba(1, 1, 1, 0.25),
            "buttonPrimaryBorderFocused": Qt.rgba(1, 1, 1, 0.9),
            "buttonSecondaryBackground": Qt.rgba(1, 1, 1, 0.08),
            "buttonSecondaryBackgroundDisabled": Qt.rgba(0, 0, 0, 0.18),
            "buttonSecondaryBackgroundHover": Qt.rgba(1, 1, 1, 0.15),
            "buttonSecondaryBackgroundPressed": Qt.rgba(1, 1, 1, 0.2),
            "buttonSecondaryBorder": Qt.rgba(1, 1, 1, 0.15),
            "buttonSecondaryBorderHover": Qt.rgba(1, 1, 1, 0.25),
            "buttonSecondaryBorderFocused": primary,
            "chipBackground": Qt.rgba(0, 0, 0, 0.28),
            "chipBorder": Qt.rgba(1, 1, 1, 0.12),
            "hoverOverlay": Qt.rgba(1, 1, 1, 0.1),
            "inputBackground": Qt.rgba(1, 1, 1, 0.1),
            "inputBorder": Qt.rgba(1, 1, 1, 0.2),
            "borderLight": Qt.rgba(1, 1, 1, 0.15)
        }
    }

    function resolveThemeTokens(themeName, flavorId, colorSchemeId) {
        if (themeName === "Catppuccin") {
            return paletteTokens(catppuccinPalettes[validFlavorIdForTheme(themeName, flavorId)] || catppuccinPalettes.mocha,
                                 validColorSchemeIdForTheme(themeName, colorSchemeId),
                                 "lavender")
        }
        if (themeName === "Rosé Pine") {
            return paletteTokens(rosePinePalettes[validFlavorIdForTheme(themeName, flavorId)] || rosePinePalettes.main,
                                 validColorSchemeIdForTheme(themeName, colorSchemeId),
                                 "iris")
        }
        return jellyfinTokens(validColorSchemeIdForTheme("Jellyfin", colorSchemeId))
    }

    // Helper to get current theme colors
    property var activeTheme: resolveThemeTokens(currentTheme, currentFlavor, currentColorScheme)

    // Background colors
    property color backgroundPrimary: activeTheme.backgroundPrimary
    property color backgroundSecondary: activeTheme.backgroundSecondary
    property color backgroundGlass: activeTheme.backgroundGlass
    property bool themeIsLight: activeTheme.isLight === true

    // Accent colors
    property color accentPrimary: activeTheme.accentPrimary
    property color accentSecondary: activeTheme.accentSecondary
    property color accentGradientStart: activeTheme.accentGradientStart
    property color accentGradientEnd: activeTheme.accentGradientEnd
    property color accentColor: accentPrimary

    // Text colors
    property color textPrimary: activeTheme.textPrimary
    property color textSecondary: activeTheme.textSecondary
    property color textDisabled: activeTheme.textDisabled
    property color textMuted: textSecondary
    property color chipBackground: activeTheme.chipBackground
    property color chipBorder: activeTheme.chipBorder
    property color textOnAccent: activeTheme.textOnAccent || "#191724"
    property color errorColor: "#ff6b6b"
    property color colorDestructive: errorColor
    property color destructiveBorder: colorDestructive
    property color destructiveHoverBg: Qt.rgba(colorDestructive.r, colorDestructive.g, colorDestructive.b, 0.2)
    
    // Overlays & Gradients
    property color overlayDark: "#60000000"
    property color overlayLight: "#40ffffff"
    property color overlayHighlight: "#10ffffff"
    // Opaque enough for badges/labels drawn over arbitrary poster artwork.
    property color overlayTextBackground: "#b8000000"
    property color overlayTextBorder: Qt.rgba(1, 1, 1, 0.18)
    property color textOnDarkOverlay: "#ffffff"
    property color gradientOverlayStart: "#400f3880"
    property color gradientOverlayMiddle: "#1c0c2099"
    property color gradientOverlayEnd: "#0a060ccc"
    property color playbackOverlayTopTint: accentPrimary
    property color playbackOverlayBottomTint: "#000000"
    property color playbackOverlayShadowTint: themeIsLight ? "#000000" : backgroundPrimary
    property color playbackOverlayAccentShadowTint: accentPrimary
    property color playbackControlGlassBackground: Qt.rgba(1, 1, 1, 0.10)
    property color playbackControlGlassBackgroundHover: Qt.rgba(1, 1, 1, 0.20)
    property color playbackControlGlassBorder: Qt.rgba(1, 1, 1, 0.20)
    property color playbackControlGlassBorderStrong: Qt.rgba(1, 1, 1, 0.40)
    property color playbackProgressTrack: Qt.rgba(1, 1, 1, 0.20)
    property color playbackProgressFill: Qt.rgba(1, 1, 1, 0.90)
    property color playbackProgressCacheFill: Qt.rgba(1, 1, 1, 0.35)
    property color playbackTimePrimary: Qt.rgba(1, 1, 1, 0.90)
    property color playbackTimeSecondary: Qt.rgba(1, 1, 1, 0.70)
    property color playbackIconColor: accentPrimary

    // UI element properties
    property color focusBorder: accentPrimary
    property color hoverOverlay: activeTheme.hoverOverlay || Qt.rgba(1, 1, 1, 0.1)
    property color inputBackground: activeTheme.inputBackground || Qt.rgba(1, 1, 1, 0.1)
    property color inputBorder: activeTheme.inputBorder || Qt.rgba(1, 1, 1, 0.2)

    // Border properties
    property color borderLight: activeTheme.borderLight || Qt.rgba(1, 1, 1, 0.15)
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
    property int fontSizeCaptionBase: 16
    property int fontSizeMediumBase: 28
    
    // Scaled font sizes
    property int fontSizeDisplay: Math.round(fontSizeDisplayBase * layoutScale)
    property int fontSizeIcon: Math.round(fontSizeIconBase * layoutScale)
    property int fontSizeHeader: Math.round(fontSizeHeaderBase * layoutScale)
    property int fontSizeTitle: Math.round(fontSizeTitleBase * layoutScale)
    property int fontSizeBody: Math.round(fontSizeBodyBase * layoutScale)
    property int fontSizeSmall: Math.round(fontSizeSmallBase * layoutScale)
    property int fontSizeCaption: Math.round(fontSizeCaptionBase * layoutScale)
    property int fontSizeMedium: Math.round(fontSizeMediumBase * layoutScale)
    property int iconSizeSmall: fontSizeSmall
    property int iconSizeMedium: fontSizeIcon
    property int iconSizeLarge: fontSizeMedium
    
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
    property int spacingXSmall: Math.max(4, Math.round(spacingSmall * 0.5))
    property int spacingLg: spacingLarge
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
    property int detailViewLogoHeightBase: 132
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

    readonly property real detailViewLogoScaleBoost: breakpoint === "XL" ? 1.28
        : breakpoint === "Large" ? 1.14
        : 1.0
    property int detailViewLogoHeight: Math.min(
        seriesLogoHeight,
        Math.round(detailViewLogoHeightBase * layoutScale * detailViewLogoScaleBoost)
    )
    
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
    property int durationMedium: durationNormal
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
    property color buttonSecondaryBackgroundDisabled: activeTheme.buttonSecondaryBackgroundDisabled
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
