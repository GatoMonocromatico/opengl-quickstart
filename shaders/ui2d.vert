#version 330 core
// -----------------------------------------------------------------------------
// ui2d.vert -- Painter2D: pixel-space quads, coverage computed in the fragment stage
// -----------------------------------------------------------------------------
// aLocal is the position relative to the shape's center (in the shape's own
// axes); aShape = (half width, half height, corner radius, border width or 0),
// all in pixels after Painter2D's transform. aUvRect is the part of uTex an image
// or glyph shows (u0, v0 at its top-left, u1, v1 at its bottom-right); aMode: 0
// shape, 1 image, 2 glyph (Text), 3 glyph and 4 colour glyph (ShapedText).
// Every program Painter2D draws with uses this stage; ui2d.glsl declares its outputs.

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aLocal;
layout (location = 3) in vec4 aShape;
layout (location = 4) in vec4 aUvRect;
layout (location = 5) in float aMode;
layout (location = 6) in vec4 aParam0;
layout (location = 7) in vec4 aParam1;

out vec4 vColor;
out vec2 vLocal;
out vec4 vShape;
out vec4 vUvRect;
out float vMode;
out vec4 vParam0;
out vec4 vParam1;
out vec2 vPixel;

uniform vec2 uScreen;

void main()
{
	// pixels, origin top-left, y down -> clip space
	gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
	vColor = aColor;
	vLocal = aLocal;
	vShape = aShape;
	vUvRect = aUvRect;
	vMode = aMode;
	vParam0 = aParam0;
	vParam1 = aParam1;
	vPixel = aPos;
}
