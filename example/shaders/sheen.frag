#version 330 core
// -----------------------------------------------------------------------------
// sheen.frag -- the motion demo's own Painter2D program
// -----------------------------------------------------------------------------
// A band of colour that shifts with the pointer, shown through an ordered-dither
// reveal grown from the quad's middle. vParam0.x is how far the reveal has grown
// (0 to about 1.25; 2 shows all of it).

#include <ui2d.glsl>

uniform vec2 uPointer;      // window pixels
uniform sampler2D uGrain;   // a 64 px noise tile, repeated

out vec4 FragColor;

void main()
{
	// 0 at the middle, 1 at the middle of each edge
	vec2 n = vLocal / vShape.xy;
	float grain = texture(uGrain, vPixel / 64.0).r - 0.5;
	float s = (vParam0.x - length(n) * 0.8 + grain * 0.25) * 6.0;
	if (s <= ui2dBayer(vPixel, 1.0)) discard;

	// tilting with the pointer, like a card turned in the light
	float h = (vPixel.x + vPixel.y) * 0.004 + dot(vPixel - uPointer, vec2(0.002, 0.0013));
	vec3 col = 0.62 + 0.3 * cos(6.2831 * (h + vec3(0.0, 0.33, 0.67)));
	// a lighter rim where it's still growing
	if (s < 1.2 && vParam0.x < 1.2) col = mix(col, vec3(1.0), 0.45);
	FragColor = vec4(col, ui2dCoverage() * vColor.a);
}
