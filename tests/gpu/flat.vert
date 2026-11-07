#version 450
// gpu_tests: positions in NDC, an optional per-instance offset, a pushed offset
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aInstance;

layout(set = 1, binding = 0) uniform Move { vec2 offset; } move;

void main()
{
	gl_Position = vec4(aPos.xy + aInstance + move.offset, aPos.z, 1.0);
}
