#include "render/Framebuffer.h"

void Framebuffer::create(int w, int h)
{
	width = w;
	height = h;

	glGenFramebuffers(1, &ID);
	glBindFramebuffer(GL_FRAMEBUFFER, ID);

	glGenTextures(1, &colorTex);
	glBindTexture(GL_TEXTURE_2D, colorTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// Distorted lookups leave [0,1]; clamping keeps the edge pixel instead of
	// tiling the board back over itself.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

	glGenRenderbuffers(1, &depthRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, depthRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRBO);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		DBG_IF(flux::verbose, "FRAMEBUFFER INCOMPLETE");
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::resize(int w, int h)
{
	if (w == width && h == height) return;
	if (ID == 0)
	{
		create(w, h);
		return;
	}

	width = w;
	height = h;

	glBindTexture(GL_TEXTURE_2D, colorTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glBindTexture(GL_TEXTURE_2D, 0);

	glBindRenderbuffer(GL_RENDERBUFFER, depthRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void Framebuffer::Bind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, ID);
	glViewport(0, 0, width, height);
}

void Framebuffer::Unbind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, screenTarget);
}

// One wrap per object and device; the GL name never changes (resizes reuse it).
static gpu::Texture wrapOnce(gpu::Device& device, unsigned name, gpu::Device*& dev, gpu::Texture& handle)
{
	if (!name) return {};
	if (dev != &device)
	{
		if (dev && handle) dev->release(handle);
		dev = &device;
		handle = gpu::gl::wrap(device, name);
	}
	return handle;
}

static void unwrap(gpu::Device*& dev, gpu::Texture& handle)
{
	if (dev && handle) dev->release(handle);
	dev = nullptr;
	handle = {};
}

gpu::Texture Framebuffer::color(gpu::Device& device) { return wrapOnce(device, colorTex, gpuDevice_, gpuColor_); }
gpu::Texture ScreenCapture::color(gpu::Device& device) { return wrapOnce(device, texture, gpuDevice_, gpuColor_); }

void Framebuffer::Delete()
{
	unwrap(gpuDevice_, gpuColor_);
	if (colorTex) glDeleteTextures(1, &colorTex);
	if (depthRBO) glDeleteRenderbuffers(1, &depthRBO);
	if (ID) glDeleteFramebuffers(1, &ID);
	ID = colorTex = depthRBO = 0;
	width = height = 0;
}

void ScreenCapture::capture(int w, int h)
{
	if (texture == 0)
	{
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		// refraction-style sampling can leave [0,1]; clamping keeps the edge
		// pixel instead of wrapping the scene around onto itself
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		width = 0;
		height = 0;
	}

	glBindTexture(GL_TEXTURE_2D, texture);

	if (w != width || h != height)
	{
		width = w;
		height = h;
		glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, width, height, 0);
	}
	else
	{
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
}

void ScreenCapture::Delete()
{
	unwrap(gpuDevice_, gpuColor_);
	if (texture) glDeleteTextures(1, &texture);
	texture = 0;
	width = height = 0;
}
