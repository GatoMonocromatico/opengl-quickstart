#pragma once
// -----------------------------------------------------------------------------
// Framebuffer.h -- offscreen colour target (+ depth) for render-to-texture
// -----------------------------------------------------------------------------
// Bind it, draw a pass, unbind, then sample `colorTex` from a later shader
// (e.g. a surface that shows another view of the scene).
//
// The colour attachment is a plain GL_RGBA8 texture with CLAMP_TO_EDGE wrapping:
// effects that sample it with distorted coordinates often fall outside [0,1],
// and GL_REPEAT there would wrap the image around onto itself.

#include "util/DebugLog.h"
#include <glad/glad.h>

class Framebuffer
{
public:
	GLuint ID = 0;
	GLuint colorTex = 0;
	GLuint depthRBO = 0;
	int width = 0, height = 0;

	Framebuffer() = default;

	// Allocates the FBO and its attachments. Safe to call once per target.
	void create(int w, int h);
	// Reallocates attachments when the requested size changed; no-op otherwise.
	void resize(int w, int h);
	void Bind();
	// What Unbind() returns to: 0 is the window; a hidden window's app points it at an
	// offscreen Framebuffer's ID so every pass that goes back to "the screen" lands there.
	static inline GLuint screenTarget = 0;
	// Rebinds the screen target. Callers must restore their own viewport.
	static void Unbind();
	void Delete();
};

// A colour texture filled by copying out of whatever is currently drawn on the
// default framebuffer. For effects that need the scene BEHIND them (refraction,
// blur): the scene is only complete once everything else has been drawn, so the
// frame is drawn normally, snapshotted here, and the effect is drawn last on top
// while sampling the snapshot.
//
// This is a copy rather than a render-to-texture pass because it costs one
// GPU-side blit and needs no format matching against the window's own buffers.
class ScreenCapture
{
public:
	GLuint texture = 0;
	int width = 0, height = 0;

	// Sizes (or resizes) the texture and copies the current framebuffer into it.
	void capture(int w, int h);
	void Delete();
};
