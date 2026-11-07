#version 330 core
// -----------------------------------------------------------------------------
// glyph2d.vert -- Painter2D::text(ShapedText&): glyph quads in window pixels
// -----------------------------------------------------------------------------

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;
layout (location = 3) in float aIsColor;

out vec2 vUV;
out vec4 vColor;
out float vIsColor;

uniform vec2 uScreen;

void main()
{
	// pixels, origin top-left, y down -> clip space
	gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
	vUV = aUV;
	vColor = aColor;
	vIsColor = aIsColor;
}
