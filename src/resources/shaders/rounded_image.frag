// Rounded image fragment shader compiled to .qsb for Qt 6 ShaderEffect.
// Keeps radius semantics aligned with Theme.imageRadius and RoundedImage.qml.
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform ubuf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float radiusPx;
    vec2 itemSize;
} buf;

layout(binding = 1) uniform sampler2D source;

void main()
{
    vec2 halfSize = 0.5 * buf.itemSize;
    vec2 px = qt_TexCoord0 * buf.itemSize;
    float radius = min(buf.radiusPx, 0.5 * min(buf.itemSize.x, buf.itemSize.y));
    vec2 edgeDistance = abs(px - halfSize) - (halfSize - vec2(radius));
    float signedDistance = length(max(edgeDistance, vec2(0.0)))
        + min(max(edgeDistance.x, edgeDistance.y), 0.0) - radius;
    float mask = 1.0 - smoothstep(-1.0, 1.0, signedDistance);
    vec4 color = texture(source, qt_TexCoord0);
    fragColor = color * mask * buf.qt_Opacity;
}




