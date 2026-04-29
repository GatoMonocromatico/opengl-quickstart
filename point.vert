#version 330 core
// -----------------------------------------------------------------------------
// point.vert — GL_POINTS: transform only; set gl_PointSize for rasterized diameter
// -----------------------------------------------------------------------------
// gl_PointSize is ignored unless GL_PROGRAM_POINT_SIZE is enabled on the CPU.

layout (location = 0) in vec3 aPos;

out vec3 FragPos;

uniform mat4 model;
uniform mat4 camMatrix;

void main()
{
   FragPos = vec3(model * vec4(aPos, 1.0));
   gl_Position = camMatrix * model * vec4(aPos, 1.0);

   // Pixel diameter of each point sprite (requires glEnable(GL_PROGRAM_POINT_SIZE)).
   gl_PointSize = 10.0;
}
