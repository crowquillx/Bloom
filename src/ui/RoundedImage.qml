pragma ComponentBehavior: Bound
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
    readonly property alias shaderRefreshCount: shaderSource.refreshCount

    // Expose effective status for placeholders/spinners.
    // Do not switch to pre-rounded until it is actually ready, so base stays visible.
    readonly property bool preRoundedReady: effectivePreferPreRounded
                                           && preRoundedSource.toString() !== ""
                                           && preRoundedLoader.item
                                           && preRoundedLoader.imageStatus === Image.Ready
    readonly property int status: (preRoundedReady ? preRoundedLoader.imageStatus : baseImage.status)
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
    readonly property bool shaderActive: !usePreRounded && shaderSupported
                                         && baseImage.status === Image.Ready

    function refreshStaticShaderSource() {
        if (!shaderActive)
            return
        shaderSource.scheduleUpdate()
        shaderSource.refreshCount += 1
    }

    onSourceChanged: Qt.callLater(refreshStaticShaderSource)
    onShaderActiveChanged: Qt.callLater(refreshStaticShaderSource)
    onWidthChanged: Qt.callLater(refreshStaticShaderSource)
    onHeightChanged: Qt.callLater(refreshStaticShaderSource)
    onSourceWidthChanged: Qt.callLater(refreshStaticShaderSource)
    onSourceHeightChanged: Qt.callLater(refreshStaticShaderSource)
    onFillModeChanged: Qt.callLater(refreshStaticShaderSource)
    onMipmapChanged: Qt.callLater(refreshStaticShaderSource)
    onSmoothChanged: Qt.callLater(refreshStaticShaderSource)
    onVisibleChanged: Qt.callLater(refreshStaticShaderSource)

    // One base image feeds both the shader and software fallback. Binding
    // hideSource to the active shader avoids the duplicate fallback download.
    Rectangle {
        id: fallbackClip
        objectName: "fallbackClip"
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        clip: true
        visible: !root.usePreRounded

        Image {
            id: baseImage
            objectName: "baseImage"
            anchors.fill: parent
            source: root.source
            sourceSize.width: root.sourceWidth
            sourceSize.height: root.sourceHeight
            fillMode: root.fillMode
            asynchronous: root.asynchronous
            cache: root.cache
            mipmap: root.mipmap
            smooth: root.smooth
            visible: true

            onStatusChanged: {
                if (status === Image.Ready)
                    Qt.callLater(root.refreshStaticShaderSource)
            }
        }
    }

    // The prerender-only image object is instantiated only when that path is
    // eligible and a source exists.
    Loader {
        id: preRoundedLoader
        objectName: "preRoundedLoader"
        anchors.fill: parent
        active: root.effectivePreferPreRounded
                && root.preRoundedSource.toString() !== ""
        visible: root.preRoundedReady
        property int imageStatus: Image.Null

        onActiveChanged: {
            if (!active)
                imageStatus = Image.Null
        }

        sourceComponent: Component {
            Image {
                objectName: "preRoundedImage"
                source: root.preRoundedSource
                sourceSize.width: root.sourceWidth
                sourceSize.height: root.sourceHeight
                fillMode: root.fillMode
                asynchronous: root.asynchronous
                cache: root.cache
                mipmap: root.mipmap
                smooth: root.smooth

                Component.onCompleted: preRoundedLoader.imageStatus = status
                onStatusChanged: preRoundedLoader.imageStatus = status
            }
        }
    }

    // Shader-based rounded corners (default fast path).
    ShaderEffectSource {
        id: shaderSource
        objectName: "shaderSource"
        sourceItem: baseImage
        hideSource: root.shaderActive
        live: false
        visible: false
        property int refreshCount: 0
    }

    ShaderEffect {
        id: roundedEffect
        objectName: "roundedEffect"
        anchors.fill: parent
        visible: root.shaderActive
        property var source: shaderSource
        property real radiusPx: Math.max(0, root.radius)
        property vector2d itemSize: Qt.vector2d(width, height)

        fragmentShader: "qrc:/shaders/rounded_image.frag.qsb"
    }

}
