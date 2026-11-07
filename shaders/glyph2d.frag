#version 330 core
// -----------------------------------------------------------------------------
// glyph2d.frag -- ShapedText atlas: tinted coverage or colour glyphs
// -----------------------------------------------------------------------------
// The atlas is premultiplied RGBA. Coverage glyphs (white) take the text colour,
// colour glyphs (emoji) keep theirs and only fade with the colour's alpha. The
// output is premultiplied too: blend with (ONE, ONE_MINUS_SRC_ALPHA).

in vec2 vUV;
in vec4 vColor;
in float vIsColor;

out vec4 FragColor;

uniform sampler2D uAtlas;

void main()
{
	vec4 t = texture(uAtlas, vUV);
	if (vIsColor > 0.5)
		FragColor = t * vColor.a;
	else
		FragColor = vec4(vColor.rgb, 1.0) * (t.a * vColor.a);
}
