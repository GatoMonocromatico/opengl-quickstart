#include "core/Window.h"
#include "util/DebugLog.h"

#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#ifdef __linux__
#include <filesystem>
#include <fstream>
#include <system_error>
#endif

// -----------------------------------------------------------------------------
// Dedicated GPU preference (hybrid-graphics laptops)
// -----------------------------------------------------------------------------
// A laptop with an integrated and a dedicated GPU renders on the integrated one
// unless the app asks otherwise. Each OS has its own way of asking:
//   * Windows: the NVIDIA Optimus and AMD PowerXpress drivers look for these two
//     exported symbols in the .exe. They must live in the executable (not a DLL)
//     and keep these exact names. The per-app choice in Windows Graphics
//     Settings still overrides them. This lib must stay static: the exports ride
//     into the .exe with this object file, which initialize() always pulls in.
//   * Linux: PRIME render offload is driven by environment variables the GL
//     driver reads when the context is created -- see preferDedicatedGpu().
//   * macOS: nothing to do. An OpenGL app that does not opt into automatic
//     graphics switching in its Info.plist already runs on the dedicated GPU.
#ifdef _WIN32
extern "C" {
	__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#ifdef __linux__
// Counts PCI display controllers (class 0x03xxxx). Reading sysfs instead of
// /dev/dri keeps the count independent of which kernel modules are loaded.
static int countDisplayControllers()
{
	int count = 0;
	std::error_code ec;
	for (const auto& dev : std::filesystem::directory_iterator("/sys/bus/pci/devices", ec))
	{
		std::ifstream classFile(dev.path() / "class");
		std::string pciClass;
		if (classFile >> pciClass && pciClass.rfind("0x03", 0) == 0)
			count++;
	}
	return count;
}
#endif

// Sets the environment variables that route OpenGL to the dedicated GPU, before
// SDL loads the GL driver. Returns the names it set so they can be undone if the
// dedicated GPU fails to produce a context; empty when nothing was changed.
static std::vector<const char*> preferDedicatedGpu()
{
	std::vector<const char*> applied;
#ifdef __linux__
	// The user already chose a GPU (launched through prime-run, or DRI_PRIME=0 to
	// force the integrated one): respect it.
	if (std::getenv("__NV_PRIME_RENDER_OFFLOAD") || std::getenv("__GLX_VENDOR_LIBRARY_NAME") || std::getenv("DRI_PRIME"))
		return applied;

	// Single-GPU machine: nothing to choose between.
	if (countDisplayControllers() < 2)
		return applied;

	std::error_code ec;
	if (std::filesystem::exists("/proc/driver/nvidia/version", ec))
	{
		// NVIDIA proprietary driver: PRIME render offload. Forcing the GLX vendor
		// breaks context creation when that driver is absent, hence the /proc check.
		setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 1);
		setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 1);
		applied = { "__NV_PRIME_RENDER_OFFLOAD", "__GLX_VENDOR_LIBRARY_NAME" };
	}
	else
	{
		// Mesa drivers (AMD, Intel Arc, nouveau): DRI_PRIME=1 selects the GPU that
		// is not driving the display.
		setenv("DRI_PRIME", "1", 1);
		applied = { "DRI_PRIME" };
	}
#endif
	return applied;
}

static void clearGpuPreference(const std::vector<const char*>& applied)
{
#ifdef __linux__
	for (const char* name : applied) unsetenv(name);
#else
	(void)applied;
#endif
}

// Wire it up in createWindowAndContext when you need driver messages (GL 4.3+ or KHR_debug).
[[maybe_unused]] static void GLAPIENTRY glDebugOutput(
	GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar* message,
	const void* userParam
)
{
	(void)source;
	(void)type;
	(void)id;
	(void)severity;
	(void)length;
	(void)userParam;
	std::cerr << "[OpenGL] " << message << std::endl;
}

// One attempt at SDL + window + GL context. Stops at the first failure and
// releases everything created so far. reportErrors=false keeps the error dialog
// closed for an attempt that initialize() is going to retry.
static SDLState createWindowAndContext(int width, int height, const WindowOptions& options, bool reportErrors)
{
	SDL_Window* window = nullptr;
	SDL_GLContext context = nullptr;
	auto fail = [&](const std::string& where) {
		std::string msg = where + ": " + SDL_GetError();
		std::cerr << msg << std::endl;
		if (reportErrors) {
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Initialization Error", msg.c_str(), nullptr);
		}
		if (context) {
			SDL_GL_DestroyContext(context);
			context = nullptr;
		}
		if (window) {
			SDL_DestroyWindow(window);
			window = nullptr;
		}
		SDL_Quit();
		return SDLState(width, height, width, height, nullptr, nullptr, false);
	};

	if (options.appId) {
		SDL_SetHint(SDL_HINT_APP_ID, options.appId);
	}

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		return fail("SDL_Init(SDL_INIT_VIDEO) failed");
	}

	// Request an OpenGL 3.3 core profile context before creating the window.
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	int contextFlags = 0;
#ifdef __APPLE__
	// macOS only hands out core profiles that are forward compatible.
	contextFlags |= SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG;
