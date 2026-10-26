pragma Singleton
import QtQuick

QtObject {
    // ============================
    // Responsive Layout System
    // ============================
    // Tokens below are design units on a fixed 16:9 canvas. layoutScale from
    // ResponsiveLayoutManager maps that canvas onto the window, so every
    // element covers the same fraction of the screen at any resolution or OS
    // display scaling (10-foot / Kodi-style). Any pixel size in QML must be
    // multiplied by layoutScale (or come from a Theme token) to stay in step.
    // Wider-than-16:9 viewports keep the same scale and add grid columns.

    // Layout scale from ResponsiveLayoutManager (logical px per design unit)
    property real layoutScale: typeof ResponsiveLayoutManager !== 'undefined' ? ResponsiveLayoutManager.layoutScale : 1.0

    // Grid and sidebar info from ResponsiveLayoutManager
    property int gridColumns: typeof ResponsiveLayoutManager !== 'undefined' ? ResponsiveLayoutManager.gridColumns : 7
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

    property string currentTheme: typeof ConfigManager !== 'undefined' ? (ConfigManager.theme || "Bloom") : "Bloom"
    property string currentFlavor: validFlavorIdForTheme(currentTheme, typeof ConfigManager !== 'undefined' ? ConfigManager.themeFlavor : "")
    property string currentColorScheme: validColorSchemeIdForTheme(currentTheme, typeof ConfigManager !== 'undefined' ? ConfigManager.themeColorScheme : "")
    property var themeNames: ["Bloom", "Jellyfin", "Rosé Pine", "Catppuccin"]
    property var flavorNames: optionLabels(themeDefinition(currentTheme).flavors || [])
    property var colorSchemeNames: optionLabels(themeDefinition(currentTheme).colorSchemes || [])
    property string currentFlavorLabel: optionLabelForId(themeDefinition(currentTheme).flavors || [], currentFlavor)
    property string currentColorSchemeLabel: optionLabelForId(themeDefinition(currentTheme).colorSchemes || [], currentColorScheme)

    property var themeCatalog: ({
        "Bloom": {
            "id": "bloom",
            "label": "Bloom",
            "defaultFlavor": "dusk",
            "defaultColorScheme": "lavender",
            "flavors": [
                { "id": "dusk", "label": "Dusk" },
                { "id": "midnight", "label": "Midnight" },
                { "id": "dawn", "label": "Dawn" }
            ],
            "colorSchemes": [
                { "id": "lavender", "label": "Lavender" },
                { "id": "orchid", "label": "Orchid" },
                { "id": "rose", "label": "Rose" },
                { "id": "peach", "label": "Peach" },
                { "id": "sky", "label": "Sky" },
                { "id": "mint", "label": "Mint" }
            ]
        },
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

    // Bloom's own plum/lavender palette. Keys follow the Catppuccin shape so the
    // shared paletteTokens() resolver handles every palette-based theme.
    property var bloomPalettes: ({
        "dusk": {
            "lavender": "#c7b5f7", "orchid": "#e2a8ec", "rose": "#f3a9c6", "peach": "#f6c09f",
            "sky": "#a2c9f4", "mint": "#9edcc6", "red": "#ff8f9c", "green": "#93d9ac", "yellow": "#f2cd84",
            "text": "#f5f1fb", "subtext1": "#cdc3dc", "subtext0": "#b2a7c4", "overlay2": "#998eac",
            "overlay1": "#857a99", "overlay0": "#71678a", "surface2": "#4a3e60", "surface1": "#3a2f4d",
            "surface0": "#2a2139", "base": "#18121f", "mantle": "#120d18", "crust": "#0c0911",
            "isLight": false
        },
        "midnight": {
            "lavender": "#c2b0f5", "orchid": "#dda2e8", "rose": "#f0a3c1", "peach": "#f3bb98",
            "sky": "#9cc4f1", "mint": "#97d8c1", "red": "#ff8f9c", "green": "#93d9ac", "yellow": "#f2cd84",
            "text": "#efeaf6", "subtext1": "#c4bad3", "subtext0": "#a99fba", "overlay2": "#8f85a2",
            "overlay1": "#7a708e", "overlay0": "#665d7b", "surface2": "#362d47", "surface1": "#281f37",
            "surface0": "#1b1526", "base": "#0a080d", "mantle": "#060508", "crust": "#030204",
            "isLight": false
        },
        "dawn": {
            "lavender": "#6a4fc0", "orchid": "#9445a3", "rose": "#ad4673", "peach": "#a85a2e",
            "sky": "#326fae", "mint": "#257f62", "red": "#b8304a", "green": "#2c7f50", "yellow": "#8a6000",
            "text": "#271f33", "subtext1": "#4f4462", "subtext0": "#625775", "overlay2": "#776c8a",
            "overlay1": "#8a809c", "overlay0": "#9c93ad", "surface2": "#c9bfdb", "surface1": "#dad2e8",
            "surface0": "#e8e2f1", "base": "#f8f5fc", "mantle": "#f0ebf7", "crust": "#e6dff0",
            "isLight": true
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
        return themeCatalog[themeName] || themeCatalog["Bloom"]
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

    // Returns `color` with its alpha replaced. Works for any QML color value.
    function withAlpha(color, alpha) {
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }

    // Linear mix of two colors; amount 0 returns `a`, 1 returns `b`.
    function mix(a, b, amount) {
        return Qt.rgba(a.r + (b.r - a.r) * amount,
                       a.g + (b.g - a.g) * amount,
                       a.b + (b.b - a.b) * amount,
                       a.a + (b.a - a.a) * amount)
    }

    function paletteTokens(palette, accentId, secondaryId) {
        var accent = palette[accentId] || palette.blue || palette.foam || palette.lavender
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
            "surfaceTint": palette.surface0 || palette.surface,
            "errorColor": palette.red || palette.love,
            "successColor": palette.green || palette.pine || palette.mint,
            "warningColor": palette.yellow || palette.gold
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
            "backgroundSecondary": "#05081a",
            "backgroundGlass": Qt.rgba(0.1, 0.1, 0.1, 0.6),
            "isLight": false,
            "accentPrimary": primary,
            "accentSecondary": secondary,
            "accentGradientStart": secondary,
            "accentGradientEnd": primary,
            "textPrimary": "#FFFFFF",
            "textSecondary": "#BDC3D1",
            "textDisabled": "#6B7385",
            "textOnAccent": "#06101f",
            "surfaceTint": "#151d33",
            "errorColor": "#ff7b7b",
            "successColor": "#7fd69a",
            "warningColor": "#f2c46d"
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
        if (themeName === "Jellyfin") {
            return jellyfinTokens(validColorSchemeIdForTheme("Jellyfin", colorSchemeId))
        }
        return paletteTokens(bloomPalettes[validFlavorIdForTheme("Bloom", flavorId)] || bloomPalettes.dusk,
                             validColorSchemeIdForTheme("Bloom", colorSchemeId),
                             "orchid")
    }

    // Helper to get current theme colors
    property var activeTheme: resolveThemeTokens(currentTheme, currentFlavor, currentColorScheme)

    // ============================
    // Base palette (per theme)
    // ============================
    property color backgroundPrimary: activeTheme.backgroundPrimary
    property color backgroundSecondary: activeTheme.backgroundSecondary
    property color backgroundGlass: activeTheme.backgroundGlass
    property bool themeIsLight: activeTheme.isLight === true
    // Alias kept for components that historically read Theme.isLight.
    property bool isLight: themeIsLight

    property color accentPrimary: activeTheme.accentPrimary
    property color accentSecondary: activeTheme.accentSecondary
    property color accentGradientStart: activeTheme.accentGradientStart
    property color accentGradientEnd: activeTheme.accentGradientEnd
    property color accentColor: accentPrimary

    // ============================
    // Text hierarchy
    // ============================
    // Primary: titles and body. Secondary: metadata and descriptions.
    // Tertiary: captions and hints. Disabled: unavailable controls only.
    property color textPrimary: activeTheme.textPrimary
    property color textSecondary: activeTheme.textSecondary
    property color textTertiary: mix(textSecondary, backgroundPrimary, 0.22)
    property color textDisabled: activeTheme.textDisabled
    property color textMuted: textSecondary
    property color textOnAccent: activeTheme.textOnAccent || "#191724"

    // ============================
    // Status colors
    // ============================
    property color errorColor: activeTheme.errorColor || "#ff7b7b"
    property color successColor: activeTheme.successColor || "#7fd69a"
    property color warningColor: activeTheme.warningColor || "#f2c46d"
    property color colorDestructive: errorColor
    property color destructiveBorder: colorDestructive
    property color destructiveHoverBg: withAlpha(colorDestructive, 0.2)
    property color favoriteColor: themeIsLight ? "#c0304a" : "#ff6f86"

    // ============================
    // Surfaces & materials
    // ============================
    // Glass is restrained: a theme-tinted translucent fill plus one hairline.
    // `surfaceGlass` sits on blurred/scrimmed artwork, `surfaceGlassStrong` is
    // for floating chrome (rail, menus) and `surfaceDialog` is near-opaque so
    // modal copy never competes with the page behind it.
    property color surfaceTint: activeTheme.surfaceTint || backgroundSecondary
    property color surfaceGlass: withAlpha(mix(backgroundPrimary, surfaceTint, 0.55), themeIsLight ? 0.80 : 0.72)
    property color surfaceGlassStrong: withAlpha(mix(backgroundPrimary, surfaceTint, 0.35), themeIsLight ? 0.92 : 0.86)
    property color surfaceDialog: withAlpha(mix(backgroundPrimary, surfaceTint, 0.45), 0.97)
    property color surfaceSunken: withAlpha(backgroundSecondary, themeIsLight ? 0.55 : 0.45)
    property color surfaceBorder: withAlpha(textPrimary, themeIsLight ? 0.14 : 0.11)
    property color surfaceBorderStrong: withAlpha(textPrimary, themeIsLight ? 0.24 : 0.20)
    // Interactive fills for secondary controls, list rows and chips.
    property color surfaceInteractive: withAlpha(textPrimary, themeIsLight ? 0.07 : 0.10)
    property color surfaceInteractiveHover: withAlpha(textPrimary, themeIsLight ? 0.11 : 0.16)
    property color surfaceInteractivePressed: withAlpha(textPrimary, themeIsLight ? 0.16 : 0.22)
    property color surfaceInteractiveDisabled: withAlpha(textPrimary, themeIsLight ? 0.04 : 0.05)
    // Placeholder fill for missing or loading artwork.
    property color artworkPlaceholder: mix(backgroundPrimary, surfaceTint, 0.75)
    property color artworkPlaceholderIcon: withAlpha(textPrimary, 0.38)
    // Legibility shadow for white provider logos on light themes (ArtworkLogo).
    property color logoShadowColor: Qt.rgba(0.08, 0.05, 0.12, 0.85)

    // Scrims. Artwork is arbitrary and often bright, so scrims are tinted from
    // the theme background and strong enough to keep body text legible.
    property color scrimModal: themeIsLight ? Qt.rgba(0.10, 0.07, 0.14, 0.42) : Qt.rgba(0, 0, 0, 0.62)
    property color scrimStrong: withAlpha(backgroundPrimary, 0.92)
    property color scrimMedium: withAlpha(backgroundPrimary, 0.72)
    property color scrimSoft: withAlpha(backgroundPrimary, 0.38)
    property color scrimClear: withAlpha(backgroundPrimary, 0.0)
    // Soft drop shadow under raised surfaces (focused hero card).
    property color elevationShadow: Qt.rgba(0, 0, 0, themeIsLight ? 0.30 : 0.65)

    // ============================
    // Actions
    // ============================
    // Primary actions use an inverse fill (light on dark themes, dark on light
    // themes) so the main action reads instantly without relying on hue.
    property color actionPrimaryBackground: textPrimary
    property color actionPrimaryBackgroundHover: mix(textPrimary, accentPrimary, 0.12)
    property color actionPrimaryBackgroundPressed: mix(textPrimary, accentPrimary, 0.24)
    property color actionPrimaryText: backgroundPrimary
    property color actionSecondaryBackground: surfaceInteractive
    property color actionSecondaryBackgroundHover: surfaceInteractiveHover
    property color actionSecondaryBackgroundPressed: surfaceInteractivePressed
    property color actionSecondaryBorder: surfaceBorder
    property color actionSecondaryText: textPrimary
    property color actionDestructiveBackground: withAlpha(colorDestructive, themeIsLight ? 0.12 : 0.18)
    property color actionDestructiveBackgroundHover: withAlpha(colorDestructive, themeIsLight ? 0.18 : 0.28)
    property color actionDestructiveText: colorDestructive
    property real disabledOpacity: 0.42

    // ============================
    // Focus vs. selection
    // ============================
    // Focus is where the keyboard/remote is: a high-contrast outline with a
    // gap and a contrasting halo, never colour alone. Selection is the current
    // page/tab/option: an accent indicator and tinted fill, no outline.
    property color focusRing: themeIsLight ? textPrimary : "#ffffff"
    property color focusRingHalo: themeIsLight ? Qt.rgba(1, 1, 1, 0.85) : Qt.rgba(0, 0, 0, 0.55)
    property int focusRingWidth: Math.max(1, Math.round(3 * layoutScale))
    property int focusRingGap: Math.max(1, Math.round(4 * layoutScale))
    property real focusScale: 1.05
    property color selectionBackground: withAlpha(accentPrimary, themeIsLight ? 0.16 : 0.22)
    property color selectionIndicator: accentPrimary
    property color selectionText: textPrimary
    property int selectionIndicatorWidth: Math.max(1, Math.round(4 * layoutScale))

    // ============================
    // Progress
    // ============================
    property color progressTrack: withAlpha(textPrimary, themeIsLight ? 0.16 : 0.24)
    property color progressFill: accentPrimary
    property color progressTrackOnArtwork: Qt.rgba(1, 1, 1, 0.28)
    property int progressHeight: Math.max(1, Math.round(6 * layoutScale))

    // Legacy overlay names (kept for existing call sites)
    property color chipBackground: surfaceInteractive
    property color chipBorder: surfaceBorder
    property color overlayDark: scrimMedium
    property color overlayLight: withAlpha(textPrimary, 0.25)
    property color overlayHighlight: withAlpha(textPrimary, 0.06)
    // Opaque enough for badges/labels drawn over arbitrary poster artwork.
    property color overlayTextBackground: Qt.rgba(0.04, 0.03, 0.06, 0.74)
    property color overlayTextBorder: Qt.rgba(1, 1, 1, 0.16)
    property color textOnDarkOverlay: "#ffffff"
    // Page backdrop scrim. Backdrops are arbitrary artwork (often bright), so the
    // scrim is tinted from the theme background and opaque enough for body text.
    property color gradientOverlayStart: withAlpha(backgroundPrimary, 0.62)
    property color gradientOverlayMiddle: withAlpha(backgroundPrimary, 0.76)
    property color gradientOverlayEnd: withAlpha(backgroundPrimary, 0.92)

    // Playback chrome sits over video, which is effectively always dark, so
    // its glass is white-based in every theme.
    property color playbackOverlayTopTint: accentPrimary
    property color playbackOverlayBottomTint: "#000000"
    property color playbackOverlayShadowTint: themeIsLight ? "#000000" : backgroundPrimary
    property color playbackOverlayAccentShadowTint: accentPrimary
    property color playbackControlGlassBackground: Qt.rgba(1, 1, 1, 0.10)
    property color playbackControlGlassBackgroundHover: Qt.rgba(1, 1, 1, 0.20)
    property color playbackControlGlassBorder: Qt.rgba(1, 1, 1, 0.16)
    property color playbackControlGlassBorderStrong: Qt.rgba(1, 1, 1, 0.36)
    property color playbackPanelBackground: Qt.rgba(0.05, 0.04, 0.07, 0.84)
    property color playbackFocusRing: "#ffffff"
    property color playbackProgressTrack: Qt.rgba(1, 1, 1, 0.22)
    property color playbackProgressFill: themeIsLight ? "#ffffff" : accentPrimary
    property color playbackProgressCacheFill: Qt.rgba(1, 1, 1, 0.35)
    property color playbackTimePrimary: Qt.rgba(1, 1, 1, 0.92)
    property color playbackTimeSecondary: Qt.rgba(1, 1, 1, 0.70)
    property color playbackIconColor: "#ffffff"
    // Unified control bar: an outer glass panel holding the scrub row, an
    // inner transport tray, and the settings pop-out. Light themes lift the
    // accent so it still reads as a light tint over video.
    property color playbackAccent: themeIsLight ? mix(accentPrimary, Qt.rgba(1, 1, 1, 1), 0.45) : accentPrimary
    property color playbackAccentGlow: withAlpha(playbackAccent, 0.55)
    property color playbackChromeSurface: Qt.rgba(0.07, 0.06, 0.10, 0.62)
    property color playbackChromeTray: Qt.rgba(1, 1, 1, 0.06)
    property color playbackChromeDivider: Qt.rgba(1, 1, 1, 0.16)
    property color playbackHeroBackground: "#ffffff"
    property color playbackHeroIcon: Qt.rgba(0.09, 0.07, 0.13, 1)
    property color playbackSkipBackground: mix(playbackAccent, Qt.rgba(1, 1, 1, 1), 0.35)
    property color playbackSkipText: Qt.rgba(0.09, 0.07, 0.13, 1)

    // UI element properties (legacy names map onto the semantic tokens)
    property color focusBorder: focusRing
    property color hoverOverlay: surfaceInteractive
    property color inputBackground: surfaceInteractive
    property color inputBorder: surfaceBorderStrong

    // Border properties
    property color borderLight: surfaceBorder
    property int borderWidth: 1

    // ============================
    // Radii
    // ============================
    // Controls < artwork < panels < dialogs. Pills use height / 2.
    property int radiusSmall: Math.round(10 * layoutScale)
    property int radiusMedium: Math.round(16 * layoutScale)
    property int radiusLarge: Math.round(22 * layoutScale)
    property int radiusXLarge: Math.round(30 * layoutScale)
    property int radiusControl: radiusMedium
    property int radiusArtwork: Math.round(20 * layoutScale)
    property int radiusPanel: Math.round(32 * layoutScale)
    property int radiusDialog: radiusXLarge

    // Blur and effects
    property int blurRadius: 40

    // Frosted Glass Card Properties
    property color cardBackground: surfaceGlass
    property color cardBackgroundHover: mix(surfaceGlass, withAlpha(textPrimary, surfaceGlass.a), 0.08)
    property color cardBackgroundFocused: mix(surfaceGlass, withAlpha(textPrimary, surfaceGlass.a), 0.14)
    property color cardBorder: surfaceBorder
    property color cardBorderHover: surfaceBorderStrong
    property color cardBorderFocused: focusRing
    property int cardBlurRadius: 24
    // Rounded corners for posters/thumbnails
    property int imageRadius: radiusArtwork

    // ============================
    // Responsive Typography
    // ============================
    // Font sizes scale with layoutScale for consistent visual proportions.
    // Captions bottom out at 18 design units so metadata stays readable from
    // across a room.
    // Inter and Inter Display are bundled (FontLoader), so type renders the
    // same on every platform. Display is for page titles, hero titles and
    // text fallbacks for missing logos; everything else uses Primary.
    property string fontPrimary: "Inter"
    property string fontDisplay: "Inter Display"
    property string fontIcon: "Material Symbols Outlined" // Icon font for UI elements
    property string fontMono: "monospace"

    // Base font sizes (at layoutScale 1.0)
    property int fontSizeHeroBase: 84
    property int fontSizeDisplayBase: 60
    property int fontSizeIconBase: 26
    property int fontSizeHeaderBase: 36
    property int fontSizeTitleBase: 32
    property int fontSizeBodyBase: 24
    property int fontSizeSmallBase: 21
    property int fontSizeCaptionBase: 18
    property int fontSizeMediumBase: 28

    // Scaled font sizes
    property int fontSizeHero: Math.round(fontSizeHeroBase * layoutScale)
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
    property int iconSizeNav: Math.round(30 * layoutScale)
    // Display type tracks tighter; eyebrows (small uppercase labels above
    // titles) track wider.
    property real displayLetterSpacing: -0.6 * layoutScale
    property real eyebrowLetterSpacing: 2.2 * layoutScale

    // Weights: regular body, medium metadata, strong labels/buttons, heavy titles.
    property int fontWeightRegular: Font.Normal
    property int fontWeightMedium: Font.Medium
    property int fontWeightStrong: Font.DemiBold
    property int fontWeightHeavy: Font.Bold

    // ============================
    // Responsive Spacing
    // ============================
    // Base spacing values (at layoutScale 1.0)
    property int spacingSmallBase: 8
    property int spacingMediumBase: 20
    property int spacingLargeBase: 32
    property int spacingXLargeBase: 40
    property int paddingLargeBase: 56

    // Scaled spacing
    property int spacingSmall: Math.round(spacingSmallBase * layoutScale)
    property int spacingMedium: Math.round(spacingMediumBase * layoutScale)
    property int spacingLarge: Math.round(spacingLargeBase * layoutScale)
    property int spacingXLarge: Math.round(spacingXLargeBase * layoutScale)
    property int spacingXSmall: Math.max(4, Math.round(spacingSmall * 0.5))
    property int spacingLg: spacingLarge
    property int paddingLarge: Math.round(paddingLargeBase * layoutScale)

    // Semantic spacing: page gutters, gaps between sections, and gaps between
    // controls inside a group. pageMarginX is the left/right safe gutter every
    // page aligns its copy and first card to. pageMarginTop clears the
    // floating navigation bar; hero pages start at 0 and keep their copy
    // below navBarReservedHeight instead.
    property int pageMarginX: Math.round(96 * layoutScale)
    property int pageMarginTop: navBarReservedHeight
    property int pageMarginBottom: Math.round(64 * layoutScale)
    property int sectionSpacing: Math.round(56 * layoutScale)
    property int sectionTitleSpacing: Math.round(18 * layoutScale)
    property int controlSpacing: Math.round(16 * layoutScale)
    property int panelPadding: Math.round(40 * layoutScale)
    property int dialogPadding: Math.round(40 * layoutScale)

    // ============================
    // Navigation bar
    // ============================
    // The bar floats over the top of every page. Pages without a hero start
    // their content at pageMarginTop (navBarReservedHeight); hero pages draw
    // under the bar. The bar recedes while a page is scrolled into its
    // content and returns when focus moves back up to it.
    property int navBarInsetTop: Math.round(34 * layoutScale)
    property int navBarHeight: Math.round(76 * layoutScale)
    property int navBarItemHeight: Math.round(64 * layoutScale)
    property int navBarReservedHeight: navBarInsetTop + navBarHeight + Math.round(30 * layoutScale)
    property int navBarScrimHeight: Math.round(260 * layoutScale)

    // ============================
    // Stage (page backdrop)
    // ============================
    // Every page sits on a full-bleed stage: artwork (StageBackdrop) or the
    // ambient theme glow. Scrims are tinted from the theme background so copy
    // stays legible over arbitrary art. "hero" keeps art vivid behind a
    // leading-edge scrim; "ambient" mutes it behind content.
    property color stageBase: backgroundPrimary
    property color stageGlowPrimary: withAlpha(accentPrimary, themeIsLight ? 0.20 : 0.16)
    property color stageGlowSecondary: withAlpha(accentSecondary, themeIsLight ? 0.16 : 0.12)
    property real stageAmbientArtOpacity: themeIsLight ? 0.22 : 0.30

    // ============================
    // Cards and shelves
    // ============================
    // Fixed design-unit sizes: a card covers the same fraction of the screen
    // at every resolution; wider viewports simply show more of them.
    property int cardGap: Math.round(28 * layoutScale)
    property int posterCardWidth: Math.round(268 * layoutScale)
    property int posterCardHeight: Math.round(402 * layoutScale)
    property int landscapeCardWidth: Math.round(456 * layoutScale)
    property int landscapeCardHeight: Math.round(256 * layoutScale)
    property int cardCaptionHeight: Math.round(84 * layoutScale)
    // Widest a hero copy block may grow (about half a 16:9 screen); fixed so
    // ultrawide screens don't stretch the reading measure.
    property int heroCopyMaxWidth: Math.round(1040 * layoutScale)
    // Room above/below a shelf so the focused card's scale and ring are not clipped.
    property int shelfFocusMargin: Math.round(22 * layoutScale)

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
    property int posterWidthLarge: Math.round(posterWidthLargeBase * layoutScale)
    property int posterHeightLarge: Math.round(posterHeightLargeBase * layoutScale)
    property int seasonPosterWidth: Math.round(seasonPosterWidthBase * layoutScale)
    property int seasonPosterHeight: Math.round(seasonPosterHeightBase * layoutScale)
    property int episodeCardHeight: Math.round(episodeCardHeightBase * layoutScale)
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

    readonly property real detailViewLogoScaleBoost: 1.14
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
    // Motion
    // ============================
    // Durations are fixed (not scaled). Reduce Motion (uiAnimationsEnabled
    // false) collapses every duration to 0 so transitions become instant.
    property int durationShort: uiAnimationsEnabled ? 150 : 0
    property int durationNormal: uiAnimationsEnabled ? 200 : 0
    property int durationMedium: durationNormal
    property int durationLong: uiAnimationsEnabled ? 300 : 0
    property int durationFade: uiAnimationsEnabled ? 500 : 0
    property int easingStandard: Easing.OutCubic
    property int easingEmphasized: Easing.OutQuint

    // ============================
    // Glassmorphic Button System (legacy names)
    // ============================

    // Primary Button (Play, main actions) - accent fill used for selection-like
    // highlights in older list delegates. New primary actions use action*.
    property color buttonPrimaryBackground: withAlpha(accentPrimary, 0.88)
    property color buttonPrimaryBackgroundHover: withAlpha(accentPrimary, 0.95)
    property color buttonPrimaryBackgroundPressed: accentPrimary
    property color buttonPrimaryBorder: surfaceBorder
    property color buttonPrimaryBorderFocused: focusRing

    // Secondary Button (Mark Watched, Back, etc.) - Frosted glass effect
    property color buttonSecondaryBackground: actionSecondaryBackground
    property color buttonSecondaryBackgroundDisabled: surfaceInteractiveDisabled
    property color buttonSecondaryBackgroundHover: actionSecondaryBackgroundHover
    property color buttonSecondaryBackgroundPressed: actionSecondaryBackgroundPressed
    property color buttonSecondaryBorder: surfaceBorder
    property color buttonSecondaryBorderHover: surfaceBorderStrong
    property color buttonSecondaryBorderFocused: focusRing

    // Icon Button (Bookmark, Favorite, etc.) - Subtle glass circles
    property color buttonIconBackground: surfaceInteractive
    property color buttonIconBackgroundHover: surfaceInteractiveHover
    property color buttonIconBackgroundPressed: surfaceInteractivePressed
    property color buttonIconBorder: surfaceBorder
    property color buttonIconBorderHover: surfaceBorderStrong
    property color buttonIconBorderFocused: focusRing

    // Tab/Filter Button - For filter tabs in library
    property color buttonTabBackground: surfaceInteractiveDisabled
    property color buttonTabBackgroundHover: surfaceInteractive
    property color buttonTabBackgroundActive: selectionBackground
    property color buttonTabBackgroundPressed: surfaceInteractivePressed
    property color buttonTabBorder: surfaceBorder
    property color buttonTabBorderHover: surfaceBorderStrong
    property color buttonTabBorderActive: selectionIndicator
    property color buttonTabBorderFocused: focusRing

    // Button dimensions (responsive)
    // Hero actions (Home hero, detail pages, Up Next) are the largest controls.
    property int buttonHeightHero: Math.round(84 * layoutScale)
    property int buttonHeightLarge: Math.round(68 * layoutScale)
    property int buttonHeightMedium: Math.round(60 * layoutScale)
    property int buttonHeightSmall: Math.round(52 * layoutScale)
    property int buttonIconSize: Math.round(60 * layoutScale)
    property int buttonFocusBorderWidth: focusRingWidth
    property int buttonBorderWidth: 1
}
