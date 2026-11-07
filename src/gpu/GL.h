#pragma once
// -----------------------------------------------------------------------------
// gpu/GL.h -- the GL objects behind gpu:: handles, for code that goes raw GL
// -----------------------------------------------------------------------------
// Valid while device.backend() == gpu::Backend::GL; every call returns 0 on
// another backend or for a handle that isn't live. Raw GL goes between passes,
// not inside one: a pass sets all the state it needs when it binds a pipeline,
// and end() puts GL back the way initialize() left it (blend on with the coverage
// blend, depth test on, no program, VAO, scissor or culling).

#include "gpu/Gpu.h"

namespace gpu::gl
{

unsigned native(const Device& device, Buffer buffer);   // buffer object
unsigned native(const Device& device, Texture texture); // GL_TEXTURE_2D (a depth texture too)
unsigned native(const Device& device, Sampler sampler); // sampler object
unsigned program(const Device& device, Pipeline pipeline); // the linked program

}
