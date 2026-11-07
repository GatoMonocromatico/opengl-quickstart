#version 450
layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform Tint { vec4 color; } tint;

void main()
{
	FragColor = tint.color;
}
