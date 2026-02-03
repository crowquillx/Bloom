import QtQuick

// Rounded image renderer with hybrid pre-rounded and shader paths.
// Uses shader-based masking by default, falls back to pre-rounded bitmaps
// when provided, and clips in software if shaders are unavailable.
Item {
    id: root

    // Primary source (unrounded)
    property url source: ""
    // Optional pre-rounded bitmap (transparent corners). When set and
    // preferPreRounded is true, this is used instead of the shader path.
    property url preRoundedSource: ""
    // Global mode: auto | shader | prerender
    property string mode: "auto"
    // Select pre-rounded assets when available (applied after mode).
    property bool preferPreRounded: true
    // Allow shader-based rounding (applied after mode).
    property bool allowShader: true
    property real radius: 12
    property int sourceWidth: 640
    property int sourceHeight: 960
    property int fillMode: Image.PreserveAspectCrop
    property bool cache: true
    property bool asynchronous: true
    property bool mipmap: true
    property bool smooth: true

    // Expose effective status for placeholders/spinners.
    // Do not switch to pre-rounded until it is actually ready, so base stays visible.
    readonly property bool preRoundedReady: effectivePreferPreRounded
                                           && preRoundedSource !== ""
                                           && preRoundedImage.status === Image.Ready
    readonly property int status: (preRoundedReady ? preRoundedImage.status : baseImage.status)
    readonly property bool loading: status === Image.Loading
    readonly property bool ready: status === Image.Ready
    readonly property bool error: status === Image.Error

    readonly property string normalizedMode: {
        const m = (mode || "auto").toString().toLowerCase()
        if (m === "shader" || m === "prerender" || m === "auto")
            return m
        return "auto"
    }
    readonly property bool modePrefersPreRounded: normalizedMode !== "shader"
    readonly property bool modeAllowsShader: normalizedMode !== "prerender"
    readonly property bool effectivePreferPreRounded: modePrefersPreRounded && preferPreRounded
    readonly property bool effectiveAllowShader: modeAllowsShader && allowShader
    readonly property bool usePreRounded: preRoundedReady
    readonly property bool shaderSupported: effectiveAllowShader && GraphicsInfo.api !== GraphicsInfo.Software

    // Raw image used for shader/fallback. Kept hidden when shader is active.
    Image {
        id: baseImage
        anchors.fill: parent
        source: root.source
        sourceSize.width: root.sourceWidth
        sourceSize.height: root.sourceHeight
        fillMode: root.fillMode
        asynchronous: root.asynchronous
        cache: root.cache
        mipmap: root.mipmap
        smooth: root.smooth
        // Kept visible so ShaderEffectSource can render; not shown because hideSource is true.
        visible: true
    }

    // Pre-rounded path (preferred when provided).
    Image {
        id: preRoundedImage
        anchors.fill: parent
        source: root.preRoundedSource
        sourceSize.width: root.sourceWidth
        sourceSize.height: root.sourceHeight
        fillMode: root.fillMode
        asynchronous: root.asynchronous
        cache: root.cache
        mipmap: root.mipmap
        smooth: root.smooth
        visible: root.preRoundedReady
    }

    // Shader-based rounded corners (default fast path).
    ShaderEffectSource {
        id: shaderSource
        sourceItem: baseImage
        hideSource: true
        live: true
        visible: false
    }

    ShaderEffect {
        id: roundedEffect
        anchors.fill: parent
        visible: !root.usePreRounded && root.shaderSupported && baseImage.status === Image.Ready
        property var source: shaderSource
        property real radiusPx: Math.max(0, root.radius)
        property vector2d itemSize: Qt.vector2d(width, height)

        fragmentShader: "qrc:/shaders/rounded_image.frag.qsb"
    }

    // Software fallback: clip the base image if shaders are unavailable or while shader path is not active.
    Rectangle {
        id: fallbackClip
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        clip: true
        visible: !root.usePreRounded && (!root.shaderSupported || baseImage.status !== Image.Ready)

        Image {
            anchors.fill: parent
            source: root.source
            sourceSize.width: root.sourceWidth
            sourceSize.height: root.sourceHeight
            fillMode: root.fillMode
            asynchronous: root.asynchronous
            cache: root.cache
            mipmap: root.mipmap
            smooth: root.smooth
            visible: fallbackClip.visible
        }
    }
}
