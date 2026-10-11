#version 330 core
// -----------------------------------------------------------------------------
// ui2d.vert -- Painter2D: pixel-space shapes, coverage computed in the fragment stage
// -----------------------------------------------------------------------------
// aLocal is the position relative to the shape's center (in the shape's own
// axes); aShape = (half width, half height, corner radius, border width or 0).

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aLocal;
layout (location = 3) in vec4 aShape;

out vec4 vColor;
out vec2 vLocal;
out vec4 vShape;

uniform vec2 uScreen;

void main()
{
	// pixels, origin top-left, y down -> clip space
	gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
	vColor = aColor;
	vLocal = aLocal;
	vShape = aShape;
}
