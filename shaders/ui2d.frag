#version 330 core
// -----------------------------------------------------------------------------
// ui2d.frag -- Painter2D: rounded box distance field, one pixel of antialiasing
// -----------------------------------------------------------------------------
// Rects, rounded rects, circles, lines and their outlines are all a rounded box
// (a circle is a box with radius = half size, a line is a rotated capsule).

in vec4 vColor;
in vec2 vLocal;
in vec4 vShape;

out vec4 FragColor;

// image(): the quad samples this instead of being a flat colour
uniform bool uTextured = false;
uniform sampler2D uTex;

float sdRoundBox(vec2 p, vec2 hs, float r)
{
	vec2 q = abs(p) - (hs - vec2(r));
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main()
{
	float d = sdRoundBox(vLocal, vShape.xy, vShape.z);
	float a = clamp(0.5 - d, 0.0, 1.0);
	// border > 0: keep only the ring of that width inside the edge
	if (vShape.w > 0.0) a *= clamp(d + vShape.w + 0.5, 0.0, 1.0);
	if (uTextured)
	{
		// the quad's own extent (its half size, without the antialiasing pixel) is the whole texture
		vec2 uv = clamp(vLocal / vShape.xy * 0.5 + 0.5, 0.0, 1.0);
		vec4 t = texture(uTex, vec2(uv.x, uv.y));
		FragColor = vec4(t.rgb * vColor.rgb, t.a * vColor.a * a);
		return;
	}
	FragColor = vec4(vColor.rgb, vColor.a * a);
}
