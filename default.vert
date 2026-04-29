#version 330 core
// -----------------------------------------------------------------------------
// default.vert — mesh pass: transforms, instance offsets, normals for lighting
// -----------------------------------------------------------------------------
// Attribute locations must match Mesh constructor LinkAttrib calls. Instance
// attributes (5,6) advance once per instance when using instanced draw calls.

layout (location = 0) in vec3 aPos;
layout (location = 2) in vec2 aTex;
layout (location = 3) in vec2 aTexMask;
layout (location = 4) in vec3 aNormal;
layout (location = 5) in vec3 aInstanceOffsetPos;
layout (location = 6) in vec2 aInstanceOffsetTex;
layout (location = 7) in float naturalLuminence;



out vec2 texCoord;
out vec2 texCoordMask;
out vec3 normal;
out vec3 FragPos;
out float ambientLight;
out float centerY;

uniform vec2 texCoordOffset;
uniform mat4 camMatrix;
uniform mat4 model;

void main()
{

   centerY = aPos.y + aInstanceOffsetPos.y;
   // World-space position (for light vectors). Instance offsets mimic many copies of the same mesh.
   FragPos = vec3(model * vec4(aPos.x + aInstanceOffsetPos.x, aPos.y + aInstanceOffsetPos.y, aPos.z + aInstanceOffsetPos.z, 1.0));

   gl_Position = camMatrix * model * vec4(aPos.x + aInstanceOffsetPos.x, aPos.y + aInstanceOffsetPos.y, aPos.z + aInstanceOffsetPos.z, 1.0);
   texCoord = vec2(aTex.x + aInstanceOffsetTex.x, aTex.y + aInstanceOffsetTex.y);
   // Uniform offset selects the current atlas cell for the transparency mask.
   texCoordMask = aTexMask + texCoordOffset;
   // Normal matrix removes non-uniform scale from normals (inverse-transpose of upper 3x3).
   normal = mat3(transpose(inverse(model))) * aNormal;
   ambientLight = naturalLuminence;
}
