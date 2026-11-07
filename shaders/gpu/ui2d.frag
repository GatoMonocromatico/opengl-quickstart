#version 450
#extension GL_GOOGLE_include_directive : require
// -----------------------------------------------------------------------------
// gpu/ui2d.frag -- Painter2D's own shader: shapes, images and glyphs in one batch
// -----------------------------------------------------------------------------
// Rects, rounded rects, circles, lines and their outlines are all a rounded box
// (a circle is a box with radius = half size, a line is a rotated capsule).
// Images and glyphs sample uTex. The helpers are ui2d_common.glsl, shared with
// apps' own GLSL 330 shaders (ui2d.glsl).

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vLocal;
layout(location = 2) in vec4 vShape;
layout(location = 3) in vec4 vUvRect;
layout(location = 4) in float vMode;
layout(location = 5) in vec4 vParam0;
layout(location = 6) in vec4 vParam1;
layout(location = 7) in vec2 vPixel;

layout(set = 2, binding = 0) uniform sampler2D uTex; // the image, or the font's atlas
layout(set = 3, binding = 0) uniform Ui2dFrag { vec2 dither; } ui2dFrag; // pushDither: strength, cell px
#define uDither ui2dFrag.dither

#include "ui2d_common.glsl"

layout(location = 0) out vec4 FragColor;

void main()
{
	if (ui2dDithered()) discard;
	FragColor = ui2dColor();
}
