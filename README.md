# opengl-quickstart

A **plug-and-play C++20 / OpenGL 3.3 Core quickstart** that builds and runs on **Linux and Windows** right after `git clone` — no system-wide library installs required. All third-party headers and pre-built binaries live inside `Libraries/`.

It is two things: a static library (`opengl_quickstart`) that apps pull in as a git submodule, and a small example app that shows how to use it. See [Use as a package](#use-as-a-package).

## Stack

| Piece | Role |
|---|---|
| **OpenGL 3.3 Core** | Graphics API (context created in `main.cpp`) |
| **GLAD** | Loads OpenGL function pointers — bundled as `glad.c` + `Libraries/include/glad/` |
| **SDL3** | Window, events, OpenGL context, timing |
| **SDL3_image** | PNG/JPG loading for `Texture` |
| **GLM** | Vectors, matrices, `lookAt`, `perspective` |
| **GlyphGL** | TrueType text for `Text` (header-only, MIT) |

All headers are under `Libraries/include/`. Pre-built libraries live under `Libraries/lib_linux/lib/` (Linux) and `Libraries/lib_win/lib/` (Windows). CMake picks the right folder automatically.

Language: **C++20**.

## Build

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Run from the project root (shaders are loaded relative to the working directory):

```bash
./build/opengl_quickstart_example
```

Or use the helper script (builds, then runs the example under GDB for a crash backtrace):

```bash
./run.sh
```

### Windows

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

SDL3.dll and SDL3_image.dll are automatically copied next to the `.exe` by a post-build step, so no manual DLL placement is needed.

### Release build

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

The `DEBUG` macro (and all `DebugLog.h` output) is enabled only in **Debug** builds; Release compiles it out completely.

### GPU selection (hybrid-graphics laptops)

On laptops with an integrated and a dedicated GPU, `initialize()` (`src/core/Window.cpp`) asks for the **dedicated** one:

- **Windows:** exports `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance` from the exe. Windows Graphics Settings can still override this per app.
- **Linux:** before `SDL_Init`, sets `__NV_PRIME_RENDER_OFFLOAD=1` + `__GLX_VENDOR_LIBRARY_NAME=nvidia` (NVIDIA proprietary driver) or `DRI_PRIME=1` (Mesa). This is skipped on single-GPU machines and whenever you already set one of these variables yourself. `DRI_PRIME=0 ./build/opengl_quickstart_example` forces the integrated GPU.
- **macOS:** nothing needed. OpenGL apps already run on the dedicated GPU there.

If the dedicated GPU can't create a context, startup retries once on the default GPU. The chosen renderer is printed at startup (`OpenGL renderer: ...`).

## Controls

| Input | Action |
|---|---|
| **O** | Toggle mouse-look (hides cursor, camera follows mouse) |
| **P** | Reset camera to forward (-Z) |
| **W / S** | Move camera forward / back (Z axis) |
| **A / D** | Move camera left / right (X axis) |
| **Q / E** | Move camera up / down (Y axis) |
| **F12** | Save the current frame to `screenshot.png` |

### Debug env vars (example app)

| Variable | Effect |
|---|---|
| `QS_CAMERA=x,y,z[,yaw,pitch]` | Place the camera at startup (`applyCameraOverride`) |
| `QS_SHOT=<path>` | Render for `QS_SHOT_AFTER` seconds (default 2), save one PNG, exit (`saveScreenshot`); the widgets demo honours it too |

### Window options

`initialize(w, h, WindowOptions{...})`:

| Field | Default | Effect |
|---|---|---|
| `title`, `resizable`, `vsync`, `msaa` | | as named |
| `appId` | `nullptr` | SDL app id, set before `SDL_Init`: on X11 it is the `WM_CLASS` class (match it with an i3 `for_window [class="..."]` rule), on Wayland the `app_id` |
| `borderless` | `false` | no title bar / decorations |
| `alwaysOnTop` | `false` | keep above other windows (the WM may ignore it) |
| `transparent` | `false` | ARGB window: pixels cleared with alpha 0 show the desktop (needs a compositor such as picom) |
| `preferDedicatedGpu` | `true` | `false` skips the GPU selection above and stays on the default GPU, so a small always-running app doesn't wake the dedicated one |

## Text charsets

`Text` is drawn with GlyphGL in UTF-8 mode. `create()` takes a charset:

```cpp
Text text;
text.create(QS_FONT_DIR "MesloLGSNerdFontMono-Regular.ttf", 18.0f, Text::Charset::Latin);
```

- `Charset::Ascii` (default): printable ASCII.
- `Charset::Latin`: ASCII + Latin-1 Supplement + Latin Extended-A + `✓ ✗ → ← • … – — “ ” ‘ ’`. About 3x the glyphs, so a bigger atlas.

The font must have the glyphs as simple (non-composite) outlines, GlyphGL skips composite ones. `MesloLGSNerdFontMono-Regular.ttf` has all of the Latin set. A codepoint that isn't in the charset is drawn and measured as `?`; control characters and malformed UTF-8 never crash, they draw nothing or `?`.

Measuring for layout (all UTF-8 aware, byte offsets, `scale` like `draw`):

- `width(s, scale)`: advance of the string.
- `ascent(scale)` / `descent(scale)`: how far glyphs reach above / below the baseline; `lineHeight()` is the line pitch.
- `indexAt(s, x, scale)`: byte offset of the caret boundary nearest x, never inside a UTF-8 sequence. Use it to turn a click into a caret.

`util/Utf8.h` has the codepoint helpers (`utf8::next/prev/snap/count/valid/sanitize`).

## UI widgets

`src/ui/` is a small 2D layer. The pieces work on their own, so a custom canvas can use `Painter2D` + `Text` + `TextEdit` + `wrap` without any widgets.

| Header | GL | What |
|---|---|---|
| `ui/Painter2D.h` | yes | batched rects, rounded rects, outlines, lines, circles in window pixels (top-left origin), `text()`, `pushClip/popClip`; own shader `shaders/ui2d.*` |
| `ui/TextEdit.h` | no | UTF-8 string + caret + selection: insert, backspace/delete (Ctrl = word), arrows, home/end, up/down over wrapped lines, select all/word, cut/paste text in and out |
| `ui/Wrap.h` | no | `wrap(text, maxWidth, measure)` -> line ranges (word wrap, long words broken, `\n` kept); `lineOfOffset`, `indexInLine(s)` |
| `ui/Context.h` | yes | immediate-mode widgets, input, `Theme` |

```cpp
ui::Painter2D painter;  painter.create();
Text font;              font.create(QS_FONT_DIR "MesloLGSNerdFontMono-Regular.ttf", 18, Text::Charset::Latin);
ui::Context ui;         ui.init(painter, font);
SDL_StartTextInput(state.window);

while (running)
{
    SDL_Event e;
    // event-driven: sleeps until input or a caret flip
    if (SDL_WaitEventTimeout(&e, ui.waitTimeoutMs()))
        do { ui.handleEvent(e); } while (SDL_PollEvent(&e));  // true = a text field wants that key
    if (!ui.needsRedraw()) continue;

    ui.beginFrame(state.width, state.height);          // drawable pixels
    if (ui.button({10, 10, 120, 32}, "Save")) save();
    ui.textField({10, 60, 400, 32}, "title", title);
    ui.endFrame();
    SDL_GL_SwapWindow(state.window);
}
```

See `example/widgets.cpp` for a full loop.

Widgets, all taking a pixel `Rect`: `button`, `checkbox`, `progressBar`, `textField` (single line, or `.multiline` with `.wrap`; caret, selection, mouse click / drag / double-click word / triple-click line, Ctrl+A/C/X/V, scrolls to the caret), `tabBar` (returns `clicked` / `closed` index), `label`, `beginScroll` / `endScroll`. Widgets are identified by their label or id string; `"Text##id"` shows `Text`, `pushId()` scopes ids in loops. `Theme` holds colors, padding, radius and font scale.

Mouse helpers for custom widgets, the same ones the widgets use: `hovered(rect)`, `pressed`, `released`, `doubleClicked`, `mouseDown`, `dragDelta()`, `wheel()` / `consumeWheel()`, `pushClip/popClip` (clips hit-testing and drawing together).

Notes: text color has no alpha (GlyphGL's shader takes rgb only). Mouse coordinates are converted to drawable pixels, so high-DPI works. `ui::Context` ignores everything it doesn't handle, the app keeps its own key handling (check `wantsKeyboard()` first).

Demo: `./build/opengl_quickstart_widgets` (`QS_SHOT=<png>` saves a frame and exits, `QS_TAB=0|1|2` picks the tab). Tests for the non-GL parts: `tests/run_tests.sh` or `ctest` in the build dir (`ui_tests`).

## Repository layout

```
opengl-quickstart/
├── CMakeLists.txt        # opengl_quickstart library + example (example only when top-level)
├── src/                  # the package: include root, so headers are "render/Mesh.h", "util/DebugLog.h", ...
│   ├── glad.c            # GLAD loader: do not edit unless regenerating
│   ├── core/
│   │   └── Window.h/.cpp # SDLState, WindowOptions, initialize/cleanup, dedicated-GPU selection
│   ├── render/
│   │   ├── Mesh.h/.cpp           # VAO+VBO+EBO geometry, instanced draw, normal generation, updateVertices
│   │   ├── VAO/VBO/EBO.h/.cpp    # buffer object wrappers (Vertex lives in VBO.h)
│   │   ├── shaderClass.h/.cpp    # compile/link GLSL; expands `#include "file"` in shaders
│   │   ├── texture.h/.cpp        # 2-D texture loading via SDL_image
│   │   ├── camera.h/.cpp         # FPS-style camera: perspective or ortho proj*view, shader upload
│   │   ├── Framebuffer.h/.cpp    # offscreen render target + ScreenCapture (copy of the drawn frame)
│   │   ├── Light.h               # CPU mirror of GLSL Light struct (std140)
│   │   ├── animation.h           # sprite-sheet grid animator (UV offset per frame)
│   │   ├── point.h/.cpp          # GL_POINTS helper (separate pipeline)
│   │   └── RoundedCorner2D.h/.cpp # round a triangle-mesh corner in 2D
│   ├── ui/
│   │   ├── Painter2D.h/.cpp      # batched 2D shapes + text + clip, shader in shaders/ui2d.*
│   │   ├── TextEdit.h/.cpp       # caret/selection/edit model (no GL)
│   │   ├── Wrap.h/.cpp           # word wrap over a measure function (no GL)
│   │   └── Context.h/.cpp        # immediate-mode widgets, input, Theme
│   └── util/
│       ├── Utf8.h                # UTF-8 decode / step / validate helpers
│       ├── DebugLog.h            # zero-cost debug logging macros (see below)
│       ├── DebugHooks.h/.cpp     # saveScreenshot, applyCameraOverride
│       ├── Timer.h               # interval flag used by FPS counter and Animation
│       ├── StdAliases.h          # stdMat<T>, stdCube<T>, arrVec<T,N>, ...
│       └── mathUtils.h/.cpp      # dsin / dcos (degrees)
│
├── shaders/              # stock shaders, found at runtime through QS_SHADER_DIR
│   ├── default.vert/.frag  # main mesh pipeline (instancing, lighting, mask alpha)
│   ├── point.vert/.frag    # GL_POINTS pipeline
│   └── ui2d.vert/.frag     # Painter2D shapes
│
├── fonts/                # bundled .ttf fonts for Text, found through QS_FONT_DIR (licenses in fonts/README.md)
│
├── example/              # app-level template: copy these into a new project and grow them
│   ├── main.cpp          # init, main loop, cleanup
│   ├── widgets.cpp       # UI widgets demo (opengl_quickstart_widgets), event-driven loop
│   ├── GameState.h       # scene objects, FPS
│   ├── Resources.h/.cpp  # central GPU asset store (shaders, meshes, textures, lights, UBO)
│   ├── gameObject.h/.cpp # logical entity: indexes a Mesh in Resources + own transform
│   └── Textures/         # sample textures
│
├── tests/                # RoundedCorner2D tests (test.cpp) and ui_tests.cpp (Utf8, Wrap, TextEdit); run_tests.sh builds and runs both
├── Libraries/
│   ├── include/          # GLAD, GLM, SDL3, SDL3_image, stb, GlyphGL headers
│   ├── lib_linux/lib/    # Linux shared objects
│   └── lib_win/lib/      # Windows import libs + DLLs
└── run.sh                # build + run the example under GDB (Linux only)
```

## Use as a package

Add the repo as a submodule and link the library. Everything (headers, SDL, GLM, GL, the `DEBUG` define on Debug builds) comes through the target.

```bash
git submodule add git@github.com:GatoMonocromatico/opengl-quickstart.git external/opengl-quickstart
```

```cmake
add_subdirectory(external/opengl-quickstart)

add_executable(my_app src/main.cpp ...)
target_link_libraries(my_app PRIVATE opengl_quickstart)
quickstart_setup_executable(my_app)   # copies SDL DLLs (Windows) / sets rpath (Linux)
```

```cpp
#include "core/Window.h"
#include "render/Mesh.h"

SDLState state = initialize(1280, 720, { .title = "My App", .resizable = false, .vsync = false });
Shader shader(QS_SHADER_DIR "default.vert", "assets/shaders/my.frag");
```

- `QS_SHADER_DIR` is the absolute path (trailing slash) to `shaders/`. Use the stock shaders from there, or ship your own and load them relative to the working dir.
- `QS_FONT_DIR` is the same for `fonts/`: `text.create(QS_FONT_DIR "VT323-Regular.ttf", 32.0f)`.
- `example/` is not part of the library. Your app owns its `main.cpp`, `Resources`, `GameState` and `GameObject`.
- Keep the library static: the Windows dedicated-GPU exports in `core/Window.cpp` only work from inside the executable.

## Adding your own geometry

1. **Define vertices and indices** — `Vertex` is declared in `VBO.h` (position, texcoord, mask texcoord, normal, instance offset, luminance).
2. **Populate `Resources::load()`** in `example/Resources.cpp` — upload meshes, load textures, set up lights, create the UBO.
3. **Add a `GameObject`** to `GameState::scenarioObjects` pointing at your mesh index.
4. **Call `Draw`** in the main loop (see `gs.scenarioObjects[0].Draw(...)` in `example/main.cpp`).

### Shader uniforms expected by `default.vert` / `default.frag`

| Uniform | Type | Purpose |
|---|---|---|
| `camMatrix` | `mat4` | Combined projection × view (from `Camera::updateMatrix`) |
| `model` | `mat4` | Per-object model transform |
| `texCoordOffset` | `vec2` | Sprite-sheet animation UV offset |
| `tex0` | `sampler2D` | Color texture (unit 0) |
| `mask0` | `sampler2D` | Alpha mask texture (unit 1) |
| `numScenarioLights` | `int` | Active lights in the `ScenarioLights` UBO block |

Lights are streamed each frame via `glBufferSubData` into a UBO bound to GLSL `layout(std140) uniform ScenarioLights`.

---

## DebugLog.h

Zero-overhead debug logging controlled by the `DEBUG` preprocessor macro. CMake defines `DEBUG` in **Debug** builds only; in Release every macro compiles away to `((void)0)` with no runtime cost.

Include it anywhere you need logging — it is already pulled into every project header.

### Macros

#### `DBG(value)` — print one value

```cpp
DBG(42);
DBG("Hello world");
DBG(someFloatVariable);
```

Prints:  `[filename.cpp] <value>`

Two-argument form adds a label:

```cpp
DBG("vertexCount", mesh.vertices.size());
```

Prints: `[filename.cpp] vertexCount=42`

---

#### `MDBG(...)` — print multiple fields on one line

Separates each argument with ` | ` after a single file prefix.

```cpp
MDBG("phase", "init done", "width", state.width, "height", state.height);
```

Prints: `[main.cpp] phase | init done | width | 1600 | height | 900`

Combine with `DBG_N` to get `name=value` pairs:

```cpp
MDBG(DBG_N("x", pos.x), DBG_N("y", pos.y), DBG_N("z", pos.z));
```

Prints: `[camera.cpp] x=1.500000 | y=0.000000 | z=-3.200000`

---

#### `DBG_N(name, value)` — named field for use inside `MDBG`

```cpp
MDBG(DBG_N("dt", deltaTime), DBG_N("fps", gs.fps));
```

---

#### `DBG_F6(value)` — float formatted to 6 decimal places

Wraps any numeric value so it prints as `fixed` with six decimal places without affecting the stream flags of surrounding output.

```cpp
MDBG(DBG_N("angle", DBG_F6(yaw)), DBG_N("pitch", DBG_F6(pitch)));
```

Prints: `[camera.cpp] angle=−90.000000 | pitch=0.000000`

---

#### `DBG_IF(cond, ...)` / `MDBG_IF(cond, ...)` — runtime-gated output

Same signatures as `DBG` and `MDBG` but only emit when `cond` is true.

Everything in the package gates on one global switch, `flux::verbose` (default `true`). Flip it once at startup to silence or re-enable all of it:

```cpp
flux::verbose = false;

DBG_IF(flux::verbose, "vertexCount", verts.size());
MDBG_IF(flux::verbose, DBG_N("min", aabb.min), DBG_N("max", aabb.max));
```

The condition is evaluated at runtime only in Debug builds. In Release both macros expand to `((void)0)` — the condition is never evaluated and the call compiles out entirely.

---

### Release behaviour

In Release, all six macros expand to `((void)0)`:

```cpp
#define DBG(...)         ((void)0)
#define MDBG(...)        ((void)0)
#define DBG_N(n, v)      ((void)0)
#define DBG_F6(x)        ((void)0)
#define DBG_IF(c, ...)   ((void)0)
#define MDBG_IF(c, ...)  ((void)0)
```

The compiler eliminates them entirely — no branches, no string literals, no I/O.

---

## License

MIT — see `LICENSE`.
