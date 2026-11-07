// gpu_tests -- the gpu:: interface on the GL backend: SDL_GPU's conventions
// (top-left origin for targets, textures, viewports and scissors; depth 0..1),
// uniform blocks, samplers, instancing, indices, culling through the flip, blend,
// the GL natives and the state a pass leaves behind; Painter2D and ShapedText
// drawing through it; ImageAtlas images and masks; Framebuffer and ScreenCapture as gpu:: textures. Needs a display (a hidden
// window holds the context); without one it exits 77 (skipped).

#include "core/Window.h"
#include "gpu/GL.h"
#include "gpu/Gpu.h"
#include "render/Framebuffer.h"
#include "render/ShapedText.h"
#include "ui/ImageAtlas.h"
#include "ui/Painter2D.h"

#include <glad/glad.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

static int passed = 0, failed = 0;
#define CHECK(cond)                                                          \
	do {                                                                     \
		if (cond) passed++;                                                  \
		else { failed++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
	} while (0)

struct RGBA { int r, g, b, a; };

static RGBA px(const std::vector<uint8_t>& p, int w, int x, int y)
{
	const size_t i = (static_cast<size_t>(y) * w + x) * 4;
	return { p[i], p[i + 1], p[i + 2], p[i + 3] };
}
static bool is(RGBA c, int r, int g, int b, int a, int tol = 1)
{
	return std::abs(c.r - r) <= tol && std::abs(c.g - g) <= tol && std::abs(c.b - b) <= tol && std::abs(c.a - a) <= tol;
}

struct Move { float offset[2]; float pad[2]; };
struct Tint { float color[4]; };

struct Fixture
{
	gpu::Device& dev;
	gpu::Pipeline flat, flatDepth, flatCull, flatBlend, tex;
	gpu::Buffer quad, half, inst, indexed, idx, uvQuad;
	gpu::Sampler nearest;
};

static gpu::Texture target(gpu::Device& dev, int w, int h)
{
	return dev.texture({ w, h, gpu::Format::RGBA8, gpu::TextureUsage::Sampled | gpu::TextureUsage::ColorTarget });
}

static void flatDraw(Fixture& f, gpu::Pass& pass, gpu::Pipeline p, gpu::Buffer vb, uint32_t n, Tint t, Move m = {})
{
	pass.bind(p);
	pass.vertices(0, vb);
	pass.vertices(1, f.inst); // per instance: one instance reads offset 0
	pass.uniforms(gpu::Stage::Vertex, 0, m);
	pass.uniforms(gpu::Stage::Fragment, 0, t);
	pass.draw(n);
}

static void testClearAndOrientation(Fixture& f)
{
	gpu::Texture t = target(f.dev, 4, 4);
	gpu::Pass pass = f.dev.beginPass({ .color = t, .clear = true, .clearColor = { 1, 0, 0, 1 } });
	CHECK(pass.width() == 4 && pass.height() == 4);
	pass.end();
	auto p = f.dev.download(t);
	CHECK(p.size() == 64 && is(px(p, 4, 0, 0), 255, 0, 0, 255) && is(px(p, 4, 3, 3), 255, 0, 0, 255));

	// the top half of NDC (+y up) is rows 0 and 1 (row 0 on top)
	pass = f.dev.beginPass({ .color = t, .clear = true, .clearColor = { 0, 0, 0, 1 } });
	flatDraw(f, pass, f.flat, f.half, 6, { { 0, 1, 0, 1 } });
	pass.end();
	p = f.dev.download(t);
	CHECK(is(px(p, 4, 0, 0), 0, 255, 0, 255) && is(px(p, 4, 3, 1), 0, 255, 0, 255));
	CHECK(is(px(p, 4, 0, 2), 0, 0, 0, 255) && is(px(p, 4, 3, 3), 0, 0, 0, 255));
	f.dev.release(t);
}

static void testScissorAndViewport(Fixture& f)
{
	gpu::Texture t = target(f.dev, 4, 4);
	gpu::Pass pass = f.dev.beginPass({ .color = t, .clear = true, .clearColor = { 0, 0, 0, 1 } });
	pass.scissor(0, 0, 2, 1); // top-left 2x1
	flatDraw(f, pass, f.flat, f.quad, 6, { { 1, 1, 1, 1 } });
	pass.noScissor();
	pass.viewport(2, 2, 2, 2); // bottom-right 2x2
	flatDraw(f, pass, f.flat, f.quad, 6, { { 0, 0, 1, 1 } });
	pass.end();
	auto p = f.dev.download(t);
	CHECK(is(px(p, 4, 0, 0), 255, 255, 255, 255) && is(px(p, 4, 1, 0), 255, 255, 255, 255));
	CHECK(is(px(p, 4, 2, 0), 0, 0, 0, 255) && is(px(p, 4, 0, 1), 0, 0, 0, 255));
	CHECK(is(px(p, 4, 2, 2), 0, 0, 255, 255) && is(px(p, 4, 3, 3), 0, 0, 255, 255));
	CHECK(is(px(p, 4, 1, 2), 0, 0, 0, 255) && is(px(p, 4, 2, 1), 0, 0, 0, 255));
	f.dev.release(t);
}

static void testTextures(Fixture& f)
{
	// uploaded row 0 is the top: sampling uv (0,0) at the top-left gives it back as is
	const uint8_t img[16] = { 255, 0, 0, 255, 0, 255, 0, 255,      // red, green
	                          0, 0, 255, 255, 255, 255, 255, 255 }; // blue, white
	gpu::Texture src = f.dev.texture({ 2, 2, gpu::Format::RGBA8, gpu::TextureUsage::Sampled });
	f.dev.upload(src, img, 0, 0, 2, 2);
	CHECK(f.dev.download(src) == std::vector<uint8_t>(img, img + 16));
	gpu::Texture a = target(f.dev, 2, 2);
	gpu::Pass pass = f.dev.beginPass({ .color = a, .clear = true });
	pass.bind(f.tex);
	pass.vertices(0, f.uvQuad);
	pass.texture(gpu::Stage::Fragment, 0, src, f.nearest);
	pass.draw(6);
	pass.end();
	CHECK(f.dev.download(a) == std::vector<uint8_t>(img, img + 16));

	// a rendered texture sampled again keeps its top on top
	gpu::Texture b = target(f.dev, 2, 2);
	pass = f.dev.beginPass({ .color = b, .clear = true });
	pass.bind(f.tex);
	pass.vertices(0, f.uvQuad);
	pass.texture(gpu::Stage::Fragment, 0, a, f.nearest);
	pass.draw(6);
	pass.end();
	CHECK(f.dev.download(b) == std::vector<uint8_t>(img, img + 16));

	// a sub-rect upload lands at its top-left position
	const uint8_t one[4] = { 9, 8, 7, 6 };
	f.dev.upload(src, one, 1, 1, 1, 1);
	auto p = f.dev.download(src);
	CHECK(is(px(p, 2, 1, 1), 9, 8, 7, 6, 0) && is(px(p, 2, 0, 0), 255, 0, 0, 255, 0));

	gpu::Texture r8 = f.dev.texture({ 3, 1, gpu::Format::R8 });
	const uint8_t g[3] = { 10, 20, 30 };
	f.dev.upload(r8, g, 0, 0, 3, 1);
	CHECK(f.dev.download(r8) == std::vector<uint8_t>(g, g + 3));
	CHECK(f.dev.desc(r8).width == 3 && f.dev.desc(r8).format == gpu::Format::R8);
	f.dev.release(r8);
	f.dev.release(src);
	f.dev.release(a);
	f.dev.release(b);
}

static void testDepth(Fixture& f)
{
	gpu::Texture t = target(f.dev, 4, 4);
	gpu::Texture d = f.dev.texture({ 4, 4, gpu::Format::Depth24Stencil8, gpu::TextureUsage::DepthTarget });
	// a quad at z 0.5 (depth range 0..1) draws over a cleared 1 and not over a cleared 0.4
	gpu::Pass pass = f.dev.beginPass({ .color = t, .clear = true, .depth = d, .clearDepth = true });
	flatDraw(f, pass, f.flatDepth, f.quad, 6, { { 1, 0, 0, 1 } }, {});
	pass.end();
	auto p = f.dev.download(t);
	CHECK(is(px(p, 4, 1, 1), 255, 0, 0, 255)); // z 0.5 quad drew
	pass = f.dev.beginPass({ .color = t, .clear = true, .depth = d, .clearDepth = true, .depthValue = 0.4f });
	flatDraw(f, pass, f.flatDepth, f.quad, 6, { { 1, 0, 0, 1 } }, {}); // z 0.5 is behind 0.4
	pass.end();
	p = f.dev.download(t);
	CHECK(is(px(p, 4, 1, 1), 0, 0, 0, 0));
	f.dev.release(t);
	f.dev.release(d);
}

static void testCullThroughFlip(Fixture& f)
{
	// f.half is counter-clockwise in NDC: a front face, kept with Cull::Back even
	// though the GL backend flips it into the texture
	gpu::Texture t = target(f.dev, 4, 4);
	gpu::Pass pass = f.dev.beginPass({ .color = t, .clear = true });
	flatDraw(f, pass, f.flatCull, f.half, 6, { { 1, 1, 0, 1 } });
	pass.end();
	auto p = f.dev.download(t);
	CHECK(is(px(p, 4, 1, 0), 255, 255, 0, 255));
	f.dev.release(t);
}

static void testInstancesIndicesBlend(Fixture& f)
{
	gpu::Texture t = target(f.dev, 4, 4);
	gpu::Pass pass = f.dev.beginPass({ .color = t, .clear = true });
	// a 1x1 px quad at the top-left, drawn twice: instance offsets 0 and +1 px right
	pass.bind(f.flat);
	pass.vertices(0, f.indexed);
	pass.vertices(1, f.inst);
	pass.indices(f.idx, gpu::IndexType::U16);
	pass.uniforms(gpu::Stage::Vertex, 0, Move{});
	pass.uniforms(gpu::Stage::Fragment, 0, Tint{ { 0, 1, 1, 1 } });
	pass.drawIndexed(6, 2);
	pass.end();
	auto p = f.dev.download(t);
	CHECK(is(px(p, 4, 0, 0), 0, 255, 255, 255) && is(px(p, 4, 1, 0), 0, 255, 255, 255));
	CHECK(is(px(p, 4, 2, 0), 0, 0, 0, 0) && is(px(p, 4, 0, 1), 0, 0, 0, 0));

	// Alpha blend over a transparent target: colour times alpha, alpha as coverage
	pass = f.dev.beginPass({ .color = t, .clear = true });
	flatDraw(f, pass, f.flatBlend, f.quad, 6, { { 1, 0, 0, 0.5f } });
	pass.end();
	p = f.dev.download(t);
	CHECK(is(px(p, 4, 2, 2), 128, 0, 0, 128));
	f.dev.release(t);
}

static void testNativesAndState(Fixture& f)
{
	gpu::Texture t = target(f.dev, 2, 2);
	CHECK(glIsTexture(gpu::gl::native(f.dev, t)) == GL_TRUE);
	CHECK(glIsBuffer(gpu::gl::native(f.dev, f.quad)) == GL_TRUE);
	CHECK(glIsSampler(gpu::gl::native(f.dev, f.nearest)) == GL_TRUE);
	CHECK(glIsProgram(gpu::gl::program(f.dev, f.flat)) == GL_TRUE);
	CHECK(gpu::gl::native(f.dev, gpu::Texture{ 999 }) == 0);

	gpu::Pass pass = f.dev.beginPass({ .color = t, .clear = true });
	flatDraw(f, pass, f.flatCull, f.quad, 6, { { 1, 1, 1, 1 } });
	pass.scissor(0, 0, 1, 1);
	pass.end();
	GLint prog = -1, vao = -1, fbo = -1;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
	CHECK(prog == 0 && vao == 0 && fbo == 0);
	CHECK(glIsEnabled(GL_DEPTH_TEST) && glIsEnabled(GL_BLEND) && !glIsEnabled(GL_SCISSOR_TEST) && !glIsEnabled(GL_CULL_FACE));

	f.dev.release(t);
	CHECK(gpu::gl::native(f.dev, t) == 0);
	gpu::Texture again = target(f.dev, 2, 2); // reuses the released slot
	CHECK(again.id == t.id && f.dev.desc(again).width == 2);
	f.dev.release(again);
}

// Painter2D through gpu:: into a bound Framebuffer, with a ShapedText whose atlas
// grows while quads of an earlier string of the same font are still in the batch:
// they must come out as if drawn alone.
static void testPainterShapedText()
{
	const std::string font = std::string(QS_FONT_DIR) + "SpaceMono-Bold.ttf";
	const int W = 160, H = 128;
	Framebuffer fb;
	fb.create(W, H);
	ui::Painter2D painter;
	painter.create();
	auto drawA = [&](ShapedText& t, bool thenMany) {
		fb.Bind();
		glViewport(0, 0, W, H);
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		painter.begin(W, H);
		painter.roundedRect(4, 4, 40, 20, 6, { 0.2f, 0.4f, 0.8f, 1 });
		painter.text(t, "A", 10, 100, { 1, 1, 1, 1 });
		if (thenMany) // far right of the target, but rasterised all the same
			painter.text(t, "BCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 1000, 100, { 1, 1, 1, 1 });
		painter.end();
		std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4);
		glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
		Framebuffer::Unbind();
		return px;
	};
	ShapedText alone, growing;
	alone.create({ { font } }, 96.0f);
	growing.create({ { font } }, 96.0f);
	const auto ref = drawA(alone, false);
	const int before = growing.atlasSize();
	const auto got = drawA(growing, true);
	CHECK(growing.atlasSize() > before); // it did grow mid-frame
	CHECK(got == ref);
	size_t lit = 0;
	for (size_t i = 0; i < ref.size(); i += 4) lit += ref[i] > 128 && ref[i + 1] > 128 && ref[i + 2] > 128;
	CHECK(lit > 200); // the A is there
	gpu::Device& dev = gpu::gl::contextDevice();
	CHECK(glIsTexture(gpu::gl::native(dev, growing.texture(dev))) == GL_TRUE);
	alone.Delete();
	growing.Delete();
	painter.Delete();
	fb.Delete();
}

// ImageAtlas entries drawn in their own colours and as masks, the atlas growing mid-batch
static void testPainterAtlas()
{
	auto solid = [](int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
		ui::Image im;
		im.w = w;
		im.h = h;
		for (int i = 0; i < w * h; i++) im.pixels.insert(im.pixels.end(), { r, g, b, a });
		return im;
	};
	const int W = 32, H = 16;
	Framebuffer fb;
	fb.create(W, H);
	ui::Painter2D painter;
	painter.create();
	ui::ImageAtlas atlas(16, 128);
	const int red = atlas.add(solid(8, 8, 255, 0, 0, 255));
	const int half = atlas.add(solid(4, 4, 128, 128, 128, 128)); // 50% white, premultiplied
	fb.Bind();
	glViewport(0, 0, W, H);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	painter.begin(W, H);
	painter.image(atlas, red, 0, 0, 8, 8);
	painter.mask(atlas, half, 8, 0, 4, 4, { 0, 0, 1, 1 });   // blue at half coverage
	painter.image(atlas, half, 12, 0, 4, 4);                 // grey over black
	painter.image(atlas, red, 16, 0, 8, 8, 0.5f);            // faded
	const int grown = atlas.add(solid(40, 40, 0, 255, 0, 255)); // forces the atlas to grow
	CHECK(atlas.size() == 64);
	painter.image(atlas, grown, 24, 8, 8, 8);
	painter.image(atlas, red, 0, 8, 8, 8); // its old uv no longer holds: drawn with the new one
	painter.end();
	std::vector<uint8_t> p(static_cast<size_t>(W) * H * 4);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
	Framebuffer::Unbind();
	auto at = [&](int x, int y) { return px(p, W, x, H - 1 - y); }; // GL reads bottom row first
	CHECK(is(at(3, 3), 255, 0, 0, 255));
	CHECK(is(at(0, 0), 255, 0, 0, 255) && is(at(7, 7), 255, 0, 0, 255)); // pixel for pixel to its edges
	CHECK(is(at(9, 1), 0, 0, 128, 255, 2));
	CHECK(is(at(13, 1), 128, 128, 128, 255, 2));
	CHECK(is(at(19, 3), 128, 0, 0, 255, 2));
	CHECK(is(at(27, 11), 0, 255, 0, 255));
	CHECK(is(at(3, 11), 255, 0, 0, 255));
	CHECK(is(at(10, 10), 0, 0, 0, 255)); // nothing drawn there
	painter.Delete();
	atlas.release();
	fb.Delete();
}

// Framebuffer and ScreenCapture as gpu:: textures, drawn by Painter2D::image
static void testFramebufferBridge()
{
	gpu::Device& dev = gpu::gl::contextDevice();
	Framebuffer src, dst;
	src.create(8, 8);
	dst.create(8, 8);
	src.Bind();
	glViewport(0, 0, 8, 8);
	glClearColor(0, 1, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	ScreenCapture cap;
	cap.capture(8, 8); // a copy of src while it's bound
	CHECK(gpu::gl::native(dev, src.color()) == src.colorTex);
	CHECK(src.color().id == src.color().id); // wrapped once
	CHECK(gpu::gl::native(dev, cap.color()) == cap.texture);

	ui::Painter2D painter;
	painter.create();
	dst.Bind();
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	painter.begin(8, 8);
	painter.image(src.color(), 0, 0, 4, 8);
	painter.image(cap.color(), 4, 0, 4, 8, glm::vec4(1, 0, 1, 1)); // tinted: green times magenta is black
	painter.end();
	uint8_t px[8 * 8 * 4];
	glReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, px);
	Framebuffer::Unbind();
	CHECK(is(RGBA{ px[(4 * 8 + 1) * 4], px[(4 * 8 + 1) * 4 + 1], px[(4 * 8 + 1) * 4 + 2], 255 }, 0, 255, 0, 255));
	CHECK(is(RGBA{ px[(4 * 8 + 6) * 4], px[(4 * 8 + 6) * 4 + 1], px[(4 * 8 + 6) * 4 + 2], 255 }, 0, 0, 0, 255));
	painter.Delete();
	src.Delete();
	dst.Delete();
	cap.Delete();
	CHECK(src.color().id == 0); // nothing to wrap once deleted
}

int main()
{
	SDLState state = initialize(64, 64, { .title = "gpu_tests", .hidden = true, .preferDedicatedGpu = false });
	if (!state.successfullyInitialized)
	{
		std::printf("gpu_tests: skipped (no display for a GL context)\n");
		return 77;
	}
	{
		auto dev = gpu::createDevice(state);
		CHECK(dev->backend() == gpu::Backend::GL);

		gpu::Shader fv = dev->shader(QS_GPU_TEST_SHADERS "flat.vert", gpu::Stage::Vertex);
		gpu::Shader ff = dev->shader(QS_GPU_TEST_SHADERS "flat.frag", gpu::Stage::Fragment);
		gpu::Shader tv = dev->shader(QS_GPU_TEST_SHADERS "tex.vert", gpu::Stage::Vertex);
		gpu::Shader tf = dev->shader(QS_GPU_TEST_SHADERS "tex.frag", gpu::Stage::Fragment);

		gpu::PipelineDesc flat{ .vertex = fv, .fragment = ff,
		                        .buffers = { { 3 * sizeof(float) }, { 2 * sizeof(float), true } },
		                        .attributes = { { 0, 0, gpu::VertexFormat::Float3, 0 }, { 1, 1, gpu::VertexFormat::Float2, 0 } } };
		gpu::PipelineDesc depth = flat;
		depth.depthTest = depth.depthWrite = true;
		depth.depthFormat = gpu::Format::Depth24Stencil8;
		gpu::PipelineDesc cull = flat;
		cull.cull = gpu::Cull::Back;
		gpu::PipelineDesc blend = flat;
		blend.blend = gpu::Blend::Alpha;
		gpu::PipelineDesc tex{ .vertex = tv, .fragment = tf, .buffers = { { 4 * sizeof(float) } },
		                       .attributes = { { 0, 0, gpu::VertexFormat::Float2, 0 }, { 1, 0, gpu::VertexFormat::Float2, 8 } } };

		Fixture f{ *dev, dev->pipeline(flat), dev->pipeline(depth), dev->pipeline(cull), dev->pipeline(blend), dev->pipeline(tex) };

		// counter-clockwise quads in NDC (+y up)
		const float quad[] = { -1, -1, 0.5f, 1, -1, 0.5f, 1, 1, 0.5f, -1, -1, 0.5f, 1, 1, 0.5f, -1, 1, 0.5f };
		const float half[] = { -1, 0, 0.5f, 1, 0, 0.5f, 1, 1, 0.5f, -1, 0, 0.5f, 1, 1, 0.5f, -1, 1, 0.5f };
		// one pixel of a 4x4 target at its top-left, and instance offsets 0 and one pixel right
		const float px1[] = { -1, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 1, 0.5f, -1, 1, 0.5f };
		const uint16_t idx[] = { 0, 1, 2, 0, 2, 3 };
		const float inst[] = { 0, 0, 0.5f, 0 };
		const float uvQuad[] = { -1, -1, 0, 1, 1, -1, 1, 1, 1, 1, 1, 0, -1, -1, 0, 1, 1, 1, 1, 0, -1, 1, 0, 0 };
		auto make = [&](uint32_t usage, const void* data, uint32_t size) {
			gpu::Buffer b = dev->buffer(usage, size);
			dev->upload(b, data, size);
			return b;
		};
		f.quad = make(gpu::BufferUsage::Vertex, quad, sizeof quad);
		f.half = make(gpu::BufferUsage::Vertex, half, sizeof half);
		f.indexed = make(gpu::BufferUsage::Vertex, px1, sizeof px1);
		f.idx = make(gpu::BufferUsage::Index, idx, sizeof idx);
		f.inst = make(gpu::BufferUsage::Vertex, inst, sizeof inst);
		f.uvQuad = make(gpu::BufferUsage::Vertex, uvQuad, sizeof uvQuad);
		f.nearest = dev->sampler({ gpu::Filter::Nearest, gpu::Filter::Nearest });
		CHECK(dev->size(f.quad) == sizeof quad);

		testClearAndOrientation(f);
		testScissorAndViewport(f);
		testTextures(f);
		testDepth(f);
		testCullThroughFlip(f);
		testInstancesIndicesBlend(f);
		testNativesAndState(f);
		testPainterShapedText();
		testPainterAtlas();
		testFramebufferBridge();
		CHECK(glGetError() == GL_NO_ERROR);

		bool threw = false;
		try { dev->shader(QS_GPU_TEST_SHADERS "missing.vert", gpu::Stage::Vertex); }
		catch (const std::exception&) { threw = true; }
		CHECK(threw);
	}
	cleanup(state);
	std::printf("%d passed, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
