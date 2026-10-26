import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

import BloomUI

/**
 * StageBackdrop - the full-bleed layer every page sits on.
 *
 * Cross-fades between artwork URLs and applies Bloom's standard scrims, tinted
 * from the theme background so copy stays legible over arbitrary art:
 *
 *   mode "hero"     sharp art, a leading-edge scrim behind hero copy on the
 *                   left and a floor that settles shelves onto the theme.
 *   mode "ambient"  softened, quieted art behind dense content (grids, lists,
 *                   settings). Art is optional here.
 *
 * With no artwork (or before it loads) the stage shows the ambient glow: two
 * soft accent lights over the theme background, so artless pages still feel
 * like part of the same space.
 *
 * `dim` (0..1) darkens the art further, e.g. as a page scrolls past its hero.
 */
Item {
    id: root

    property string source: ""
    property string mode: "ambient"
    property real dim: 0.0
    /// Show the accent glow under the art (and alone when there is no art).
    property bool glow: true

    readonly property bool hero: mode === "hero"
    readonly property bool hasArtwork: (showA ? imageA : imageB).status === Image.Ready
                                       && root.source !== ""

    /// Emitted when `source` finished loading and is now on screen.
    signal artworkShown(string url)
    /// Emitted when `source` failed to load, so the page can try a fallback.
    signal artworkFailed(string url)

    property bool showA: true

    clip: true

    // Theme base.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.backgroundPrimary }
            GradientStop { position: 1.0; color: Theme.backgroundSecondary }
        }
    }

    // Ambient glow: two large, soft accent lights.
    Shape {
        anchors.fill: parent
        visible: root.glow
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: root.width * 0.18; centerY: root.height * 0.04
                centerRadius: root.width * 0.55
                focalX: centerX; focalY: centerY
                GradientStop { position: 0.0; color: Theme.stageGlowPrimary }
                GradientStop { position: 1.0; color: Theme.withAlpha(Theme.stageGlowPrimary, 0) }
            }
            PathRectangle { x: 0; y: 0; width: root.width; height: root.height }
        }

        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: root.width * 0.92; centerY: root.height * 0.88
                centerRadius: root.width * 0.48
                focalX: centerX; focalY: centerY
                GradientStop { position: 0.0; color: Theme.stageGlowSecondary }
                GradientStop { position: 1.0; color: Theme.withAlpha(Theme.stageGlowSecondary, 0) }
            }
            PathRectangle { x: 0; y: 0; width: root.width; height: root.height }
        }
    }

    // Artwork, cross-faded. Ambient mode softens and quiets it.
    Item {
        id: artLayer
        anchors.fill: parent
        opacity: root.hero ? 1.0 : Theme.stageAmbientArtOpacity
        Behavior on opacity {
            enabled: Theme.uiAnimationsEnabled
            NumberAnimation { duration: Theme.durationFade }
        }

        layer.enabled: !root.hero && GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect {
            blurEnabled: true
            blur: 0.6
            blurMax: Theme.blurRadius
            saturation: -0.1
        }

        Image {
            id: imageA
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: root.showA && status === Image.Ready ? 1.0 : 0.0
            Behavior on opacity {
                enabled: Theme.uiAnimationsEnabled
                NumberAnimation { duration: Theme.durationFade }
            }
            onStatusChanged: root.checkStatus(imageA)
        }

        Image {
            id: imageB
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: !root.showA && status === Image.Ready ? 1.0 : 0.0
            Behavior on opacity {
                enabled: Theme.uiAnimationsEnabled
                NumberAnimation { duration: Theme.durationFade }
            }
            onStatusChanged: root.checkStatus(imageB)
        }
    }

    // Hero: leading-edge scrim behind the copy...
    Rectangle {
        anchors.fill: parent
        visible: root.hero
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.92) }
            GradientStop { position: 0.28; color: Theme.withAlpha(Theme.backgroundPrimary, 0.74) }
            GradientStop { position: 0.52; color: Theme.withAlpha(Theme.backgroundPrimary, 0.24) }
            GradientStop { position: 0.78; color: Theme.withAlpha(Theme.backgroundPrimary, 0.0) }
        }
    }

    // ...and a floor that settles shelves onto the theme background.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * (root.hero ? 0.55 : 1.0)
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.withAlpha(Theme.backgroundPrimary, 0.0) }
            GradientStop { position: 0.5; color: Theme.withAlpha(Theme.backgroundPrimary, root.hero ? 0.66 : 0.35) }
            GradientStop { position: 1.0; color: Theme.withAlpha(Theme.backgroundPrimary, root.hero ? 0.97 : 0.70) }
        }
    }

    // Extra dimming requested by the page (e.g. scrolled past the hero).
    Rectangle {
        anchors.fill: parent
        color: Theme.backgroundPrimary
        opacity: Math.max(0, Math.min(1, root.dim))
        visible: opacity > 0
    }

    function checkStatus(img) {
        // Only act on the image loading the CURRENT source.
        if (img.source.toString() !== root.source)
            return
        if (img.status === Image.Ready || (img.status === Image.Null && root.source === "")) {
            root.showA = img === imageA
            if (img.status === Image.Ready && root.source !== "")
                root.artworkShown(root.source)
        } else if (img.status === Image.Error) {
            console.log("Stage backdrop failed to load: " + img.source)
            root.artworkFailed(root.source)
        }
    }

    onSourceChanged: {
        // Load the new URL into the INACTIVE image.
        var target = showA ? imageB : imageA
        if (target.source.toString() === source && target.status === Image.Ready) {
            showA = target === imageA
            return
        }
        target.source = source
        // A pixmap-cache hit loads synchronously, and Ready -> Ready emits no
        // statusChanged, so check here or the old artwork stays on screen.
        checkStatus(target)
    }
}