#endif
#ifndef NDEBUG
	contextFlags |= SDL_GL_CONTEXT_DEBUG_FLAG;
#endif
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, contextFlags);
	// Depth buffer bit depth; required for glEnable(GL_DEPTH_TEST) to resolve occlusion correctly.
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	if (options.transparent) SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	if (options.msaa > 0) {
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, options.msaa);
	}

	SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
	if (options.resizable) windowFlags |= SDL_WINDOW_RESIZABLE;
	if (options.borderless) windowFlags |= SDL_WINDOW_BORDERLESS;
	if (options.alwaysOnTop) windowFlags |= SDL_WINDOW_ALWAYS_ON_TOP;
	if (options.transparent) windowFlags |= SDL_WINDOW_TRANSPARENT;
	window = SDL_CreateWindow(options.title, width, height, windowFlags);
	if (!window) {
		return fail("SDL_CreateWindow failed");
	}
	if (options.resizable) SDL_SetWindowMinimumSize(window, 10, 10);

	// GL context holds all OpenGL state for this thread; one window typically has one context.
	context = SDL_GL_CreateContext(window);
	if (!context) {
		return fail("SDL_GL_CreateContext failed");
	}

	if (!SDL_GL_MakeCurrent(window, context)) {
		return fail("SDL_GL_MakeCurrent failed");
	}

	// a missing swap control only costs frame pacing, not worth refusing to start
	const int swapInterval = options.vsync ? 1 : 0;
	if (!SDL_GL_SetSwapInterval(swapInterval)) {
		std::cerr << "SDL_GL_SetSwapInterval(" << swapInterval << ") failed: " << SDL_GetError() << std::endl;
	}

	// GLAD loads function pointers for this OpenGL version; SDL supplies the loader hook.
	if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
		return fail("gladLoadGLLoader failed");
	}

	// Which GPU actually got the context -- the quickest check that the
	// dedicated-GPU preference worked.
	std::cout << "OpenGL renderer: " << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
		<< " | vendor: " << reinterpret_cast<const char*>(glGetString(GL_VENDOR))
		<< " | version: " << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << std::endl;
	MDBG_IF(flux::verbose, DBG_N("phase", "gladLoadGLLoader completed (OpenGL entry points ready)"));

	// GL_BLEND: alpha transparency; GL_DEPTH_TEST: near fragments win; GL_PROGRAM_POINT_SIZE: allow gl_PointSize in shaders.
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_PROGRAM_POINT_SIZE);
	// Standard premultiplied-style blending for textures with alpha.
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	int drawableW = 0;
	int drawableH = 0;
	SDL_GetWindowSizeInPixels(window, &drawableW, &drawableH);
	drawableW = std::max(drawableW, 10);
	drawableH = std::max(drawableH, 10);
	glViewport(0, 0, drawableW, drawableH);

// #ifndef NDEBUG
// 	if (GLAD_GL_VERSION_4_3 || GLAD_GL_KHR_debug) {
// 		glEnable(GL_DEBUG_OUTPUT);
// 		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
// 		glDebugMessageCallback(glDebugOutput, nullptr);
// 	}
// #endif

	SDLState state(drawableW, drawableH, width, height, window, context, true);

	return state;
}

SDLState initialize(int width, int height, const WindowOptions& options)
{
	// Must run before SDL_Init: the GL driver reads these when the context is created.
	std::vector<const char*> gpuPreference;
	if (options.preferDedicatedGpu) gpuPreference = preferDedicatedGpu();

	SDLState state = createWindowAndContext(width, height, options, gpuPreference.empty());
	if (!state.successfullyInitialized && !gpuPreference.empty())
	{
		// The dedicated GPU was requested but couldn't produce a context (e.g. the
		// driver is installed but offload isn't configured). Running on the
		// integrated GPU beats not running at all.
		std::cerr << "Dedicated GPU unavailable, retrying on the default GPU" << std::endl;
		clearGpuPreference(gpuPreference);
		state = createWindowAndContext(width, height, options, true);
	}

	return state;
}

void cleanup(SDLState& state)
{
	if (state.context) {
		SDL_GL_DestroyContext(state.context);
		state.context = nullptr;
	}
	if (state.window) {
		SDL_DestroyWindow(state.window);
		state.window = nullptr;
	}
	SDL_Quit();
}
