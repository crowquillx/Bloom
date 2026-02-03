// Rounded image fragment shader compiled to .qsb for Qt 6 ShaderEffect.
// Keeps radius semantics aligned with Theme.imageRadius and RoundedImage.qml.
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D source;

layout(std140, binding = 1) uniform ubuf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float radiusPx;
    vec2 itemSize;
} buf;

void main()
{
    vec2 px = qt_TexCoord0 * buf.itemSize;
    float radius = min(buf.radiusPx, 0.5 * min(buf.itemSize.x, buf.itemSize.y));
    vec2 dist = min(px, buf.itemSize - px);
    float mask = smoothstep(radius - 1.0, radius + 1.0, min(dist.x, dist.y));
    vec4 color = texture(source, qt_TexCoord0);
    fragColor = vec4(color.rgb, color.a * mask) * buf.qt_Opacity;
}






