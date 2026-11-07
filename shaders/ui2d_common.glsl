// -----------------------------------------------------------------------------
// ui2d_common.glsl -- Painter2D's fragment helpers, shared by both declarations
// -----------------------------------------------------------------------------
// Included after what it reads is declared: the varyings (vColor, vLocal, vShape,
// vUvRect, vMode, vParam0, vParam1, vPixel), uTex and uDither. ui2d.glsl declares
// them for an app's GLSL 330 shader (GL only); gpu/ui2d.frag for the stock GLSL
// 450 one built by qs_gpu_shaders.

// signed distance to a rounded box of half size hs, centred on the origin
float ui2dBox(vec2 p, vec2 hs, float r)
{
	vec2 q = abs(p) - (hs - vec2(r));
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// where this pixel samples uTex: across the quad's own rect, kept inside it, so the
// pixel of antialiasing around an image doesn't reach past its edge
vec2 ui2dUv()
{
	vec2 t = clamp(vLocal / max(vShape.xy, vec2(1e-4)) * 0.5 + 0.5, 0.0, 1.0);
	return mix(vUvRect.xy, vUvRect.zw, t);
}

// How much of this pixel the quad covers: a shape's edge with a pixel of antialiasing
// (only its ring when it has a border), an image's rect, or a glyph's ink.
float ui2dCoverage()
{
	if (vMode > 2.5) return texture(uTex, ui2dUv()).a;
	if (vMode > 1.5) return texture(uTex, ui2dUv()).r;
	float d = ui2dBox(vLocal, vShape.xy, vShape.z);
	float a = clamp(0.5 - d, 0.0, 1.0);
	if (vShape.w > 0.0) a *= clamp(d + vShape.w + 0.5, 0.0, 1.0);
	return a;
}

// what Painter2D's own shader draws: the colour (or the image times it) by the coverage
vec4 ui2dColor()
{
	float a = ui2dCoverage();
	if (vMode > 0.5 && vMode < 1.5)
	{
		vec4 t = texture(uTex, ui2dUv());
		return vec4(t.rgb * vColor.rgb, t.a * vColor.a * a);
	}
	if (vMode > 3.5) // a colour glyph keeps its colours (the atlas is premultiplied)
	{
		vec4 t = texture(uTex, ui2dUv());
		return vec4(t.rgb / max(t.a, 1e-4), t.a * vColor.a);
	}
	return vec4(vColor.rgb, vColor.a * a);
}

// The 4x4 ordered-dither threshold of a pixel, in cells of `cell` pixels: a value in
// (0, 1); something of strength s shows where s is above it (0.5 = a checkerboard).
float ui2dBayer(vec2 pixel, float cell)
{
	const int m[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
	ivec2 p = ivec2(floor(pixel / cell)) & 3;
	return (float(m[p.y * 4 + p.x]) + 0.5) / 16.0;
}

// pushDither: whether this pixel is dithered away
bool ui2dDithered()
{
	return uDither.x < 1.0 && uDither.x <= ui2dBayer(vPixel, uDither.y);
}
