#version 450
// gpu_tests: a textured quad; uv (0, 0) is the texture's top-left
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 0) out vec2 vUV;

void main()
{
	vUV = aUV;
	gl_Position = vec4(aPos, 0.5, 1.0);
}
