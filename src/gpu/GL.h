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

#include <memory>

namespace gpu::gl
{

// The device of the GL context current on this thread, made on first use, with no
// window to present (present() does nothing): what Painter2D::create() draws
// through. It stays until cleanup() destroys the context (handles still held on
// it then just stop working), so a ShapedText or Painter2D may outlive another
// one that used it.
Device& contextDevice();
// cleanup() calls it before destroying ctx: retires ctx's contextDevice()
void forgetContext(void* ctx);

unsigned native(const Device& device, Buffer buffer);   // buffer object
unsigned native(const Device& device, Texture texture); // GL_TEXTURE_2D (a depth texture too)
unsigned native(const Device& device, Sampler sampler); // sampler object
unsigned program(const Device& device, Pipeline pipeline); // the linked program

// GL objects made elsewhere, as handles. Releasing the handle leaves the GL object.
// A wrapped texture has no TextureDesc (sizes 0) and keeps its own sampling state
// when bound with no Sampler.
Texture wrap(Device& device, unsigned glTexture);
// A program linked elsewhere (an app's own GLSL 330 fragment shader on a
// qs_gpu_shaders vertex shader, say) as a pipeline with desc's layout and state.
// The uniform blocks and samplers of desc.vertex (and desc.fragment, when given)
// are bound to their slots.
Pipeline import(Device& device, unsigned glProgram, const PipelineDesc& desc);

// A pass into whatever framebuffer is bound now, w x h pixels, keeping the app's
// viewport: GL's own convention there (no flip), as code drawing into a bound
// Framebuffer expects. Nothing is cleared.
Pass beginPassOnBound(Device& device, int w, int h);

}
