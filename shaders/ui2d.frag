#version 330 core
// -----------------------------------------------------------------------------
// ui2d.frag -- Painter2D's own shader: shapes, images and glyphs in one batch
// -----------------------------------------------------------------------------
// Rects, rounded rects, circles, lines and their outlines are all a rounded box
// (a circle is a box with radius = half size, a line is a rotated capsule).
// Images and glyphs sample uTex. The parts are in ui2d.glsl, for apps' own shaders.

#include "ui2d.glsl"

out vec4 FragColor;

void main()
{
	if (ui2dDithered()) discard;
	FragColor = ui2dColor();
}
